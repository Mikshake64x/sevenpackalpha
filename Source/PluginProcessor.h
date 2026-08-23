#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <map>
#include "FeedbackCompressor.h"
#include "SoftClipper.h"
#include "Crossover.h"
#include "AiLearnAnalyzer.h"
#include "LicenseManager.h"
#include "SpectrumAnalyzerState.h"

/**
    SixpackAudioProcessor (multibande 5 bandes)
    ------------------------------------------------
    Le signal est separe en 5 bandes (Low / LowMid / Mid / HighMid / High)
    par un Crossover a 4 points (voir Crossover.h, generalise depuis le
    crossover 3 bandes/2 points de Tripressor), et CHAQUE bande recoit sa
    propre chaine FeedbackCompressor complete et independante (moteur
    Suppressor/Distressor inchange), avec son propre jeu de parametres --
    comme un vrai multibande hardware (style M76).

    FUSION DE BANDES (voir idCrossoverXEnabled ci-dessous) : on n'est pas
    oblige d'avoir les 5 bandes actives en permanence -- chacun des 4
    points de crossover peut etre desactive individuellement, ce qui
    fusionne les 2 bandes voisines en une seule zone traitee par le
    compresseur de la bande de plus petit indice du groupe (voir
    processBlock() : mapping groupOwner[], calcule une fois par bloc a
    partir des 4 flags enabled). Le Crossover lui-meme continue de
    splitter TOUJOURS aux 4 frequences configurees -- c'est PluginProcessor
    qui somme les sous-bandes brutes d'un groupe fusionne et n'appelle le
    compresseur QUE pour la bande "proprietaire" du groupe (les autres ne
    traitent rien, silence, pas de double compression).

    Convention de nommage des parametres par bande (voir bandParamId()) :
    la bande MID (indice 2) garde les IDs d'origine du Suppressor
    mono-bande SANS suffixe (ex: "input"), pour que les presets/sessions
    Suppressor mono-bande existants se rechargent tels quels sur la bande
    Mid. Les autres bandes recoivent un suffixe ("_low" / "_lowmid" /
    "_highmid" / "_high").

    Ce qui reste GLOBAL (pas par bande, decide avec l'utilisateur) :
    - Power (bypass complet du plugin)
    - Soft Clip / THD Valve (UN SEUL etage, applique apres la sommation
      des bandes, pas par bande)
    - Crossover Wide/Linear + les 4 frequences/enabled de crossover
    - VU-metres (In/Out globaux, avant split / apres sommation+clip)

    Ce qui reste PAR BANDE :
    - Toute la chaine de compression (Input/Ratio/Attack/Release/Output/
      Detector HP+Bell+Link/Audio HP+Dist2+Dist3/Optical/British)
    - Le bouton Sidechain externe (chaque bande decide independamment si
      son detecteur ecoute le bus sidechain externe)
*/
class SixpackAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum Band { Low = 0, LowMid = 1, Mid = 2, HighMid = 3, Presence = 4, High = 5, NumBands = 6 };
    static constexpr int NumCrossovers = NumBands - 1; // 5 points de crossover

    SixpackAudioProcessor();
    ~SixpackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Construit l'ID de parametre de compression pour une bande donnee --
    // voir note de classe ci-dessus pour la convention Mid = sans suffixe.
    static juce::String bandParamId (const char* baseId, int band);

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    LicenseManager licenseManager { "sixpack-pro" };

    // --- Lus par le GUI (thread-safe, pas d'allocation) ---

    // GR combinee (max L/R) PAR BANDE -- une plaque de bande par indice.
    // Toujours >=0 : quand l'enveloppe reelle (signee -- voir
    // FeedbackCompressor::getGainReductionDb()) est NEGATIVE (boost, cote
    // Expand), cette valeur reste a 0 -- voir grMeterExpandDb ci-dessous
    // pour le boost, meter separe (demande explicite : "une autre bande de
    // diode qui affiche l'expander en temps reel").
    std::array<std::atomic<float>, (size_t) NumBands> grMeterDb { {} };

    // Boost Expand (max L/R) PAR BANDE -- symetrique de grMeterDb ci-dessus
    // mais pour la partie NEGATIVE de l'enveloppe (cote Below/Expand).
    // Independant de grMeterDb : en mode Both, un instant donne n'alimente
    // JAMAIS les deux a la fois (le signal est soit au-dessus soit en
    // dessous du seuil a cet instant precis, voir la doc de
    // FeedbackCompressor::processSample()) -- les 2 meters s'allument donc
    // en alternance selon le programme, jamais simultanement sur UN SEUL
    // sample, mais peuvent tous les 2 sembler "actifs" sur une fenetre de
    // temps humaine (l'un pendant les passages forts, l'autre pendant les
    // passages calmes).
    std::array<std::atomic<float>, (size_t) NumBands> grMeterExpandDb { {} };

    // GR combinee PAR CANAL (max des 3 bandes, ce canal) -- utilisee
    // uniquement par le NeedleVU global de la plaque du haut (mode "GR"),
    // qui n'a pas de notion de bande. Purement un indicateur d'ensemble,
    // les vraies valeurs par bande restent grMeterDb[] ci-dessus.
    std::atomic<float> grMeterDbL { 0.0f }, grMeterDbR { 0.0f };

    // Niveau de sortie PAR BANDE (max L/R, dBFS, pic bloc avec attaque
    // instantanee/retombee douce) -- signal WET reellement produit par
    // cette bande (post compression/expand/mix/output gain, voir wetL/wetR
    // dans processBlock()), demande explicite : "ajouter le niveau de
    // sortie de chaque bande dans le rack du bas". Plancher a -100dB (pas
    // -inf) pour un affichage stable en silence.
    std::array<std::atomic<float>, (size_t) NumBands> bandOutputLevelDb;

    // --- VU a aiguille GLOBAL (plaque du haut, unique) : niveau d'entree
    // AVANT le split crossover, niveau de sortie APRES sommation + etage
    // global THD/Limit. Meme principe que l'ancien Suppressor mono-bande,
    // desormais mesure au niveau global du plugin plutot que par bande. ---
    std::atomic<float> inputLevelDbL  { -100.0f }, inputLevelDbR  { -100.0f };
    std::atomic<float> outputLevelDbL { -100.0f }, outputLevelDbR { -100.0f };

    // Mode selectionne par chaque NeedleModeSelector (0=GR 1=IN 2=OUT
    // 3=THD) -- persiste entre 2 ouvertures du plugin ET a travers la
    // sauvegarde/rechargement de session DAW (voir getStateInformation/
    // setStateInformation). GR en mode multibande = moyenne des 3 bandes
    // (voir PluginEditor), pas une bande unique.
    int needleModeIndex1 = 0, needleModeIndex2 = 0;

    // Facteur d'echelle de la fenetre (menu Options, engrenage) -- 1.0 /
    // 0.75 / 0.5, proportions conservees (voir SixpackAudioProcessorEditor::
    // applyUiScale()). Vit sur le processeur pour la meme raison que
    // linkInEnabled ci-dessous : l'editeur peut etre detruit et
    // recree par l'hote sans que ce choix ne se perde.
    // Retour utilisateur explicite : le plugin doit s'ouvrir a 90% par
    // defaut (pas 100%, pas 95%).
    std::atomic<float> uiScale { 0.90f };

    // Theme SOMBRE (0, defaut)/CLAIR (1) -- retour utilisateur explicite :
    // "je souhaite faire un design plus clair qu'on peut changer dans
    // l'engrenage". Persiste dans apvts.state (meme mecanisme que
    // needleModeIndex1/2, voir getStateInformation()/setStateInformation())
    // -- pas un AudioParameter (preference d'affichage, pas automatable).
    std::atomic<float> lightTheme { 0.0f };

    // --- THD Valve GLOBAL (plaque du bas) : UN SEUL etage de saturation
    // (post-sommation des 3 bandes, un seul Drive/Engage), mais mesure sur
    // 3 voies -- le residu de distorsion qu'il genere est analyse par un
    // crossover (voir processBlock()) pour savoir ou tombent les
    // harmoniques generees, Low/Mid/High -- pas 3 saturations separees.
    std::atomic<float> clipMeterAmount { 0.0f };
    std::atomic<float> clipMeterAmountL { 0.0f }, clipMeterAmountR { 0.0f };
    std::array<std::atomic<float>, (size_t) NumBands> clipMeterAmountBand { {} };

    // Etat des boutons Link In / Link Out -- PAS des parametres APVTS (pas
    // automatisables, pas sauvegardes dans les presets), mais doivent
    // quand meme vivre ICI (dans le processeur) et non dans l'editeur :
    // certains hotes/le standalone detruisent et recreent l'editeur au
    // redimensionnement de la fenetre, ce qui effacerait un simple
    // booleen membre de l'editeur. Le processeur, lui, ne meurt jamais au
    // redimensionnement. Meme raisonnement que Nuke Mext.
    std::atomic<bool> linkInEnabled  { false };
    std::atomic<bool> linkOutEnabled { false };

    // Bouton AI (barre de controle, au-dessus de Analog/Linear) : capture
    // quelques secondes de programme audio puis propose des valeurs de
    // depart pour Crossover Low/High + Input/Ratio/Attack/Release par
    // bande -- voir AiLearnAnalyzer.h pour le detail de l'analyse et le
    // cycle d'etats. L'editeur poll getState()/getResult() (meme mecanisme
    // de "polling" que le reste du GUI, voir SixpackEditorContent::
    // timerCallback()) et applique le resultat via un ramp de knob, comme
    // un chargement de preset.
    AiLearnAnalyzer aiLearnAnalyzer;

    // Fond de spectre du nouvel editeur de crossover (voir
    // CrossoverSpectrumEditor.h) -- lu par l'UI (thread timer), alimente
    // par processBlock() (thread audio), voir SpectrumAnalyzerState.h.
    SpectrumAnalyzerState spectrumAnalyzer;
    void startAiLearn()
    {
        const int targetIndex = aiMasteringTargetParam != nullptr ? (int) std::round (aiMasteringTargetParam->load()) : 0;
        const int compressionModeIndex = aiCompressionModeParam != nullptr ? (int) std::round (aiCompressionModeParam->load()) : 1;
        const int attackReleaseCharIndex = aiAttackReleaseCharacterParam != nullptr ? (int) std::round (aiAttackReleaseCharacterParam->load()) : 1;
        const int gainReductionIndex = aiGainReductionModeParam != nullptr ? (int) std::round (aiGainReductionModeParam->load()) : 1;
        const bool scopeCrossover   = aiScopeCrossoverParam   != nullptr && aiScopeCrossoverParam->load()   > 0.5f;
        const bool scopeDetectors   = aiScopeDetectorsParam   != nullptr && aiScopeDetectorsParam->load()   > 0.5f;
        const bool scopeCompression = aiScopeCompressionParam == nullptr || aiScopeCompressionParam->load() > 0.5f;
        const int styleIndex = aiStyleParam != nullptr ? (int) std::round (aiStyleParam->load()) : 0;
        const bool fidelityEnabled = aiFidelityParam != nullptr && aiFidelityParam->load() > 0.5f;
        const int expandModeIndex = aiExpandModeParam != nullptr ? (int) std::round (aiExpandModeParam->load()) : 0;
        const int expandCharacterIndex = aiExpandCharacterParam != nullptr ? (int) std::round (aiExpandCharacterParam->load()) : 0;
        const int expandAmountIndex = aiExpandAmountParam != nullptr ? (int) std::round (aiExpandAmountParam->load()) : 0;
        // Chaque Learn calcule et pousse LES DEUX jeux de parametres (Comp
        // ET Expand) pour les 3 bandes a chaque fois (demande explicite :
        // "je veux que AI regle en meme temps le comp et l'expand") -- plus
        // besoin de lire l'etat Comp/Expand manuel actuel des bandes ici,
        // voir AiLearnAnalyzer::run() et SixpackEditorContent::
        // applyAiLearnResult().
        aiLearnAnalyzer.beginCapture (getSampleRate(), targetIndex, compressionModeIndex, attackReleaseCharIndex,
                                       gainReductionIndex,
                                       scopeCrossover, scopeDetectors, scopeCompression,
                                       styleIndex, fidelityEnabled,
                                       expandModeIndex, expandCharacterIndex, expandAmountIndex);
    }

    void toggleABCompare();
    void selectABSlot (bool wantB) { if (wantB != usingSlotB) toggleABCompare(); }
    bool isUsingSlotB() const noexcept { return usingSlotB; }

    // --- Identifiants "de base" des parametres de compression par-bande ---
    static constexpr const char* idInput               = "input";
    static constexpr const char* idRatioIndex          = "ratioIndex";
    static constexpr const char* idAttack              = "attack";
    static constexpr const char* idRelease             = "release";
    static constexpr const char* idOutput              = "output";

    // Reglages Input/Ratio/Attack/Release/Output DEDIES au mode EXPAND
    // (demande explicite : "Comp et expand partage la meme bande, mais les
    // reglages doivent etre independant ... quand on passe sur expand les
    // reglages de bande sont uniquement expand") -- meme role/plage que
    // les 5 IDs ci-dessus, mais lus par le DSP UNIQUEMENT quand le mode
    // Above/Below de la bande est Expand pur (voir updateDspFromParams()).
    // Both continue d'utiliser les reglages COMPRESSION ci-dessus (demande
    // explicite : "quand on passe sur both on mets les reglage de
    // compression"). Les knobs de la plaque de bande sont RE-attaches a ce
    // jeu de parametres quand on bascule sur Expand (voir
    // SixpackEditorContent::rebindBandKnobsForMode()).
    static constexpr const char* idInputExpand      = "inputExpand";
    static constexpr const char* idRatioIndexExpand = "ratioIndexExpand";
    static constexpr const char* idAttackExpand     = "attackExpand";
    static constexpr const char* idReleaseExpand    = "releaseExpand";
    static constexpr const char* idOutputExpand     = "outputExpand";
    static constexpr const char* idDetectorHp   = "detectorHp";
    static constexpr const char* idDetectorHpFreq  = "detectorHpFreq";
    static constexpr const char* idDetectorHpSlope = "detectorHpSlope";
    static constexpr const char* idDetectorBell = "detectorBell";
    static constexpr const char* idDetectorBellFreq = "detectorBellFreq";
    static constexpr const char* idDetectorBellGain = "detectorBellGain";
    // 2e point Bell -- remplace le bouton HP sur la bande MID (deja bornee
    // par le crossover, un HP y est redondant) : donne a MID 2 points de
    // forme du sidechain au lieu d'un seul, voir DetectorFilterGraph. Existe
    // techniquement sur les 3 bandes (meme boucle de creation de parametres)
    // mais seule MID a un bouton pour l'engager dans l'UI.
    static constexpr const char* idDetectorBell2 = "detectorBell2";
    static constexpr const char* idDetectorBell2Freq = "detectorBell2Freq";
    static constexpr const char* idDetectorBell2Gain = "detectorBell2Gain";
    static constexpr const char* idDetectorLink = "detectorLink";
    // Par bande -- chaque bande decide independamment d'ecouter le bus
    // sidechain externe (voir doc de classe).
    static constexpr const char* idSidechainEnable = "sidechainEnable";
    static constexpr const char* idAudioHp      = "audioHp";
    static constexpr const char* idAudioDist2   = "audioDist2";
    static constexpr const char* idAudioDist3   = "audioDist3";
    static constexpr const char* idOptical              = "optical";
    static constexpr const char* idBritish              = "british";

    // Above / Below (demande explicite, "dans le meme esprit que
    // Ableton") -- false = Above (compression classique, traite le signal
    // AU-DESSUS du seuil), true = Below (traite le signal EN DESSOUS du
    // seuil -- compression ascendante/expansion, voir FeedbackCompressor::
    // setAboveBelowMode()). Le bouton (a gauche d'Optical) change de nom
    // ABOVE/BELOW selon l'etat.
    static constexpr const char* idAboveBelow            = "aboveBelow";

    // Faders Mix (Dry/Wet) et Width PAR BANDE (voir FaderControl.h) --
    // Mix blende, pour cette bande uniquement, le signal compresse (wet)
    // et son dry de reference (post Output gain, sans compression) AVANT
    // que les 3 bandes ne soient sommees. Width encode Mid/Side sur le
    // signal WET de la bande (apres Mix), n'affecte jamais le detecteur.
    // 100% = neutre (comportement inchange) pour les deux par defaut.
    static constexpr const char* idBandMix   = "bandMix";
    static constexpr const char* idBandWidth = "bandWidth";

    // --- Parametres GLOBAUX (non rattaches a une bande) ---
    static constexpr const char* idPower                = "power";
    static constexpr const char* idSoftClip             = "softClip"; // THD Valve global
    // Choix du "flavor" de saturation de l'etage THD Valve (voir
    // SoftClipper::SatMode) : Classic = comportement d'origine inchange
    // (defaut), les 4 autres sont des variantes type mastering (Tape/Iron/
    // Air/Clip), voir ThdValveModule pour les boutons. Un seul choix
    // possible a la fois -- selectionner "aucun" (reclique sur le mode
    // actif) revient a Classic.
    static constexpr const char* idThdMode              = "thdMode";

    // 5 points de crossover (idCrossoverX = frequence Hz, voir Crossover.h/
    // Crossover::kSplitMinHz/MaxHz/DefaultHz pour les bornes ; idCrossoverXEnabled
    // = ce point separe-t-il reellement 2 bandes, ou est-il "eteint" (les 2
    // bandes voisines fusionnent, voir doc de classe ci-dessus)). Index 0 =
    // le plus bas (Low/LowMid), index 4 = le plus haut (Presence/High).
    static constexpr const char* idCrossover[NumCrossovers] = { "crossoverA", "crossoverB", "crossoverC", "crossoverD", "crossoverE" };
    static constexpr const char* idCrossoverEnabled[NumCrossovers] = { "crossoverAEnabled", "crossoverBEnabled", "crossoverCEnabled", "crossoverDEnabled", "crossoverEEnabled" };

    // Cible de mastering LUFS pour le bouton AI (menu Options/engrenage,
    // section "AI") -- reglage PERSISTANT (sauvegarde en session/preset,
    // c'est un choix de workflow, pas un resultat d'analyse). "Off" (index
    // 0) = comportement AI actuel inchange, aucune correction de loudness.
    // Choisir une plateforme fait AUSSI regler Output (par bande, egal sur
    // les 3) + engager le Soft Clip (ceiling true-peak, garde-fou) au
    // prochain apprentissage AI, en plus de Crossover/Input/Ratio/Attack/
    // Release/Mix/Width -- voir AiLearnAnalyzer::kLufsTargets. "Side Key"
    // (dernier choix, -6 LUFS) est le repere le plus agressif de la liste --
    // demande explicite, simple cible LUFS comme les autres, PAS un
    // mecanisme special (l'ancienne version "pousse le THD a 30%" a ete
    // retiree, ce n'etait pas ce qui etait demande).
    static constexpr const char* idAiMasteringTarget = "aiMasteringTarget";

    // Portee du bouton AI (menu Options, section "AI", demande explicite :
    // "AI ne s'occupe que de la compression si on le souhaite") -- 3
    // interrupteurs INDEPENDANTS (pas un radio, contrairement au target de
    // mastering ci-dessus) qui decident quelles categories de reglages le
    // prochain apprentissage a le droit de toucher. Crossover et
    // Compression actifs par defaut (comportement d'origine) ; Detecteurs
    // desactive par defaut (fonctionnalite optionnelle, opt-in).
    static constexpr const char* idAiScopeCrossover   = "aiScopeCrossover";
    static constexpr const char* idAiScopeDetectors   = "aiScopeDetectors";
    static constexpr const char* idAiScopeCompression = "aiScopeCompression";

    // Intensite generale de la compression que le bouton AI propose (menu
    // Options, section AI, demande explicite : "3 modes de compression,
    // Soft, Medium et Hard", + un choix "Off" -- demande explicite : "on
    // doit pouvoir agir que sur les points AI qui ne vont pas" -- Off
    // desactive UNIQUEMENT l'ecriture de l'Input par le bouton AI, laisse
    // le reste de la portee Compression intact) -- influence l'Input
    // calcule par bande, voir AiLearnAnalyzer::kCompressionModeBias.
    static constexpr const char* idAiCompressionMode = "aiCompressionMode";

    // Caractere Attack/Release propose par le bouton AI (menu Options,
    // section AI, demande explicite : "Smooth, Medium, Punchy" + un choix
    // "Off" -- Off desactive UNIQUEMENT l'ecriture d'Attack/Release par le
    // bouton AI) -- axe INDEPENDANT du mode de compression ci-dessus, voir
    // AiLearnAnalyzer::kAttackReleaseCharacterBias.
    static constexpr const char* idAiAttackReleaseCharacter = "aiAttackReleaseCharacter";

    // Intensite globale de gain reduction proposee par le bouton AI (menu
    // Options, section AI, demande explicite suite a "je trouve que ca
    // compresse beaucoup" : Smooth/Mid/Hard + un choix "Off" -- Off
    // desactive UNIQUEMENT l'ecriture du Ratio par le bouton AI, et saute
    // la simulation de plafond GR, voir run()) -- axe SUPPLEMENTAIRE,
    // independant du mode de compression et du caractere attack/release
    // ci-dessus, voir AiLearnAnalyzer::kGainReductionBias.
    static constexpr const char* idAiGainReductionMode = "aiGainReductionMode";

    // Triade AI Expand (demande explicite : "pour l'ia de expand, il faut
    // des reglages comme pour l'ia comp" -- meme role que idAiCompressionMode/
    // idAiAttackReleaseCharacter/idAiGainReductionMode ci-dessus, mais pour
    // les bandes en mode Expand/Both). Voir AiLearnAnalyzer::kExpandModeBias/
    // kExpandCharacterBias/kExpandAmountBias.
    static constexpr const char* idAiExpandMode      = "aiExpandMode";
    static constexpr const char* idAiExpandCharacter = "aiExpandCharacter";
    static constexpr const char* idAiExpandAmount    = "aiExpandAmount";

    // Style musical propose par le bouton AI (menu Options, section AI,
    // demande explicite : "plusieurs styles musicaux qui vont changer la
    // balance des outputs en fonction du style. Bass +3dB en bass, Loud
    // +3dB en bass et high, rock...") -- offset statique par bande ajoute
    // a l'Output, "Off" = aucun offset, voir AiLearnAnalyzer::kStyleOutputBiasDb.
    static constexpr const char* idAiStyle = "aiStyle";

    // Fidelity (menu Options, section AI, demande explicite : "une fonction
    // qui reprend la balance bass mid aigue du mix pour la remettre pareil
    // apres compression") -- interrupteur independant, off par defaut, voir
    // AiLearnAnalyzer::Result::applyFidelity.
    static constexpr const char* idAiFidelity = "aiFidelity";

    // Selecteur ANALOG / LINEAR -- false = ANALOG (WIDE, pente 1er ordre,
    // 0 latence) ; true (LINEAR) = FIR a phase lineaire, latence reglable
    // (voir idLinearLatencyMode). (ANALOG 2 retiree, voir doc de classe
    // Crossover.h.)
    static constexpr const char* idDigitalCrossover = "digitalCrossover";

    // Solo par bande -- des qu'une bande est soloee, les autres sont
    // exclues du mix global (pas juste attenuees). Off sur les 6 = comportement
    // normal, toutes les bandes jouent. Pilote par le SoloSwitch (levier
    // switch_metal.png + diode) a gauche de chaque rangee/plaque de bande.
    // Une bande fusionnee (voir idCrossoverXEnabled) n'a pas de solo propre
    // effectif puisqu'elle ne traite rien (voir processBlock()).
    static constexpr const char* idSolo[NumBands] = { "soloLow", "soloLowMid", "soloMid", "soloHighMid", "soloPresence", "soloHigh" };

    // Budget de latence du mode LINEAR : 0=Low(~7ms) / 1=Medium(~25ms) /
    // 2=High(~80ms). Ignore en mode WIDE (toujours 0 latence).
    static constexpr const char* idLinearLatencyMode = "linearLatencyMode";

    // Table des 7 ratios discrets (index -> ratio reel), et de leurs knees
    // -- voir le manuel EL8 : 2:1 a un knee tres long, 20:1 quasi aucun.
    // NUKE retire (peu utile), 8:1 ajoute a la place, juste apres 6:1.
    static constexpr float ratioTable[7] = { 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 20.0f };
    static constexpr float kneeTable[7]  = { 24.0f, 18.0f, 12.0f, 9.0f, 7.5f, 6.0f, 1.0f };
    static constexpr float defaultAttackMs  = 5.60f;
    static constexpr float defaultReleaseMs = 50.0f;

private:
    void timerCallback() override;
    void updateDspFromParams();

    // Applique la contrainte de monotonie stricte (avec ecart minimum en
    // echelle log) entre les 4 frequences de crossover -- appele a chaque
    // bloc AVANT de pousser les valeurs dans les Crossover (voir
    // processBlock()). Ne modifie PAS les parametres APVTS eux-memes
    // (l'utilisateur reste libre de faire glisser un curseur "a travers"
    // son voisin dans l'UI -- seul le DSP est protege), retourne les 4
    // frequences deja triees/espacees.
    std::array<float, (size_t) NumCrossovers> sanitizeCrossoverFrequencies() const noexcept;

    std::atomic<juce::uint32> lastProcessBlockMs { 0 };

    std::map<juce::String, float> abSlotA, abSlotB;
    bool usingSlotB = false;

    // IMPORTANT : vector<unique_ptr<Crossover>>, PAS vector<Crossover> --
    // evite de dependre de la default-constructibilite de Crossover pour
    // vector::resize() (voir Crossover.h / note Nuke Mext -- meme
    // contournement C2512 MSVC). Une instance par canal (L/R).
    std::vector<std::unique_ptr<Crossover>> crossovers;

    // [band][channel] -- chaine de compression independante par bande.
    std::array<std::vector<FeedbackCompressor>, (size_t) NumBands> compressors;

    SoftClipper clipperL, clipperR; // etage GLOBAL, post-sommation des bandes

    // Buffers scratch pour le split 5 bandes -- 1 par (bande x canal),
    // remplis en une passe par bloc via Crossover::processBlock().
    // Dimensionnes dans prepareToPlay(), jamais realloues dans processBlock().
    std::array<juce::AudioBuffer<float>, (size_t) NumBands> rawBandBuffers;

    // Enveloppe (lineaire, thread audio uniquement) du niveau de sortie
    // par bande -- retombee appliquee ECHANTILLON PAR ECHANTILLON (voir
    // processBlock()), pas une fois par bloc comme avant (retour
    // utilisateur : "les vu metre output ... sont pas fluides, on dirait
    // qu'ils sont crantes quand je coupe le son" -- avec un gros buffer
    // hote, une seule retombee par bloc cree des paliers visibles ; en
    // suivant chaque echantillon, la retombee reste lisse quelle que soit
    // la taille de bloc de l'hote). bandOutputLevelDb[] ci-dessus reste
    // l'atomic lu par l'UI, mis a jour a partir de cette enveloppe.
    std::array<float, (size_t) NumBands> bandOutputEnvLin {};
    float bandOutputPerSampleDecay = 1.0f; // recalcule dans prepareToPlay()

    // --- Analyse THD par bande (voir ThdValveModule.h) : le residu de
    // distorsion (signal avant clip moins signal apres clip, voir
    // processBlock()) est passe dans un 2e crossover DEDIE A L'ANALYSE
    // (memes frequences que le crossover principal), uniquement pour
    // savoir dans quelle bande tombent les harmoniques generees par
    // l'etage de saturation UNIQUE -- ce crossover ne touche jamais au
    // signal audio reellement envoye en sortie. ---
    std::vector<std::unique_ptr<Crossover>> thdAnalysisCrossovers; // 1 par canal
    juce::AudioBuffer<float> thdResidualBuffer;
    std::array<juce::AudioBuffer<float>, (size_t) NumBands> thdBandBuffers;

    std::array<std::atomic<float>*, (size_t) NumBands> inputParam            {};
    std::array<std::atomic<float>*, (size_t) NumBands> ratioIndexParam       {};
    std::array<std::atomic<float>*, (size_t) NumBands> attackParam          {};
    std::array<std::atomic<float>*, (size_t) NumBands> releaseParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> outputParam          {};
    std::array<std::atomic<float>*, (size_t) NumBands> inputExpandParam      {};
    std::array<std::atomic<float>*, (size_t) NumBands> ratioIndexExpandParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> attackExpandParam    {};
    std::array<std::atomic<float>*, (size_t) NumBands> releaseExpandParam   {};
    std::array<std::atomic<float>*, (size_t) NumBands> outputExpandParam    {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorHpParam      {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorHpFreqParam  {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorHpSlopeParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBellParam    {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBellFreqParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBellGainParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBell2Param    {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBell2FreqParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorBell2GainParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> detectorLinkParam    {};
    std::array<std::atomic<float>*, (size_t) NumBands> sidechainEnableParam {};
    std::array<std::atomic<float>*, (size_t) NumBands> audioHpParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> audioDist2Param      {};
    std::array<std::atomic<float>*, (size_t) NumBands> audioDist3Param      {};
    std::array<std::atomic<float>*, (size_t) NumBands> opticalParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> britishParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> bandMixParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> bandWidthParam       {};

    std::atomic<float>* powerParam            = nullptr;
    std::atomic<float>* softClipParam         = nullptr;
    std::atomic<float>* thdModeParam          = nullptr;
    std::atomic<float>* aiMasteringTargetParam = nullptr;
    std::atomic<float>* aiScopeCrossoverParam   = nullptr;
    std::atomic<float>* aiScopeDetectorsParam   = nullptr;
    std::atomic<float>* aiScopeCompressionParam = nullptr;
    std::atomic<float>* aiCompressionModeParam  = nullptr;
    std::atomic<float>* aiAttackReleaseCharacterParam = nullptr;
    std::atomic<float>* aiGainReductionModeParam = nullptr;
    std::atomic<float>* aiExpandModeParam      = nullptr;
    std::atomic<float>* aiExpandCharacterParam = nullptr;
    std::atomic<float>* aiExpandAmountParam    = nullptr;
    std::atomic<float>* aiStyleParam    = nullptr;
    std::atomic<float>* aiFidelityParam = nullptr;
    std::array<std::atomic<float>*, (size_t) NumCrossovers> crossoverFreqParam    {};
    std::array<std::atomic<float>*, (size_t) NumCrossovers> crossoverEnabledParam {};
    std::atomic<float>* digitalCrossoverParam = nullptr;
    std::atomic<float>* linearLatencyModeParam = nullptr;
    int lastReportedLatencySamples = 0; // pour ne rappeler setLatencySamples() que si la valeur change reellement
    std::array<std::atomic<float>*, (size_t) NumBands> soloParam {};

    // Calcule, a partir des 4 flags idCrossoverXEnabled, quelle bande
    // "possede" (traite) chaque groupe de bandes fusionnees -- voir doc de
    // classe. groupOwner[b] == b si la bande b n'est fusionnee avec aucune
    // voisine ; sinon groupOwner[b] == indice le plus bas du groupe auquel
    // b appartient (et cette bande b, si different de son propre indice,
    // ne traite rien -- voir processBlock()).
    std::array<int, (size_t) NumBands> computeGroupOwners() const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixpackAudioProcessor)
};
