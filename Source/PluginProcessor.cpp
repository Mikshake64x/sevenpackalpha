#include "PluginProcessor.h"
#include "PluginEditor.h"

constexpr float SixpackAudioProcessor::ratioTable[7];
constexpr float SixpackAudioProcessor::kneeTable[7];
constexpr const char* SixpackAudioProcessor::idCrossover[NumCrossovers];
constexpr const char* SixpackAudioProcessor::idCrossoverEnabled[NumCrossovers];
constexpr const char* SixpackAudioProcessor::idSolo[NumBands];

// Correction de calibration fixe PAR BANDE, mesuree via PluginDoctor
// (Linear Analysis, Release au plus court pour eviter tout artefact
// d'enveloppe) pour obtenir une reponse parfaitement plate -- SEPAREE par
// mode de crossover (ANALOG/WIDE vs LINEAR), la reconstruction n'etant pas
// la meme (filtres differents). Appliquee ICI, EN PLUS de ce que
// l'utilisateur regle au knob -- jamais visible sur l'affichage (les
// knobs Input/Output restent a 0.00 dB par defaut), comme un trim d'usine
// cache sur du materiel calibre. Meme principe que Nuke Mext.
//
// IMPORTANT (generalisation 5 bandes) : les valeurs mesurees historiques
// (Tripressor, 3 bandes/2 crossovers) ne s'appliquent PAS a la nouvelle
// topologie a 4 crossovers -- une mesure PluginDoctor n'a de sens que pour
// la geometrie de filtres EXACTE qui produit la derive. Plutot que de
// reconduire par erreur des chiffres qui ne correspondent a rien ici, les
// 5 trims sont remis A PLAT (0.0) en attendant une nouvelle campagne de
// mesure sur le crossover 4 points -- a refaire avant une sortie publique
// de sixpack, voir note de suivi laissee au demandeur.
static constexpr float kInputCalibrationTrimDbAnalog[(size_t) SixpackAudioProcessor::NumBands]  = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static constexpr float kOutputCalibrationTrimDbAnalog[(size_t) SixpackAudioProcessor::NumBands] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static constexpr float kInputCalibrationTrimDbLinear[(size_t) SixpackAudioProcessor::NumBands]  = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static constexpr float kOutputCalibrationTrimDbLinear[(size_t) SixpackAudioProcessor::NumBands] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

juce::String SixpackAudioProcessor::bandParamId (const char* baseId, int band)
{
    if (band == Mid)
        return juce::String (baseId); // compat presets/sessions Suppressor mono-bande
    static const char* suffix[SixpackAudioProcessor::NumBands] = { "_low", "_lowmid", "", "_highmid", "_presence", "_high", "_air" };
    return juce::String (baseId) + suffix[band];
}

SixpackAudioProcessor::SixpackAudioProcessor()
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
                         // 7 bus de sortie stereo par bande -- DESACTIVES par
                         // defaut (aucun impact sur un insert stereo normal).
                         // En VST3 l'hote active ceux qu'il veut ; en
                         // standalone ils sont forces actifs au demarrage
                         // (voir plus bas) pour alimenter une carte multi-out.
                         // Ordre = enum Band : bus 1 = Low ... bus 7 = Air.
                         .withOutput ("Low",      juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Low Mid",  juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Mid",      juce::AudioChannelSet::stereo(), false)
                         .withOutput ("High Mid", juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Presence", juce::AudioChannelSet::stereo(), false)
                         .withOutput ("High",     juce::AudioChannelSet::stereo(), false)
                         .withOutput ("Air",      juce::AudioChannelSet::stereo(), false)
                       #endif
                         )
{
    for (auto& v : bandOutputLevelDb)
        v.store (-100.0f);

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
        bandMixParam[b]          = apvts.getRawParameterValue (bandParamId (idBandMix, band));
        bandWidthParam[b]        = apvts.getRawParameterValue (bandParamId (idBandWidth, band));
        bandRouteOutParam[b]     = apvts.getRawParameterValue (bandParamId (idBandRouteOut, band));
        bandOutPairIndex[b].store (band + 1); // defaut : Low->3-4, Low-Mid->5-6, ... Air->15-16
    }

    // Standalone : les 7 bus de sortie par bande sont forces actifs des la
    // construction, pour que getTotalNumOutputChannels() == 16 et que le
    // wrapper standalone (voir StandaloneApp.cpp, channelConfig {2,16})
    // ouvre la carte son avec 16 sorties -- mix sur 1-2, Low sur 3-4, ...
    // Air sur 15-16. En VST3/AU/Unity on ne touche a rien : l'hote garde
    // la main sur l'activation des bus (desactives par defaut).
    if (wrapperType == wrapperType_Standalone)
    {
        auto layout = getBusesLayout();
        for (int busIdx = 1; busIdx <= (int) NumBands; ++busIdx)
            if (busIdx < layout.outputBuses.size())
                layout.outputBuses.getReference (busIdx) = juce::AudioChannelSet::stereo();

        setBusesLayout (layout); // no-op si refuse -- les bus restent inactifs
    }

    powerParam            = apvts.getRawParameterValue (idPower);
    softClipParam         = apvts.getRawParameterValue (idSoftClip);
    thdModeParam          = apvts.getRawParameterValue (idThdMode);
    thdOutputParam        = apvts.getRawParameterValue (idThdOutput);
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
    for (int x = 0; x < NumCrossovers; ++x)
    {
        crossoverFreqParam[(size_t) x]    = apvts.getRawParameterValue (idCrossover[x]);
        crossoverEnabledParam[(size_t) x] = apvts.getRawParameterValue (idCrossoverEnabled[x]);
    }
    digitalCrossoverParam = apvts.getRawParameterValue (idDigitalCrossover);
    linearLatencyModeParam = apvts.getRawParameterValue (idLinearLatencyMode);
    for (int band = 0; band < (int) NumBands; ++band)
        soloParam[(size_t) band] = apvts.getRawParameterValue (idSolo[band]);

    startTimerHz (10);
}

SixpackAudioProcessor::~SixpackAudioProcessor()
{
    stopTimer();
}

void SixpackAudioProcessor::timerCallback()
{
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const juce::uint32 last = lastProcessBlockMs.load();

    if (last == 0 || now - last > 250)
    {
        for (int band = 0; band < (int) NumBands; ++band)
        {
            grMeterDb[(size_t) band].store (0.0f);
            grMeterExpandDb[(size_t) band].store (0.0f);
            clipMeterAmountBand[(size_t) band].store (0.0f);
        }
        grMeterDbL.store (0.0f);
        grMeterDbR.store (0.0f);

        // Meme bug que grMeterDb ci-dessus, meme fix (retour utilisateur :
        // "dans le vu metre THD quand je coupe le son, il reste une led
        // d'allumee continuelement") -- quand l'hote arrete d'appeler
        // processBlock() (transport stoppe, piste desactivee...), la
        // ballistique de clipMeterAmount (decroissance *0.995/echantillon,
        // voir SoftClipper::processSample()) se fige a sa derniere valeur
        // au lieu de continuer a decroitre vers 0, laissant la premiere
        // diode de ThdHeaderModule allumee indefiniment.
        clipMeterAmount.store (0.0f);
        clipMeterAmountL.store (0.0f);
        clipMeterAmountR.store (0.0f);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SixpackAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    static const char* bandNamePrefix[(size_t) NumBands] = { "Low ", "Low-Mid ", "", "High-Mid ", "Presence ", "High ", "Air " };

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
            NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f), band == NumBands - 1 ? 16000.0f : 50.0f,
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

        // Routage sur sa propre sortie (voir idBandRouteOut) -- off par
        // defaut : comportement inchange, la bande va dans le mix principal.
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (idBandRouteOut, band), 1 }, prefix + "Separate Out", false));
    }

    // --- Parametres globaux ---
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idPower, 1 }, "Power", true));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoftClip, 1 }, "Soft Clip", true));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idThdMode, 1 }, "THD Mode",
        StringArray { "Soft", "Tape", "Iron", "Air" }, 0));

    // Plafond de sortie du THD Valve -- -0.5 dB par defaut (ancienne valeur
    // fixe), abaissable jusqu'a -3 dB depuis la barre du bas. Pas de skew :
    // plage courte, lecture directe en dB.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idThdOutput, 1 }, "THD Output Ceiling",
        NormalisableRange<float> (-3.0f, -0.5f, 0.1f), -0.5f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Defaut "Match" (dernier choix, index 9) au lieu de "Off" (retour
    // utilisateur : screenshots des options a l'ouverture montrant MATCH
    // coche par defaut, avec les 3 autres reglages AI Expand ci-dessous).
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiMasteringTarget, 1 }, "AI Mastering Target",
        StringArray { "Off", "Apple Music", "YouTube", "Spotify", "Deezer", "SoundCloud", "TV", "CD", "Side Key", "Match" }, 9));

    // Coche par defaut (retour utilisateur : "il faut qu'il soit active
    // dans AI scope [crossover]") -- coherent avec Compression, deja ON
    // par defaut ci-dessous.
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeCrossover, 1 }, "AI Scope: Crossover", true));
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
    // des reglages comme pour l'ia comp"). Defauts REVUS (retour
    // utilisateur : screenshots des options a l'ouverture montrant
    // Hard/Smooth/Hard coches par defaut) -- Hard/Smooth/Hard au lieu de
    // Off/Off/Off. ATTENTION, consequence assumee (confirmee explicitement
    // par l'utilisateur) : des qu'UN SEUL de ces 3 axes n'est pas sur Off,
    // AI Learn bascule AUTOMATIQUEMENT les 3 bandes en EXPAND (voir
    // AiLearnAnalyzer::run()), ce qui desactive silencieusement la
    // compression au-dessus du seuil (Expand n'a pas de branche Above) des
    // le tout premier clic sur Learn -- avant cette revision, ces 3 axes
    // defautaient a Off precisement pour eviter ca (opt-in strict), mais
    // l'utilisateur a confirme vouloir ce basculement automatique.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandMode, 1 }, "AI Expand Mode",
        StringArray { "Off", "Soft", "Medium", "Hard" }, 3));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandCharacter, 1 }, "AI Expand Attack/Release Character",
        StringArray { "Off", "Smooth", "Medium", "Punchy" }, 1));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiExpandAmount, 1 }, "AI Expand Amount",
        StringArray { "Off", "Smooth", "Mid", "Hard" }, 3));

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

    static const char* crossoverName[NumCrossovers] = { "Crossover A", "Crossover B", "Crossover C", "Crossover D", "Crossover E", "Crossover F" };
    for (int x = 0; x < NumCrossovers; ++x)
    {
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { idCrossover[x], 1 }, crossoverName[x],
            NormalisableRange<float> (Crossover::kSplitMinHz[x], Crossover::kSplitMaxHz[x], 0.1f, 0.4f), Crossover::kSplitDefaultHz[x],
            AudioParameterFloatAttributes().withLabel ("Hz")));

        // Actif par defaut sur les 5 -- comportement "6 bandes" par
        // defaut a l'ouverture, coherent avec l'ancien plugin 3 bandes qui
        // n'avait pas de notion de fusion. Voir doc de classe
        // (PluginProcessor.h) pour le mecanisme de fusion.
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { idCrossoverEnabled[x], 1 }, crossoverName[x] + juce::String (" Enabled"), true));
    }

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idDigitalCrossover, 1 }, "Linear Crossover", false));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idLinearLatencyMode, 1 }, "Linear Latency",
        StringArray { "Low", "Medium", "High" }, 0));

    static const char* soloName[(size_t) NumBands] = { "Solo Low", "Solo Low-Mid", "Solo Mid", "Solo High-Mid", "Solo Presence", "Solo High", "Solo Air" };
    for (int band = 0; band < (int) NumBands; ++band)
        params.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { idSolo[band], 1 }, soloName[band], false));

    return { params.begin(), params.end() };
}

void SixpackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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

    for (auto& buf : rawBandBuffers)
        buf.setSize (numChannels, samplesPerBlock);

    thdAnalysisCrossovers.clear();
    thdAnalysisCrossovers.reserve ((size_t) numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        thdAnalysisCrossovers.push_back (std::make_unique<Crossover>());
    for (auto& xoPtr : thdAnalysisCrossovers)
        xoPtr->prepare (sampleRate, samplesPerBlock);

    thdResidualBuffer.setSize (numChannels, samplesPerBlock);
    for (auto& buf : thdBandBuffers)
        buf.setSize (numChannels, samplesPerBlock);

    // Toujours 2 canaux : suffit pour un sidechain mono ou stereo (voir
    // sidechainCopyBuffer, PluginProcessor.h).
    sidechainCopyBuffer.setSize (2, samplesPerBlock);

    // Redispatch des sorties standalone : autant de canaux que le buffer de
    // process en fournira (16 en standalone via channelConfig {2,16}).
    outputRemapScratch.setSize (juce::jmax (2, getTotalNumOutputChannels()), samplesPerBlock);

    spectrumAnalyzer.prepare (sampleRate);

    // Coefficient de retombee PAR ECHANTILLON pour bandOutputEnvLin (voir
    // doc du membre, PluginProcessor.h) -- meme taux que l'ancienne
    // ballistique bloc (40dB/s), mais applique en continu : gain qui
    // multiplie l'enveloppe a CHAQUE echantillon pour une decroissance de
    // 40dB par seconde en continu (independant de la taille de bloc de
    // l'hote).
    bandOutputPerSampleDecay = (float) std::pow (10.0, -40.0 / 20.0 / juce::jmax (1.0, sampleRate));
    for (auto& env : bandOutputEnvLin)
        env = 0.0f;

    lastReportedLatencySamples = -1; // force la premiere mise a jour dans processBlock()
}

void SixpackAudioProcessor::releaseResources() {}

void SixpackAudioProcessor::setStandaloneOutputChannelNames (const juce::StringArray& names)
{
    const juce::ScopedLock sl (standaloneOutNamesLock);
    standaloneOutChannelNames = names;
}

juce::StringArray SixpackAudioProcessor::getStandaloneOutputChannelNames() const
{
    const juce::ScopedLock sl (standaloneOutNamesLock);
    return standaloneOutChannelNames;
}

int SixpackAudioProcessor::getStandaloneOutputPairCount() const
{
    const juce::ScopedLock sl (standaloneOutNamesLock);
    return standaloneOutChannelNames.size() / 2;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SixpackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    const auto scSet = layouts.getChannelSet (true, 1);
    if (! scSet.isDisabled() && scSet != juce::AudioChannelSet::mono() && scSet != juce::AudioChannelSet::stereo())
        return false;

    // Bus de sortie par bande (index 1..7) : chacun doit etre soit
    // desactive, soit stereo. L'hote (ou le standalone) est libre d'en
    // activer autant qu'il veut, dans n'importe quelle combinaison.
    for (int busIdx = 1; busIdx <= (int) NumBands; ++busIdx)
    {
        const auto set = layouts.getChannelSet (false, busIdx);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}
#endif

void SixpackAudioProcessor::updateDspFromParams()
{
    const bool xoLinear = digitalCrossoverParam->load() > 0.5f;
    const float* inputTrim  = xoLinear ? kInputCalibrationTrimDbLinear  : kInputCalibrationTrimDbAnalog;
    const float* outputTrim = xoLinear ? kOutputCalibrationTrimDbLinear : kOutputCalibrationTrimDbAnalog;

    for (int band = 0; band < (int) NumBands; ++band)
    {
        const size_t b = (size_t) band;
        const bool isOpto = opticalParam[b]->load() > 0.5f;
        const bool isBritish = britishParam[b]->load() > 0.5f;

        // Compresseur (branche ABOVE) et Expandeur (branche BELOW) tournent
        // TOUJOURS ensemble, chacun avec ses PROPRES reglages en permanence
        // (retour utilisateur explicite : "on va faire apparaitre les
        // expandeurs" -- ils ont maintenant chacun leurs knobs dedies dans
        // l'UI, voir BandPanel::expandRangeKnob/expandAttackKnob). Plus de
        // bouton Comp/Expand pour choisir quel jeu de parametres s'applique
        // -- idAboveBelow reste dans l'APVTS pour la compat des presets/
        // automations existants mais n'est plus lu ici.
        const auto aboveBelowMode = FeedbackCompressor::AboveBelowMode::Both;

        const int ratioIdx = juce::jlimit (0, 6, (int) std::round (ratioIndexParam[b]->load()));
        const float ratio = ratioTable[(size_t) ratioIdx];
        const float knee  = kneeTable[(size_t) ratioIdx];
        const float effAttackMs  = attackParam[b]->load();
        const float effReleaseMs = releaseParam[b]->load();

        // Branche BELOW (expansion) : TOUJOURS le jeu de parametres Expand
        // dedie (Ratio/Release n'ont pas de knob expose -- "pas besoin du
        // reglage release sur les expandeurs" -- mais restent automatables
        // via l'APVTS a leur valeur par defaut).
        const int belowRatioIdx = juce::jlimit (0, 6, (int) std::round (ratioIndexExpandParam[b]->load()));
        const float belowRatio = ratioTable[(size_t) belowRatioIdx];
        const float belowKnee  = kneeTable[(size_t) belowRatioIdx];
        const float belowAttackMs  = attackExpandParam[b]->load();
        const float belowReleaseMs = releaseExpandParam[b]->load();
        // Output : UN SEUL knob partage entre Comp et Expand (retour
        // utilisateur explicite : pas de knob Output dedie a l'expandeur --
        // idOutputExpand reste dans l'APVTS pour la compat des anciens
        // presets, mais n'a pas d'effet DSP).
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
            comp.setDetectorHpIsLowPass (band == NumBands - 1); // bande la plus haute (Air) : HP -> LP (voir doc de classe)
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

std::array<int, (size_t) SixpackAudioProcessor::NumBands> SixpackAudioProcessor::computeGroupOwners() const noexcept
{
    std::array<int, (size_t) NumBands> owner {};
    owner[0] = 0;
    for (int b = 1; b < (int) NumBands; ++b)
    {
        const bool crossoverIsEnabled = crossoverEnabledParam[(size_t) (b - 1)]->load() > 0.5f;
        // Crossover eteint -> b rejoint le groupe de son voisin bas
        // (proprietaire deja resolu -- propage a travers une chaine de
        // plusieurs crossovers eteints consecutifs, pas juste le voisin
        // immediat).
        owner[(size_t) b] = crossoverIsEnabled ? b : owner[(size_t) (b - 1)];
    }
    return owner;
}

std::array<float, (size_t) SixpackAudioProcessor::NumCrossovers> SixpackAudioProcessor::sanitizeCrossoverFrequencies() const noexcept
{
    std::array<float, (size_t) NumCrossovers> hz {};
    for (int x = 0; x < NumCrossovers; ++x)
        hz[(size_t) x] = juce::jlimit (Crossover::kSplitMinHz[x], Crossover::kSplitMaxHz[x], crossoverFreqParam[(size_t) x]->load());

    // Ecart minimum en RATIO de frequence (echelle log, plus coherent
    // perceptivement sur 20Hz-18kHz qu'un ecart en Hz absolu) entre 2
    // points consecutifs -- protege le DSP d'un ordre non-croissant si
    // l'utilisateur fait glisser un curseur au-dela de son voisin dans
    // l'UI (celle-ci reste libre de le permettre visuellement, seul le
    // signal pousse au Crossover est protege ici).
    constexpr float minRatio = 1.05f;
    for (int x = 1; x < NumCrossovers; ++x)
    {
        const float minAllowed = hz[(size_t) (x - 1)] * minRatio;
        if (hz[(size_t) x] < minAllowed)
            hz[(size_t) x] = juce::jmin (Crossover::kSplitMaxHz[x], minAllowed);
    }
    // 2e passe descendante : cas limite ou la passe ascendante pousserait
    // un point au-dela de sa PROPRE borne max (curseurs tous compresses en
    // butee haute) -- retro-propage la contrainte pour rester strictement
    // croissant meme dans ce cas extreme.
    for (int x = NumCrossovers - 2; x >= 0; --x)
    {
        const float maxAllowed = hz[(size_t) (x + 1)] / minRatio;
        if (hz[(size_t) x] > maxAllowed)
            hz[(size_t) x] = juce::jmax (Crossover::kSplitMinHz[x], maxAllowed);
    }
    return hz;
}

void SixpackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    {
        aiLearnAnalyzer.pushSamples (dataL, dataR, numSamples);
        spectrumAnalyzer.pushSamples (dataL, numSamples); // fond de spectre de l'editeur de crossover, voir SpectrumAnalyzerState.h
    }

    // Bus sidechain externe -- voir constructeur. Le bouton SC de CHAQUE
    // bande decide independamment si celle-ci ecoute ce signal.
    // IMPORTANT : on COPIE le sidechain dans sidechainCopyBuffer tout de
    // suite, car ses canaux dans le buffer de process sont partages avec le
    // 1er bus de sortie par bande (buses API) -- ecrire une sortie de bande
    // plus bas ecraserait sinon le sidechain avant sa lecture.
    auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    const bool sidechainBusActive = sidechainBuffer.getNumChannels() > 0;
    const float* scDataL = nullptr;
    const float* scDataR = nullptr;
    if (sidechainBusActive)
    {
        const int scChans = juce::jmin (2, sidechainBuffer.getNumChannels());
        for (int ch = 0; ch < scChans; ++ch)
            sidechainCopyBuffer.copyFrom (ch, 0, sidechainBuffer, ch, 0, numSamples);
        scDataL = sidechainCopyBuffer.getReadPointer (0);
        scDataR = scChans > 1 ? sidechainCopyBuffer.getReadPointer (1) : scDataL;
    }

    // Bus de sortie par bande (index 1..NumBands, voir idBandRouteOut).
    // Recuperes et remis a ZERO systematiquement : une bande non routee,
    // fusionnee dans une voisine, ou un power-off laisse ainsi sa sortie
    // dediee SILENCIEUSE (jamais de fuite du signal d'entree). Tableau de
    // pointeurs nul quand le bus correspondant est desactive par l'hote.
    std::array<float*, (size_t) NumBands> bandOutL {};
    std::array<float*, (size_t) NumBands> bandOutR {};
    for (int band = 0; band < (int) NumBands; ++band)
    {
        auto bandBus = getBusBuffer (buffer, false, 1 + band);
        if (bandBus.getNumChannels() > 0)
        {
            bandBus.clear();
            bandOutL[(size_t) band] = bandBus.getWritePointer (0);
            bandOutR[(size_t) band] = bandBus.getNumChannels() > 1 ? bandBus.getWritePointer (1) : nullptr;
        }
    }

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
        for (auto& amt : clipMeterAmountBand)
            amt.store (0.0f);
        return;
    }

    updateDspFromParams();

    const auto xoHz = sanitizeCrossoverFrequencies();
    const bool  xoLinear = digitalCrossoverParam->load() > 0.5f;
    const int latencyModeIndex = juce::jlimit (0, 2, (int) std::round (linearLatencyModeParam->load()));
    const auto latencyMode = (Crossover::LatencyMode) latencyModeIndex;

    const bool softClipOn = softClipParam->load() > 0.5f;
    clipperL.setHardLimitEnabled (softClipOn);
    clipperR.setHardLimitEnabled (softClipOn);

    const auto satMode = (SoftClipper::SatMode) juce::jlimit (0, 3, (int) std::round (thdModeParam->load()));
    clipperL.setMode (satMode);
    clipperR.setMode (satMode);

    // Plafond de sortie THD reglable (-0.5 -> -3 dB, voir idThdOutput) --
    // ne mord que quand le THD Valve tourne (branches LIMIT + Tape/Iron/Air
    // de SoftClipper::processSample()).
    const float thdCeilingDb = juce::jlimit (-3.0f, -0.5f, thdOutputParam->load());
    clipperL.setCeilingDb (thdCeilingDb);
    clipperR.setCeilingDb (thdCeilingDb);

    // Calcule UNE SEULE FOIS ici (reutilise plus bas pour la fusion des
    // buffers) -- necessaire des maintenant pour le Solo ci-dessous : une
    // bande fusionnee/non-owner ne traite jamais rien (voir plus bas), son
    // solo n'a donc aucun sens et ne doit JAMAIS pouvoir couper le son de
    // la bande "owner" qui la remplace (bug corrige : un solo reste sur
    // une bande fusionnee restait actif/silencieux sans plus jamais
    // pouvoir etre entendu, et pouvait meme faire taire l'owner si lui
    // seul n'etait pas soloe).
    const auto groupOwner = computeGroupOwners();
    bool bandIsOwner[(size_t) NumBands];
    for (int band = 0; band < (int) NumBands; ++band)
        bandIsOwner[(size_t) band] = groupOwner[(size_t) band] == band;

    // Solo par bande : des qu'une bande OWNER est soloee, les autres
    // groupes sont exclus du mix (pas seulement attenues).
    bool soloOn[(size_t) NumBands];
    bool anySolo = false;
    for (int band = 0; band < (int) NumBands; ++band)
    {
        soloOn[(size_t) band] = bandIsOwner[(size_t) band] && soloParam[(size_t) band]->load() > 0.5f;
        anySolo = anySolo || soloOn[(size_t) band];
    }
    bool playBand[(size_t) NumBands];
    for (int band = 0; band < (int) NumBands; ++band)
        playBand[(size_t) band] = ! anySolo || soloOn[(size_t) band];

    bool bandSidechainOn[(size_t) NumBands];
    bool bandDetectorLink[(size_t) NumBands];
    bool bandRouteOut[(size_t) NumBands];   // vers sa propre sortie (voir idBandRouteOut)
    float bandMixLin[(size_t) NumBands];    // fader Mix, 0..1 (voir idBandMix)
    float bandWidthLin[(size_t) NumBands];  // fader Width, 0..2, 1=neutre (voir idBandWidth)
    float bandOutGainLin[(size_t) NumBands]; // meme gain de sortie que celui applique par le compresseur, pour le dry de reference du fader Mix
    for (int band = 0; band < (int) NumBands; ++band)
    {
        // Route sur sa sortie dediee uniquement si : le parametre est ON,
        // ET le bus correspondant est reellement actif (hote/standalone),
        // ET la bande est "owner" (une bande fusionnee n'a pas de signal
        // propre -- elle est deja comptee dans sa voisine). Sinon la bande
        // reste dans le mix principal (comportement inchange).
        bandRouteOut[(size_t) band]     = bandRouteOutParam[(size_t) band]->load() > 0.5f
                                          && bandOutL[(size_t) band] != nullptr
                                          && bandIsOwner[(size_t) band];
        bandSidechainOn[(size_t) band]  = sidechainEnableParam[(size_t) band]->load() > 0.5f;
        bandDetectorLink[(size_t) band] = dataR != nullptr && detectorLinkParam[(size_t) band]->load() > 0.5f;
        bandMixLin[(size_t) band]       = juce::jlimit (0.0f, 1.0f, bandMixParam[(size_t) band]->load() * 0.01f);
        bandWidthLin[(size_t) band]     = juce::jlimit (0.0f, 2.0f, bandWidthParam[(size_t) band]->load() * 0.01f);
        bandOutGainLin[(size_t) band]   = juce::Decibels::decibelsToGain (outputParam[(size_t) band]->load()
                                              + (xoLinear ? kOutputCalibrationTrimDbLinear[(size_t) band] : kOutputCalibrationTrimDbAnalog[(size_t) band]));
    }

    // --- Split 5 bandes, une passe par canal sur tout le bloc -- obligatoire
    // pour le mode LINEAR (convolution FFT bloc entier). ---
    for (int ch = 0; ch < numChannels && ch < (int) crossovers.size(); ++ch)
    {
        auto& xo = *crossovers[(size_t) ch];
        for (int x = 0; x < NumCrossovers; ++x)
            xo.setCrossoverHz (x, xoHz[(size_t) x]);
        xo.setLinearPhaseMode (xoLinear);
        xo.setLatencyMode (latencyMode);

        float* outs[(size_t) NumBands];
        for (int band = 0; band < (int) NumBands; ++band)
            outs[(size_t) band] = rawBandBuffers[(size_t) band].getWritePointer (ch);
        xo.processBlock (buffer.getReadPointer (ch), outs, numSamples);
    }

    // --- Fusion des bandes voisines dont le crossover est desactive (voir
    // doc de classe / computeGroupOwners()) -- fait ICI au niveau du buffer
    // ENTIER, pas dans la boucle par-echantillon plus bas : le contenu de
    // rawBandBuffers[owner] devient directement la somme brute de tout son
    // groupe, les buffers des bandes non-owner restent inchanges mais ne
    // sont plus lus pour le reste du bloc (voir bandIsOwner ci-dessous).
    // Correct quel que soit l'ordre/la longueur de la chaine de fusion :
    // on parcourt les bandes en indice CROISSANT et chaque bande n'est lue
    // comme source qu'UNE SEULE fois, avant d'avoir pu etre elle-meme
    // modifiee en tant que cible (groupOwner[b] <= b toujours). groupOwner/
    // bandIsOwner deja calcules plus haut (voir bloc Solo).
    for (int band = 1; band < (int) NumBands; ++band)
    {
        if (groupOwner[(size_t) band] != band)
            for (int ch = 0; ch < numChannels; ++ch)
                rawBandBuffers[(size_t) groupOwner[(size_t) band]].addFrom (ch, 0, rawBandBuffers[(size_t) band], ch, 0, numSamples);
    }

    const float* bandL[(size_t) NumBands];
    const float* bandR[(size_t) NumBands];
    for (int band = 0; band < (int) NumBands; ++band)
    {
        bandL[(size_t) band] = rawBandBuffers[(size_t) band].getReadPointer (0);
        bandR[(size_t) band] = dataR != nullptr ? rawBandBuffers[(size_t) band].getReadPointer (1) : nullptr;
    }

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

        float bandSampleL[(size_t) NumBands], bandSampleR[(size_t) NumBands];
        for (int band = 0; band < (int) NumBands; ++band)
        {
            bandSampleL[(size_t) band] = bandL[(size_t) band][i];
            bandSampleR[(size_t) band] = dataR != nullptr ? bandR[(size_t) band][i] : 0.0f;
        }

        float sumL = 0.0f, sumR = 0.0f;

        for (int band = 0; band < (int) NumBands; ++band)
        {
            // Bande fusionnee dans une voisine (crossover desactive, voir
            // doc de classe) : son audio est deja compte dans la bande
            // "owner" ci-dessus (fusion faite au niveau buffer, avant la
            // boucle) -- ne rien ajouter au mix ici (double comptage sinon),
            // juste laisser le GR meter retomber doucement a 0.
            if (! bandIsOwner[(size_t) band])
            {
                compL[(size_t) band]->relaxGainReductionToZero();
                if (compR[(size_t) band] != nullptr)
                    compR[(size_t) band]->relaxGainReductionToZero();
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

            // Enveloppe de niveau de sortie -- attaque instantanee, retombee
            // ECHANTILLON PAR ECHANTILLON (voir doc de bandOutputEnvLin,
            // PluginProcessor.h) -- corrige un affichage "crante" au VU-metre
            // avec de gros buffers hote (retour utilisateur explicite).
            {
                auto& env = bandOutputEnvLin[(size_t) band];
                const float instant = juce::jmax (std::abs (wetL), std::abs (wetR));
                env = instant > env ? instant : env * bandOutputPerSampleDecay;
            }

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
                const float outL = mid + side;
                const float outR = mid - side;

                if (bandRouteOut[(size_t) band])
                {
                    // Split : la bande sort UNIQUEMENT sur son bus dedie,
                    // elle est retiree du mix principal (voir idBandRouteOut).
                    bandOutL[(size_t) band][i] += outL;
                    if (bandOutR[(size_t) band] != nullptr)
                        bandOutR[(size_t) band][i] += outR;
                }
                else
                {
                    sumL += outL;
                    sumR += outR;
                }
            }
            else
            {
                if (bandRouteOut[(size_t) band])
                    bandOutL[(size_t) band][i] += wetL;
                else
                    sumL += wetL;
            }
        }

        // Publier l'enveloppe vers l'UI PLUSIEURS FOIS par bloc (pas
        // seulement a la fin, voir ancien code plus bas) : avec de gros
        // buffers hote, ne publier qu'une fois par callback recreait un
        // affichage "crante" meme apres avoir lisse la retombee interne
        // echantillon par echantillon -- l'UI (Timer 30Hz, BandMetersRack)
        // ne voyait jamais qu'UNE seule nouvelle valeur par bloc. Retour
        // utilisateur explicite : toujours cranté apres le 1er correctif.
        if ((i & 63) == 0 || i == numSamples - 1)
        {
            for (int band = 0; band < (int) NumBands; ++band)
                bandOutputLevelDb[(size_t) band].store (juce::Decibels::gainToDecibels (bandOutputEnvLin[(size_t) band], -100.0f));
        }

        const bool applyClip = softClipOn;
        if (dataL != nullptr)
        {
            dataL[i] = applyClip ? clipperL.processSample (sumL) : sumL;
            residL[i] = sumL - dataL[i]; // 0 si pas de clip
        }
        if (dataR != nullptr)
        {
            dataR[i] = applyClip ? clipperR.processSample (sumR) : sumR;
            if (residR != nullptr)
                residR[i] = sumR - dataR[i];
        }
    }

    // Meters de sortie (retour utilisateur : "on supprime la protection true
    // peak" -- l'etage de correction dedie sur-echantillonne 4x a ete
    // retire, son filtrage anti-repliement permanent grignotait le haut du
    // spectre meme sans depassement reel ; seule la protection true-peak
    // deja integree a SoftClipper pour Tape/Iron/Air/Limit subsiste, voir
    // SoftClipper::applyTruePeakCeiling()).
    for (int i = 0; i < numSamples; ++i)
    {
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
            for (int x = 0; x < NumCrossovers; ++x)
                xo.setCrossoverHz (x, xoHz[(size_t) x]);
            xo.setLinearPhaseMode (xoLinear);
            xo.setLatencyMode (latencyMode);

            float* outs[(size_t) NumBands];
            for (int band = 0; band < (int) NumBands; ++band)
                outs[(size_t) band] = thdBandBuffers[(size_t) band].getWritePointer (ch);
            xo.processBlock (thdResidualBuffer.getReadPointer (ch), outs, numSamples);
        }

        float peakResid[(size_t) NumBands] = {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int band = 0; band < (int) NumBands; ++band)
            {
                const float* bp = thdBandBuffers[(size_t) band].getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    peakResid[(size_t) band] = juce::jmax (peakResid[(size_t) band], std::abs (bp[i]));
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
        for (int band = 0; band < (int) NumBands; ++band)
            updateBandAmount (clipMeterAmountBand[(size_t) band], peakResid[(size_t) band]);
    }
    else
    {
        // THD desengage -- retombee douce vers 0 (meme ballistique), pas
        // un saut net.
        const float blockDecay = std::pow (0.995f, (float) numSamples);
        for (int band = 0; band < (int) NumBands; ++band)
            clipMeterAmountBand[(size_t) band].store (clipMeterAmountBand[(size_t) band].load() * blockDecay);
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

    // Niveau de sortie par bande : deja publie plusieurs fois pendant la
    // boucle ci-dessus (voir doc de bandOutputEnvLin, PluginProcessor.h),
    // rien a faire ici.

    inputLevelDbL.store  (juce::Decibels::gainToDecibels (peakInL, -100.0f));
    inputLevelDbR.store  (juce::Decibels::gainToDecibels (peakInR, -100.0f));
    outputLevelDbL.store (juce::Decibels::gainToDecibels (peakOutL, -100.0f));
    outputLevelDbR.store (juce::Decibels::gainToDecibels (peakOutR, -100.0f));

    const float clipAmountL = softClipOn ? clipperL.getClipAmount() : 0.0f;
    const float clipAmountR = softClipOn ? clipperR.getClipAmount() : 0.0f;
    clipMeterAmountL.store (clipAmountL);
    clipMeterAmountR.store (clipAmountR);
    clipMeterAmount.store (juce::jmax (clipAmountL, clipAmountR));

    // --- Redispatch des sorties (STANDALONE uniquement) ---
    // Le buffer de process fait 16 canaux en standalone (channelConfig
    // {2,16} de SixpackStandaloneApp). On les traite comme 8 paires stereo
    // a plat et on place : le mix principal sur la paire mainOutPairIndex,
    // chaque bande routee sur bandOutPairIndex[b]. Plusieurs sources sur la
    // meme paire se somment. En VST3/AU on ne touche a rien (l'hote route
    // les bus nommes).
    if (wrapperType == wrapperType_Standalone)
    {
        const int scratchCh = juce::jmin (buffer.getNumChannels(), outputRemapScratch.getNumChannels());
        const int numPairs  = scratchCh / 2;
        if (numPairs >= 1 && numSamples <= outputRemapScratch.getNumSamples())
        {
            for (int ch = 0; ch < scratchCh; ++ch)
                outputRemapScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
            for (int ch = 0; ch < scratchCh; ++ch)
                buffer.clear (ch, 0, numSamples);

            auto place = [&] (int srcPair, int dstPair)
            {
                dstPair = juce::jlimit (0, numPairs - 1, dstPair);
                const int s = srcPair * 2, d = dstPair * 2;
                buffer.addFrom (d,     0, outputRemapScratch, s,     0, numSamples);
                buffer.addFrom (d + 1, 0, outputRemapScratch, s + 1, 0, numSamples);
            };

            place (0, mainOutPairIndex.load()); // paire source 0 = mix principal
            for (int band = 0; band < (int) NumBands; ++band)
                if (bandRouteOut[(size_t) band])
                    place (1 + band, bandOutPairIndex[(size_t) band].load());
        }
    }

    // Latence identique sur tous les canaux (memes reglages pousses a
    // chaque Crossover ci-dessus) -- ne rappeler setLatencySamples() que si
    // la valeur a reellement change.
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

juce::AudioProcessorEditor* SixpackAudioProcessor::createEditor()
{
    return new SixpackAudioProcessorEditor (*this);
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
            SixpackAudioProcessor::idInput, SixpackAudioProcessor::idRatioIndex,
            SixpackAudioProcessor::idAttack, SixpackAudioProcessor::idRelease,
            SixpackAudioProcessor::idOutput,
            SixpackAudioProcessor::idInputExpand, SixpackAudioProcessor::idRatioIndexExpand,
            SixpackAudioProcessor::idAttackExpand, SixpackAudioProcessor::idReleaseExpand,
            SixpackAudioProcessor::idOutputExpand,
            SixpackAudioProcessor::idDetectorHp, SixpackAudioProcessor::idDetectorHpFreq,
            SixpackAudioProcessor::idDetectorHpSlope, SixpackAudioProcessor::idDetectorBell,
            SixpackAudioProcessor::idDetectorBellFreq, SixpackAudioProcessor::idDetectorBellGain,
            SixpackAudioProcessor::idDetectorBell2, SixpackAudioProcessor::idDetectorBell2Freq,
            SixpackAudioProcessor::idDetectorBell2Gain,
            SixpackAudioProcessor::idDetectorLink, SixpackAudioProcessor::idSidechainEnable,
            SixpackAudioProcessor::idAudioHp, SixpackAudioProcessor::idAudioDist2,
            SixpackAudioProcessor::idAudioDist3, SixpackAudioProcessor::idOptical,
            SixpackAudioProcessor::idBritish, SixpackAudioProcessor::idAboveBelow,
            SixpackAudioProcessor::idBandMix, SixpackAudioProcessor::idBandWidth,
            SixpackAudioProcessor::idBandRouteOut,
        };
        for (int band = 0; band < (int) SixpackAudioProcessor::NumBands; ++band)
            for (auto* base : bandBaseIds)
                v.push_back (SixpackAudioProcessor::bandParamId (base, band));

        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
        {
            v.push_back (SixpackAudioProcessor::idCrossover[x]);
            v.push_back (SixpackAudioProcessor::idCrossoverEnabled[x]);
        }
        v.push_back (SixpackAudioProcessor::idSoftClip);
        v.push_back (SixpackAudioProcessor::idThdMode);
        v.push_back (SixpackAudioProcessor::idThdOutput);
        // Analog/Linear -- INCLUS (chaque slot A/B memorise et
        // restaure son propre mode de crossover, sur demande explicite --
        // pas exclu comme Power/Solo, contrairement au comportement par
        // defaut copie de Nuke Mext).
        v.push_back (SixpackAudioProcessor::idDigitalCrossover);
        return v;
    }();
    return ids;
}

void SixpackAudioProcessor::toggleABCompare()
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

void SixpackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("needleModeIndex1", needleModeIndex1, nullptr);
    apvts.state.setProperty ("needleModeIndex2", needleModeIndex2, nullptr);
    apvts.state.setProperty ("lightTheme", lightTheme.load() > 0.5f, nullptr);

    // Routage multi-sorties standalone (voir mainOutPairIndex/bandOutPairIndex).
    apvts.state.setProperty ("mainOutPair", mainOutPairIndex.load(), nullptr);
    for (int b = 0; b < (int) NumBands; ++b)
        apvts.state.setProperty ("bandOutPair" + juce::String (b), bandOutPairIndex[(size_t) b].load(), nullptr);

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

void SixpackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    lightTheme.store ((bool) apvts.state.getProperty ("lightTheme", true) ? 1.0f : 0.0f);

    mainOutPairIndex.store ((int) apvts.state.getProperty ("mainOutPair", 0));
    for (int b = 0; b < (int) NumBands; ++b)
        bandOutPairIndex[(size_t) b].store ((int) apvts.state.getProperty ("bandOutPair" + juce::String (b), b + 1));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SixpackAudioProcessor();
}
