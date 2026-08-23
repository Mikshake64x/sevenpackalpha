#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

/**
    SpectrumAnalyzerState
    ----------------------
    Analyseur de spectre CONTINU (contrairement a AiLearnAnalyzer, qui
    capture un instantane ponctuel) utilise pour dessiner le fond de
    spectre du nouvel editeur de crossover (voir CrossoverSpectrumEditor.h)
    -- capture le signal D'ENTREE du plugin (avant tout traitement),
    thread audio -> thread UI via un tableau de magnitudes std::atomic,
    sans allocation ni verrou dans le chemin audio.

    Tampon circulaire a RECOUVREMENT (voir hopSize/pushSamples()) : chaque
    FFT est declenchee tous les hopSize echantillons (pas tous les
    fftSize), en reutilisant les echantillons deja vus -- necessaire pour
    combiner une bonne resolution frequentielle (FFT large) ET une
    animation fluide (mises a jour frequentes), les deux etant sinon en
    tension directe (retour utilisateur : "l'analyse FFT est pas tres
    fluide et pas precise" -- un ordre plus grand SANS recouvrement
    aurait ameliore la precision mais aggrave la fluidite).
*/
class SpectrumAnalyzerState
{
public:
    // EXPLICITE et necessaire, meme raison que Crossover() = default dans
    // Crossover.h : JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (voir tout
    // en bas de la classe) supprime le constructeur de copie, ce qui
    // empeche le compilateur de generer implicitement le constructeur PAR
    // DEFAUT -- necessaire ici pour la construction en membre simple
    // (spectrumAnalyzer) dans PluginProcessor.
    SpectrumAnalyzerState() = default;

    // Ordre 13 (8192, ~5.4Hz/bin a 44.1kHz) -- deux crans au-dessus de
    // l'ordre 11 d'origine (qui ne donnait qu'environ 9 bins entre
    // 20-200Hz, d'ou l'aspect "en pixels/grossier" sur l'axe log). Le
    // cout par FFT est plus eleve, mais reste negligeable pour le thread
    // audio (~8192 points, quelques dizaines de microsecondes) -- voir
    // hopSize ci-dessous pour la frequence de declenchement reelle.
    static constexpr int fftOrder = 13;
    static constexpr int fftSize  = 1 << fftOrder; // 8192
    static constexpr int numBins  = fftSize / 2;

    // Recouvrement 87.5% (hop = fftSize/8) -- une FFT toutes les ~23ms a
    // 44.1kHz (~43Hz), au lieu d'une FFT tous les fftSize echantillons
    // (~186ms/5.4Hz sans recouvrement, bien trop lent pour paraitre
    // fluide). C'est CE recouvrement qui decouple resolution et fluidite.
    static constexpr int hopSize = fftSize / 8; // 1024

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        writePos = 0;
        samplesSinceHop = 0;
        std::fill (fifo.begin(), fifo.end(), 0.0f);
    }

    // Thread AUDIO -- appele en fin de processBlock() avec le signal SEC
    // (avant crossover/compression), mono (L, ou moyenne L/R -- voir
    // appelant). Ecrit dans le tampon circulaire (jamais reinitialise/
    // remis a zero -- contrairement a l'ancienne version, les fftSize
    // derniers echantillons restent toujours disponibles), declenche une
    // FFT tous les hopSize echantillons.
    void pushSamples (const float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            fifo[(size_t) writePos] = samples[i];
            writePos = (writePos + 1) % fftSize;

            if (++samplesSinceHop >= hopSize)
            {
                samplesSinceHop = 0;
                runFft();
            }
        }
    }

    // Thread UI -- magnitude lissee du bin FFT le plus proche, NORMALISEE
    // par le niveau de reference auto-suivi (voir autoGainRef/runFft()) --
    // retour utilisateur : "la mesure doit etre adaptative en fonction du
    // niveau ... tout le temps au meme niveau si le son est faible ou fort
    // en entree". Echelle 0..~1 de confort visuel (pas calibree en dB
    // absolu, ce fond de spectre est un repere visuel, pas un mesureur
    // precis) -- seule la FORME spectrale doit varier avec le programme,
    // pas la hauteur globale de la courbe.
    float getMagnitudeAtBin (int bin) const noexcept
    {
        if (bin < 0 || bin >= numBins)
            return 0.0f;
        const float ref = juce::jmax (kAutoGainFloor, autoGainRef.load());
        return juce::jlimit (0.0f, 1.0f, magnitudes[(size_t) bin].load() / ref);
    }

    // Thread UI -- meme lecture que getMagnitudeAtBin(), mais interpolee
    // LINEAIREMENT entre les 2 bins encadrant freqHz (indice de bin
    // fractionnaire) plutot que de retomber sur le bin entier le plus
    // proche. Necessaire pour un trace lisse en echelle LOG : pres du
    // grave, plusieurs pixels d'ecran consecutifs tombent sur le MEME bin
    // entier (resolution FFT lineaire trop grossiere a ces frequences) --
    // sans interpolation, le trace produit des paliers/"pixels" visibles
    // au lieu d'une courbe continue.
    float getMagnitudeInterpolated (float freqHz) const noexcept
    {
        if (fs <= 0.0)
            return 0.0f;
        const float binF = juce::jlimit (0.0f, (float) (numBins - 1), freqHz / (float) (fs * 0.5) * (float) numBins);
        const int lo = (int) binF;
        const int hi = juce::jmin (numBins - 1, lo + 1);
        const float frac = binF - (float) lo;
        return getMagnitudeAtBin (lo) + (getMagnitudeAtBin (hi) - getMagnitudeAtBin (lo)) * frac;
    }

    double getSampleRate() const noexcept { return fs; }

private:
    void runFft()
    {
        // Les fftSize derniers echantillons, dans l'ordre chronologique --
        // writePos pointe sur la case la PLUS ANCIENNE (prochaine a etre
        // ecrasee), donc le tampon lu a partir de la est deja dans le bon
        // ordre (recouvrement : la majorite de ces echantillons ont deja
        // servi lors d'appels precedents, seuls les hopSize derniers sont
        // nouveaux).
        std::array<float, (size_t) fftSize * 2> fftData {};
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = fifo[(size_t) ((writePos + i) % fftSize)];

        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Plafond de clamp elargi (etait 1.0, avant normalisation AGC) --
        // laisse de la marge pour qu'un signal fort produise un pic REEL
        // au-dessus de 1.0, que autoGainRef peut alors suivre pour faire
        // redescendre l'affichage a une hauteur comparable.
        //
        // Lissage temporel resserre (etait *0.90 par FRAME) -- avec le
        // recouvrement 8x plus frequent qu'avant, le meme facteur aurait
        // fait retomber la courbe bien plus vite en temps reel (8x plus
        // d'appels/seconde) ; *0.985/frame donne une retombee comparable
        // en temps reel a l'ancien *0.90 non-recouvert, tout en profitant
        // des mises a jour frequentes pour une animation fluide plutot
        // que des paliers.
        float peak = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            const float raw = juce::jlimit (0.0f, 4.0f, fftData[(size_t) i] / (float) fftSize * 4.0f);
            peak = juce::jmax (peak, raw);
            const float current = magnitudes[(size_t) i].load();
            magnitudes[(size_t) i].store (raw > current ? raw : current * 0.985f);
        }

        // Suivi de niveau lent (AGC) -- attaque assez rapide (rattrape une
        // entree plus forte en quelques frames FFT), retombee TRES lente
        // (ne redescend qu'apres un silence prolonge, evite le pompage
        // visuel sur de simples creux de programme) -- voir
        // getMagnitudeAtBin() pour la division qui normalise l'affichage
        // par cette reference. Coefficients resserres (etaient 0.35/0.995
        // par frame) pour la meme raison que le lissage ci-dessus -- frames
        // 8x plus frequentes.
        const float refNow = autoGainRef.load();
        const float target = juce::jmax (kAutoGainFloor, peak);
        autoGainRef.store (target > refNow ? refNow + (target - refNow) * 0.06f : refNow * 0.9994f);
    }

    static constexpr float kAutoGainFloor = 0.05f;

    double fs = 44100.0;
    std::array<float, (size_t) fftSize> fifo {};
    int writePos = 0;
    int samplesSinceHop = 0;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::array<std::atomic<float>, (size_t) numBins> magnitudes { {} };
    std::atomic<float> autoGainRef { kAutoGainFloor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzerState)
};
