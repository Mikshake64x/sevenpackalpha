#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

/**
    Crossover
    ---------
    Separe un signal mono en 6 bandes via 5 points de crossover (kNumSplits
    = 5, kNumBandsOut = 6) -- generalisation du crossover 3 bandes/2 points
    de Tripressor (porte tel quel depuis Nuke Mext) a sixpack. Deux
    modes, selecteur ANALOG / LINEAR cote UI (voir PluginEditor et
    XoverModeButton) :

    - ANALOG (WIDE, par defaut) : extraction par un vrai passe-bas 1er
      ordre (6dB/octave) a CHAQUE point de crossover, et le "reste" a
      chaque etage est calcule par SOUSTRACTION EXACTE (reste = entree -
      bas). Latence nulle.

    - LINEAR (ex-"DIGITAL") : FIR a PHASE LINEAIRE, meme architecture de
      reconstruction par SOUSTRACTION EXACTE que ANALOG. Latence reglable
      via 3 budgets (Low/Medium/High, voir setLatencyMode()).

    (ANALOG 2 -- variante pente 2e ordre testee un temps pour reduire le
    residu grave dans les bandes hautes -- retiree a la demande de
    l'utilisateur : "on va supprimer analog 2". setWideSteepSlope() et le
    parametre idWideSteepSlope n'existent plus.)

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
    un vrai FIR a phase lineaire (bien plus raide que le 1er ordre de WIDE).
    Generalisation a K = kNumSplits etages en cascade (K=2 dans l'original
    Tripressor, K=4 ici) :

        rest_0 = x
        band_i        = FIR_LP_i(rest_i)                          pour i = 0..K-1
        rest_{i+1}     = delay(rest_i, D_i) - band_i               -- D_i = retard de groupe de FIR_LP_i
        band_K (finale) = rest_K                                   -- dernier reste, jamais filtre

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
         d'oscillations en peigne sur les etages internes (signature d'un
         leger desalignement temporel, plus visible sur les filtres courts
         car l'erreur absolue pese proportionnellement plus lourd).
    Plutot que de continuer a deviner le comportement interne d'un moteur
    boite noire, cette classe implemente sa PROPRE convolution FIR directe
    (tampon circulaire, boucle simple) : le retard de groupe d'un FIR
    symetrique (Type I, N coefficients impair) est alors EXACTEMENT (N-1)/2
    PAR CONSTRUCTION MATHEMATIQUE, sans aucune ambiguite. Cout CPU plus
    eleve par echantillon (O(N) au lieu d'une FFT partitionnee), mais pour
    les longueurs utilisees ici, largement gerable en temps reel (build
    Release) -- la garantie de justesse prime desormais sur l'optimisation.

    ALIGNEMENT TEMPOREL (generalisation du bug corrige sur Tripressor 3
    bandes) : chaque band_i (i<K) ne porte intrinsequement que le retard
    cumule JUSQU'A son propre etage (D_0+...+D_i), alors que le dernier
    reste band_K porte le retard cumule TOTAL (D_0+...+D_{K-1}). Sommer les
    bandes brutes sans compensation melangerait donc K+1 references
    temporelles differentes -- pas une identite algebrique valide, meme
    sans aucune compression. Chaque band_i (i<K) recoit donc un retard
    supplementaire de compensation = somme des D_j pour j>i (voir
    bandAligners ci-dessous), pour que les 5 bandes en sortie partagent
    TOUTES la meme reference temporelle cumulee -- generalisation directe
    du lowOutputAligner (D2) de l'architecture 3 bandes.

    Le parametre APVTS reste "digitalCrossover" (compatibilite des sessions
    et projets DAW existants) -- seuls le nom d'affichage ("LINEAR" au lieu
    de "DIGITAL"), les noms de methodes/membres C++ et le DSP interne ont
    change.
    ============================================================================

    IMPORTANT -- fusion de bandes desactivees (voir PluginEditor/
    PluginProcessor) : cette classe splitte TOUJOURS le signal aux 4
    frequences configurees, meme si l'utilisateur a "desactive" un
    crossover dans l'UI pour fusionner 2 bandes voisines. La fusion
    (sommation des sous-bandes brutes + choix du compresseur "proprietaire"
    du groupe fusionne) est geree cote PluginProcessor::processBlock(), pas
    ici -- Crossover reste un simple splitteur a 4 points fixes, sans
    notion d'activation.
*/
class Crossover
{
public:
    static constexpr int kNumSplits   = 5;
    static constexpr int kNumBandsOut = kNumSplits + 1; // 6

    // Bornes et defauts par point de crossover (index 0 = le plus bas, 3 =
    // le plus haut). Reference partagee avec PluginProcessor::
    // createParameterLayout(), qui construit ses NormalisableRange/valeurs
    // par defaut a partir de ces memes tableaux (evite toute divergence
    // entre le DSP et les parametres APVTS).
    //
    // Les 4 bornes sont VOLONTAIREMENT identiques (20Hz-20kHz, la plage
    // audible complete) plutot que resserrees par "slot" comme dans une
    // premiere version -- retour utilisateur : quand une bande voisine est
    // fusionnee (crossover desactive, voir SixpackAudioProcessor::
    // computeGroupOwners()), le crossover actif adjacent doit pouvoir
    // s'etendre librement sur toute la largeur ainsi liberee, pas rester
    // coince dans l'ancien intervalle etroit prevu pour 5 bandes actives.
    // L'ordre croissant entre les 4 (necessaire au DSP, voir Crossover::
    // processBlock()) reste applique dynamiquement cote UI/DSP (voir
    // SixpackAudioProcessor::sanitizeCrossoverFrequencies() et
    // CrossoverSpectrumEditor -- un crossover desactive se laisse pousser
    // par ses voisins actifs plutot que de leur faire obstacle), pas par
    // des bornes de parametre distinctes par indice.
    static constexpr float kSplitMinHz[kNumSplits]     = {   20.0f,   20.0f,    20.0f,    20.0f,    20.0f };
    static constexpr float kSplitMaxHz[kNumSplits]     = { 20000.0f, 20000.0f, 20000.0f, 20000.0f, 20000.0f };
    // Valeurs d'ouverture retour utilisateur explicite (2026-08-20, revues
    // pour la 6e bande "Presence") : 60 / 110 / 300 / 1000 / 2500 Hz.
    static constexpr float kSplitDefaultHz[kNumSplits] = {   60.0f,  110.0f,   300.0f, 1000.0f, 2500.0f };

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

        for (auto& b : branches)
            b.reset();
        dirtyWide = true;

        for (auto& f : firFilters)       f.reset();
        for (auto& d : restDelays)       d.reset();
        for (auto& a : bandAligners)     a.reset();

        dirtyLinear = true;
        updateLinearFilters();
    }

    void reset()
    {
        for (auto& b : branches)
            b.reset();
        for (auto& f : firFilters)       f.reset();
        for (auto& d : restDelays)       d.reset();
        for (auto& a : bandAligners)     a.reset();
    }

    // splitIndex 0..3 (0 = crossover le plus bas). Clampe aux bornes
    // kSplitMinHz/kSplitMaxHz[splitIndex] -- l'ORDRE croissant entre les 4
    // frequences n'est PAS garanti ici (juste les bornes individuelles) :
    // c'est PluginProcessor qui applique la contrainte de monotonie/ecart
    // minimum avant d'appeler ce setter (voir sanitizeCrossoverFrequencies()).
    void setCrossoverHz (int splitIndex, float hz) noexcept
    {
        jassert (splitIndex >= 0 && splitIndex < kNumSplits);
        hz = juce::jlimit (kSplitMinHz[splitIndex], kSplitMaxHz[splitIndex], hz);
        if (! juce::approximatelyEqual (crossoverHz[(size_t) splitIndex], hz))
        {
            crossoverHz[(size_t) splitIndex] = hz;
            dirtyWide = true;
            dirtyLinear = true;
        }
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

    // Latence CUMULEE (tous les etages), en echantillons, du mode
    // actuellement actif -- 0 en WIDE.
    int getLatencySamples() const noexcept
    {
        return linearMode ? currentLinearLatencySamples : 0;
    }

    // Point d'entree unique, quel que soit le mode. outs doit pointer vers
    // kNumBandsOut (5) buffers d'au moins numSamples echantillons, dans
    // l'ordre croissant de frequence (outs[0] = bande la plus basse).
    void processBlock (const float* in, float* const* outs, int numSamples) noexcept
    {
        if (linearMode)
        {
            if (dirtyLinear)
                updateLinearFilters();

            for (int i = 0; i < numSamples; ++i)
            {
                float rawBand[kNumSplits];
                float rest = in[i];

                for (int s = 0; s < kNumSplits; ++s)
                {
                    const float filtered     = firFilters[(size_t) s].processSample (rest);
                    const float delayedRest  = restDelays[(size_t) s].processSample (rest);
                    rest = delayedRest - filtered; // complement EXACT
                    rawBand[s] = filtered;
                }

                outs[kNumSplits][i] = rest; // derniere bande = dernier reste, deja aligne (retard cumule = total)

                for (int s = 0; s < kNumSplits; ++s)
                    outs[s][i] = bandAligners[(size_t) s].processSample (rawBand[s]);
            }
        }
        else
        {
            if (dirtyWide)
                updateWideCoefficients();

            for (int i = 0; i < numSamples; ++i)
            {
                float rest = in[i];
                for (int s = 0; s < kNumSplits; ++s)
                {
                    const float low = branches[(size_t) s].processSample (rest);
                    outs[s][i] = low;
                    rest = rest - low; // complement EXACT, WIDE = filtres 1er ordre, aucun retard a compenser
                }
                outs[kNumSplits][i] = rest;
            }
        }
    }

private:
    // ================= WIDE =================
    // Un seul passe-bas 1er ordre par point de crossover (l'ancien 2e
    // etage "identite" de l'implementation 3-bandes ne faisait rien --
    // simplifie ici en un filtre unique par branche).
    std::array<juce::dsp::IIR::Filter<float>, kNumSplits> branches;

    void updateWideCoefficients()
    {
        const float nyquistMargin = (float) (fs * 0.45);
        for (int s = 0; s < kNumSplits; ++s)
        {
            const float safeHz = juce::jlimit (20.0f, nyquistMargin, crossoverHz[(size_t) s]);
            branches[(size_t) s].coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, safeHz);
        }
        dirtyWide = false;
    }

    std::array<float, kNumSplits> crossoverHz { { kSplitDefaultHz[0], kSplitDefaultHz[1], kSplitDefaultHz[2], kSplitDefaultHz[3], kSplitDefaultHz[4] } };
    bool dirtyWide = true;

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
    // les kNumSplits etages de crossover, pour que la latence CUMULEE
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
        // Plancher releve a kNumSplits*14 (etait 14 pour 2 etages) : une
        // fois le meme budget reparti sur 4 etages au lieu de 2, chaque
        // etage a besoin d'un minimum de coefficients avant que son propre
        // plancher jmax(15,...) individuel ne prenne le relais -- choix de
        // conception propre a la generalisation 5 bandes, absent du code
        // 3-bandes d'origine.
        const int samplesForTarget = juce::jmax (kNumSplits * 14, (int) std::round (targetLatencyMs[(size_t) latencyIndex] * 0.001 * fs));

        const float nyquistMargin = (float) (fs * 0.45);
        float safeHz[kNumSplits];
        double invFreq[kNumSplits];
        double invSum = 0.0;
        for (int s = 0; s < kNumSplits; ++s)
        {
            safeHz[s] = juce::jlimit (20.0f, nyquistMargin, crossoverHz[(size_t) s]);
            invFreq[s] = 1.0 / (double) safeHz[s];
            invSum += invFreq[s];
        }

        // Repartition PROPORTIONNELLE a 1/frequence (PAS 1/sqrt(frequence),
        // bug corrige -- retour utilisateur explicite : "quand je monte le
        // output de la bande low qui a son crossover a 48hz ca monte
        // jusqu'a 100hz, les frequences des bandes en lineaire ne sont pas
        // bien respectees") -- un FIR a une largeur de transition ABSOLUE
        // (en Hz) qui ne depend QUE du nombre de coefficients (a fenetre
        // Kaiser fixee), pas de la frequence de coupure -- donc pour une
        // raideur RELATIVE (en octaves) constante d'un etage a l'autre, il
        // faut N ~ fs/frequence (proportionnel a 1/f), pas 1/sqrt(f) : ce
        // dernier sous-alloue les basses frequences, qui se retrouvent avec
        // une transition bien plus large que necessaire par rapport a leur
        // frequence de coupure (une pente 1/sqrt(f) etait un compromis
        // insuffisant, pas la formule exacte decrite par le commentaire
        // d'origine ci-dessus). Le dernier etage absorbe le reste de
        // l'arrondi pour que la somme reste EXACTEMENT samplesForTarget
        // (meme principe que l'original 2 etages).
        int tapsRaw[kNumSplits];
        int assigned = 0;
        for (int s = 0; s < kNumSplits - 1; ++s)
        {
            const double fraction = invFreq[s] / invSum;
            tapsRaw[s] = (int) std::round (samplesForTarget * fraction);
            assigned += tapsRaw[s];
        }
        tapsRaw[kNumSplits - 1] = samplesForTarget - assigned;

        int delaySamples[kNumSplits];
        for (int s = 0; s < kNumSplits; ++s)
        {
            const int taps = makeOdd (juce::jmax (15, tapsRaw[s]));
            auto coeffs = designLowpassFIR (fs, safeHz[s], taps);
            firFilters[(size_t) s].setCoefficients (std::move (coeffs));

            // Retard EXACT (pas mesure, pas suppose approximativement) :
            // c'est la position du pic du sinc fenetre dans
            // designLowpassFIR(), qui est TOUJOURS a l'indice (N-1)/2 par
            // construction (symetrie du FIR Type I).
            delaySamples[s] = (taps - 1) / 2;
            restDelays[(size_t) s].setDelaySamples (delaySamples[s]);
        }

        int cumDelay = 0;
        for (int s = 0; s < kNumSplits; ++s)
            cumDelay += delaySamples[s];
        currentLinearLatencySamples = cumDelay;

        // bandAligners[s] compense l'ecart entre le retard intrinseque de
        // band_s (somme des delais JUSQU'A l'etage s inclus) et le retard
        // cumule TOTAL -- generalisation du lowOutputAligner (D2) de
        // l'architecture 3 bandes. Le dernier etage (s = kNumSplits-1)
        // recoit toujours un retard de 0 (deja a la reference totale).
        int running = 0;
        for (int s = 0; s < kNumSplits; ++s)
        {
            running += delaySamples[s];
            bandAligners[(size_t) s].setDelaySamples (cumDelay - running);
        }

        dirtyLinear = false;
    }

    double fs = 44100.0;
    int maxBlockSize = 512;

    bool linearMode = false; // WIDE par defaut, comportement inchange
    LatencyMode latencyMode = LatencyMode::Low; // coherent avec le defaut APVTS ("Low CPU")
    bool dirtyLinear = true;

    std::array<DirectFirFilter, kNumSplits>   firFilters;   // extrait band_s = FIR_LP_s(rest_s)
    std::array<IntegerDelayLine, kNumSplits>  restDelays;   // compense le retard du FIR_LP_s avant soustraction (rest_{s+1})
    std::array<IntegerDelayLine, kNumSplits>  bandAligners; // retard de compensation par bande, voir note de classe

    int currentLinearLatencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Crossover)
};
