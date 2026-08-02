#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

/**
    Crossover
    ---------
    Separe un signal mono en 3 bandes (Low / Mid / High) via 2 points de
    crossover. Deux modes, bouton "WIDE / LINEAR" cote UI (voir
    PluginEditor et XoverModeButton) :

    - WIDE (par defaut, INCHANGE) : extraction par un vrai passe-bas 1er
      ordre (6dB/octave), et le "reste" a chaque etage est calcule par
      SOUSTRACTION EXACTE (reste = entree - bas). Latence nulle.

    - LINEAR (ex-"DIGITAL") : FIR a PHASE LINEAIRE, meme architecture de
      reconstruction par SOUSTRACTION EXACTE que WIDE. Latence reglable
      via 3 budgets (Low/Medium/High, voir setLatencyMode()).

    ============================================================================
    HISTORIQUE -- pourquoi LINEAR remplace l'ancien mode "DIGITAL" (LR4) :
    ============================================================================
    L'ancien mode DIGITAL (Linkwitz-Riley 4e ordre, recombine par annulation
    de phase LP4+HP4=passe-tout) casse des que chaque bande recoit un
    FeedbackCompressor INDEPENDANT (gain different par bande) : la
    recombinaison reposant sur une PRESQUE-ANNULATION de 2 vecteurs de
    magnitude quasi egale et de phase quasi opposee au crossover, le
    moindre ecart de gain y est amplifie de facon demesuree (diagnostic
    PluginDoctor : "trous" quasi-totaux pile aux frequences de crossover).

    LINEAR resout ca en reconstruisant par SOUSTRACTION EXACTE (algebrique,
    immune a toute difference de gain entre bandes), comme WIDE, mais avec
    un vrai FIR a phase lineaire (bien plus raide que le 1er ordre de WIDE) :

        low   = FIR_LP(x)
        reste = delay(x, D)      - low     -- D = retard de groupe du FIR_LP
        mid   = FIR_LP2(reste)
        high  = delay(reste, D2) - mid     -- D2 = retard de groupe du FIR_LP2

    ============================================================================
    IMPLEMENTATION FIR -- POURQUOI UNE CONVOLUTION DIRECTE, PAS juce::dsp::Convolution :
    ============================================================================
    Deux tentatives avec juce::dsp::Convolution (moteur FFT partitionne) ont
    chacune introduit une regression serieuse au diagnostic PluginDoctor :
      1) Supposer le retard de groupe theorique (N-1)/2 -- laissait un leger
         creux residuel au crossover (le moteur ajoute vraisemblablement un
         buffering interne non capture par cette formule).
      2) Mesurer empiriquement le retard REEL par impulsion de test sur le
         moteur -- a corrompu durablement l'etat interne du moteur de
         PRODUCTION (resonances en dents de scie), puis, une fois corrige
         pour mesurer sur une instance jetable, a quand meme laisse un motif
         d'oscillations en peigne sur les etages Mid/High (signature d'un
         leger desalignement temporel, plus visible sur les filtres courts
         car l'erreur absolue pese proportionnellement plus lourd).
    Plutot que de continuer a deviner le comportement interne d'un moteur
    boite noire, cette classe implemente sa PROPRE convolution FIR directe
    (tampon circulaire, boucle simple) : le retard de groupe d'un FIR
    symetrique (Type I, N coefficients impair) est alors EXACTEMENT (N-1)/2
    PAR CONSTRUCTION MATHEMATIQUE, sans aucune ambiguite. Cout CPU plus
    eleve par echantillon (O(N) au lieu d'une FFT partitionnee), mais pour
    les longueurs utilisees ici (quelques centaines a ~2000 coefficients),
    largement gerable en temps reel (build Release) -- la garantie de
    justesse prime desormais sur l'optimisation.

    BUG D'ALIGNEMENT TEMPOREL CORRIGE (trouve via un test Dry/Wet a 0%, qui
    isole le crossover pur de toute compression) : au 2eme etage,
    mid+high = rest[n-D2] porte un retard CUMULE D+D2 (D du 1er etage, D2
    du 2eme). Mais "low" seul ne porte que D (celui de son propre FIR). Au
    MEME indice de sortie n, low et (mid+high) referencaient donc 2 instants
    DIFFERENTS du signal d'entree -- pas une identite algebrique valide,
    meme sans compression. Un retard supplementaire de D2 sur Low
    (lowOutputAligner) aligne les 3 bandes sur la MEME reference temporelle
    cumulee D+D2. C'est l'equivalent, pour cette architecture par
    soustraction, du passe-tout de compensation de phase que l'ancien mode
    DIGITAL appliquait deja sur Low pour la meme raison structurelle.

    Le parametre APVTS reste "digitalCrossover" (compatibilite des sessions
    et projets DAW existants) -- seuls le nom d'affichage ("LINEAR" au lieu
    de "DIGITAL"), les noms de methodes/membres C++ et le DSP interne ont
    change.
    ============================================================================

    Points de crossover calibres via mesures PluginDoctor (point -3dB aux 2
    butees mesurees de chaque knob hardware) :
      - Low  crossover (-3dB) : ~30Hz (mini) a ~155Hz (maxi)
      - High crossover (-3dB) : ~570Hz (mini) a ~2930Hz (maxi)
*/
class Crossover
{
public:
    struct Bands
    {
        float low = 0.0f, mid = 0.0f, high = 0.0f;
    };

    // EXPLICITE et necessaire : JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (voir tout en bas de la classe) supprime le constructeur de copie --
    // et en C++, declarer (meme supprime) UN SEUL constructeur quel qu'il
    // soit empeche le compilateur de generer implicitement le constructeur
    // PAR DEFAUT. Sans cette ligne, ni std::vector<Crossover>::resize() ni
    // std::make_unique<Crossover>() ne peuvent construire l'objet.
    Crossover() = default;

    // Budget de latence du mode LINEAR (voir setLatencyMode()). Choisi par
    // l'utilisateur via le menu engrenage (LatencyModeButton), pas
    // automatisable -- c'est un reglage "technique", pas un parametre de mix.
    enum class LatencyMode { Low = 0, Medium = 1, High = 2 };

    void prepare (double sampleRate, int maximumBlockSize)
    {
        fs = sampleRate;
        maxBlockSize = juce::jmax (1, maximumBlockSize);

        for (auto* f : allWideFilters())
            f->reset();
        dirtyWide = true;

        lowFilter.reset();
        midFilter.reset();
        delayLowSplit.reset();
        delayHighSplit.reset();
        lowOutputAligner.reset();

        dirtyLinear = true;
        updateLinearFilters();
    }

    void reset()
    {
        for (auto* f : allWideFilters())
            f->reset();
        lowFilter.reset();
        midFilter.reset();
        delayLowSplit.reset();
        delayHighSplit.reset();
        lowOutputAligner.reset();
    }

    void setLowCrossoverHz (float hz) noexcept
    {
        hz = juce::jlimit (20.0f, 400.0f, hz);
        if (! juce::approximatelyEqual (lowHz, hz)) { lowHz = hz; dirtyWide = true; dirtyLinear = true; }
    }

    void setHighCrossoverHz (float hz) noexcept
    {
        hz = juce::jlimit (300.0f, 6000.0f, hz);
        if (! juce::approximatelyEqual (highHz, hz)) { highHz = hz; dirtyWide = true; dirtyLinear = true; }
    }

    // false = WIDE (1er ordre, soustraction exacte, 0 latence, comportement
    //         par defaut inchange)
    // true  = LINEAR (FIR phase lineaire, soustraction exacte, latence
    //         reglable -- voir setLatencyMode())
    void setLinearPhaseMode (bool shouldBeLinear) noexcept
    {
        linearMode = shouldBeLinear;
    }

    // Budget de latence du mode LINEAR -- ignore en mode WIDE (toujours 0).
    void setLatencyMode (LatencyMode mode) noexcept
    {
        if (mode != latencyMode) { latencyMode = mode; dirtyLinear = true; }
    }

    // Latence CUMULEE (etage bas + etage haut), en echantillons, du mode
    // actuellement actif -- 0 en WIDE.
    int getLatencySamples() const noexcept
    {
        return linearMode ? currentLinearLatencySamples : 0;
    }

    // Point d'entree unique, quel que soit le mode.
    void processBlock (const float* in, float* outLow, float* outMid, float* outHigh, int numSamples) noexcept
    {
        if (linearMode)
        {
            if (dirtyLinear)
                updateLinearFilters();

            for (int i = 0; i < numSamples; ++i)
            {
                const float x = in[i];
                const float low = lowFilter.processSample (x);
                const float delayedX = delayLowSplit.processSample (x);
                const float rest = delayedX - low;

                const float mid = midFilter.processSample (rest);
                const float delayedRest = delayHighSplit.processSample (rest);
                const float high = delayedRest - mid;

                // BUG STRUCTUREL CORRIGE : mid+high porte un retard cumule
                // D+D2 (D du 1er etage + D2 du 2eme, voir note de classe),
                // alors que "low" seul ne porte que D (celui de son propre
                // FIR). Sans ce retard supplementaire de D2 sur Low, la
                // somme low+mid+high melange 2 references temporelles
                // differentes -- pas une identite algebrique valide, meme
                // sans aucune compression (confirme par un test Dry/Wet a
                // 0%, qui isole le crossover pur de toute dynamique).
                const float lowAligned = lowOutputAligner.processSample (low);

                outLow[i]  = lowAligned;
                outMid[i]  = mid;
                outHigh[i] = high;
            }
        }
        else
        {
            if (dirtyWide)
                updateWideCoefficients();

            for (int i = 0; i < numSamples; ++i)
            {
                const auto b = processWideSample (in[i]);
                outLow[i]  = b.low;
                outMid[i]  = b.mid;
                outHigh[i] = b.high;
            }
        }
    }

private:
    // ================= WIDE (INCHANGE) =================
    struct BranchFilterFloat
    {
        juce::dsp::IIR::Filter<float> stage1, stage2;
        inline float processSample (float x) noexcept { return stage2.processSample (stage1.processSample (x)); }
        void reset() { stage1.reset(); stage2.reset(); }
    };

    inline Bands processWideSample (float x) noexcept
    {
        Bands b;
        b.low = loBranch.processSample (x);
        const float rest1 = x - b.low; // complement EXACT

        b.mid  = hiBranch.processSample (rest1);
        b.high = rest1 - b.mid; // complement EXACT
        return b;
    }

    std::array<juce::dsp::IIR::Filter<float>*, 4> allWideFilters()
    {
        return { &loBranch.stage1, &loBranch.stage2, &hiBranch.stage1, &hiBranch.stage2 };
    }

    void updateWideCoefficients()
    {
        const float nyquistMargin = (float) (fs * 0.45);
        const float safeLow  = juce::jlimit (20.0f, nyquistMargin, lowHz);
        const float safeHigh = juce::jlimit (20.0f, nyquistMargin, highHz);

        loBranch.stage1.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, safeLow);
        loBranch.stage2.coefficients = identityCoeffsFloat();

        hiBranch.stage1.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, safeHigh);
        hiBranch.stage2.coefficients = identityCoeffsFloat();

        dirtyWide = false;
    }

    static juce::dsp::IIR::Coefficients<float>::Ptr identityCoeffsFloat()
    {
        return new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }

    BranchFilterFloat loBranch, hiBranch;
    float lowHz  = 80.0f;
    float highHz = 1200.0f;
    bool  dirtyWide = true;

    // ================= LINEAR (FIR phase lineaire, convolution directe) =================

    // Filtre FIR direct-form : tampon circulaire simple + boucle de
    // convolution, DELIBEREMENT non-optimise (pas de FFT, pas de
    // partitionnement) -- le retard de groupe d'un FIR symetrique impair
    // (Type I) est EXACT et connu a la compilation : (N-1)/2. Voir note de
    // classe pour l'historique complet (pourquoi pas juce::dsp::Convolution).
    class DirectFirFilter
    {
    public:
        void setCoefficients (std::vector<float> newCoeffs)
        {
            coeffs = std::move (newCoeffs);
            const size_t n = juce::jmax ((size_t) 1, coeffs.size());
            // Tampon DOUBLE LONGUEUR : l'echantillon courant est ecrit a 2
            // endroits (writePos et writePos+n), ce qui garantit une
            // fenetre de lecture CONTIGUE de n echantillons (pas de modulo
            // par coefficient dans la boucle chaude) -- le compilateur
            // auto-vectorise (SIMD) bien plus facilement un simple produit
            // scalaire sur une plage contigue qu'une boucle avec indexation
            // circulaire par coefficient.
            history.assign (n * 2, 0.0f);
            writePos = 0;
        }

        void reset()
        {
            std::fill (history.begin(), history.end(), 0.0f);
            writePos = 0;
        }

        inline float processSample (float x) noexcept
        {
            const size_t n = coeffs.size();
            if (n == 0)
                return x;

            history[writePos] = x;
            history[writePos + n] = x;

            // Fenetre contigue [writePos+1 .. writePos+n] : contient les n
            // derniers echantillons dans l'ordre chronologique (le plus
            // ancien en window[0], le plus recent en window[n-1]) --
            // verifie empiriquement, l'index d'ecriture doit progresser en
            // sens CROISSANT (mod n) pour que cette fenetre soit correcte.
            const float* window = history.data() + writePos + 1;
            float acc = 0.0f;
            for (size_t k = 0; k < n; ++k)
                acc += coeffs[k] * window[k];

            writePos = (writePos + 1 == n) ? 0 : (writePos + 1);
            return acc;
        }

    private:
        std::vector<float> coeffs;
        std::vector<float> history;
        size_t writePos = 0;
    };

    // Ligne a retard a nombre d'echantillons ENTIER exact -- pas d'
    // interpolation, pas d'ambiguite (contrairement a juce::dsp::DelayLine
    // dont on ne maitrisait pas bien le comportement interne).
    class IntegerDelayLine
    {
    public:
        void setDelaySamples (int delaySamples)
        {
            delay = juce::jmax (0, delaySamples);
            buffer.assign ((size_t) (delay + 1), 0.0f);
            writePos = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        inline float processSample (float x) noexcept
        {
            if (buffer.size() <= 1)
                return x; // delai nul

            buffer[(size_t) writePos] = x;
            int readPos = writePos - delay;
            if (readPos < 0)
                readPos += (int) buffer.size();
            const float y = buffer[(size_t) readPos];
            writePos = (writePos + 1) % (int) buffer.size();
            return y;
        }

    private:
        std::vector<float> buffer;
        int writePos = 0;
        int delay = 0;
    };

    // Budget de latence CIBLE par mode, en millisecondes -- reparti entre
    // les 2 etages de crossover (bas et haut), pour que la latence CUMULEE
    // corresponde a la cible choisie par l'utilisateur.
    static constexpr float targetLatencyMs[3] = { 7.0f, 25.0f, 80.0f };

    static int makeOdd (int n) noexcept { return (n % 2 == 0) ? n + 1 : n; }

    // Conception d'un passe-bas FIR a phase lineaire (Type I, N coefficients
    // impair -> retard de groupe EXACTEMENT (N-1)/2 echantillons) par la
    // methode fenetree (sinc ideal x fenetre de Kaiser). Beta=6.0 vise une
    // attenuation de bande coupee d'environ 60dB.
    static std::vector<float> designLowpassFIR (double sampleRate, float cutoffHz, int numTaps)
    {
        numTaps = juce::jmax (15, makeOdd (numTaps));
        const int M = numTaps - 1;
        const double fc = (double) cutoffHz / sampleRate; // frequence normalisee (0..0.5)
        constexpr double beta = 6.0;
        const double i0Beta = besselI0 (beta);

        std::vector<float> h ((size_t) numTaps, 0.0f);

        double sum = 0.0;
        for (int n = 0; n <= M; ++n)
        {
            const double m = (double) n - (double) M * 0.5;
            double sincVal;
            if (std::abs (m) < 1.0e-9)
                sincVal = 2.0 * fc;
            else
                sincVal = std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);

            const double ratio = (M > 0) ? ((2.0 * (double) n / (double) M) - 1.0) : 0.0; // -1..1
            const double windowArg = beta * std::sqrt (juce::jmax (0.0, 1.0 - ratio * ratio));
            const double window = besselI0 (windowArg) / i0Beta;

            const double val = sincVal * window;
            h[(size_t) n] = (float) val;
            sum += val;
        }

        // Normalisation du gain DC a 1.
        if (std::abs (sum) > 1.0e-9)
        {
            const float norm = (float) (1.0 / sum);
            for (auto& v : h)
                v *= norm;
        }

        return h;
    }

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            const double kd = (double) k;
            term *= (x / (2.0 * kd)) * (x / (2.0 * kd));
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }

    void updateLinearFilters()
    {
        const int latencyIndex = (int) latencyMode;
        const int samplesForTarget = juce::jmax (14, (int) std::round (targetLatencyMs[(size_t) latencyIndex] * 0.001 * fs));

        const float nyquistMargin = (float) (fs * 0.45);
        const float safeLow  = juce::jlimit (20.0f, nyquistMargin, lowHz);
        const float safeHigh = juce::jlimit (20.0f, nyquistMargin, highHz);

        // Repartition PROPORTIONNELLE a 1/sqrt(frequence), pas 50/50 -- un
        // FIR a besoin d'un nombre de coefficients ~ fs/frequence pour une
        // raideur RELATIVE (en octaves) donnee : a nombre de coefficients
        // egal, un crossover a 97Hz reste tres mou alors qu'un crossover a
        // 2845Hz devient bien plus raide que necessaire.
        const double invLow  = 1.0 / std::sqrt ((double) safeLow);
        const double invHigh = 1.0 / std::sqrt ((double) safeHigh);
        const double lowFraction = invLow / (invLow + invHigh);

        const int tapsLowRaw  = (int) std::round (samplesForTarget * lowFraction);
        const int tapsHighRaw = samplesForTarget - tapsLowRaw;

        const int tapsLow  = makeOdd (juce::jmax (15, tapsLowRaw));
        const int tapsHigh = makeOdd (juce::jmax (15, tapsHighRaw));

        auto lowCoeffs  = designLowpassFIR (fs, safeLow,  tapsLow);
        auto highCoeffs = designLowpassFIR (fs, safeHigh, tapsHigh);

        lowFilter.setCoefficients (std::move (lowCoeffs));
        midFilter.setCoefficients (std::move (highCoeffs));

        // Retard EXACT (pas mesure, pas suppose approximativement) : c'est
        // la position du pic du sinc fenetre dans designLowpassFIR(), qui
        // est TOUJOURS a l'indice (N-1)/2 par construction (symetrie du
        // FIR Type I). Aucune ambiguite possible, contrairement au moteur
        // de convolution boite noire utilise precedemment.
        lowSplitDelaySamples  = (tapsLow  - 1) / 2;
        highSplitDelaySamples = (tapsHigh - 1) / 2;
        currentLinearLatencySamples = lowSplitDelaySamples + highSplitDelaySamples;

        delayLowSplit.setDelaySamples (lowSplitDelaySamples);
        delayHighSplit.setDelaySamples (highSplitDelaySamples);

        // Retard SUPPLEMENTAIRE sur Low : D2 uniquement (pas D+D2) -- Low
        // porte deja intrinsequement D via son propre filtrage, il ne lui
        // manque que le D2 accumule par Mid/High au 2eme etage pour etre
        // sur la MEME reference temporelle (voir note de classe / bug
        // structurel corrige).
        lowOutputAligner.setDelaySamples (highSplitDelaySamples);

        dirtyLinear = false;
    }

    double fs = 44100.0;
    int maxBlockSize = 512;

    bool linearMode = false; // WIDE par defaut, comportement inchange
    LatencyMode latencyMode = LatencyMode::Low; // coherent avec le defaut APVTS ("Low CPU")
    bool dirtyLinear = true;

    DirectFirFilter lowFilter;  // extrait LOW = FIR_LP(x)
    DirectFirFilter midFilter;  // extrait MID = FIR_LP2(reste)
    IntegerDelayLine delayLowSplit;  // compense le retard du FIR_LP  avant soustraction
    IntegerDelayLine delayHighSplit; // compense le retard du FIR_LP2 avant soustraction
    IntegerDelayLine lowOutputAligner; // retard supplementaire D2 sur Low, pour aligner avec Mid+High (voir note de classe)

    int lowSplitDelaySamples  = 0;
    int highSplitDelaySamples = 0;
    int currentLinearLatencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Crossover)
};
