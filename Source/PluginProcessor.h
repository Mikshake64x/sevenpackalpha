#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <map>
#include "FeedbackCompressor.h"
#include "SoftClipper.h"
#include "Crossover.h"
#include "AiLearnAnalyzer.h"

/**
    SuppressorAudioProcessor (multibande)
    --------------------------------------
    Le signal est separe en 3 bandes (Low / Mid / High) par un Crossover
    (voir Crossover.h, porte tel quel depuis Nuke Mext), et CHAQUE bande
    recoit sa propre chaine FeedbackCompressor complete et independante
    (moteur Suppressor/Distressor inchange), avec son propre jeu de
    parametres -- comme un vrai multibande hardware (style M76).

    Convention de nommage des parametres par bande (voir bandParamId()) :
    la bande MID garde les IDs d'origine du Suppressor mono-bande SANS
    suffixe (ex: "input"), pour que les presets/sessions Suppressor
    mono-bande existants se rechargent tels quels sur la bande Mid. Les
    bandes Low/High recoivent respectivement le suffixe "_low" / "_high".

    Ce qui reste GLOBAL (pas par bande, decide avec l'utilisateur) :
    - Power (bypass complet du plugin)
    - Soft Clip / THD Valve (UN SEUL etage, applique apres la sommation
      des 3 bandes, pas par bande)
    - Crossover Wide/Linear + frequences de crossover
    - VU-metres (In/Out globaux, avant split / apres sommation+clip)

    Ce qui reste PAR BANDE :
    - Toute la chaine de compression (Input/Ratio/Attack/Release/Output/
      Detector HP+Bell+Link/Audio HP+Dist2+Dist3/Optical/British)
    - Le bouton Sidechain externe (chaque bande decide independamment si
      son detecteur ecoute le bus sidechain externe)
*/
class SuppressorAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum Band { Low = 0, Mid = 1, High = 2, NumBands = 3 };

    SuppressorAudioProcessor();
    ~SuppressorAudioProcessor() override;

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

    // --- Lus par le GUI (thread-safe, pas d'allocation) ---

    // GR combinee (max L/R) PAR BANDE -- une plaque de bande par indice.
    std::array<std::atomic<float>, (size_t) NumBands> grMeterDb { {} };

    // GR combinee PAR CANAL (max des 3 bandes, ce canal) -- utilisee
    // uniquement par le NeedleVU global de la plaque du haut (mode "GR"),
    // qui n'a pas de notion de bande. Purement un indicateur d'ensemble,
    // les vraies valeurs par bande restent grMeterDb[] ci-dessus.
    std::atomic<float> grMeterDbL { 0.0f }, grMeterDbR { 0.0f };

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
    // 0.75 / 0.5, proportions conservees (voir SuppressorAudioProcessorEditor::
    // applyUiScale()). Vit sur le processeur pour la meme raison que
    // linkInEnabled/bypassHeld ci-dessous : l'editeur peut etre detruit et
    // recree par l'hote sans que ce choix ne se perde.
    std::atomic<float> uiScale { 0.95f };

    // --- THD Valve GLOBAL (plaque du bas) : UN SEUL etage de saturation
    // (post-sommation des 3 bandes, un seul Drive/Engage), mais mesure sur
    // 3 voies -- le residu de distorsion qu'il genere est analyse par un
    // crossover (voir processBlock()) pour savoir ou tombent les
    // harmoniques generees, Low/Mid/High -- pas 3 saturations separees.
    std::atomic<float> clipMeterAmount { 0.0f };
    std::atomic<float> clipMeterAmountL { 0.0f }, clipMeterAmountR { 0.0f };
    std::atomic<float> clipMeterAmountLow { 0.0f }, clipMeterAmountMid { 0.0f }, clipMeterAmountHigh { 0.0f };

    // Etat des boutons Link In / Link Out -- PAS des parametres APVTS (pas
    // automatisables, pas sauvegardes dans les presets), mais doivent
    // quand meme vivre ICI (dans le processeur) et non dans l'editeur :
    // certains hotes/le standalone detruisent et recreent l'editeur au
    // redimensionnement de la fenetre, ce qui effacerait un simple
    // booleen membre de l'editeur. Le processeur, lui, ne meurt jamais au
    // redimensionnement. Meme raisonnement que Nuke Mext.
    std::atomic<bool> linkInEnabled  { false };
    std::atomic<bool> linkOutEnabled { false };

    // Bouton BYPASS (barre du haut) : MOMENTANE -- il faut rester appuye
    // dessus pour bypasser. Contrairement a Power, les diodes/ecrans
    // restent allumes (les compresseurs continuent de tourner en interne,
    // seule la sortie finale bascule sur le signal sec, voir processBlock()).
    // Meme raisonnement que linkInEnabled ci-dessus (vit sur le processeur,
    // pas l'editeur), meme si son etat "maintenu" ne survit naturellement
    // pas a une simple pression (pas besoin de le persister en session).
    std::atomic<bool> bypassHeld { false };

    // Bouton AI (barre de controle, au-dessus de Analog/Linear) : capture
    // quelques secondes de programme audio puis propose des valeurs de
    // depart pour Crossover Low/High + Input/Ratio/Attack/Release par
    // bande -- voir AiLearnAnalyzer.h pour le detail de l'analyse et le
    // cycle d'etats. L'editeur poll getState()/getResult() (meme mecanisme
    // de "polling" que le reste du GUI, voir SuppressorEditorContent::
    // timerCallback()) et applique le resultat via un ramp de knob, comme
    // un chargement de preset.
    AiLearnAnalyzer aiLearnAnalyzer;
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
        aiLearnAnalyzer.beginCapture (getSampleRate(), targetIndex, compressionModeIndex, attackReleaseCharIndex,
                                       gainReductionIndex,
                                       scopeCrossover, scopeDetectors, scopeCompression,
                                       styleIndex, fidelityEnabled);
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
    static constexpr const char* idCrossoverLow  = "crossoverLow";   // Hz, voir Crossover.h
    static constexpr const char* idCrossoverHigh = "crossoverHigh";  // Hz, voir Crossover.h

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

    // Bouton WIDE / LINEAR -- false (WIDE) = pente douce 1er ordre, 0
    // latence ; true (LINEAR) = FIR a phase lineaire, latence reglable
    // (voir idLinearLatencyMode). Meme parametre/DSP que Nuke Mext.
    static constexpr const char* idDigitalCrossover = "digitalCrossover";

    // Solo par bande -- des qu'une bande est soloee, les 2 autres sont
    // exclues du mix global (pas juste attenuees). Off sur les 3 = comportement
    // normal, les 3 bandes jouent. Pilote par le SoloSwitch (levier
    // switch_metal.png + diode) a gauche de chaque rangee de bande.
    static constexpr const char* idSoloLow  = "soloLow";
    static constexpr const char* idSoloMid  = "soloMid";
    static constexpr const char* idSoloHigh = "soloHigh";

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

    SoftClipper clipperL, clipperR; // etage GLOBAL, post-sommation des 3 bandes

    // Buffers scratch pour le split Low/Mid/High -- 1 par canal, remplis
    // en une passe par bloc via Crossover::processBlock(). Dimensionnes
    // dans prepareToPlay(), jamais realloues dans processBlock().
    juce::AudioBuffer<float> lowBandBuffer, midBandBuffer, highBandBuffer;

    // --- Analyse THD par bande (voir ThdValveModule.h) : le residu de
    // distorsion (signal avant clip moins signal apres clip, voir
    // processBlock()) est passe dans un 2e crossover DEDIE A L'ANALYSE
    // (memes frequences que le crossover principal), uniquement pour
    // savoir dans quelle bande tombent les harmoniques generees par
    // l'etage de saturation UNIQUE -- ce crossover ne touche jamais au
    // signal audio reellement envoye en sortie. ---
    std::vector<std::unique_ptr<Crossover>> thdAnalysisCrossovers; // 1 par canal
    juce::AudioBuffer<float> thdResidualBuffer, thdLowBuffer, thdMidBuffer, thdHighBuffer;

    std::array<std::atomic<float>*, (size_t) NumBands> inputParam            {};
    std::array<std::atomic<float>*, (size_t) NumBands> ratioIndexParam       {};
    std::array<std::atomic<float>*, (size_t) NumBands> attackParam          {};
    std::array<std::atomic<float>*, (size_t) NumBands> releaseParam         {};
    std::array<std::atomic<float>*, (size_t) NumBands> outputParam          {};
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
    std::atomic<float>* aiStyleParam    = nullptr;
    std::atomic<float>* aiFidelityParam = nullptr;
    std::atomic<float>* crossoverLowParam     = nullptr;
    std::atomic<float>* crossoverHighParam    = nullptr;
    std::atomic<float>* digitalCrossoverParam = nullptr;
    std::atomic<float>* linearLatencyModeParam = nullptr;
    int lastReportedLatencySamples = 0; // pour ne rappeler setLatencySamples() que si la valeur change reellement
    std::atomic<float>* soloLowParam  = nullptr;
    std::atomic<float>* soloMidParam  = nullptr;
    std::atomic<float>* soloHighParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuppressorAudioProcessor)
};
