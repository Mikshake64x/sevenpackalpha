#pragma once
#include <JuceHeader.h>
#include <algorithm>
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
    //
    // "MATCH" (demande explicite : "une case Match pour volume match ...
    // ça match au meme volume que la source en RMS") -- DERNIER choix,
    // different des autres : au lieu de viser une valeur LUFS FIXE, il vise
    // la loudness MESUREE de la source elle-meme, mais en corrigeant le
    // vrai ecart introduit par la compression (contrairement aux cibles
    // fixes ci-dessus, qui supposent juste que l'Output pousse le signal
    // directement au niveau vise). Volume-matching pour comparer wet/dry a
    // niveau egal, sans que l'oreille ne favorise le plus fort. Pas
    // d'entree utile dans kLufsTargets (voir kMatchTargetIndex, traite a
    // part dans run()).
    static constexpr int kNumMasteringTargets = 10;
    static constexpr int kMatchTargetIndex = kNumMasteringTargets - 1;
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
        0.0f,    // Match (non utilise, cible calculee dynamiquement)
    };

    // 7 bandes (Low/LowMid/Mid/HighMid/Presence/High/Air), 6 points de
    // crossover -- memes indices que SixpackAudioProcessor::Band/NumBands/
    // NumCrossovers (pas d'include croise possible, voir kRatioTable/
    // kKneeTable plus bas pour la meme contrainte).
    static constexpr int kNumBands = 7;
    static constexpr int kNumCrossovers = kNumBands - 1; // 6

    struct Result
    {
        std::array<float, (size_t) kNumCrossovers> crossoverHz { { 90.0f, 270.0f, 650.0f, 1700.0f, 3500.0f, 6500.0f } };
        std::array<float, (size_t) kNumBands> inputDb {};
        std::array<float, (size_t) kNumBands> ratioIndex {};  // 0..6 (flottant, l'appelant arrondit)
        std::array<float, (size_t) kNumBands> attackMs {};
        std::array<float, (size_t) kNumBands> releaseMs {};
        std::array<float, (size_t) kNumBands> mixPercent   { { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } }; // 0..100
        std::array<float, (size_t) kNumBands> widthPercent { { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } }; // 0..200, 100=neutre

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
        std::array<bool, (size_t) kNumBands>  detectorBellOn     { { false, false, false, false, false, false } };
        std::array<float, (size_t) kNumBands> detectorBellFreqHz { { 650.0f, 650.0f, 650.0f, 650.0f, 650.0f, 650.0f } };
        std::array<float, (size_t) kNumBands> detectorBellGainDb { { 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f } };

        // Cible de mastering (menu Options, section AI) -- seulement rempli
        // si un target != Off etait selectionne au moment du clic. Meme
        // correction (en dB) appliquee aux 5 Output de bande + Soft Clip
        // recommande ON (garde-fou true-peak) -- voir doc de classe.
        bool applyLoudnessTarget = false;
        std::array<float, (size_t) kNumBands> outputDb {};
        bool recommendSoftClipOn = false;
        float measuredLufs = -23.0f; // informatif seulement, pas applique tel quel

        // Style musical (menu Options, section AI, demande explicite :
        // "plusieurs styles musicaux qui vont changer la balance des
        // outputs en fonction du style") -- simple offset statique par
        // bande, voir kStyleOutputBiasDb. "Off" = pas d'offset.
        bool applyStyle = false;
        std::array<float, (size_t) kNumBands> styleOutputDb {};

        // Fidelity (menu Options, section AI, demande explicite : "une
        // fonction qui reprend la balance bass mid aigue du mix pour la
        // remettre pareil apres compression") -- mesure la balance relative
        // par bande AVANT compression puis la balance qu'aurait le signal
        // APRES (simulation), et corrige l'Output de chaque bande pour
        // retrouver la balance d'origine. Independant de tout le reste --
        // se combine (s'additionne) avec Style et la cible LUFS.
        bool applyFidelity = false;
        std::array<float, (size_t) kNumBands> fidelityOutputDb {};

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

        // AI Expand (demande explicite : "je veux que AI regle en meme
        // temps le comp et l'expand ... je veux pas cliquer une fois pour
        // regler les compresseurs, et une fois en plus pour les
        // expandeurs") -- UN SEUL Learn calcule et pousse LES DEUX jeux de
        // parametres pour LES 3 BANDES A CHAQUE FOIS, independamment du
        // mode Comp/Expand actuellement affiche -- comme ca, basculer une
        // bande de Comp a Expand (ou l'inverse) pour comparer ne demande
        // plus de relancer un Learn, les 2 jeux de valeurs sont deja
        // pretes. Champs SEPARES des inputDb/ratioIndex/attackMs/releaseMs
        // ci-dessus (qui restent la partie COMPRESSION) -- meme structure,
        // meme role, mais destines a idInputExpand/idRatioIndexExpand/
        // idAttackExpand/idReleaseExpand. Detection PROPRE (floor/
        // quietness, pas crest factor -- voir run()). pushExpand*
        // gouvernent chacun leur propre axe, independamment des push*
        // compression ci-dessus.
        std::array<float, (size_t) kNumBands> expandInputDb {};   // Range (dB, voir FeedbackCompressor::setExpandRangeDb())
        std::array<float, (size_t) kNumBands> expandRatioIndex {};
        std::array<float, (size_t) kNumBands> expandAttackMs {};
        std::array<float, (size_t) kNumBands> expandReleaseMs {};
        bool pushExpandInput         = true; // Range -- gouverne par AI Expand Amount (voir kExpandAmountRangeMinDb/MaxDb)
        bool pushExpandRatio         = true; // gouverne par AI Expand Mode (voir kExpandModeRatioBoost)
        bool pushExpandAttackRelease = true; // gouverne par AI Expand Character

        // Output : correction de makeup associee au boost applique par
        // l'expansion (voir run()) -- 0dB pour les bandes non concernees.
        // S'additionne a applyLoudnessTarget/applyStyle/applyFidelity dans
        // l'editeur (meme mecanisme de somme des sources Output).
        bool applyExpandOutput = false;
        std::array<float, (size_t) kNumBands> expandOutputDb {};
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

    // --- Triade AI EXPAND (demande explicite : "pour l'ia de expand, il
    // faut des reglages comme pour l'ia comp ... avec des reglages de
    // detection d'ia propre") -- meme structure/roles que la triade
    // Compression Mode/Attack-Release Character/Gain Reduction ci-dessus,
    // mais appliquee UNIQUEMENT aux bandes en mode Expand/Both (voir
    // run()), et sur une detection PROPRE (floor/quietness du signal, pas
    // le crest factor pic/RMS utilise pour la compression -- voir
    // floorCrestDb dans run()).

    // AI Expand Mode (demande explicite : "IA expand mode Off, soft,
    // medium, hard") -- pilote UNIQUEMENT le Ratio (demande explicite :
    // "un pour le range, un pour le ratio et un pour l'attaque" -- 3 axes
    // strictement independants, voir kExpandModeRatioBoost plus bas). Le
    // Range est gouverne par AI Expand Amount (kExpandAmountRangeMinDb/MaxDb),
    // l'Attack/Release par AI Expand Character (kExpandCharacterBias).
    //
    // Soft reste subtil (demande explicite d'origine), mais Medium et Hard
    // ont ete RENFORCES (demande explicite : "il faut forcer un peu plus
    // le medium et le hard") -- plus de ratioBoost, attack/release plus
    // rapides (scale plus bas) pour un effet nettement plus marque a ces
    // 2 crans, tout en gardant le ratio hors de la zone extreme (20:1,
    // voir le plafond d'index dans run()).
    // AI Expand Mode -- controle UNIQUEMENT le Ratio (retour utilisateur :
    // "il y a 3 reglages d'ai. un pour le range, un pour le ratio et un
    // pour l'attaque" -- plus de melange avec Attack/Release, qui est le
    // role EXCLUSIF de Character ci-dessous). Ecart large pour rester
    // audible en soi (Soft = ratio quasi neutre, Hard = ratio marque).
    static constexpr int kNumExpandModes = 3;
    static constexpr int kNumExpandModeChoices = 4; // + "Off"
    static constexpr float kExpandModeRatioBoost[kNumExpandModes] = { 0.20f, 0.75f, 1.80f }; // Soft/Medium/Hard

    // AI Expand Attack/Release Character -- controle UNIQUEMENT l'Attack/
    // Release (demande explicite : "un pour l'attaque" -- role EXCLUSIF,
    // plus melange avec Mode). Sens INVERSE de la compression : pour la
    // compression, Smooth = attack RAPIDE (rattrape tout, transparent) ;
    // pour l'expansion, Smooth = attack LENTE (le boost monte en douceur,
    // sans a-coup) et Punchy = attack TRES COURTE (le boost saute
    // immediatement, plus agressif/percutant a l'oreille). Ecart tres
    // large pour que la difference s'entende clairement.
    struct ExpandCharacterBias { float attackScale, releaseScale; };
    static constexpr int kNumExpandCharacters = 3;
    static constexpr int kNumExpandCharacterChoices = 4; // + "Off"
    static constexpr ExpandCharacterBias kExpandCharacterBias[kNumExpandCharacters] =
    {
        { 3.00f, 1.80f }, // Smooth -- attack TRES LONGUE
        { 1.00f, 1.00f }, // Medium
        { 0.25f, 0.50f }, // Punchy -- attack TRES COURTE
    };

    // AI Expand Amount -- controle UNIQUEMENT le Range (demande explicite :
    // "un pour le range" -- role EXCLUSIF, plus de multiplicateur sur le
    // Ratio, qui est le role de Mode ci-dessus). Pousse DIRECTEMENT la
    // valeur du knob Range (demande explicite : "le knob input du mode
    // comp passe en mode range") -- pas de simulation/dichotomie, la DSP
    // elle-meme respecte nativement cette limite (voir FeedbackCompressor::
    // setExpandRangeDb()).
    //
    // Valeurs REVUES A LA BAISSE (retour utilisateur : "c'est pas tres
    // musical la, c'est trop") -- chaque mode est desormais une PLAGE
    // (min..max), pas une valeur fixe : Smooth 0.2-0.4dB, Mid 0.5-1dB,
    // Hard 2-3dB. La position dans la plage suit la mesure "te" (plancher/
    // quietness reel de la bande, voir run()) -- plus le plancher est
    // profond/marque, plus on se rapproche du haut de la plage du mode.
    static constexpr int kNumExpandAmountModes = 3;
    static constexpr int kNumExpandAmountModeChoices = 4; // + "Off"
    // Plancher de Hard remonte de 1.0 a 2.0dB (retour utilisateur : "j'ai
    // que 1db de range alors que je demande 2.1" -- le resultat pouvait
    // descendre jusqu'a 1dB quand l'IA jugeait le creux dynamique modeste,
    // ce qui paraissait trop faible/imprevisible pour un mode cense etre le
    // plus marque des 3) -- Hard couvre desormais 2-3dB, jamais moins,
    // quelle que soit la mesure "te".
    static constexpr float kExpandAmountRangeMinDb[kNumExpandAmountModes] = { 0.2f, 0.5f, 2.0f }; // Smooth/Mid/Hard
    static constexpr float kExpandAmountRangeMaxDb[kNumExpandAmountModes] = { 0.4f, 1.0f, 3.0f };

    // Tables ratio/knee dupliquees depuis SixpackAudioProcessor (pas
    // d'include croise possible -- PluginProcessor.h inclut deja ce
    // fichier). Doivent rester synchronisees si la table change la-bas.
    static constexpr float kRatioTable[7] = { 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 20.0f };
    static constexpr float kKneeTable[7]  = { 24.0f, 18.0f, 12.0f, 9.0f, 7.5f, 6.0f, 1.0f };

    // Styles musicaux (menu Options, section AI, demande explicite :
    // "plusieurs styles musicaux qui vont changer la balance des outputs en
    // fonction du style. Bass +3dB sur la bande bass, Loud +3dB en bass et
    // high...") -- offsets STATIQUES par bande (Low/LowMid/Mid/HighMid/
    // Presence/High/Air) ajoutes a l'Output, independamment de tout le reste
    // (ratio/input/etc). Simple lookup, pas d'analyse -- contrairement a
    // Fidelity ci-dessous. "Off" (index 0, pas d'entree ici) = aucun
    // offset. Colonnes LowMid/HighMid INTERPOLEES depuis les valeurs Low/
    // Mid/High mesurees a l'origine (Tripressor, 3 bandes) -- Presence
    // (6e bande, ajoutee ensuite) INTERPOLEE a son tour entre HighMid et
    // High. Air (7e bande) laissee a 0.0f partout -- pas encore de retour
    // utilisateur pour la caler, a affiner empiriquement comme les autres.
    static constexpr int kNumStyles = 5;
    static constexpr int kNumStyleChoices = 6; // + "Off"
    static constexpr float kStyleOutputBiasDb[kNumStyles][(size_t) kNumBands] =
    {
        { 3.0f, 1.5f, 0.0f, 0.0f, 0.0f,  0.0f, 0.0f }, // Bass
        { 3.0f, 1.5f, 0.0f, 1.5f, 2.25f, 3.0f, 0.0f }, // Loud (courbe "smiley")
        { 2.0f, 1.5f, 1.0f, 1.5f, 1.75f, 2.0f, 0.0f }, // Rock (leger boost general, un peu de presence)
        { 0.0f, 1.0f, 2.0f, 1.5f, 1.25f, 1.0f, 0.0f }, // Pop (presence vocale + un peu d'air)
        { 4.0f, 2.0f, 0.0f, 1.5f, 2.25f, 3.0f, 0.0f }, // Electronic (smiley plus marque)
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
                        int expandModeIndex, int expandCharacterIndex, int expandAmountIndex,
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
        expandMode       = juce::jlimit (0, kNumExpandModeChoices - 1, expandModeIndex);
        expandCharacter  = juce::jlimit (0, kNumExpandCharacterChoices - 1, expandCharacterIndex);
        expandAmount     = juce::jlimit (0, kNumExpandAmountModeChoices - 1, expandAmountIndex);
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
            // Bug corrige au passage (ajout 7e bande) : seules les 3
            // premieres colonnes (Low/LowMid/Mid) etaient recopiees ici,
            // alors que kStyleOutputBiasDb a toujours eu une valeur pour
            // chacune des kNumBands colonnes et que PluginEditor::
            // applyAiResult() les lit TOUTES (voir result.styleOutputDb[band])
            // -- HighMid/Presence/High ne recevaient donc jamais leur biais,
            // contrairement au commentaire d'origine ("Loud +3dB en bass ET
            // high").
            const auto& biasDb = kStyleOutputBiasDb[(size_t) (style - 1)];
            for (int band = 0; band < kNumBands; ++band)
                r.styleOutputDb[(size_t) band] = biasDb[(size_t) band];
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

        // Meme mecanique pour la triade Expand (voir doc de classe/Result).
        const bool expandModeOn      = expandMode > 0;
        const bool expandCharacterOn = expandCharacter > 0;
        const bool expandAmountOn    = expandAmount > 0;
        // 3 axes STRICTEMENT independants (voir doc plus bas) -- chaque
        // push* suit MAINTENANT l'axe qui gouverne reellement sa cible :
        // pushExpandInput (Range) -> Amount, pushExpandRatio -> Mode,
        // pushExpandAttackRelease -> Character. BUG CORRIGE : pushExpandRatio
        // etait reste cale sur Amount (reste de l'ancien design ou Amount
        // gouvernait aussi le Ratio) -- avec Amount sur "Off" et Mode
        // regle, le Ratio ne se poussait jamais ("AI expand rien qui
        // marche").
        r.pushExpandInput         = expandAmountOn;
        r.pushExpandAttackRelease = expandCharacterOn;
        r.pushExpandRatio         = expandModeOn;
        const int expandModeBiasIdx      = juce::jlimit (0, kNumExpandModes - 1, expandModeOn ? expandMode - 1 : 1);
        const int expandCharacterBiasIdx = juce::jlimit (0, kNumExpandCharacters - 1, expandCharacterOn ? expandCharacter - 1 : 1);
        const int expandAmountBiasIdx    = juce::jlimit (0, kNumExpandAmountModes - 1, expandAmountOn ? expandAmount - 1 : 1);

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
            // 6 points de crossover -> fractions d'energie cumulee visant
            // des bandes Low 15% / LowMid 20% / Mid 30% / HighMid 20% /
            // Presence 7% / High 5% / Air 3% -- X1-X5 inchanges (decoupage
            // jusqu'a Presence/High), X6 (nouveau, frontiere High/Air, 7e
            // bande) ajoute a 97%.
            static constexpr float kEnergyFractions[kNumCrossovers] = { 0.15f, 0.35f, 0.65f, 0.85f, 0.92f, 0.97f };

            // Plages resserrees DEDIEES a l'AI Scope Crossover (retour
            // utilisateur historique sur Tripressor : "il me trouve souvent
            // des incoherences ... notamment sur Xover high" -- un clamp
            // trop large laisse passer des propositions peu musicales)
            // UNIQUEMENT quand le scope Crossover est actif -- le calcul
            // "interne" garde la plage complete Crossover::kSplitMinHz/MaxHz
            // quand le scope est desactive (utilisee aussi pour decouper
            // les bandes du scope Detecteurs, voir bandRangeHz() plus bas,
            // meme quand Crossover est off -- pas de raison de la resserrer
            // la).
            // Tolerances retour utilisateur explicite (2026-08-20, revues
            // pour la 6e bande "Presence") : X1 60-110Hz, X2 110-300Hz, X3
            // 300-1000Hz, X4 1000-2500Hz, X5 2500-5000Hz. X6 (frontiere
            // High/Air, 7e bande) ajoute a 5000-10000Hz.
            static constexpr float kTightMinHz[kNumCrossovers] = {  60.0f,  110.0f,  300.0f, 1000.0f, 2500.0f, 5000.0f };
            static constexpr float kTightMaxHz[kNumCrossovers] = { 110.0f,  300.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f };

            for (int x = 0; x < kNumCrossovers; ++x)
            {
                const float lo = applyCrossover ? kTightMinHz[x] : Crossover::kSplitMinHz[x];
                const float hi = applyCrossover ? kTightMaxHz[x] : Crossover::kSplitMaxHz[x];
                r.crossoverHz[(size_t) x] = juce::jlimit (lo, hi, (float) findCrossoverHz (kEnergyFractions[x]));
            }

            // Impose un ecart minimum (ratio 1.35, plus genereux que le
            // ratio 1.05 de securite DSP dans PluginProcessor -- ici on veut
            // des propositions musicalement distinctes, pas juste valides)
            // entre 2 points consecutifs -- l'analyse d'energie cumulee peut
            // sinon proposer 2 points quasi confondus sur un spectre plat.
            constexpr float minRatio = 1.35f;
            for (int x = 1; x < kNumCrossovers; ++x)
            {
                const float minAllowed = r.crossoverHz[(size_t) (x - 1)] * minRatio;
                if (r.crossoverHz[(size_t) x] < minAllowed)
                    r.crossoverHz[(size_t) x] = juce::jmin (Crossover::kSplitMaxHz[x], minAllowed);
            }
            for (int x = kNumCrossovers - 2; x >= 0; --x)
            {
                const float maxAllowed = r.crossoverHz[(size_t) (x + 1)] / minRatio;
                if (r.crossoverHz[(size_t) x] > maxAllowed)
                    r.crossoverHz[(size_t) x] = juce::jmax (Crossover::kSplitMinHz[x], maxAllowed);
            }
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
                const double lo = (band == 0) ? 20.0    : (double) r.crossoverHz[(size_t) (band - 1)];
                const double hi = (band == kNumBands - 1) ? 18000.0 : (double) r.crossoverHz[(size_t) band];
                return { lo, hi };
            };

            for (int band = 0; band < kNumBands; ++band)
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

        // --- 2. Split 5 bandes hors-ligne EN STEREO (crossover Wide, 0
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
            for (int x = 0; x < kNumCrossovers; ++x)
                xo->setCrossoverHz (x, r.crossoverHz[(size_t) x]);
        }

        std::array<juce::AudioBuffer<float>, (size_t) kNumBands> splitL, splitR;
        for (int band = 0; band < kNumBands; ++band)
        {
            splitL[(size_t) band].setSize (1, numSamples);
            splitR[(size_t) band].setSize (1, numSamples);
        }
        float* outsL[(size_t) kNumBands];
        float* outsR[(size_t) kNumBands];
        for (int band = 0; band < kNumBands; ++band)
        {
            outsL[(size_t) band] = splitL[(size_t) band].getWritePointer (0);
            outsR[(size_t) band] = splitR[(size_t) band].getWritePointer (0);
        }
        xoL.processBlock (left,  outsL, numSamples);
        xoR.processBlock (right, outsR, numSamples);

        const float* bandL[(size_t) kNumBands];
        const float* bandR[(size_t) kNumBands];
        for (int band = 0; band < kNumBands; ++band)
        {
            bandL[(size_t) band] = splitL[(size_t) band].getReadPointer (0);
            bandR[(size_t) band] = splitR[(size_t) band].getReadPointer (0);
        }

        // Temps de Release par defaut, du plus lent (Low) au plus rapide
        // (High) -- LowMid/HighMid INTERPOLES (moyenne geometrique des
        // voisins) depuis les 3 valeurs mesurees a l'origine (Tripressor),
        // Presence (6e bande) INTERPOLEE a son tour entre HighMid et High.
        static constexpr float baseReleaseMs[(size_t) kNumBands] = { 220.0f, 165.0f, 120.0f, 92.0f, 80.0f, 70.0f, 60.0f };
        // Biais de largeur par bande (demande explicite : jouer aussi sur
        // le Mono/Stereo Spread) -- convention mastering classique :
        // resserrer le bas (mono-compatibilite), garder le medium honnete,
        // n'ouvrir le haut que si le signal source a REELLEMENT du contenu
        // stereo la-haut (jamais elargir artificiellement du contenu deja
        // mono). facteur applique a stereonessDb (cote - milieu, en dB).
        // LowMid/HighMid INTERPOLES entre leurs voisins (meme reserve que
        // baseReleaseMs/kStyleOutputBiasDb ci-dessus), Presence interpolee
        // entre HighMid et High.
        static constexpr float widthBiasPerDb[(size_t) kNumBands] = { -3.0f, -1.0f, 1.0f, 2.0f, 2.5f, 3.0f, 3.5f };
        static constexpr float widthMin[(size_t) kNumBands] = { 40.0f, 60.0f, 80.0f, 90.0f, 95.0f, 100.0f, 100.0f };
        static constexpr float widthMax[(size_t) kNumBands] = { 100.0f, 110.0f, 120.0f, 145.0f, 158.0f, 170.0f, 180.0f };

        // Fidelity : balance relative par bande AVANT compression (rmsDb,
        // deja calculee dans la boucle ci-dessous) vs. APRES (simulation,
        // meme FeedbackCompressor que le plafond GR, mais on accumule ici
        // la RMS en SORTIE plutot que la GR crete). Rempli dans la boucle,
        // corrige juste apres.
        float origRmsDbArr[(size_t) kNumBands] = {};
        float postRmsDbArr[(size_t) kNumBands] = {};

        for (int band = 0; band < kNumBands; ++band)
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

            // --- AI Expand (demande explicite : "je veux que AI regle en
            // meme temps le comp et l'expand ... pas cliquer une fois pour
            // les compresseurs et une fois pour les expandeurs") --
            // calcule et remplit TOUJOURS les champs expand* ci-dessous,
            // pour les 3 bandes, EN PLUS du calcul compression ci-dessus
            // (inputDb/ratioIndex/attackMs/releaseMs, jamais ecrase) --
            // comme ca, basculer une bande de Comp a Expand pour comparer
            // n'exige plus de relancer un Learn, les 2 jeux de valeurs
            // sont deja prets. Detection PROPRE (demande explicite : "avec
            // des reglages de detection d'ia propre") : au lieu du crest
            // factor pic/RMS utilise pour la compression, on cherche le
            // PLANCHER du signal -- RMS par fenetres courtes (~46ms), 15e
            // percentile le plus bas -- qui represente le niveau des
            // passages calmes (silence entre notes, queues de reverb) la
            // ou l'expansion agit reellement, plutot que la RMS globale.
            {
                constexpr int windowSamples = 2048; // ~46ms a 44.1kHz
                std::vector<float> blockRmsDb;
                blockRmsDb.reserve ((size_t) (numSamples / windowSamples) + 1);
                for (int start = 0; start < numSamples; start += windowSamples)
                {
                    const int n = juce::jmin (windowSamples, numSamples - start);
                    double sumSqBlock = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        const float mono = 0.5f * (dL[start + i] + dR[start + i]);
                        sumSqBlock += (double) mono * (double) mono;
                    }
                    const float blockRms = (float) std::sqrt (sumSqBlock / juce::jmax (1, n));
                    blockRmsDb.push_back (juce::Decibels::gainToDecibels (blockRms, -100.0f));
                }
                std::sort (blockRmsDb.begin(), blockRmsDb.end());
                const float floorDb = blockRmsDb.empty() ? rmsDb
                    : blockRmsDb[(size_t) juce::jlimit (0, (int) blockRmsDb.size() - 1,
                                                          juce::roundToInt (0.15f * (float) (blockRmsDb.size() - 1)))];
                // Combien le plancher est plus calme que la moyenne de la
                // bande -- l'equivalent du crest factor, mais cote calme.
                const float floorCrestDb = juce::jlimit (0.0f, 40.0f, rmsDb - floorDb);

                // Zone morte (retour utilisateur : "le high n'est pas
                // compresse et l'expandeur mets +1db avec le learn ...
                // comment eviter ce genre de mesure foireuse") -- un ecart
                // moyenne/plancher MODESTE (<10dB) releve juste de la
                // dynamique musicale normale, pas un vrai "trou" a
                // combler -- on ignore ces cas (te=0, aucune proposition)
                // plutot que de coller un boost meme discret sur une bande
                // qui n'en a pas vraiment besoin. Seul un ecart NETTEMENT
                // marque produit une proposition, avec une intensite
                // proportionnelle a l'ampleur reelle du trou. Plafond
                // ramene de 30 a 25dB (retour utilisateur : "en hard [Range]
                // peut monter jusqu'a 2.1db", alors que le mode couvre 1-3dB
                // -- un ecart de 30dB pour toucher le haut de la plage etait
                // trop extreme, la plupart des programmes n'y arrivaient
                // jamais) -- Hard (et les autres modes/axes, qui partagent
                // cette meme mesure te) atteignent desormais leur plafond
                // sur des programmes moins extremes.
                constexpr float kFloorDeadZoneDb = 10.0f;
                constexpr float kFloorCrestCeilingDb = 25.0f;
                const float te = juce::jlimit (0.0f, 1.0f,
                    (floorCrestDb - kFloorDeadZoneDb) / (kFloorCrestCeilingDb - kFloorDeadZoneDb));

                // 3 axes STRICTEMENT independants (retour utilisateur : "il
                // y a 3 reglages d'ai. un pour le range, un pour le ratio
                // et un pour l'attaque") -- chacun ne touche QUE sa propre
                // cible, plus de chevauchement entre eux.

                // Range -- gouverne EXCLUSIVEMENT par Amount (demande
                // explicite : "le knob input du mode comp passe en mode
                // range"). Chaque mode est une PLAGE (retour utilisateur :
                // "c'est pas tres musical, c'est trop" -- Smooth 0.2-0.4dB,
                // Mid 0.5-1dB, Hard 2-3dB), interpolee selon "te" (voir
                // zone morte ci-dessus) -- pas de simulation/dichotomie, la
                // DSP elle-meme respecte nativement ce plafond (voir
                // FeedbackCompressor::setExpandRangeDb()). 0dB (Off) =
                // expansion desactivee.
                r.expandInputDb[(size_t) band] = expandAmountOn
                    ? juce::jmap (te, kExpandAmountRangeMinDb[expandAmountBiasIdx], kExpandAmountRangeMaxDb[expandAmountBiasIdx])
                    : 0.0f;

                // Ratio -- gouverne EXCLUSIVEMENT par Mode. Plafond d'index
                // a 3.0 (max 6:1, pas 20:1) : un ratio extreme n'a plus
                // grand interet maintenant que Range borne de toute facon
                // le boost final, mais reste plus "dur"/abrupt a l'oreille
                // des qu'il se declenche.
                r.expandRatioIndex[(size_t) band] = juce::jlimit (0.0f, 3.0f, te * 2.0f * kExpandModeRatioBoost[expandModeBiasIdx]);

                // Attack/Release -- gouvernes EXCLUSIVEMENT par Character.
                // Plafond Attack a 500ms (aligne sur le plafond manuel du
                // knob, retour utilisateur : "200ms pour l'attaque d'un
                // expandeur c'est pas assez") pour laisser "Smooth" (x3.0)
                // vraiment atteindre le "tres long" demande. Plancher a
                // 2ms (subtilite + marge contre l'artefact de
                // redressement, voir le lissage detecteur dans
                // FeedbackCompressor).
                const auto& expChar = kExpandCharacterBias[expandCharacterBiasIdx];
                r.expandAttackMs[(size_t) band] = juce::jlimit (2.0f, 500.0f,
                    (30.0f - te * 15.0f) * expChar.attackScale);
                // Release plus long que la compression (baseReleaseMs*1.5,
                // plancher a 80ms) : l'expansion doit suivre les
                // fluctuations lentes du plancher, pas pomper sur chaque
                // micro-silence.
                r.expandReleaseMs[(size_t) band] = juce::jlimit (80.0f, 3000.0f,
                    (baseReleaseMs[band] * 1.5f - te * 30.0f) * expChar.releaseScale);

                // Output : le boost souleve la moyenne globale de la bande --
                // petite correction negative proportionnelle, pour que
                // "Expand" ne se traduise pas juste par "plus fort dans
                // l'ensemble". Magnitude proportionnee au plafond de Range
                // (voir kExpandAmountRangeMinDb/MaxDb) -- une grosse correction de
                // makeup n'a pas de sens pour un boost de 1 a 8dB max.
                r.applyExpandOutput = true;
                r.expandOutputDb[(size_t) band] = juce::jlimit (-4.0f, 0.0f, -te * 1.5f);

                if (threadShouldExit())
                    return;
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
        // balance RELATIVE entre bandes retrouve, APRES compression, celle
        // qu'elle avait AVANT (demande explicite : "une fonction qui
        // reprend la balance bass mid aigue du mix pour la remettre pareil
        // apres compression"). On compare les deviations par rapport a la
        // moyenne des 5 bandes (pas les RMS absolues, qui n'ont pas de
        // raison d'etre egales) avant/apres, et on corrige l'ecart. ---
        if (fidelity)
        {
            float origMean = 0.0f, postMean = 0.0f;
            for (int band = 0; band < kNumBands; ++band)
            {
                origMean += origRmsDbArr[band];
                postMean += postRmsDbArr[band];
            }
            origMean /= (float) kNumBands;
            postMean /= (float) kNumBands;
            for (int band = 0; band < kNumBands; ++band)
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
            // Mesure K-weighted (approximation BS.1770) reutilisable -- sert
            // a la fois pour la source d'origine (toujours) et, en mode
            // Match, pour le signal APRES compression simulee.
            auto measureLufsDb = [&] (const float* l, const float* r2, int n) -> float
            {
                juce::dsp::IIR::Filter<float> shelfL, hpL, shelfR, hpR;
                shelfL.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 1500.0, 0.7071, juce::Decibels::decibelsToGain (4.0f));
                hpL.coefficients    = juce::dsp::IIR::Coefficients<float>::makeHighPass  (sampleRate, 38.0);
                shelfR.coefficients = shelfL.coefficients;
                hpR.coefficients    = hpL.coefficients;

                double msL = 0.0, msR = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const float kl = hpL.processSample (shelfL.processSample (l[i]));
                    const float kr = hpR.processSample (shelfR.processSample (r2[i]));
                    msL += (double) kl * (double) kl;
                    msR += (double) kr * (double) kr;
                }
                msL /= juce::jmax (1, n);
                msR /= juce::jmax (1, n);

                // Formule BS.1770 (ponderation egale L/R = 1.0 chacun).
                const double sumMs = juce::jmax (1.0e-12, msL + msR);
                return (float) (-0.691 + 10.0 * std::log10 (sumMs));
            };

            const float measuredLufs = measureLufsDb (left, right, numSamples);
            r.measuredLufs = measuredLufs;

            float gainNeededDb;
            if (masteringTarget == kMatchTargetIndex)
            {
                // Match (demande explicite : "ça match au meme volume que
                // la source en RMS") -- simule le VRAI FeedbackCompressor
                // par bande (memes reglages que ceux calcules plus haut) et
                // somme les 5 bandes pour reconstituer le signal APRES
                // compression (Output=0dB), puis corrige l'ecart de
                // loudness reellement introduit -- contrairement aux
                // cibles fixes ci-dessus, qui n'ont pas besoin de simuler
                // quoi que ce soit.
                std::vector<float> postL ((size_t) numSamples, 0.0f), postR ((size_t) numSamples, 0.0f);
                for (int band = 0; band < kNumBands; ++band)
                {
                    const int ratioIdxInt = juce::jlimit (0, 6, juce::roundToInt (r.ratioIndex[(size_t) band]));
                    FeedbackCompressor simL, simR;
                    for (auto* sim : { &simL, &simR })
                    {
                        sim->prepare (sampleRate);
                        sim->setInputDriveDb (r.inputDb[(size_t) band]);
                        sim->setRatio (kRatioTable[(size_t) ratioIdxInt]);
                        sim->setKneeDb (kKneeTable[(size_t) ratioIdxInt]);
                        sim->setAttackMs (r.attackMs[(size_t) band]);
                        sim->setReleaseMs (r.releaseMs[(size_t) band]);
                        sim->setOutputDb (0.0f);
                    }

                    const float* dL = bandL[band];
                    const float* dR = bandR[band];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        postL[(size_t) i] += simL.processSample (dL[i]);
                        postR[(size_t) i] += simR.processSample (dR[i]);
                    }

                    if (threadShouldExit())
                        return;
                }

                const float postLufs = measureLufsDb (postL.data(), postR.data(), numSamples);
                gainNeededDb = juce::jlimit (-12.0f, 12.0f, measuredLufs - postLufs);
            }
            else
            {
                const float targetLufs = kLufsTargets[(size_t) masteringTarget];
                gainNeededDb = juce::jlimit (-12.0f, 12.0f, targetLufs - measuredLufs);
            }

            r.applyLoudnessTarget = true;
            r.recommendSoftClipOn = true;
            for (int band = 0; band < kNumBands; ++band)
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
    int expandMode = 1;
    int expandCharacter = 1;
    int expandAmount = 1;
    int targetSamples = 0;
    int writePos = 0;
    juce::AudioBuffer<float> capturedL, capturedR;
    Result result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiLearnAnalyzer)
};
