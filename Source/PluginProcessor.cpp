#include "PluginProcessor.h"
#include "PluginEditor.h"

constexpr float SuppressorAudioProcessor::ratioTable[7];
constexpr float SuppressorAudioProcessor::kneeTable[7];

// Correction de calibration fixe (Low/Mid/High), mesuree via PluginDoctor
// (Linear Analysis, Release au plus court pour eviter tout artefact
// d'enveloppe) pour obtenir une reponse parfaitement plate -- SEPAREE par
// mode de crossover (ANALOG/WIDE vs LINEAR), la reconstruction n'etant pas
// la meme (filtres differents). Appliquee ICI, EN PLUS de ce que
// l'utilisateur regle au knob -- jamais visible sur l'affichage (les
// knobs Input/Output restent a 0.00 dB par defaut), comme un trim d'usine
// cache sur du materiel calibre. Meme principe que Nuke Mext.
static constexpr float kInputCalibrationTrimDbAnalog[3]  = {  0.0f,  0.0f,  0.0f }; // Low, Mid, High
static constexpr float kOutputCalibrationTrimDbAnalog[3] = { -1.2f, -0.6f, -0.3f }; // Low, Mid, High
static constexpr float kInputCalibrationTrimDbLinear[3]  = {  0.0f,  0.0f,  0.0f }; // Low, Mid, High
static constexpr float kOutputCalibrationTrimDbLinear[3] = { -0.5f, -0.2f,  0.0f }; // Low, Mid, High

juce::String SuppressorAudioProcessor::bandParamId (const char* baseId, int band)
{
    if (band == Mid)
        return juce::String (baseId); // compat presets/sessions Suppressor mono-bande
    return juce::String (baseId) + (band == Low ? "_low" : "_high");
}

SuppressorAudioProcessor::SuppressorAudioProcessor()
     : AudioProcessor (BusesProperties()
                       #if ! JucePlugin_IsMidiEffect
                        #if ! JucePlugin_IsSynth
                         .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                         // Bus sidechain externe (desactive par defaut) --
                         // route depuis une autre piste dans le DAW. Chaque
                         // bande decide independamment de l'ecouter ou non
                         // (voir idSidechainEnable, par bande).
                         .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        #endif
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       #endif
                         )
{
    for (int band = 0; band < (int) NumBands; ++band)
    {
        const size_t b = (size_t) band;
        inputParam[b]            = apvts.getRawParameterValue (bandParamId (idInput, band));
        ratioIndexParam[b]       = apvts.getRawParameterValue (bandParamId (idRatioIndex, band));
        attackParam[b]           = apvts.getRawParameterValue (bandParamId (idAttack, band));
        releaseParam[b]          = apvts.getRawParameterValue (bandParamId (idRelease, band));
        outputParam[b]           = apvts.getRawParameterValue (bandParamId (idOutput, band));
        inputExpandParam[b]      = apvts.getRawParameterValue (bandParamId (idInputExpand, band));
        ratioIndexExpandParam[b] = apvts.getRawParameterValue (bandParamId (idRatioIndexExpand, band));
        attackExpandParam[b]     = apvts.getRawParameterValue (bandParamId (idAttackExpand, band));
        releaseExpandParam[b]    = apvts.getRawParameterValue (bandParamId (idReleaseExpand, band));
        outputExpandParam[b]     = apvts.getRawParameterValue (bandParamId (idOutputExpand, band));
        detectorHpParam[b]       = apvts.getRawParameterValue (bandParamId (idDetectorHp, band));
        detectorHpFreqParam[b]   = apvts.getRawParameterValue (bandParamId (idDetectorHpFreq, band));
        detectorHpSlopeParam[b]  = apvts.getRawParameterValue (bandParamId (idDetectorHpSlope, band));
        detectorBellParam[b]     = apvts.getRawParameterValue (bandParamId (idDetectorBell, band));
        detectorBellFreqParam[b] = apvts.getRawParameterValue (bandParamId (idDetectorBellFreq, band));
        detectorBellGainParam[b] = apvts.getRawParameterValue (bandParamId (idDetectorBellGain, band));
        detectorBell2Param[b]     = apvts.getRawParameterValue (bandParamId (idDetectorBell2, band));
        detectorBell2FreqParam[b] = apvts.getRawParameterValue (bandParamId (idDetectorBell2Freq, band));
        detectorBell2GainParam[b] = apvts.getRawParameterValue (bandParamId (idDetectorBell2Gain, band));
        detectorLinkParam[b]     = apvts.getRawParameterValue (bandParamId (idDetectorLink, band));
        sidechainEnableParam[b]  = apvts.getRawParameterValue (bandParamId (idSidechainEnable, band));
        audioHpParam[b]          = apvts.getRawParameterValue (bandParamId (idAudioHp, band));
        audioDist2Param[b]       = apvts.getRawParameterValue (bandParamId (idAudioDist2, band));
        audioDist3Param[b]       = apvts.getRawParameterValue (bandParamId (idAudioDist3, band));
        opticalParam[b]          = apvts.getRawParameterValue (bandParamId (idOptical, band));
        britishParam[b]          = apvts.getRawParameterValue (bandParamId (idBritish, band));
        aboveBelowParam[b]       = apvts.getRawParameterValue (bandParamId (idAboveBelow, band));
        bandMixParam[b]          = apvts.getRawParameterValue (bandParamId (idBandMix, band));
        bandWidthParam[b]        = apvts.getRawParameterValue (bandParamId (idBandWidth, band));
    }

    powerParam            = apvts.getRawParameterValue (idPower);
    softClipParam         = apvts.getRawParameterValue (idSoftClip);
    thdModeParam          = apvts.getRawParameterValue (idThdMode);
    aiMasteringTargetParam = apvts.getRawParameterValue (idAiMasteringTarget);
    aiScopeCrossoverParam   = apvts.getRawParameterValue (idAiScopeCrossover);
    aiScopeDetectorsParam   = apvts.getRawParameterValue (idAiScopeDetectors);
    aiScopeCompressionParam = apvts.getRawParameterValue (idAiScopeCompression);
    aiCompressionModeParam  = apvts.getRawParameterValue (idAiCompressionMode);
    aiAttackReleaseCharacterParam = apvts.getRawParameterValue (idAiAttackReleaseCharacter);
    aiGainReductionModeParam = apvts.getRawParameterValue (idAiGainReductionMode);
    aiExpandModeParam      = apvts.getRawParameterValue (idAiExpandMode);
    aiExpandCharacterParam = apvts.getRawParameterValue (idAiExpandCharacter);
    aiExpandAmountParam    = apvts.getRawParameterValue (idAiExpandAmount);
    aiStyleParam    = apvts.getRawParameterValue (idAiStyle);
    aiFidelityParam = apvts.getRawParameterValue (idAiFidelity);
    crossoverLowParam     = apvts.getRawParameterValue (idCrossoverLow);
    crossoverHighParam    = apvts.getRawParameterValue (idCrossoverHigh);
    digitalCrossoverParam = apvts.getRawParameterValue (idDigitalCrossover);
    linearLatencyModeParam = apvts.getRawParameterValue (idLinearLatencyMode);
    soloLowParam  = apvts.getRawParameterValue (idSoloLow);
    soloMidParam  = apvts.getRawParameterValue (idSoloMid);
    soloHighParam = apvts.getRawParameterValue (idSoloHigh);

    startTimerHz (10);
}

SuppressorAudioProcessor::~SuppressorAudioProcessor()
{
    stopTimer();
}

void SuppressorAudioProcessor::timerCallback()
{
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const juce::uint32 last = lastProcessBlockMs.load();

    if (last == 0 || now - last > 250)
    {
        for (int band = 0; band < (int) NumBands; ++band)
        {
            grMeterDb[(size_t) band].store (0.0f);
            grMeterExpandDb[(size_t) band].store (0.0f);
        }
        grMeterDbL.store (0.0f);
        grMeterDbR.store (0.0f);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SuppressorAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    static const char* bandNamePrefix[3] = { "Low ", "", "High " };

    for (int band = 0; band < (int) NumBands; ++band)
    {
        const juce::String prefix (bandNamePrefix[band]);

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idInput, band), 1 }, prefix + "Input",
            NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (idRatioIndex, band), 1 }, prefix + "Ratio",
            StringArray { "2:1", "3:1", "4:1", "6:1", "8:1", "10:1 Opto", "20:1" }, 3));

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idAttack, band), 1 }, prefix + "Attack",
            NormalisableRange<float> (0.05f, 40.0f, 0.001f, 0.3f), defaultAttackMs,
            AudioParameterFloatAttributes().withLabel ("ms")));

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idRelease, band), 1 }, prefix + "Release",
            NormalisableRange<float> (50.0f, 3000.0f, 0.1f, 0.3f), defaultReleaseMs,
            AudioParameterFloatAttributes().withLabel ("ms")));

        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idOutput, band), 1 }, prefix + "Output",
            NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        // Jeu de reglages DEDIE au mode Expand (demande explicite : "les
        // reglages doivent etre independant") -- memes plages/defauts que
        // les 5 parametres compression ci-dessus, mais un stockage
        // separe : voir idInputExpand et la doc associee.
        // Range, PAS Input (demande explicite : "le knob input du mode
        // comp passe en mode range" -- SEULEMENT pour Expand, Comp garde
        // son knob Input normal) -- plafond du boost applicable, 0dB par
        // defaut = expansion COMPLETEMENT DESACTIVEE (demande explicite :
        // "l'expander doit pas fonctionner de base, il fonctionne juste
        // quand on lui augmente le seuil"). Voir FeedbackCompressor::
        // setExpandRangeDb() -- remplace l'ancien role "Input drive" de ce
        // parametre, qui ne pouvait de toute facon jamais desactiver
        // l'expansion (un vrai silence reste sous n'importe quel seuil,
        // meme apres un pre-gain enorme).
        // Retour a un flat 0.0f sur les 3 bandes (demande explicite : "les
        // ranges des expand doivent etre a 0 a l'ouverture du plug in") --
        // la calibration par bande (0.8/0.2/0.0) etait valable pour
        // l'ancien systeme a 3 etats, mais avec la nouvelle philosophie
        // Comp/Expand (2 etats), l'expansion doit rester STRICTEMENT
        // desactivee tant que l'utilisateur ne monte pas Range a la main.
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idInputExpand, band), 1 }, prefix + "Expand Range",
            NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
        // Ratio par defaut plus doux que la compression (2:1, index 0, au
        // lieu de 6:1) -- retour utilisateur : le meme ratio "costaud" par
        // defaut que la compression rendait l'expansion "super forte" des
        // l'engagement, meme au point 0 (voir aussi le plafond de boost
        // dans FeedbackCompressor::processSample()).
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (idRatioIndexExpand, band), 1 }, prefix + "Ratio Expand",
            StringArray { "2:1", "3:1", "4:1", "6:1", "8:1", "10:1 Opto", "20:1" }, 0));
        // Plage remontee a 200ms (etait 40ms, comme la compression) --
        // retour utilisateur : "40ms d'attaque pour un expander c'est
        // juste" -- les expanders/upward compressors de mastering
        // typiques tournent souvent en dizaines/centaines de ms d'attaque
        // (reaction plus lente, plus musicale, moins de risque de pompage
        // sur les micro-silences) -- 40ms etait trop court pour couvrir ce
        // terrain. La compression (idAttack) garde sa plage d'origine,
        // inchangee -- seul le jeu Expand est concerne.
        // Plafond remonte a 500ms (etait 200 -- retour utilisateur : "200ms
        // pour l'attaque d'un expandeur c'est pas assez").
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idAttackExpand, band), 1 }, prefix + "Attack Expand",
            NormalisableRange<float> (0.05f, 500.0f, 0.001f, 0.2f), defaultAttackMs,
            AudioParameterFloatAttributes().withLabel ("ms")));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idReleaseExpand, band), 1 }, prefix + "Release Expand",
            NormalisableRange<float> (50.0f, 3000.0f, 0.1f, 0.3f), defaultReleaseMs,
            AudioParameterFloatAttributes().withLabel ("ms")));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idOutputExpand, band), 1 }, prefix + "Output Expand",
            NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idDetectorHp, band), 1 }, prefix + "Detector HP", false));
        // Plage elargie a 20-20000Hz (au lieu de 2000Hz max) : le graphe XY
        // (DetectorFilterGraph) zoome desormais sur la plage propre a
        // chaque bande (voir PluginEditor), qui peut monter bien au-dela
        // de 2000Hz pour MID/HIGH -- la bande HIGH utilise ce meme point
        // en mode LP (voir FeedbackCompressor::setDetectorHpIsLowPass()).
        // Defauts demandes : HP (Low/Mid) 50Hz, LP (High, voir
        // FeedbackCompressor::setDetectorHpIsLowPass()) 16000Hz, les deux a
        // 48dB/oct (cran de pente max, voir SidechainEQ::maxSlopeSteps).
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idDetectorHpFreq, band), 1 }, prefix + "Detector HP Frequency",
            NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f), band == High ? 16000.0f : 50.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));
        params.push_back (std::make_unique<AudioParameterInt> (
            ParameterID { bandParamId (idDetectorHpSlope, band), 1 }, prefix + "Detector HP Slope", 1, 8, 8));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idDetectorBell, band), 1 }, prefix + "Detector Bell", false));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idDetectorBellFreq, band), 1 }, prefix + "Detector Bell Frequency",
            NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f), 650.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idDetectorBellGain, band), 1 }, prefix + "Detector Bell Gain",
            NormalisableRange<float> (0.0f, 18.0f, 0.1f), 6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        // 2e point Bell -- remplace le bouton HP sur la bande MID dans
        // l'UI (voir doc idDetectorBell2 dans PluginProcessor.h). Existe
        // sur les 3 bandes (meme boucle), defaut a une frequence plus
        // aigue que le 1er Bell pour etre utile d'entree de jeu si active.
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idDetectorBell2, band), 1 }, prefix + "Detector Bell 2", false));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idDetectorBell2Freq, band), 1 }, prefix + "Detector Bell 2 Frequency",
            NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f), 3000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idDetectorBell2Gain, band), 1 }, prefix + "Detector Bell 2 Gain",
            NormalisableRange<float> (0.0f, 18.0f, 0.1f), 6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idDetectorLink, band), 1 }, prefix + "Detector Link", false));

        // Par bande -- chaque bande a son propre bouton d'ecoute du bus
        // sidechain externe (voir doc de classe PluginProcessor.h).
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idSidechainEnable, band), 1 }, prefix + "Sidechain", false));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idAudioHp, band), 1 }, prefix + "Audio HP", false));
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idAudioDist2, band), 1 }, prefix + "Audio Tube", false));
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idAudioDist3, band), 1 }, prefix + "Audio Fuzz", false));

        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idOptical, band), 1 }, prefix + "Optical", false));
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idBritish, band), 1 }, prefix + "Overkill", false));

        // Comp/Expand (demande explicite : "on va juste mettre expand et
        // comp. quand on est en mode comp c'est comme si on etait en both
        // ... le mode expand passe sur l'expander") -- 2 choix seulement :
        // "Comp" (index 0, defaut) mappe en interne sur
        // FeedbackCompressor::AboveBelowMode::Both (compression au-dessus
        // ET expansion en-dessous tournent en meme temps, sur les reglages
        // Comp), "Expand" (index 1) mappe sur AboveBelowMode::Below (voir
        // updateDspFromParams()). Plus d'etat "Comp solo" dedie : comme
        // idInputExpand (Range) est a 0dB par defaut, la contribution
        // d'expansion est mathematiquement nulle tant qu'on ne le monte
        // pas a la main -- Comp solo reste donc le comportement par
        // defaut, sans avoir besoin d'un 3e choix.
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (idAboveBelow, band), 1 }, prefix + "Comp/Expand",
            StringArray { "Comp", "Expand" }, 0));

        // Fader Mix -- 100% par defaut (bande entierement traitee, comportement inchange).
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idBandMix, band), 1 }, prefix + "Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        // Fader Width -- 100% par defaut (stereo d'origine, neutre).
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (idBandWidth, band), 1 }, prefix + "Width",
            NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    // --- Parametres globaux ---
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idPower, 1 }, "Power", true));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoftClip, 1 }, "Soft Clip", true));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idThdMode, 1 }, "THD Mode",
        StringArray { "Soft", "Tape", "Iron", "Air" }, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiMasteringTarget, 1 }, "AI Mastering Target",
        StringArray { "Off", "Apple Music", "YouTube", "Spotify", "Deezer", "SoundCloud", "TV", "CD", "Side Key", "Match" }, 0));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeCrossover, 1 }, "AI Scope: Crossover", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeDetectors, 1 }, "AI Scope: Detectors", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeCompression, 1 }, "AI Scope: Compression", true));

    // Chaque axe a maintenant un choix "Off" (index 0) -- demande explicite :
    // "on doit pouvoir agir que sur les points AI qui ne vont pas". Off ne
    // desactive QUE l'ecriture de sa propre cible (voir AiLearnAnalyzer::
    // Result::pushInput/pushRatio/pushAttackRelease), le reste de la portee
    // Compression (voir idAiScopeCompression) continue de fonctionner.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiCompressionMode, 1 }, "AI Compression Mode",
        StringArray { "Off", "Soft", "Medium", "Hard" }, 2));

    // Punchy par defaut a l'ouverture du plugin (demande explicite : "on
    // coche AI attaque punchy ... a l'etat de demarrage").
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiAttackReleaseCharacter, 1 }, "AI Attack/Release Character",
        StringArray { "Off", "Smooth", "Medium", "Punchy" }, 3));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiGainReductionMode, 1 }, "AI Gain Reduction",
        StringArray { "Off", "Smooth", "Mid", "Hard" }, 2));

    // Triade AI Expand (demande explicite : "pour l'ia de expand, il faut
    // des reglages comme pour l'ia comp") -- memes 3 axes/valeurs par
    // Off par defaut (contrairement a la triade compression ci-dessus,
    // Medium/Medium/Mid) -- IMPORTANT : des qu'UN SEUL de ces 3 axes n'est
    // pas sur Off, AI Learn bascule desormais AUTOMATIQUEMENT les 3 bandes
    // en EXPAND (voir AiLearnAnalyzer::run(), retour utilisateur : "je
    // veux pas avoir a activer les bandes expand pour les activer"). Si
    // ces params defautaient a un mode reel comme la compression, AI Learn
    // switcherait TOUJOURS les bandes en Expand (et donc desactiverait
    // silencieusement la compression au-dessus du seuil, Expand n'ayant
    // pas de branche Above) des le premier clic -- opt-in strict requis.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandMode, 1 }, "AI Expand Mode",
        StringArray { "Off", "Soft", "Medium", "Hard" }, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandCharacter, 1 }, "AI Expand Attack/Release Character",
        StringArray { "Off", "Smooth", "Medium", "Punchy" }, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandAmount, 1 }, "AI Expand Amount",
        StringArray { "Off", "Smooth", "Mid", "Hard" }, 0));

    // Style musical (menu Options, section AI, demande explicite : "je
    // souhaite ajouter plusieurs styles musicaux qui vont changer la
    // balance des outputs en fonction du style") -- offset statique par
    // bande, voir AiLearnAnalyzer::kStyleOutputBiasDb. "Off" par defaut.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiStyle, 1 }, "AI Style",
        StringArray { "Off", "Bass", "Loud", "Rock", "Pop", "Electronic" }, 0));

    // Fidelity (menu Options, section AI, demande explicite : "une fonction
    // qui reprend la balance bass mid aigue du mix pour la remettre pareil
    // apres compression") -- ON par defaut a l'ouverture du plugin (demande
    // explicite : "ai fidelity activé à l'etat de demarrage").
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiFidelity, 1 }, "AI Fidelity", true));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idCrossoverLow, 1 }, "Crossover Low",
        NormalisableRange<float> (20.0f, 400.0f, 0.1f, 0.4f), 121.5f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idCrossoverHigh, 1 }, "Crossover High",
        NormalisableRange<float> (300.0f, 6000.0f, 1.0f, 0.4f), 2521.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idDigitalCrossover, 1 }, "Linear Crossover", false));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idLinearLatencyMode, 1 }, "Linear Latency",
        StringArray { "Low", "Medium", "High" }, 0));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoloLow, 1 }, "Solo Low", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoloMid, 1 }, "Solo Mid", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoloHigh, 1 }, "Solo High", false));

    return { params.begin(), params.end() };
}

void SuppressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumInputChannels());

    crossovers.clear();
    crossovers.reserve ((size_t) numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        crossovers.push_back (std::make_unique<Crossover>());
    for (auto& xoPtr : crossovers)
        xoPtr->prepare (sampleRate, samplesPerBlock);

    for (int band = 0; band < (int) NumBands; ++band)
    {
        compressors[(size_t) band].resize ((size_t) numChannels);
        for (auto& c : compressors[(size_t) band])
            c.prepare (sampleRate);
    }

    clipperL.reset();
    clipperR.reset();

    lowBandBuffer.setSize (numChannels, samplesPerBlock);
    midBandBuffer.setSize (numChannels, samplesPerBlock);
    highBandBuffer.setSize (numChannels, samplesPerBlock);

    thdAnalysisCrossovers.clear();
    thdAnalysisCrossovers.reserve ((size_t) numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        thdAnalysisCrossovers.push_back (std::make_unique<Crossover>());
    for (auto& xoPtr : thdAnalysisCrossovers)
        xoPtr->prepare (sampleRate, samplesPerBlock);

    thdResidualBuffer.setSize (numChannels, samplesPerBlock);
    thdLowBuffer.setSize (numChannels, samplesPerBlock);
    thdMidBuffer.setSize (numChannels, samplesPerBlock);
    thdHighBuffer.setSize (numChannels, samplesPerBlock);

    lastReportedLatencySamples = -1; // force la premiere mise a jour dans processBlock()
}

void SuppressorAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SuppressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    const auto scSet = layouts.getChannelSet (true, 1);
    if (! scSet.isDisabled() && scSet != juce::AudioChannelSet::mono() && scSet != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

void SuppressorAudioProcessor::updateDspFromParams()
{
    const bool xoLinear = digitalCrossoverParam->load() > 0.5f;
    const float* inputTrim  = xoLinear ? kInputCalibrationTrimDbLinear  : kInputCalibrationTrimDbAnalog;
    const float* outputTrim = xoLinear ? kOutputCalibrationTrimDbLinear : kOutputCalibrationTrimDbAnalog;

    for (int band = 0; band < (int) NumBands; ++band)
    {
        const size_t b = (size_t) band;
        const bool isOpto = opticalParam[b]->load() > 0.5f;
        const bool isBritish = britishParam[b]->load() > 0.5f;

        // idAboveBelow n'expose plus que 2 choix cote UI -- Comp/Expand
        // (demande explicite : "on va juste mettre expand et comp ...
        // quand on est en mode comp c'est comme si on etait en both").
        // Mapping interne : TOUJOURS AboveBelowMode::Both, quel que soit
        // le choix Comp/Expand (retour utilisateur : "quand on appuie sur
        // expand, on doit voir et entendre le compresseur travailler ...
        // si on entend pas le compresseur, comment on travaille proprement
        // l'expandeur" -- l'ancien mapping Expand -> Below coupait purement
        // et simplement la reduction au-dessus du seuil des qu'on
        // selectionnait Expand, rendant le compresseur totalement muet/
        // invisible pendant qu'on regle l'expandeur. Desormais Comp/Expand
        // ne fait plus que choisir QUEL jeu de parametres (Ratio/Attack/
        // Release, partages par les deux branches Above et Below de cette
        // meme instance -- voir doc de FeedbackCompressor::AboveBelowMode)
        // pilote le tout ; les deux branches tournent TOUJOURS ensemble.
        const bool useExpandSettings = juce::roundToInt (aboveBelowParam[b]->load()) == 1;
        const auto aboveBelowMode = FeedbackCompressor::AboveBelowMode::Both;

        // Branche ABOVE (compression) : TOUJOURS les reglages Compression,
        // quel que soit le bouton Comp/Expand -- retour utilisateur
        // ("j'entends que l'expand" en mode Expand) : avant, la branche
        // Above utilisait le jeu de parametres Expand des qu'on bascule sur
        // Expand (ratio/attaque souvent tres doux, penses pour
        // l'expansion), rendant la compression quasi-inaudible. Desormais
        // seule la branche BELOW (expansion) bascule vers le jeu dedie.
        const int ratioIdx = juce::jlimit (0, 6, (int) std::round (ratioIndexParam[b]->load()));
        const float ratio = ratioTable[(size_t) ratioIdx];
        const float knee  = kneeTable[(size_t) ratioIdx];
        const float effAttackMs  = attackParam[b]->load();
        const float effReleaseMs = releaseParam[b]->load();
        const float effOutputDb  = outputParam[b]->load();

        // Branche BELOW (expansion) : Comp (mode Comp) mire les reglages
        // Compression (comportement identique a l'ancien "Both"), Expand
        // (mode Expand) bascule vers le jeu de parametres Expand dedie.
        const int belowRatioIdx = juce::jlimit (0, 6, (int) std::round (
            (useExpandSettings ? ratioIndexExpandParam[b] : ratioIndexParam[b])->load()));
        const float belowRatio = ratioTable[(size_t) belowRatioIdx];
        const float belowKnee  = kneeTable[(size_t) belowRatioIdx];
        const float belowAttackMs  = (useExpandSettings ? attackExpandParam[b]  : attackParam[b])->load();
        const float belowReleaseMs = (useExpandSettings ? releaseExpandParam[b] : releaseParam[b])->load();
        // Output : desormais UN SEUL knob partage entre Comp et Expand
        // (retour utilisateur : "j'ai l'impression que je clique sur comp
        // et expand" -- deux Output independants faisaient sauter le
        // niveau au clic entre les deux modes ; Above et Below tournant
        // maintenant TOUJOURS ensemble, un seul makeup gain a du sens).
        // idOutputExpand n'est plus lu ici (reste dans l'APVTS pour la
        // compat des anciens presets, mais n'a plus d'effet DSP).
        const float belowOutputDb  = outputParam[b]->load();

        // Le knob "Input" du jeu de parametres Expand est REAFFECTE en
        // Range (demande explicite : "juste pour la fonction expander il
        // passe en mode range") -- il ne pilote plus le pre-gain du
        // detecteur, seulement le plafond de boost (voir
        // FeedbackCompressor::setExpandRangeDb()). En Expand pur, le
        // pre-gain reste donc NEUTRE (0dB, seul le trim de calibration
        // s'applique) -- Comp garde son knob Input normal.
        //
        // Range TOUJOURS pris depuis le parametre Expand calibre, quel que
        // soit le mode (retour utilisateur : "on dirait que le compresseur
        // compresse l'expandeur" -- l'ancien Both utilisait un plafond fixe
        // en dur a 24dB, ignorant completement le Range calibre par
        // l'utilisateur pour Expand pur -- l'ecart entre les deux modes
        // donnait l'impression que Both ecrasait le cote expansion sous
        // une reduction bien plus genereuse/incontrolee que ce qui avait
        // ete calibre. Desormais le MEME Range s'applique partout, pour un
        // comportement coherent d'un mode a l'autre.
        //
        // Input TOUJOURS pris depuis le parametre Compression (idInput),
        // meme en Expand -- le compresseur tournant desormais EN
        // PERMANENCE (voir aboveBelowMode ci-dessus), son pre-gain ne doit
        // plus etre coupe des qu'on bascule sur Expand (l'ancien code le
        // mettait a 0dB, ce qui neutralisait discretement le reglage
        // d'entree du compresseur pendant qu'on regle l'expandeur).
        const float effInputDb  = inputParam[b]->load();
        const float effRangeDb  = inputExpandParam[b]->load();

        const bool detHp = detectorHpParam[b]->load() > 0.5f;
        const float detHpFreq = detectorHpFreqParam[b]->load();
        const int detHpSlope = (int) std::round (detectorHpSlopeParam[b]->load());
        const bool detBandEmphasis = detectorBellParam[b]->load() > 0.5f;
        const float detBellFreq = detectorBellFreqParam[b]->load();
        const float detBellGain = detectorBellGainParam[b]->load();
        const bool detBandEmphasis2 = detectorBell2Param[b]->load() > 0.5f;
        const float detBell2Freq = detectorBell2FreqParam[b]->load();
        const float detBell2Gain = detectorBell2GainParam[b]->load();

        const bool audHp = audioHpParam[b]->load() > 0.5f;
        const bool dist2On = audioDist2Param[b]->load() > 0.5f;
        const bool dist3On = audioDist3Param[b]->load() > 0.5f;

        float colourAmount = 0.0f;
        float colourBias = 0.0f;
        if (dist2On) { colourAmount += 25.0f; colourBias += -0.6f; }
        if (dist3On) { colourAmount += 55.0f; colourBias +=  0.6f; }
        if (dist2On && dist3On) colourBias *= 0.5f;
        colourAmount = juce::jmin (colourAmount, 80.0f);

        for (auto& comp : compressors[b])
        {
            comp.setInputDriveDb (effInputDb + inputTrim[b]);
            comp.setRatio (ratio);
            comp.setKneeDb (knee);
            comp.setAttackMs (effAttackMs);
            comp.setReleaseMs (effReleaseMs);
            comp.setBelowRatio (belowRatio);
            comp.setBelowKneeDb (belowKnee);
            comp.setBelowAttackMs (belowAttackMs);
            comp.setBelowReleaseMs (belowReleaseMs);
            // Output : le knob actuellement affiche/edite (Comp ou Expand
            // selon le mode) pilote le makeup gain applique au signal
            // combine (les deux branches Above/Below tournant ensemble).
            comp.setOutputDb (belowOutputDb + outputTrim[b]);
            comp.setOptoCharacter (isOpto);
            comp.setBritishMode (isBritish);
            comp.setAboveBelowMode (aboveBelowMode);
            comp.setExpandRangeDb (effRangeDb);
            comp.setDetectorHpIsLowPass (band == High); // bande HIGH : HP -> LP (voir doc de classe)
            comp.setDetectorHpEngaged (detHp);
            comp.setDetectorHpFrequency (detHpFreq);
            comp.setDetectorHpSlopeSteps (detHpSlope);
            comp.setDetectorBandEmphasis (detBandEmphasis);
            comp.setDetectorBellFrequency (detBellFreq);
            comp.setDetectorBellGainDb (detBellGain);
            comp.setDetectorBandEmphasis2 (detBandEmphasis2);
            comp.setDetectorBell2Frequency (detBell2Freq);
            comp.setDetectorBell2GainDb (detBell2Gain);
            comp.setAudioHpEngaged (audHp);
            comp.setColourAmount (colourAmount);
            comp.setColourHarmonicBias (colourBias);
        }
    }
}

void SuppressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    lastProcessBlockMs.store (juce::Time::getMillisecondCounter());

    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const bool powerOn = powerParam->load() > 0.5f;

    auto* dataL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* dataR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Bouton AI : capture le programme SEC (avant tout traitement) pendant
    // la fenetre d'ecoute -- voir AiLearnAnalyzer.h. Ne fait rien tant que
    // l'etat n'est pas Capturing (verifie en interne), donc cout nul le
    // reste du temps.
    if (dataL != nullptr)
        aiLearnAnalyzer.pushSamples (dataL, dataR, numSamples);

    // Bus sidechain externe -- voir constructeur. Le bouton SC de CHAQUE
    // bande decide independamment si celle-ci ecoute ce signal.
    auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    const bool sidechainBusActive = sidechainBuffer.getNumChannels() > 0;
    const float* scDataL = sidechainBusActive ? sidechainBuffer.getReadPointer (0) : nullptr;
    const float* scDataR = sidechainBusActive && sidechainBuffer.getNumChannels() > 1
                                ? sidechainBuffer.getReadPointer (1) : scDataL;

    if (! powerOn)
    {
        for (int band = 0; band < (int) NumBands; ++band)
        {
            for (auto& c : compressors[(size_t) band])
                c.reset();
            grMeterDb[(size_t) band].store (0.0f);
            grMeterExpandDb[(size_t) band].store (0.0f);
        }
        grMeterDbL.store (0.0f); grMeterDbR.store (0.0f);
        clipperL.reset(); clipperR.reset();
        for (auto& xoPtr : crossovers)
            xoPtr->reset();
        inputLevelDbL.store (-100.0f);  inputLevelDbR.store (-100.0f);
        outputLevelDbL.store (-100.0f); outputLevelDbR.store (-100.0f);
        clipMeterAmount.store (0.0f);
        clipMeterAmountL.store (0.0f); clipMeterAmountR.store (0.0f);
        clipMeterAmountLow.store (0.0f); clipMeterAmountMid.store (0.0f); clipMeterAmountHigh.store (0.0f);
        return;
    }

    updateDspFromParams();

    const float xoLowHz  = crossoverLowParam->load();
    const float xoHighHz = crossoverHighParam->load();
    const bool  xoLinear = digitalCrossoverParam->load() > 0.5f;
    const int latencyModeIndex = juce::jlimit (0, 2, (int) std::round (linearLatencyModeParam->load()));
    const auto latencyMode = (Crossover::LatencyMode) latencyModeIndex;

    const bool softClipOn = softClipParam->load() > 0.5f;
    clipperL.setHardLimitEnabled (softClipOn);
    clipperR.setHardLimitEnabled (softClipOn);

    const auto satMode = (SoftClipper::SatMode) juce::jlimit (0, 3, (int) std::round (thdModeParam->load()));
    clipperL.setMode (satMode);
    clipperR.setMode (satMode);

    const bool bypassOn = bypassHeld.load();

    // Solo par bande : des qu'une bande est soloee, les 2 autres sont
    // exclues du mix (pas seulement attenuees).
    const bool soloLow  = soloLowParam->load()  > 0.5f;
    const bool soloMid  = soloMidParam->load()  > 0.5f;
    const bool soloHigh = soloHighParam->load() > 0.5f;
    const bool anySolo  = soloLow || soloMid || soloHigh;
    const bool playBand[(size_t) NumBands] = { ! anySolo || soloLow, ! anySolo || soloMid, ! anySolo || soloHigh };

    bool bandSidechainOn[(size_t) NumBands];
    bool bandDetectorLink[(size_t) NumBands];
    float bandMixLin[(size_t) NumBands];    // fader Mix, 0..1 (voir idBandMix)
    float bandWidthLin[(size_t) NumBands];  // fader Width, 0..2, 1=neutre (voir idBandWidth)
    float bandOutGainLin[(size_t) NumBands]; // meme gain de sortie que celui applique par le compresseur, pour le dry de reference du fader Mix
    for (int band = 0; band < (int) NumBands; ++band)
    {
        bandSidechainOn[(size_t) band]  = sidechainEnableParam[(size_t) band]->load() > 0.5f;
        bandDetectorLink[(size_t) band] = dataR != nullptr && detectorLinkParam[(size_t) band]->load() > 0.5f;
        bandMixLin[(size_t) band]       = juce::jlimit (0.0f, 1.0f, bandMixParam[(size_t) band]->load() * 0.01f);
        bandWidthLin[(size_t) band]     = juce::jlimit (0.0f, 2.0f, bandWidthParam[(size_t) band]->load() * 0.01f);
        bandOutGainLin[(size_t) band]   = juce::Decibels::decibelsToGain (outputParam[(size_t) band]->load()
                                              + (xoLinear ? kOutputCalibrationTrimDbLinear[(size_t) band] : kOutputCalibrationTrimDbAnalog[(size_t) band]));
    }

    // --- Split Low/Mid/High, une passe par canal sur tout le bloc --
    // obligatoire pour le mode LINEAR (convolution FFT bloc entier). ---
    for (int ch = 0; ch < numChannels && ch < (int) crossovers.size(); ++ch)
    {
        auto& xo = *crossovers[(size_t) ch];
        xo.setLowCrossoverHz (xoLowHz);
        xo.setHighCrossoverHz (xoHighHz);
        xo.setLinearPhaseMode (xoLinear);
        xo.setLatencyMode (latencyMode);

        xo.processBlock (buffer.getReadPointer (ch),
                          lowBandBuffer.getWritePointer (ch),
                          midBandBuffer.getWritePointer (ch),
                          highBandBuffer.getWritePointer (ch),
                          numSamples);
    }

    const float* lowL  = lowBandBuffer.getReadPointer (0);
    const float* midL  = midBandBuffer.getReadPointer (0);
    const float* highL = highBandBuffer.getReadPointer (0);
    const float* lowR  = dataR != nullptr ? lowBandBuffer.getReadPointer (1)  : nullptr;
    const float* midR  = dataR != nullptr ? midBandBuffer.getReadPointer (1)  : nullptr;
    const float* highR = dataR != nullptr ? highBandBuffer.getReadPointer (1) : nullptr;

    FeedbackCompressor* compL[(size_t) NumBands];
    FeedbackCompressor* compR[(size_t) NumBands];
    for (int band = 0; band < (int) NumBands; ++band)
    {
        compL[(size_t) band] = &compressors[(size_t) band][0];
        compR[(size_t) band] = dataR != nullptr && compressors[(size_t) band].size() > 1
                                    ? &compressors[(size_t) band][1] : nullptr;
    }

    float peakInL = 0.0f, peakInR = 0.0f, peakOutL = 0.0f, peakOutR = 0.0f;

    // Residu de distorsion (avant clip - apres clip) -- rempli ci-dessous,
    // analyse par bande APRES la boucle (voir plus bas).
    auto* residL = thdResidualBuffer.getWritePointer (0);
    auto* residR = dataR != nullptr && thdResidualBuffer.getNumChannels() > 1
                        ? thdResidualBuffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = dataL != nullptr ? dataL[i] : 0.0f;
        const float dryR = dataR != nullptr ? dataR[i] : 0.0f;

        peakInL = juce::jmax (peakInL, std::abs (dryL));
        peakInR = juce::jmax (peakInR, std::abs (dryR));

        const float bandSampleL[(size_t) NumBands] = { lowL[i], midL[i], highL[i] };
        const float bandSampleR[(size_t) NumBands] = { dataR != nullptr ? lowR[i]  : 0.0f,
                                                         dataR != nullptr ? midR[i]  : 0.0f,
                                                         dataR != nullptr ? highR[i] : 0.0f };

        float sumL = 0.0f, sumR = 0.0f;

        for (int band = 0; band < (int) NumBands; ++band)
        {
            // Bypass momentane : le signal ne traverse plus les
            // compresseurs (sortie = somme des bandes SECHES, voir plus
            // bas), mais l'aiguille/LED de GR de CHAQUE bande doit retomber
            // a 0 en suivant son propre temps de Release -- pas de saut
            // net. Le bypass HERITE du Solo : si une bande est soloee, le
            // bypass ne fait entendre QUE cette bande (a l'etat sec), pas
            // le signal d'entree complet -- "une fonction bypass qui prend
            // la fonction solo", comme demande.
            if (bypassOn)
            {
                compL[(size_t) band]->relaxGainReductionToZero();
                if (compR[(size_t) band] != nullptr)
                    compR[(size_t) band]->relaxGainReductionToZero();

                if (playBand[(size_t) band])
                {
                    if (compR[(size_t) band] != nullptr)
                    {
                        sumL += bandSampleL[(size_t) band];
                        sumR += bandSampleR[(size_t) band];
                    }
                    else
                    {
                        sumL += bandSampleL[(size_t) band];
                    }
                }
                continue;
            }

            const bool scOn = bandSidechainOn[(size_t) band] && sidechainBusActive;
            const float scL = scOn ? scDataL[i] : 0.0f;
            const float scR = scOn ? (scDataR != nullptr ? scDataR[i] : scL) : 0.0f;

            const float yL = compL[(size_t) band]->processSample (bandSampleL[(size_t) band], scOn ? &scL : nullptr);
            const float yR = compR[(size_t) band] != nullptr
                                  ? compR[(size_t) band]->processSample (bandSampleR[(size_t) band], scOn ? &scR : nullptr)
                                  : 0.0f;

            if (bandDetectorLink[(size_t) band] && compR[(size_t) band] != nullptr)
            {
                const float lvlL = compL[(size_t) band]->getFeedbackTapLevel();
                const float lvlR = compR[(size_t) band]->getFeedbackTapLevel();
                const float combined = 0.5f * (lvlL + lvlR);
                compL[(size_t) band]->setFeedbackTapLevel (combined);
                compR[(size_t) band]->setFeedbackTapLevel (combined);
            }

            // Fader Mix : blende, POUR CETTE BANDE UNIQUEMENT, le signal
            // compresse (wet) et son dry de reference (post Output gain,
            // sans compression -- meme reference que dryBand ci-dessous).
            const float dryBandL = bandSampleL[(size_t) band] * bandOutGainLin[(size_t) band];
            const float dryBandR = bandSampleR[(size_t) band] * bandOutGainLin[(size_t) band];
            const float wetL = dryBandL + (yL - dryBandL) * bandMixLin[(size_t) band];
            const float wetR = dryBandR + (yR - dryBandR) * bandMixLin[(size_t) band];

            // Fader Width : encodage Mid/Side sur le signal WET de la bande
            // (apres Mix), applique uniquement si le canal R existe --
            // sans effet audible sur un bus mono (Side toujours nul).
            // Bande non-soloee (playBand false) : exclue du mix, comme
            // avant -- mais le compresseur continue de tourner ci-dessus
            // (GR meter/detecteur restent vivants meme muets).
            if (! playBand[(size_t) band])
                continue;

            if (compR[(size_t) band] != nullptr)
            {
                const float mid  = (wetL + wetR) * 0.5f;
                const float side = (wetL - wetR) * 0.5f * bandWidthLin[(size_t) band];
                sumL += mid + side;
                sumR += mid - side;
            }
            else
            {
                sumL += wetL;
            }
        }

        // Bypass momentane (bouton maintenu, voir bypassHeld) : sumL/sumR
        // contiennent deja la somme des bandes SECHES (remplies ci-dessus
        // en respectant le Solo -- voir boucle par bande) au lieu du
        // signal compresse -- le clipper/THD est aussi court-circuite,
        // comme un vrai bypass hardware.
        const bool applyClip = softClipOn && ! bypassOn;
        if (dataL != nullptr)
        {
            dataL[i] = applyClip ? clipperL.processSample (sumL) : sumL;
            residL[i] = sumL - dataL[i]; // 0 si pas de clip (applyClip false) ou en bypass
        }
        if (dataR != nullptr)
        {
            dataR[i] = applyClip ? clipperR.processSample (sumR) : sumR;
            if (residR != nullptr)
                residR[i] = sumR - dataR[i];
        }

        peakOutL = juce::jmax (peakOutL, dataL != nullptr ? std::abs (dataL[i]) : 0.0f);
        peakOutR = juce::jmax (peakOutR, dataR != nullptr ? std::abs (dataR[i]) : 0.0f);
    }

    // --- Analyse THD par bande : le residu de distorsion (rempli ci-dessus)
    // est passe dans le crossover d'analyse dedie (memes frequences que le
    // crossover principal) pour savoir ou tombent les harmoniques generees
    // par l'etage de saturation UNIQUE -- n'affecte jamais le signal audio
    // reellement envoye en sortie. Recalcule a CHAQUE bloc (la decimation
    // testee ici a ete revertie -- la difference etait visible/genante sur
    // les aiguilles de saturation). ---
    if (softClipOn)
    {
        for (int ch = 0; ch < numChannels && ch < (int) thdAnalysisCrossovers.size(); ++ch)
        {
            auto& xo = *thdAnalysisCrossovers[(size_t) ch];
            xo.setLowCrossoverHz (xoLowHz);
            xo.setHighCrossoverHz (xoHighHz);
            xo.setLinearPhaseMode (xoLinear);
            xo.setLatencyMode (latencyMode);
            xo.processBlock (thdResidualBuffer.getReadPointer (ch),
                              thdLowBuffer.getWritePointer (ch),
                              thdMidBuffer.getWritePointer (ch),
                              thdHighBuffer.getWritePointer (ch),
                              numSamples);
        }

        float peakResidLow = 0.0f, peakResidMid = 0.0f, peakResidHigh = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* lo = thdLowBuffer.getReadPointer (ch);
            const float* mi = thdMidBuffer.getReadPointer (ch);
            const float* hi = thdHighBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                peakResidLow  = juce::jmax (peakResidLow,  std::abs (lo[i]));
                peakResidMid  = juce::jmax (peakResidMid,  std::abs (mi[i]));
                peakResidHigh = juce::jmax (peakResidHigh, std::abs (hi[i]));
            }
        }

        // Meme ballistique que SoftClipper::getClipAmount() (attaque
        // instantanee, retombee douce), mais appliquee par bloc -- decay
        // compose sur numSamples pour equivaloir au *0.995 par-echantillon
        // d'origine.
        const float blockDecay = std::pow (0.995f, (float) numSamples);
        auto updateBandAmount = [blockDecay] (std::atomic<float>& amt, float peak)
        {
            const float target = juce::jlimit (0.0f, 1.0f, peak * 2.5f);
            const float current = amt.load();
            amt.store (target > current ? target : current * blockDecay);
        };
        updateBandAmount (clipMeterAmountLow,  peakResidLow);
        updateBandAmount (clipMeterAmountMid,  peakResidMid);
        updateBandAmount (clipMeterAmountHigh, peakResidHigh);
    }
    else
    {
        // THD desengage -- retombee douce vers 0 (meme ballistique), pas
        // un saut net.
        const float blockDecay = std::pow (0.995f, (float) numSamples);
        clipMeterAmountLow.store  (clipMeterAmountLow.load()  * blockDecay);
        clipMeterAmountMid.store  (clipMeterAmountMid.load()  * blockDecay);
        clipMeterAmountHigh.store (clipMeterAmountHigh.load() * blockDecay);
    }

    float overallGrL = 0.0f, overallGrR = 0.0f;
    for (int band = 0; band < (int) NumBands; ++band)
    {
        // Enveloppe SIGNEE (positive = reduction, negative = boost Expand,
        // voir FeedbackCompressor::processSample()) -- separee en 2 meters
        // independants plutot qu'un abs() unique, pour pouvoir afficher
        // reduction et boost sur 2 rangees de diodes distinctes (demande
        // explicite : "une autre bande de diode qui affiche l'expander en
        // temps reel").
        const float envL = compL[(size_t) band]->getGainReductionDb();
        const float envR = compR[(size_t) band] != nullptr ? compR[(size_t) band]->getGainReductionDb() : 0.0f;
        const float grDbL = std::abs (envL);
        const float grDbR = std::abs (envR);
        grMeterDb[(size_t) band].store (juce::jmax (juce::jmax (envL, 0.0f), juce::jmax (envR, 0.0f)));
        grMeterExpandDb[(size_t) band].store (juce::jmax (juce::jmax (-envL, 0.0f), juce::jmax (-envR, 0.0f)));
        overallGrL = juce::jmax (overallGrL, grDbL);
        overallGrR = juce::jmax (overallGrR, grDbR);
    }
    // GR "d'ensemble" par canal (max des 3 bandes) -- pour le NeedleVU
    // global de la plaque du haut, voir doc de classe (PluginProcessor.h).
    grMeterDbL.store (overallGrL);
    grMeterDbR.store (overallGrR);

    inputLevelDbL.store  (juce::Decibels::gainToDecibels (peakInL, -100.0f));
    inputLevelDbR.store  (juce::Decibels::gainToDecibels (peakInR, -100.0f));
    outputLevelDbL.store (juce::Decibels::gainToDecibels (peakOutL, -100.0f));
    outputLevelDbR.store (juce::Decibels::gainToDecibels (peakOutR, -100.0f));

    const float clipAmountL = softClipOn ? clipperL.getClipAmount() : 0.0f;
    const float clipAmountR = softClipOn ? clipperR.getClipAmount() : 0.0f;
    clipMeterAmountL.store (clipAmountL);
    clipMeterAmountR.store (clipAmountR);
    clipMeterAmount.store (juce::jmax (clipAmountL, clipAmountR));

    // Latence identique sur tous les canaux (memes reglages pousses a
    // chaque Crossover ci-dessus) -- ne rappeler setLatencySamples() que
    // si elle a reellement change.
    if (! crossovers.empty())
    {
        const int currentLatency = crossovers.front()->getLatencySamples();
        if (currentLatency != lastReportedLatencySamples)
        {
            lastReportedLatencySamples = currentLatency;
            setLatencySamples (currentLatency);
        }
    }
}

juce::AudioProcessorEditor* SuppressorAudioProcessor::createEditor()
{
    return new SuppressorAudioProcessorEditor (*this);
}

// Tous les parametres de compression des 3 bandes (+ faders Mix/Width) + les
// 2 crossovers + le THD Valve global -- des reglages de SON a comparer.
// Power, le mode Wide/Linear, le budget de latence et les Solo restent
// exclus (etats globaux/utilitaires, pas des reglages de son), meme
// raisonnement que Nuke Mext.
static const std::vector<juce::String>& getABParamIds()
{
    static const std::vector<juce::String> ids = [] {
        std::vector<juce::String> v;
        static constexpr const char* bandBaseIds[] =
        {
            SuppressorAudioProcessor::idInput, SuppressorAudioProcessor::idRatioIndex,
            SuppressorAudioProcessor::idAttack, SuppressorAudioProcessor::idRelease,
            SuppressorAudioProcessor::idOutput,
            SuppressorAudioProcessor::idInputExpand, SuppressorAudioProcessor::idRatioIndexExpand,
            SuppressorAudioProcessor::idAttackExpand, SuppressorAudioProcessor::idReleaseExpand,
            SuppressorAudioProcessor::idOutputExpand,
            SuppressorAudioProcessor::idDetectorHp, SuppressorAudioProcessor::idDetectorHpFreq,
            SuppressorAudioProcessor::idDetectorHpSlope, SuppressorAudioProcessor::idDetectorBell,
            SuppressorAudioProcessor::idDetectorBellFreq, SuppressorAudioProcessor::idDetectorBellGain,
            SuppressorAudioProcessor::idDetectorBell2, SuppressorAudioProcessor::idDetectorBell2Freq,
            SuppressorAudioProcessor::idDetectorBell2Gain,
            SuppressorAudioProcessor::idDetectorLink, SuppressorAudioProcessor::idSidechainEnable,
            SuppressorAudioProcessor::idAudioHp, SuppressorAudioProcessor::idAudioDist2,
            SuppressorAudioProcessor::idAudioDist3, SuppressorAudioProcessor::idOptical,
            SuppressorAudioProcessor::idBritish, SuppressorAudioProcessor::idAboveBelow,
            SuppressorAudioProcessor::idBandMix, SuppressorAudioProcessor::idBandWidth,
        };
        for (int band = 0; band < (int) SuppressorAudioProcessor::NumBands; ++band)
            for (auto* base : bandBaseIds)
                v.push_back (SuppressorAudioProcessor::bandParamId (base, band));

        v.push_back (SuppressorAudioProcessor::idCrossoverLow);
        v.push_back (SuppressorAudioProcessor::idCrossoverHigh);
        v.push_back (SuppressorAudioProcessor::idSoftClip);
        v.push_back (SuppressorAudioProcessor::idThdMode);
        // Analog/Linear -- INCLUS (chaque slot A/B memorise et restaure
        // son propre mode de crossover, sur demande explicite -- pas
        // exclu comme Power/Solo, contrairement au comportement par
        // defaut copie de Nuke Mext).
        v.push_back (SuppressorAudioProcessor::idDigitalCrossover);
        return v;
    }();
    return ids;
}

void SuppressorAudioProcessor::toggleABCompare()
{
    auto& activeSlot = usingSlotB ? abSlotB : abSlotA;

    for (auto& id : getABParamIds())
        if (auto* p = apvts.getParameter (id))
            activeSlot[id] = p->getValue();

    usingSlotB = ! usingSlotB;
    auto& targetSlot = usingSlotB ? abSlotB : abSlotA;

    if (targetSlot.empty())
        targetSlot = activeSlot;

    for (auto& [id, value] : targetSlot)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (value);
}

void SuppressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("needleModeIndex1", needleModeIndex1, nullptr);
    apvts.state.setProperty ("needleModeIndex2", needleModeIndex2, nullptr);

    auto state = apvts.copyState();

    auto& activeSlot = usingSlotB ? abSlotB : abSlotA;
    for (auto& id : getABParamIds())
        if (auto* p = apvts.getParameter (id))
            activeSlot[id] = p->getValue();

    juce::ValueTree abTree ("AB_COMPARE");
    abTree.setProperty ("usingSlotB", usingSlotB, nullptr);
    for (auto& [id, value] : abSlotA) abTree.setProperty ("A_" + id, value, nullptr);
    for (auto& [id, value] : abSlotB) abTree.setProperty ("B_" + id, value, nullptr);
    state.appendChild (abTree, nullptr);

    juce::MemoryOutputStream stream (destData, true);
    state.writeToStream (stream);
}

void SuppressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! tree.isValid())
        return;

    abSlotA.clear();
    abSlotB.clear();
    usingSlotB = false;

    if (auto abTree = tree.getChildWithName ("AB_COMPARE"); abTree.isValid())
    {
        usingSlotB = (bool) abTree.getProperty ("usingSlotB", false);

        for (auto& id : getABParamIds())
        {
            if (abTree.hasProperty ("A_" + id))
                abSlotA[id] = (float) abTree.getProperty ("A_" + id);
            if (abTree.hasProperty ("B_" + id))
                abSlotB[id] = (float) abTree.getProperty ("B_" + id);
        }

        tree.removeChild (abTree, nullptr);
    }

    apvts.replaceState (tree);
    needleModeIndex1 = apvts.state.getProperty ("needleModeIndex1", 1);
    needleModeIndex2 = apvts.state.getProperty ("needleModeIndex2", 1);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuppressorAudioProcessor();
}
