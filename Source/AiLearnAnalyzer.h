#pragma once
#include <JuceHeader.h>
#include <utility>
#include "Crossover.h"
#include "FeedbackCompressor.h"

/**
    AiLearnAnalyzer
    ---------------
    Moteur du bouton "AI" (barre de controle, au-dessus de Analog/Linear) :
    capture quelques secondes du programme audio en cours (STEREO -- voir
    plus bas, necessaire pour l'analyse de largeur), puis calcule hors du
    thread audio des valeurs de depart pour Crossover Low/High et, par
    bande, Input/Ratio/Attack/Release/Mix/Width. PAS un vrai reseau de
    neurones -- un jeu de regles/heuristiques (analyse spectrale + facteur
    de crete + correlation stereo), mais le comportement cote utilisateur
    (bouton qui "ecoute" puis pose des valeurs) est celui demande.

    Duree de capture allongee a 8s (demande explicite : "5 secondes c'est
    pas beaucoup pour faire une moyenne") -- plus de materiau = mesures RMS/
    crete/stereo plus representatives du morceau, moins sensibles a un
    silence ou un unique coup de grosse caisse tombes dans la fenetre.

    Cycle de vie (State) :
      Idle -> Capturing (le thread audio remplit `capturedL/R` via
      pushSamples(), voir PluginProcessor::processBlock()) -> des que le
      buffer est plein, le thread AUDIO bascule l'etat sur Analyzing et
      demarre CE thread (juce::Thread) pour le calcul lourd (FFT, mesures
      par bande) -> Done (resultat pret, lu par l'editeur via getResult()
      puis acknowledge() pour revenir a Idle).

    Thread-safety : `capturedL/R` ne sont ecrits QUE pendant Capturing
    (thread audio) et QUE lus pendant Analyzing/Done (ce thread) -- jamais
    les deux en meme temps, donc pas besoin de verrou sur les buffers eux-
    memes. `result` est un POD simple ecrit juste avant le store() de
    `state` a Done ; la paire store(seq_cst)/load(seq_cst) sur `state`
    etablit la relation "happens-before" necessaire pour que la lecture de
    `result` cote editeur (apres avoir observe Done) soit garantie a jour --
    idiome standard de synchronisation par drapeau atomique, pas besoin
    d'atomique sur `result` lui-meme.
*/
class AiLearnAnalyzer : private juce::Thread
{
public:
    enum class State { Idle, Capturing, Analyzing, Done };

    // Cibles de loudness integree (LUFS) par plateforme, memes indices que
    // l'AudioParameterChoice idAiMasteringTarget (PluginProcessor.cpp) :
    // "Off", "Apple Music", "YouTube", "Spotify", "Deezer", "SoundCloud",
    // "TV", "CD", "Side Key". Reperes d'usage courant dans l'industrie (pas
    // des specifications officielles garanties -- ces cibles evoluent
    // parfois cote plateforme) : Apple Music/Spotify/YouTube visent
    // generalement -14 a -16 LUFS, TV suit EBU R128 (-23 LUFS), CD n'a pas
    // de norme stricte -- on vise un master "loud" traditionnel (~-9 LUFS).
    // "Side Key" (demande explicite) est le repere le plus agressif de la
    // liste, -6 LUFS -- une cible LUFS comme les autres, rien de special.
    static constexpr int kNumMasteringTargets = 9;
    static constexpr float kLufsTargets[kNumMasteringTargets] =
    {
        0.0f,    // Off (non utilise, voir applyLoudnessTarget)
        -16.0f,  // Apple Music
        -14.0f,  // YouTube
        -14.0f,  // Spotify
        -15.0f,  // Deezer
        -14.0f,  // SoundCloud
        -23.0f,  // TV (EBU R128)
        -9.0f,   // CD
        -6.0f,   // Side Key
    };

    struct Result
    {
        float crossoverLowHz = 121.5f, crossoverHighHz = 2521.0f;
        std::array<float, 3> inputDb {};
        std::array<float, 3> ratioIndex {};  // 0..6 (flottant, l'appelant arrondit)
        std::array<float, 3> attackMs {};
        std::array<float, 3> releaseMs {};
        std::array<float, 3> mixPercent   { { 100.0f, 100.0f, 100.0f } }; // 0..100
        std::array<float, 3> widthPercent { { 100.0f, 100.0f, 100.0f } }; // 0..200, 100=neutre

        // Portee (menu Options, section AI, demande explicite : "AI ne
        // s'occupe que de la compression si on le souhaite") -- decide ce
        // que l'EDITEUR a le droit de pousser vers les parametres. Les
        // valeurs ci-dessus sont TOUJOURS calculees en interne (necessaires
        // au split par bande, meme quand elles ne sont pas poussees), seuls
        // ces drapeaux decident de la partie realmente appliquee.
        bool applyCrossoverScope   = true;
        bool applyDetectorsScope   = false;
        bool applyCompressionScope = true;

        // Detecteur sidechain (Bell) par bande -- pointe sur la frequence
        // dominante mesuree DANS cette bande, engage seulement si un pic
        // assez marque a ete trouve. Calcule seulement si applyDetectorsScope.
        std::array<bool, 3>  detectorBellOn     { { false, false, false } };
        std::array<float, 3> detectorBellFreqHz { { 650.0f, 650.0f, 650.0f } };
        std::array<float, 3> detectorBellGainDb { { 6.0f, 6.0f, 6.0f } };

        // Cible de mastering (menu Options, section AI) -- seulement rempli
        // si un target != Off etait selectionne au moment du clic. Meme
        // correction (en dB) appliquee aux 3 Output de bande + Soft Clip
        // recommande ON (garde-fou true-peak) -- voir doc de classe.
        bool applyLoudnessTarget = false;
        std::array<float, 3> outputDb {};
        bool recommendSoftClipOn = false;
        float measuredLufs = -23.0f; // informatif seulement, pas applique tel quel

        // Style musical (menu Options, section AI, demande explicite :
        // "plusieurs styles musicaux qui vont changer la balance des
        // outputs en fonction du style") -- simple offset statique par
        // bande, voir kStyleOutputBiasDb. "Off" = pas d'offset.
        bool applyStyle = false;
        std::array<float, 3> styleOutputDb {};

        // Fidelity (menu Options, section AI, demande explicite : "une
        // fonction qui reprend la balance bass mid aigue du mix pour la
        // remettre pareil apres compression") -- mesure la balance relative
        // Low/Mid/High AVANT compression puis la balance qu'aurait le
        // signal APRES (simulation), et corrige l'Output de chaque bande
        // pour retrouver la balance d'origine. Independant de tout le
        // reste -- se combine (s'additionne) avec Style et la cible LUFS.
        bool applyFidelity = false;
        std::array<float, 3> fidelityOutputDb {};

        // Chaque axe (Compression Mode / Gain Reduction / Attack-Release
        // Character) a son propre choix "Off" (demande explicite : "on doit
        // pouvoir agir que sur les points AI qui ne vont pas") -- decide si
        // l'EDITEUR pousse la cible CORRESPONDANTE. Les valeurs ci-dessus
        // (inputDb/ratioIndex/attackMs/releaseMs) sont TOUJOURS calculees en
        // interne (necessaires les unes aux autres, ex: la simulation de
        // plafond GR a besoin d'un Input meme si son axe est Off), seuls ces
        // drapeaux decident de ce qui est reellement pousse.
        bool pushInput          = true; // gouverne par AI Compression Mode
        bool pushRatio          = true; // gouverne par AI Gain Reduction
        bool pushAttackRelease  = true; // gouverne par AI Attack/Release Character
    };

    // Modes de compression proposes par le bouton AI (demande explicite :
    // "3 mode de compression. Soft, Medium, et Hard") -- indices alignes
    // sur l'AudioParameterChoice idAiCompressionMode. Chaque champ influence
    // une etape du calcul par bande (voir run()) :
    //   ratioBoost   : multiplie "t" (facteur de crete normalise) avant de
    //                  choisir le ratio -- Hard autorise a remonter plus
    //                  haut dans la table de ratios pour le meme signal.
    //   inputTargetDb: cible RMS visee en entree de detecteur -- Hard pousse
    //                  plus fort (plus proche de 0dB), Soft reste prudent.
    //   attackScale/releaseScale : multiplicateurs sur les temps calcules --
    //                  Hard = plus rapide (< 1), Soft = plus lent (> 1).
    // NB : "Off" (index 0 du parametre APVTS) n'a pas d'entree ici -- ces
    // tables ne couvrent que les 3 VRAIS modes (indices param 1..3, voir
    // kNumCompressionModeChoices/kNumAttackReleaseCharacterChoices/
    // kNumGainReductionModeChoices plus bas pour le total incluant Off).
    struct CompressionModeBias { float ratioBoost, inputTargetDb, attackScale, releaseScale; };
    static constexpr int kNumCompressionModes = 3;
    static constexpr int kNumCompressionModeChoices = 4; // + "Off"
    static constexpr CompressionModeBias kCompressionModeBias[kNumCompressionModes] =
    {
        { 0.65f, -21.0f, 1.35f, 1.30f }, // Soft
        { 1.00f, -18.0f, 1.00f, 1.00f }, // Medium (comportement d'origine)
        { 1.45f, -15.0f, 0.70f, 0.75f }, // Hard
    };

    // Caractere Attack/Release propose par le bouton AI (demande explicite :
    // "Punchy, Smooth et Medium ... punchy mets les releases court", ordre
    // Smooth a gauche/Punchy a droite) -- AXE INDEPENDANT du mode de
    // compression ci-dessus (intensite) : celui-ci ne change QUE la FORME
    // attack/release, pas le ratio ni l'input. Multiplie APRES
    // kCompressionModeBias (les deux se combinent). Smooth = attack plus
    // rapide (rattrape tout, plus transparent) + release plus long ; Medium
    // = neutre (1.0/1.0) ; Punchy = attack plus lent (laisse passer le
    // transitoire) + release COURT.
    struct AttackReleaseCharacterBias { float attackScale, releaseScale; };
    static constexpr int kNumAttackReleaseCharacters = 3;
    static constexpr int kNumAttackReleaseCharacterChoices = 4; // + "Off"
    static constexpr AttackReleaseCharacterBias kAttackReleaseCharacterBias[kNumAttackReleaseCharacters] =
    {
        { 0.65f, 1.55f }, // Smooth
        { 1.00f, 1.00f }, // Medium
        { 1.35f, 0.55f }, // Punchy
    };

    // Intensite GLOBALE de gain reduction (demande explicite suite a "je
    // trouve que ca compresse beaucoup" : Smooth/Mid/Hard) -- AXE
    // SUPPLEMENTAIRE, independant du mode de compression et du caractere
    // attack/release ci-dessus. Multiplie ratioBoost ET recule (ou avance)
    // la cible d'input -- Smooth calme franchement l'ensemble (moins de
    // ratio, moins pousse), Hard va plus loin. Se combine avec
    // kCompressionModeBias (les deux se multiplient) -- Medium/Mid =
    // combinaison neutre, comportement d'origine.
    struct GainReductionBias { float ratioMul, inputBackoffDb; };
    static constexpr int kNumGainReductionModes = 3;
    static constexpr int kNumGainReductionModeChoices = 4; // + "Off"
    static constexpr GainReductionBias kGainReductionBias[kNumGainReductionModes] =
    {
        { 0.45f,  +5.0f }, // Smooth
        { 1.00f,   0.0f }, // Mid (comportement d'origine)
        { 1.40f,  -3.0f }, // Hard
    };

    // Plafond EXPLICITE de gain reduction en dB par mode (demande
    // explicite : "le smooth tape max a -5dB de GR, le medium a -9 et le
    // hard -15"). Verifie APRES coup par simulation (voir run()) : le
    // ratio/input choisis ci-dessus donnent un point de depart, puis on
    // simule le VRAI FeedbackCompressor (meme classe que le DSP temps reel)
    // sur le signal de la bande et on mesure la GR crete reellement
    // obtenue ; si elle depasse le plafond, on recule l'Input (dichotomie)
    // jusqu'a rentrer dans le plafond -- jamais l'inverse (on ne pousse
    // jamais l'Input pour ATTEINDRE le plafond, seulement pour ne pas le
    // depasser).
    static constexpr float kGainReductionCapDb[kNumGainReductionModes] = { 5.0f, 9.0f, 15.0f };

    // Tables ratio/knee dupliquees depuis SuppressorAudioProcessor (pas
    // d'include croise possible -- PluginProcessor.h inclut deja ce
    // fichier). Doivent rester synchronisees si la table change la-bas.
    static constexpr float kRatioTable[7] = { 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 20.0f };
    static constexpr float kKneeTable[7]  = { 24.0f, 18.0f, 12.0f, 9.0f, 7.5f, 6.0f, 1.0f };

    // Styles musicaux (menu Options, section AI, demande explicite :
    // "plusieurs styles musicaux qui vont changer la balance des outputs en
    // fonction du style. Bass +3dB sur la bande bass, Loud +3dB en bass et
    // high...") -- offsets STATIQUES par bande (Low/Mid/High) ajoutes a
    // l'Output, independamment de tout le reste (ratio/input/etc). Simple
    // lookup, pas d'analyse -- contrairement a Fidelity ci-dessous. "Off"
    // (index 0, pas d'entree ici) = aucun offset.
    static constexpr int kNumStyles = 5;
    static constexpr int kNumStyleChoices = 6; // + "Off"
    static constexpr float kStyleOutputBiasDb[kNumStyles][3] =
    {
        { 3.0f, 0.0f, 0.0f }, // Bass
        { 3.0f, 0.0f, 3.0f }, // Loud (courbe "smiley")
        { 2.0f, 1.0f, 2.0f }, // Rock (leger boost general, un peu de presence)
        { 0.0f, 2.0f, 1.0f }, // Pop (presence vocale + un peu d'air)
        { 4.0f, 0.0f, 3.0f }, // Electronic (smiley plus marque)
    };

    AiLearnAnalyzer() : juce::Thread ("AI Learn Analyzer") {}
    ~AiLearnAnalyzer() override { stopThread (2000); }

    // Demarre une nouvelle capture -- ignore si une capture/analyse est
    // deja en cours. Appele depuis le thread MESSAGE (clic sur le bouton AI).
    // masteringTargetIndex : valeur courante de idAiMasteringTarget au
    // moment du clic (0 = Off) -- voir kLufsTargets. compressionModeIndex/
    // attackReleaseCharacterIndex/gainReductionModeIndex : 0 = Off (n'ecrit
    // pas la cible correspondante, voir Result::pushInput/pushRatio/
    // pushAttackRelease), 1..3 = le mode reel (index-1 dans les tables de
    // biais ci-dessus). scopeCrossover/Detectors/Compression : valeurs
    // courantes des 3 interrupteurs de portee (menu Options, section AI)
    // au moment du clic.
    void beginCapture (double sampleRateIn, int masteringTargetIndex, int compressionModeIndex,
                        int attackReleaseCharacterIndex, int gainReductionModeIndex,
                        bool scopeCrossover, bool scopeDetectors, bool scopeCompression,
                        int styleIndex, bool fidelityEnabled,
                        double durationSeconds = 8.0)
    {
        auto s = state.load();
        if (s == State::Capturing || s == State::Analyzing)
            return;

        sampleRate = sampleRateIn;
        masteringTarget = juce::jlimit (0, kNumMasteringTargets - 1, masteringTargetIndex);
        compressionMode  = juce::jlimit (0, kNumCompressionModeChoices - 1, compressionModeIndex);
        attackReleaseCharacter = juce::jlimit (0, kNumAttackReleaseCharacterChoices - 1, attackReleaseCharacterIndex);
        gainReductionMode = juce::jlimit (0, kNumGainReductionModeChoices - 1, gainReductionModeIndex);
        applyCrossover   = scopeCrossover;
        applyDetectors   = scopeDetectors;
        applyCompression = scopeCompression;
        style            = juce::jlimit (0, kNumStyleChoices - 1, styleIndex);
        fidelity         = fidelityEnabled;
        targetSamples = juce::jmax (1, (int) (sampleRateIn * durationSeconds));
        capturedL.setSize (1, targetSamples, false, false, true);
        capturedR.setSize (1, targetSamples, false, false, true);
        writePos = 0;
        state.store (State::Capturing);
    }

    // Appele UNIQUEMENT depuis processBlock() (thread audio), et seulement
    // si getState() == Capturing -- copie les DEUX canaux (necessaire pour
    // la mesure de largeur stereo par bande, voir run()). Si l'entree est
    // mono (right == nullptr), duplique L sur R -- le calcul de largeur
    // convergera naturellement vers "neutre" (aucune energie de cote).
    // Des que le buffer cible est plein, bascule sur Analyzing et demarre
    // le calcul en tache de fond.
    void pushSamples (const float* left, const float* right, int numSamples) noexcept
    {
        if (state.load() != State::Capturing)
            return;

        auto* destL = capturedL.getWritePointer (0);
        auto* destR = capturedR.getWritePointer (0);
        const int remaining = targetSamples - writePos;
        const int n = juce::jmin (numSamples, remaining);
        for (int i = 0; i < n; ++i)
        {
            destL[writePos + i] = left[i];
            destR[writePos + i] = (right != nullptr) ? right[i] : left[i];
        }
        writePos += n;

        if (writePos >= targetSamples)
        {
            state.store (State::Analyzing);
            startThread (juce::Thread::Priority::low);
        }
    }

    State getState() const noexcept { return state.load(); }

    // Valide uniquement apres avoir observe getState() == Done -- voir note
    // de thread-safety ci-dessus.
    const Result& getResult() const noexcept { return result; }

    // A appeler par l'editeur une fois le resultat applique, pour permettre
    // un nouvel apprentissage.
    void acknowledge() noexcept { state.store (State::Idle); }

private:
    void run() override
    {
        Result r;
        r.applyCrossoverScope   = applyCrossover;
        r.applyDetectorsScope   = applyDetectors;
        r.applyCompressionScope = applyCompression;

        // Style : simple lookup, aucune analyse necessaire -- "Off" (index
        // 0 du parametre) n'a pas d'entree dans kStyleOutputBiasDb.
        r.applyStyle = style > 0;
        if (r.applyStyle)
        {
            const auto& biasDb = kStyleOutputBiasDb[(size_t) (style - 1)];
            r.styleOutputDb[0] = biasDb[0];
            r.styleOutputDb[1] = biasDb[1];
            r.styleOutputDb[2] = biasDb[2];
        }
        r.applyFidelity = fidelity;

        // Chaque axe (1..3 = mode reel, 0 = Off) decide independamment si sa
        // cible est poussee -- voir doc de Result. Les index de biais
        // ci-dessous retombent sur "Medium" (indice 1) quand un axe est Off,
        // uniquement pour garder un calcul interne sain (ex: la simulation
        // de plafond GR a besoin d'un Input meme si Compression Mode est Off).
        const bool compressionModeOn = compressionMode > 0;
        const bool characterOn       = attackReleaseCharacter > 0;
        const bool gainReductionOn   = gainReductionMode > 0;
        r.pushInput         = compressionModeOn;
        r.pushAttackRelease = characterOn;
        r.pushRatio         = gainReductionOn;
        const int compressionBiasIdx = juce::jlimit (0, kNumCompressionModes - 1, compressionModeOn ? compressionMode - 1 : 1);
        const int characterBiasIdx   = juce::jlimit (0, kNumAttackReleaseCharacters - 1, characterOn ? attackReleaseCharacter - 1 : 1);
        const int gainReductionBiasIdx = juce::jlimit (0, kNumGainReductionModes - 1, gainReductionOn ? gainReductionMode - 1 : 1);

        const int numSamples = capturedL.getNumSamples();
        const float* left  = capturedL.getReadPointer (0);
        const float* right = capturedR.getReadPointer (0);

        // --- 1. Spectre moyen (Welch, fenetre Hann, recouvrement 50%) sur
        // le downmix mono, pour situer les 2 frequences de crossover --
        // methode "energie egale" : on cherche les frequences ou l'energie
        // cumulee (20Hz-18kHz) franchit ~28% puis ~72% du total. ---
        constexpr int fftOrder = 12;
        constexpr int fftSize  = 1 << fftOrder;
        const int numBins = fftSize / 2 + 1;

        if (numSamples < fftSize)
        {
            result = r; // pas assez de signal capture -- garde les valeurs par defaut
            state.store (State::Done);
            return;
        }

        std::vector<float> monoDownmix ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            monoDownmix[(size_t) i] = 0.5f * (left[i] + right[i]);

        juce::dsp::FFT fft (fftOrder);
        juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> avgMagSq (numBins, 0.0f);
        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
        const int hop = fftSize / 2;
        int numFrames = 0;

        for (int start = 0; start + fftSize <= numSamples; start += hop)
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            std::copy (monoDownmix.begin() + start, monoDownmix.begin() + start + fftSize, fftData.begin());
            window.multiplyWithWindowingTable (fftData.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (int bin = 0; bin < numBins; ++bin)
                avgMagSq[(size_t) bin] += fftData[(size_t) bin] * fftData[(size_t) bin];

            ++numFrames;

            if (threadShouldExit())
                return;
        }

        if (numFrames == 0)
        {
            result = r;
            state.store (State::Done);
            return;
        }

        // IMPORTANT : la repartition de l'energie brute (magnitude^2) sur
        // une grille de frequences LINEAIRE ecrase quasi tout dans les
        // graves -- la musique a un spectre naturellement decroissant
        // (basse/kick/bas-medium concentrent l'essentiel de l'energie
        // "brute"), et en plus les basses frequences occupent tres peu de
        // bins FFT (resolution lineaire = tres peu de Hz par bin en bas,
        // beaucoup en haut). Resultat mesure : le seuil des 72% d'energie
        // cumulee tombait quasi systematiquement sous 300Hz, quel que soit
        // le style -- Crossover High restait bloque au plancher du
        // parametre (bug rapporte : "le xover hi est toujours a 300").
        //
        // Fix (v1, INSUFFISANT) : regrouper l'energie par bande LOGARITHMIQUE
        // avant de chercher les seuils cumulatifs. Mais la premiere version
        // de ce regroupement assignait les bandes log EN PARCOURANT LES
        // BANDES et en y "aspirant" une plage de bins -- pres du grave, la
        // resolution FFT (~10-20Hz/bin) est bien plus grossiere que le pas
        // logarithmique choisi, donc PLUSIEURS bandes log consecutives
        // retombaient sur le MEME bin, dont l'energie etait alors comptee
        // plusieurs fois. Resultat : le grave restait quand meme surpondere
        // et Crossover High retombait sur son plancher (bug persistant
        // malgre le passage en log -- rapporte par l'utilisateur).
        //
        // Fix (v2) : on inverse le sens de l'assignation -- on parcourt les
        // BINS (chacun compte UNE SEULE FOIS, par construction) et chacun
        // vote pour SA bande log (celle qui contient sa frequence centrale).
        // Plus de double comptage possible, quelle que soit la finesse de
        // la grille log choisie par rapport a la resolution FFT.
        const double binHz = sampleRate / (double) fftSize;

        constexpr int numLogBands = 120;
        constexpr double logFreqLo = 20.0, logFreqHi = 18000.0;
        const double logStep = std::log (logFreqHi / logFreqLo) / (double) numLogBands;

        std::vector<double> logBandEnergy (numLogBands, 0.0);
        std::vector<double> logBandFreq   (numLogBands, 0.0);
        for (int k = 0; k < numLogBands; ++k)
        {
            const double fLo = logFreqLo * std::exp (logStep * (double) k);
            const double fHi = logFreqLo * std::exp (logStep * (double) (k + 1));
            logBandFreq[(size_t) k] = std::sqrt (fLo * fHi); // moyenne geometrique = "centre" perceptuel
        }

        const int binLo = juce::jlimit (1, numBins - 1, (int) (logFreqLo / binHz));
        const int binHi = juce::jlimit (binLo, numBins - 1, (int) (logFreqHi / binHz));
        for (int bin = binLo; bin <= binHi; ++bin)
        {
            const double freq = bin * binHz;
            int k = (int) (std::log (freq / logFreqLo) / logStep);
            k = juce::jlimit (0, numLogBands - 1, k);
            logBandEnergy[(size_t) k] += avgMagSq[(size_t) bin];
        }

        double totalEnergy = 0.0;
        for (double e : logBandEnergy)
            totalEnergy += e;

        auto findCrossoverHz = [&] (double fraction) -> double
        {
            const double target = totalEnergy * fraction;
            double cum = 0.0;
            for (int k = 0; k < numLogBands; ++k)
            {
                cum += logBandEnergy[(size_t) k];
                if (cum >= target)
                    return logBandFreq[(size_t) k];
            }
            return logBandFreq[(size_t) (numLogBands - 1)];
        };

        if (totalEnergy > 1.0e-12)
        {
            r.crossoverLowHz  = juce::jlimit (20.0f,  400.0f,  (float) findCrossoverHz (0.28));
            r.crossoverHighHz = juce::jlimit (300.0f, 6000.0f, (float) findCrossoverHz (0.72));
            if (r.crossoverHighHz <= r.crossoverLowHz * 1.5f)
                r.crossoverHighHz = juce::jmin (6000.0f, r.crossoverLowHz * 3.0f);
        }

        if (threadShouldExit())
            return;

        // --- 1bis. Detecteur sidechain (Bell) par bande : cherche le PIC
        // spectral le plus marque a l'interieur de chaque bande (sur le
        // spectre lineaire avgMagSq, plus precis en frequence que la grille
        // log utilisee pour les crossovers). Calcule SEULEMENT si la portee
        // "Detecteurs" est active (menu Options, opt-in, off par defaut).
        // Le Bell n'est engage que si le pic depasse nettement la moyenne
        // de sa bande (peakiness > 3x) : un spectre plat n'a pas de
        // "frequence dominante" a cibler, mieux vaut laisser le Bell eteint
        // que d'en inventer une. ---
        if (applyDetectors)
        {
            auto bandRangeHz = [&] (int band) -> std::pair<double, double>
            {
                if (band == 0) return { 20.0, (double) r.crossoverLowHz };
                if (band == 1) return { (double) r.crossoverLowHz, (double) r.crossoverHighHz };
                return { (double) r.crossoverHighHz, 18000.0 };
            };

            for (int band = 0; band < 3; ++band)
            {
                const auto range = bandRangeHz (band);
                const int bLo = juce::jlimit (1, numBins - 1, (int) (range.first  / binHz));
                const int bHi = juce::jlimit (bLo, numBins - 1, (int) (range.second / binHz));

                double sum = 0.0;
                int peakBin = bLo;
                double peakVal = 0.0;
                for (int bin = bLo; bin <= bHi; ++bin)
                {
                    const double v = avgMagSq[(size_t) bin];
                    sum += v;
                    if (v > peakVal) { peakVal = v; peakBin = bin; }
                }

                const int count = juce::jmax (1, bHi - bLo + 1);
                const double avg = sum / (double) count;
                const double peakiness = peakVal / juce::jmax (1.0e-12, avg);

                r.detectorBellFreqHz[(size_t) band] = juce::jlimit (20.0f, 20000.0f, (float) (peakBin * binHz));

                if (peakiness > 3.0)
                {
                    r.detectorBellOn[(size_t) band] = true;
                    // 3x..12x la moyenne de bande -> 4..14dB de renfort du
                    // detecteur a cette frequence (plage parametre 0..18dB).
                    const double t2 = juce::jlimit (0.0, 1.0, (peakiness - 3.0) / 9.0);
                    r.detectorBellGainDb[(size_t) band] = juce::jlimit (0.0f, 18.0f, (float) (4.0 + t2 * 10.0));
                }
                else
                {
                    r.detectorBellOn[(size_t) band] = false;
                }
            }

            if (threadShouldExit())
                return;
        }

        // --- 2. Split Low/Mid/High hors-ligne EN STEREO (crossover Wide, 0
        // latence) avec les frequences tout juste calculees -- un filtre
        // par canal (memes coefficients), pour ne jamais melanger l'etat
        // interne des filtres L et R. Le split mono utilise pour Input/
        // Ratio/Attack/Release est simplement la moyenne L+R post-filtrage
        // (filtrage lineaire => commute avec la moyenne, pas besoin d'un
        // 3e passage). ---
        Crossover xoL, xoR;
        for (auto* xo : { &xoL, &xoR })
        {
            xo->prepare (sampleRate, numSamples);
            xo->setLinearPhaseMode (false);
            xo->setLowCrossoverHz (r.crossoverLowHz);
            xo->setHighCrossoverHz (r.crossoverHighHz);
        }

        juce::AudioBuffer<float> lowL (1, numSamples), midL (1, numSamples), highL (1, numSamples);
        juce::AudioBuffer<float> lowR (1, numSamples), midR (1, numSamples), highR (1, numSamples);
        xoL.processBlock (left,  lowL.getWritePointer (0), midL.getWritePointer (0), highL.getWritePointer (0), numSamples);
        xoR.processBlock (right, lowR.getWritePointer (0), midR.getWritePointer (0), highR.getWritePointer (0), numSamples);

        const float* bandL[3] = { lowL.getReadPointer (0), midL.getReadPointer (0), highL.getReadPointer (0) };
        const float* bandR[3] = { lowR.getReadPointer (0), midR.getReadPointer (0), highR.getReadPointer (0) };

        static constexpr float baseReleaseMs[3] = { 220.0f, 120.0f, 70.0f };
        // Biais de largeur par bande (demande explicite : jouer aussi sur
        // le Mono/Stereo Spread) -- convention mastering classique :
        // resserrer le bas (mono-compatibilite), garder le medium honnete,
        // n'ouvrir le haut que si le signal source a REELLEMENT du contenu
        // stereo la-haut (jamais elargir artificiellement du contenu deja
        // mono). facteur applique a stereonessDb (cote - milieu, en dB).
        static constexpr float widthBiasPerDb[3] = { -3.0f, 1.0f, 3.0f };
        static constexpr float widthMin[3] = { 40.0f, 80.0f, 100.0f };
        static constexpr float widthMax[3] = { 100.0f, 120.0f, 170.0f };

        // Fidelity : balance relative Low/Mid/High AVANT compression
        // (rmsDb, deja calculee dans la boucle ci-dessous) vs. APRES
        // (simulation, meme FeedbackCompressor que le plafond GR, mais on
        // accumule ici la RMS en SORTIE plutot que la GR crete). Rempli
        // dans la boucle, corrige juste apres.
        float origRmsDbArr[3]  = { 0.0f, 0.0f, 0.0f };
        float postRmsDbArr[3]  = { 0.0f, 0.0f, 0.0f };

        for (int band = 0; band < 3; ++band)
        {
            const float* dL = bandL[band];
            const float* dR = bandR[band];

            double sumSq = 0.0, sumSqMid = 0.0, sumSqSide = 0.0;
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float mono = 0.5f * (dL[i] + dR[i]);
                const float mid  = mono;
                const float side = 0.5f * (dL[i] - dR[i]);

                sumSq     += (double) mono * (double) mono;
                sumSqMid  += (double) mid  * (double) mid;
                sumSqSide += (double) side * (double) side;
                peak = juce::jmax (peak, std::abs (mono));
            }

            const float rms    = (float) std::sqrt (sumSq / juce::jmax (1, numSamples));
            const float rmsDb  = juce::Decibels::gainToDecibels (rms,  -100.0f);
            const float peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
            const float crestDb = juce::jlimit (0.0f, 30.0f, peakDb - rmsDb);

            const float midRms  = (float) std::sqrt (sumSqMid  / juce::jmax (1, numSamples));
            const float sideRms = (float) std::sqrt (sumSqSide / juce::jmax (1, numSamples));
            const float stereonessDb = juce::Decibels::gainToDecibels (sideRms, -100.0f)
                                      - juce::Decibels::gainToDecibels (midRms, -100.0f);

            const auto& bias = kCompressionModeBias[compressionBiasIdx];
            const auto& gr   = kGainReductionBias[gainReductionBiasIdx];

            // Input : vise une RMS cible en entree de detecteur qui depend
            // du mode (Soft plus prudent, Hard plus pousse) -- voir
            // kCompressionModeBias -- PUIS reculee/avancee par l'intensite
            // globale de gain reduction (Smooth recule encore, Hard pousse
            // encore -- voir kGainReductionBias).
            r.inputDb[(size_t) band] = juce::jlimit (-30.0f, 30.0f, bias.inputTargetDb + gr.inputBackoffDb - rmsDb);

            // Ratio : plus le crest factor est eleve (signal transitoire),
            // plus on autorise un ratio eleve pour le controler -- mappe
            // [6dB..24dB] -> un facteur [0..1] boostee par le mode
            // (ratioBoost) PUIS par l'intensite de gain reduction (ratioMul)
            // avant de choisir l'index dans la table de ratios. En Soft/
            // Medium/Smooth ca reste bas ; Hard+Hard peut aller chercher
            // jusqu'a 20:1 (index 6) sur du signal vraiment transitoire, en
            // connaissance de cause (2 modes explicitement "durs" combines).
            const float t = juce::jlimit (0.0f, 1.0f, (crestDb - 6.0f) / 18.0f);
            r.ratioIndex[(size_t) band] = juce::jlimit (0.0f, 6.0f, t * 4.0f * bias.ratioBoost * gr.ratioMul);

            // Attack/Release : memes formules qu'avant, mises a l'echelle par
            // le mode d'intensite (Hard = plus rapide, Soft = plus lent) PUIS
            // par le caractere Punchy/Medium/Smooth (axe independant -- voir
            // kAttackReleaseCharacterBias).
            const auto& character = kAttackReleaseCharacterBias[characterBiasIdx];
            r.attackMs[(size_t) band]  = juce::jlimit (0.05f, 40.0f,
                (20.0f - t * 15.0f) * bias.attackScale * character.attackScale);
            r.releaseMs[(size_t) band] = juce::jlimit (50.0f, 3000.0f,
                (baseReleaseMs[band] - t * 30.0f) * bias.releaseScale * character.releaseScale);

            // --- Plafond de gain reduction (demande explicite : "le smooth
            // tape max a -5dB de GR, le medium a -9 et le hard -15") --
            // simule le VRAI FeedbackCompressor (meme classe que le DSP
            // temps reel) avec les reglages ci-dessus sur le signal REEL de
            // cette bande, mesure la GR crete obtenue, et recule l'Input
            // par dichotomie si elle depasse le plafond du mode. Jamais
            // l'inverse (on ne pousse jamais l'Input pour ATTEINDRE le
            // plafond -- seulement pour ne pas le depasser). Saute cette
            // simulation (couteuse) si AI Gain Reduction est sur "Off" --
            // le Ratio n'est de toute facon pas pousse dans ce cas. ---
            if (gainReductionOn)
            {
                const int ratioIdxInt = juce::jlimit (0, 6, juce::roundToInt (r.ratioIndex[(size_t) band]));
                const float ratioValue = kRatioTable[(size_t) ratioIdxInt];
                const float kneeValue  = kKneeTable[(size_t) ratioIdxInt];
                const float attackMs   = r.attackMs[(size_t) band];
                const float releaseMs  = r.releaseMs[(size_t) band];

                auto measurePeakGrDb = [&] (float inputDb) -> float
                {
                    FeedbackCompressor sim;
                    sim.prepare (sampleRate);
                    sim.setInputDriveDb (inputDb);
                    sim.setRatio (ratioValue);
                    sim.setKneeDb (kneeValue);
                    sim.setAttackMs (attackMs);
                    sim.setReleaseMs (releaseMs);
                    sim.setOutputDb (0.0f);

                    float peakGr = 0.0f;
                    for (int i = 0; i < numSamples; ++i)
                    {
                        sim.processSample (0.5f * (dL[i] + dR[i]));
                        peakGr = juce::jmax (peakGr, std::abs (sim.getGainReductionDb()));
                    }
                    return peakGr;
                };

                const float capDb = kGainReductionCapDb[gainReductionBiasIdx];
                float testInputDb = r.inputDb[(size_t) band];
                if (measurePeakGrDb (testInputDb) > capDb)
                {
                    float lo = -30.0f, hi = testInputDb;
                    for (int iter = 0; iter < 10; ++iter)
                    {
                        const float mid = 0.5f * (lo + hi);
                        if (measurePeakGrDb (mid) > capDb)
                            hi = mid;
                        else
                            lo = mid;

                        if (threadShouldExit())
                            return;
                    }
                    r.inputDb[(size_t) band] = lo;
                }
            }

            origRmsDbArr[band] = rmsDb;

            if (fidelity)
            {
                const int ratioIdxInt = juce::jlimit (0, 6, juce::roundToInt (r.ratioIndex[(size_t) band]));
                FeedbackCompressor sim;
                sim.prepare (sampleRate);
                sim.setInputDriveDb (r.inputDb[(size_t) band]);
                sim.setRatio (kRatioTable[(size_t) ratioIdxInt]);
                sim.setKneeDb (kKneeTable[(size_t) ratioIdxInt]);
                sim.setAttackMs (r.attackMs[(size_t) band]);
                sim.setReleaseMs (r.releaseMs[(size_t) band]);
                sim.setOutputDb (0.0f);

                double sumSqOut = 0.0;
                for (int i = 0; i < numSamples; ++i)
                {
                    const float out = sim.processSample (0.5f * (dL[i] + dR[i]));
                    sumSqOut += (double) out * (double) out;
                }
                const float outRms = (float) std::sqrt (sumSqOut / juce::jmax (1, numSamples));
                postRmsDbArr[band] = juce::Decibels::gainToDecibels (outRms, -100.0f);

                if (threadShouldExit())
                    return;
            }

            // Mix : les bandes qui recoivent le plus de compression (t eleve)
            // gardent un filet de securite parallele (un peu de dry en
            // reserve) plutot que 100% wet -- 85%..100%.
            r.mixPercent[(size_t) band] = juce::jlimit (70.0f, 100.0f, 100.0f - t * 15.0f);

            // Width : ajuste depuis le neutre (100%) selon la "stereo-ite"
            // reellement mesuree dans CETTE bande, avec un biais different
            // par bande (voir tables ci-dessus) -- ne jamais elargir un
            // signal deja mono, ne jamais forcer un signal deja large a se
            // resserrer au-dela de sa bande.
            const float target = 100.0f + stereonessDb * widthBiasPerDb[band];
            r.widthPercent[(size_t) band] = juce::jlimit (widthMin[band], widthMax[band], target);

            if (threadShouldExit())
                return;
        }

        // --- 2bis. Fidelity : corrige l'Output de chaque bande pour que la
        // balance RELATIVE Low/Mid/High retrouve, APRES compression, celle
        // qu'elle avait AVANT (demande explicite : "une fonction qui
        // reprend la balance bass mid aigue du mix pour la remettre pareil
        // apres compression"). On compare les deviations par rapport a la
        // moyenne des 3 bandes (pas les RMS absolues, qui n'ont pas de
        // raison d'etre egales) avant/apres, et on corrige l'ecart. ---
        if (fidelity)
        {
            const float origMean = (origRmsDbArr[0] + origRmsDbArr[1] + origRmsDbArr[2]) / 3.0f;
            const float postMean = (postRmsDbArr[0] + postRmsDbArr[1] + postRmsDbArr[2]) / 3.0f;
            for (int band = 0; band < 3; ++band)
            {
                const float origRel = origRmsDbArr[band] - origMean;
                const float postRel = postRmsDbArr[band] - postMean;
                r.fidelityOutputDb[(size_t) band] = juce::jlimit (-12.0f, 12.0f, origRel - postRel);
            }
        }

        // --- 3. Cible de mastering (menu Options, section AI) : mesure la
        // loudness integree (LUFS, approximation ITU-R BS.1770 -- filtre K
        // en 2 etages + moyenne quadratique, SANS le gating officiel de la
        // norme complete -- suffisant pour une correction automatique, pas
        // pour une certification broadcast) du programme capture, puis
        // calcule l'ecart de gain necessaire pour rejoindre la cible de la
        // plateforme choisie. Applique EGALEMENT aux 3 bandes (Output) --
        // demande explicite : "en jouant sur les outputs et le THD" (le
        // Soft Clip, recommande ON, fournit le garde-fou true-peak qui
        // absorbe l'exces plutot que de laisser des overs). Rien de tout
        // ca ne s'applique si masteringTarget == 0 (Off). ---
        if (masteringTarget > 0)
        {
            juce::dsp::IIR::Filter<float> shelfL, hpL, shelfR, hpR;
            shelfL.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 1500.0, 0.7071, juce::Decibels::decibelsToGain (4.0f));
            hpL.coefficients    = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sampleRate, 38.0);
            shelfR.coefficients = shelfL.coefficients;
            hpR.coefficients    = hpL.coefficients;

            std::vector<float> kL ((size_t) numSamples), kR ((size_t) numSamples);
            std::copy (left,  left  + numSamples, kL.begin());
            std::copy (right, right + numSamples, kR.begin());

            for (int i = 0; i < numSamples; ++i)
            {
                kL[(size_t) i] = hpL.processSample (shelfL.processSample (kL[(size_t) i]));
                kR[(size_t) i] = hpR.processSample (shelfR.processSample (kR[(size_t) i]));
            }

            double msL = 0.0, msR = 0.0;
            for (int i = 0; i < numSamples; ++i)
            {
                msL += (double) kL[(size_t) i] * (double) kL[(size_t) i];
                msR += (double) kR[(size_t) i] * (double) kR[(size_t) i];
            }
            msL /= juce::jmax (1, numSamples);
            msR /= juce::jmax (1, numSamples);

            // Formule BS.1770 (ponderation egale L/R = 1.0 chacun).
            const double sumMs = juce::jmax (1.0e-12, msL + msR);
            const float measuredLufs = (float) (-0.691 + 10.0 * std::log10 (sumMs));
            r.measuredLufs = measuredLufs;

            const float targetLufs = kLufsTargets[(size_t) masteringTarget];
            const float gainNeededDb = juce::jlimit (-12.0f, 12.0f, targetLufs - measuredLufs);

            r.applyLoudnessTarget = true;
            r.recommendSoftClipOn = true;
            for (int band = 0; band < 3; ++band)
                r.outputDb[(size_t) band] = juce::jlimit (-30.0f, 30.0f, gainNeededDb);
        }

        result = r;
        state.store (State::Done);
    }

    std::atomic<State> state { State::Idle };
    double sampleRate = 44100.0;
    int masteringTarget = 0;
    int compressionMode = 1;
    int attackReleaseCharacter = 1;
    int gainReductionMode = 1;
    bool applyCrossover = true, applyDetectors = false, applyCompression = true;
    int style = 0;
    bool fidelity = false;
    int targetSamples = 0;
    int writePos = 0;
    juce::AudioBuffer<float> capturedL, capturedR;
    Result result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiLearnAnalyzer)
};
