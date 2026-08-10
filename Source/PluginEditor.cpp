#include "PluginEditor.h"

static constexpr int kNumBands = (int) SuppressorAudioProcessor::NumBands;

// Bornes FIXES de l'axe X du graphe XY (DetectorFilterGraph) aux deux
// extremites du spectre -- LOW commence a 20Hz, HIGH finit a 20000Hz.
// Entre les deux, chaque bande suit les knobs XLO/XHI en direct (voir
// buildBandPanel()). std::atomic requis pour matcher la signature du
// graphe (memes refs que XLO/XHI, pas de copie).
static std::atomic<float> kFilterGraphMinHz { 20.0f };
static std::atomic<float> kFilterGraphMaxHz { 20000.0f };

namespace
{
    // Dossier de stockage des presets utilisateur (persistant entre les
    // sessions) -- un fichier .xml par preset, nomme d'apres son nom. Meme
    // mecanisme que Nuke Mext (getUserPresetsDirectory()).
    juce::File getUserPresetsDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("SideKeySystem").getChildFile ("SuppressorMultiband").getChildFile ("Presets");
        dir.createDirectory();
        return dir;
    }

    void writePresetXml (const SuppressorEditorContent::PresetData& p, const juce::File& file)
    {
        juce::XmlElement xml ("SuppressorMultibandPreset");
        xml.setAttribute ("name", p.name);
        xml.setAttribute ("author", p.author);
        xml.setAttribute ("crossoverLow", (double) p.crossoverLow);
        xml.setAttribute ("crossoverHigh", (double) p.crossoverHigh);
        xml.setAttribute ("linearMode", p.linearMode);
        xml.setAttribute ("softClipOn", p.softClipOn);
        xml.setAttribute ("thdMode", p.thdMode);

        for (int b = 0; b < 3; ++b)
        {
            auto* bandXml = xml.createNewChildElement ("Band");
            bandXml->setAttribute ("index", b);
            const auto& bp = p.bands[b];
            bandXml->setAttribute ("input", (double) bp.input);
            bandXml->setAttribute ("ratioIndex", bp.ratioIndex);
            bandXml->setAttribute ("attack", (double) bp.attack);
            bandXml->setAttribute ("release", (double) bp.release);
            bandXml->setAttribute ("output", (double) bp.output);
            bandXml->setAttribute ("aboveBelow", bp.aboveBelow);
            bandXml->setAttribute ("rangeExpand", (double) bp.rangeExpand);
            bandXml->setAttribute ("ratioIndexExpand", bp.ratioIndexExpand);
            bandXml->setAttribute ("attackExpand", (double) bp.attackExpand);
            bandXml->setAttribute ("releaseExpand", (double) bp.releaseExpand);
            bandXml->setAttribute ("outputExpand", (double) bp.outputExpand);
            bandXml->setAttribute ("detectorHp", bp.detectorHp);
            bandXml->setAttribute ("detectorHpFreq", (double) bp.detectorHpFreq);
            bandXml->setAttribute ("detectorHpSlope", bp.detectorHpSlope);
            bandXml->setAttribute ("detectorBell", bp.detectorBell);
            bandXml->setAttribute ("detectorBellFreq", (double) bp.detectorBellFreq);
            bandXml->setAttribute ("detectorBellGain", (double) bp.detectorBellGain);
            bandXml->setAttribute ("detectorBell2", bp.detectorBell2);
            bandXml->setAttribute ("detectorBell2Freq", (double) bp.detectorBell2Freq);
            bandXml->setAttribute ("detectorBell2Gain", (double) bp.detectorBell2Gain);
            bandXml->setAttribute ("detectorLink", bp.detectorLink);
            bandXml->setAttribute ("sidechainEnable", bp.sidechainEnable);
            bandXml->setAttribute ("audioHp", bp.audioHp);
            bandXml->setAttribute ("audioDist2", bp.audioDist2);
            bandXml->setAttribute ("audioDist3", bp.audioDist3);
            bandXml->setAttribute ("optical", bp.optical);
            bandXml->setAttribute ("british", bp.british);
            bandXml->setAttribute ("bandMix", (double) bp.bandMix);
            bandXml->setAttribute ("bandWidth", (double) bp.bandWidth);
        }

        xml.writeTo (file);
    }

    bool readPresetXml (const juce::File& file, SuppressorEditorContent::PresetData& out)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || xml->getTagName() != "SuppressorMultibandPreset")
            return false;

        out.name          = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        out.author        = xml->getStringAttribute ("author", "User");
        out.isFactory      = false;
        out.crossoverLow  = (float) xml->getDoubleAttribute ("crossoverLow", 121.5);
        out.crossoverHigh = (float) xml->getDoubleAttribute ("crossoverHigh", 2521.0);
        out.linearMode    = xml->getBoolAttribute ("linearMode", false);
        out.softClipOn    = xml->getBoolAttribute ("softClipOn", true);
        out.thdMode       = xml->getIntAttribute ("thdMode", 0);

        for (auto* bandXml = xml->getChildByName ("Band"); bandXml != nullptr;
             bandXml = bandXml->getNextElementWithTagName ("Band"))
        {
            const int idx = bandXml->getIntAttribute ("index", 0);
            if (idx < 0 || idx > 2)
                continue;

            auto& bp = out.bands[idx];
            bp.input            = (float) bandXml->getDoubleAttribute ("input", 0.0);
            bp.ratioIndex        = bandXml->getIntAttribute ("ratioIndex", 3);
            bp.attack            = (float) bandXml->getDoubleAttribute ("attack", SuppressorAudioProcessor::defaultAttackMs);
            bp.release           = (float) bandXml->getDoubleAttribute ("release", SuppressorAudioProcessor::defaultReleaseMs);
            bp.output            = (float) bandXml->getDoubleAttribute ("output", 0.0);
            bp.aboveBelow        = bandXml->getIntAttribute ("aboveBelow", 0);
            bp.rangeExpand       = (float) bandXml->getDoubleAttribute ("rangeExpand", 0.0);
            bp.ratioIndexExpand  = bandXml->getIntAttribute ("ratioIndexExpand", 0);
            bp.attackExpand      = (float) bandXml->getDoubleAttribute ("attackExpand", SuppressorAudioProcessor::defaultAttackMs);
            bp.releaseExpand     = (float) bandXml->getDoubleAttribute ("releaseExpand", SuppressorAudioProcessor::defaultReleaseMs);
            bp.outputExpand      = (float) bandXml->getDoubleAttribute ("outputExpand", 0.0);
            bp.detectorHp        = bandXml->getBoolAttribute ("detectorHp", false);
            bp.detectorHpFreq    = (float) bandXml->getDoubleAttribute ("detectorHpFreq", 100.0);
            bp.detectorHpSlope   = bandXml->getIntAttribute ("detectorHpSlope", 2);
            bp.detectorBell      = bandXml->getBoolAttribute ("detectorBell", false);
            bp.detectorBellFreq  = (float) bandXml->getDoubleAttribute ("detectorBellFreq", 650.0);
            bp.detectorBellGain  = (float) bandXml->getDoubleAttribute ("detectorBellGain", 6.0);
            bp.detectorBell2     = bandXml->getBoolAttribute ("detectorBell2", false);
            bp.detectorBell2Freq = (float) bandXml->getDoubleAttribute ("detectorBell2Freq", 3000.0);
            bp.detectorBell2Gain = (float) bandXml->getDoubleAttribute ("detectorBell2Gain", 6.0);
            bp.detectorLink      = bandXml->getBoolAttribute ("detectorLink", false);
            bp.sidechainEnable   = bandXml->getBoolAttribute ("sidechainEnable", false);
            bp.audioHp           = bandXml->getBoolAttribute ("audioHp", false);
            bp.audioDist2        = bandXml->getBoolAttribute ("audioDist2", false);
            bp.audioDist3        = bandXml->getBoolAttribute ("audioDist3", false);
            bp.optical           = bandXml->getBoolAttribute ("optical", false);
            bp.british           = bandXml->getBoolAttribute ("british", false);
            bp.bandMix           = (float) bandXml->getDoubleAttribute ("bandMix", 100.0);
            bp.bandWidth         = (float) bandXml->getDoubleAttribute ("bandWidth", 100.0);
        }

        return true;
    }
}

SuppressorEditorContent::SuppressorEditorContent (SuppressorAudioProcessor& p)
    : processorRef (p)
{
    // Opaque -- paint() couvre deja tout le fond (backgroundCache ou
    // renderMetalPanel, voir plus bas), donc pas besoin que JUCE efface
    // en dessous. Aide aussi a eviter le clignotement blanc au
    // redimensionnement (voir SuppressorAudioProcessorEditor::setOpaque).
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    // --- Barre de controle globale ---
    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idPower, powerButton);

    // BYPASS : momentane -- pas de ButtonAttachment, on pilote directement
    // le bouton via son etat "isDown" (maintenu enfonce = bypass actif).
    // setClickingTogglesState(true) (defaut du constructeur ToggleSwitch)
    // desactive ici pour que le clic ne "verrouille" pas l'etat.
    addAndMakeVisible (bypassButton);
    bypassButton.setClickingTogglesState (false);
    bypassButton.onStateChange = [this]
    {
        const bool down = bypassButton.isDown();
        bypassButton.setToggleState (down, juce::dontSendNotification);
        processorRef.bypassHeld.store (down);
    };

    // A/B : bascule persistante entre 2 jeux de reglages (voir
    // SuppressorAudioProcessor::toggleABCompare()/selectABSlot()) -- pas de
    // ButtonAttachment (etat gere par le processeur, pas un parametre APVTS).
    addAndMakeVisible (abButton);
    abButton.setIsB (processorRef.isUsingSlotB());
    abButton.onSelect = [this] (bool wantB)
    {
        processorRef.selectABSlot (wantB);
        abButton.setIsB (processorRef.isUsingSlotB());
    };

    setupKnob (crossoverLowKnob,  SuppressorAudioProcessor::idCrossoverLow,  "XLO", " Hz", 0);
    setupKnob (crossoverHighKnob, SuppressorAudioProcessor::idCrossoverHigh, "XHI", " Hz", 0);

    // Leger degrade de couleur sur la bague noire des 2 knobs Xover (voir
    // SuppressorLookAndFeel::setKnobAccentGradient()) -- XLO est "entre" Low
    // et Mid, XHI est "entre" Mid et High, pour bien les distinguer des
    // knobs par bande (qui restent noirs purs) d'un coup d'oeil.
    SuppressorLookAndFeel::setKnobAccentGradient (crossoverLowKnob.slider, SuppressorColours::ledCyan, SuppressorColours::ledAmber);
    SuppressorLookAndFeel::setKnobAccentGradient (crossoverHighKnob.slider, SuppressorColours::ledAmber, SuppressorColours::ledRed);

    addAndMakeVisible (xoverModeButton);
    xoverModeButton.setWideTooltip  ("Crossover 1er ordre, 0 latence (comportement par defaut).");
    xoverModeButton.setLinearTooltip ("Crossover FIR a phase lineaire, latence reglable (icone engrenage).");
    xoverModeButton.onSelect = [this] (bool wantLinear)
    {
        if (auto* param = processorRef.apvts.getParameter (SuppressorAudioProcessor::idDigitalCrossover))
            param->setValueNotifyingHost (wantLinear ? 1.0f : 0.0f);
    };

    addAndMakeVisible (latencyModeButton);
    latencyModeButton.onClick = [this] { showOptionsOverlay(); };

    addAndMakeVisible (aiLearnButton);
    aiLearnButton.setTooltip ("Ecoute quelques secondes du programme audio et propose Crossover + Input/Ratio/Attack/Release par bande.");
    aiLearnButton.onClick = [this] { processorRef.startAiLearn(); };

    addAndMakeVisible (linkInSwitch);
    linkInSwitch.setToggleState (processorRef.linkInEnabled.load(), juce::dontSendNotification);
    linkInSwitch.onClick = [this]
    {
        processorRef.linkInEnabled.store (linkInSwitch.getToggleState());
        updateInputLinkVisuals();
    };

    addAndMakeVisible (linkOutSwitch);
    linkOutSwitch.setToggleState (processorRef.linkOutEnabled.load(), juce::dontSendNotification);
    linkOutSwitch.onClick = [this]
    {
        processorRef.linkOutEnabled.store (linkOutSwitch.getToggleState());
        updateOutputLinkVisuals();
    };

    addAndMakeVisible (suppressorLogoButton);
    suppressorLogoButton.onClick = [this] { aboutOverlay.setVisible (true); aboutOverlay.toFront (true); };

    // --- 3 bandes ---
    static const juce::Colour bandAccents[kNumBands] = { SuppressorColours::ledCyan, SuppressorColours::ledAmber, SuppressorColours::ledRed };
    static const char* bandTags[kNumBands] = { "LOW", "MID", "HIGH" };
    for (int band = 0; band < kNumBands; ++band)
        buildBandPanel (band, bandAccents[band], bandTags[band]);

    // Installe les handlers onValueChange (une seule fois -- voir
    // wireInputLink()/wireOutputLink(), qui consultent l'etat des
    // interrupteurs Link a CHAQUE mouvement plutot que d'etre reinstalles
    // au clic, meme principe que Nuke Mext).
    wireInputLink();
    wireOutputLink();
    updateInputLinkVisuals();
    updateOutputLinkVisuals();

    // --- Plaque VU/THD globale ---
    addAndMakeVisible (needleVu1);
    addAndMakeVisible (needleVu2);
    needleVu1Modes.onSelect = [this] (int idx) { needleVu1.setMode (NeedleVU::modeForIndex (idx)); processorRef.needleModeIndex1 = idx; };
    needleVu2Modes.onSelect = [this] (int idx) { needleVu2.setMode (NeedleVU::modeForIndex (idx)); processorRef.needleModeIndex2 = idx; };
    addAndMakeVisible (needleVu1Modes);
    addAndMakeVisible (needleVu2Modes);

    addAndMakeVisible (thdValve);
    softClipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idSoftClip, thdValve.engageSwitch);

    // 4 boutons de mode THD (SOFT/TAPE/IRON/AIR, voir ThdValveModule) --
    // radio exclusif sur idThdMode (0..3), TOUJOURS exactement un actif
    // (SOFT = comportement d'origine, plus un etat "rien de selectionne").
    // Meme mecanique que RatioSelector (ParameterAttachment manuel, pas de
    // ButtonAttachment -- ce n'est pas un booleen).
    if (auto* thdModeParamPtr = processorRef.apvts.getParameter (SuppressorAudioProcessor::idThdMode))
    {
        thdModeAttachment = std::make_unique<juce::ParameterAttachment> (
            *thdModeParamPtr,
            [this] (float v)
            {
                const int mode = (int) std::round (v);
                thdValve.softToggle.setToggleState (mode == 0, juce::dontSendNotification);
                thdValve.tapeToggle.setToggleState (mode == 1, juce::dontSendNotification);
                thdValve.ironToggle.setToggleState (mode == 2, juce::dontSendNotification);
                thdValve.airToggle.setToggleState  (mode == 3, juce::dontSendNotification);
            },
            nullptr);

        auto wireThdModeButton = [this] (ToggleSwitch& btn, int modeIndex)
        {
            btn.onClick = [this, modeIndex] { thdModeAttachment->setValueAsCompleteGesture ((float) modeIndex); };
        };
        wireThdModeButton (thdValve.softToggle, 0);
        wireThdModeButton (thdValve.tapeToggle, 1);
        wireThdModeButton (thdValve.ironToggle, 2);
        wireThdModeButton (thdValve.airToggle, 3);

        thdModeAttachment->sendInitialUpdate();
    }

    addChildComponent (aboutOverlay);
    aboutOverlay.setVisible (false);
    aboutOverlay.setLicenseManager (processorRef.licenseManager);
    aboutOverlay.onDismiss = [this] { aboutOverlay.setVisible (false); };
    aboutOverlay.onOpenLicense = [this]
    {
        aboutOverlay.setVisible (false);
        if (onOpenLicenseRequested)
            onOpenLicenseRequested();
    };

    addChildComponent (optionsOverlay);
    optionsOverlay.setVisible (false);
    optionsOverlay.onDismiss = [this] { optionsOverlay.setVisible (false); };
    optionsOverlay.onOpenLicense = [this]
    {
        optionsOverlay.setVisible (false);
        if (onOpenLicenseRequested)
            onOpenLicenseRequested();
    };
    optionsOverlay.onScaleSelected = [this] (float scale) { if (onScaleRequested) onScaleRequested (scale); };
    // Meme avertissement que Nuke Mext (showLatencyModeMenu()) : Low CPU
    // s'applique immediatement, mais Medium/High CPU demandent confirmation
    // (cout CPU/latence nettement plus eleve).
    optionsOverlay.onLatencySelected = [this] (int index)
    {
        auto* param = processorRef.apvts.getParameter (SuppressorAudioProcessor::idLinearLatencyMode);
        if (param == nullptr)
            return;

        const int currentIndex = (int) std::round (param->convertFrom0to1 (param->getValue()));

        if (index == 0)
        {
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
            return;
        }

        const juce::String modeLabel = (index == 1) ? "Medium CPU" : "High CPU";
        const juce::String recommendedCores = (index == 1) ? "4" : "8";

        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, "Higher CPU usage",
            modeLabel + " gives a steeper LINEAR crossover, but uses noticeably more CPU "
            "and adds more latency. Recommended on a CPU with at least " + recommendedCores
            + " cores. Continue?", "Continue", "Cancel");

        juce::AlertWindow::showAsync (options, [this, param, index, currentIndex] (int alertResult)
        {
            if (alertResult == 1) // "Continue" est le 1er bouton ajoute
                param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
            else
                optionsOverlay.setCurrentLatencyIndex (currentIndex); // annule la selection visuelle
        });
    };

    // --- Section AI (menu Options, demande explicite) ---
    optionsOverlay.onAiTargetSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiMasteringTarget, (float) index);
    };
    optionsOverlay.onAiCompressionModeSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiCompressionMode, (float) index);
    };
    optionsOverlay.onAiCharacterSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiAttackReleaseCharacter, (float) index);
    };
    optionsOverlay.onAiGainReductionSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiGainReductionMode, (float) index);
    };
    optionsOverlay.onAiScopeChanged = [this] (int scopeIndex, bool enabled)
    {
        static constexpr const char* scopeIds[3] =
        {
            SuppressorAudioProcessor::idAiScopeCrossover,
            SuppressorAudioProcessor::idAiScopeDetectors,
            SuppressorAudioProcessor::idAiScopeCompression,
        };
        if (scopeIndex >= 0 && scopeIndex < 3)
            setParamValue (scopeIds[scopeIndex], enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiStyleSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiStyle, (float) index);
    };
    optionsOverlay.onAiFidelityChanged = [this] (bool enabled)
    {
        setParamValue (SuppressorAudioProcessor::idAiFidelity, enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiExpandModeSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiExpandMode, (float) index);
    };
    optionsOverlay.onAiExpandCharacterSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiExpandCharacter, (float) index);
    };
    optionsOverlay.onAiExpandAmountSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiExpandAmount, (float) index);
    };

    // --- Presets rapides (meme mecanisme que Nuke Mext) ---
    buildPresetList();

    presetSectionLabel.setText ("preset", juce::dontSendNotification);
    presetSectionLabel.setJustificationType (juce::Justification::centredLeft);
    presetSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    presetSectionLabel.setColour (juce::Label::textColourId, SuppressorColours::textDim);
    addAndMakeVisible (presetSectionLabel);

    prevPresetButton.onClick = [this] { goToPreset (-1); };
    nextPresetButton.onClick = [this] { goToPreset (1); };
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    presetNameButton.onClick = [this] { openPresetBrowser(); };
    addAndMakeVisible (presetNameButton);

    presetAuthorLabel.setJustificationType (juce::Justification::centred);
    presetAuthorLabel.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::italic)));
    presetAuthorLabel.setColour (juce::Label::textColourId, SuppressorColours::textDim);
    addAndMakeVisible (presetAuthorLabel);

    copyButton.onClick = [this] { copyStateToClipboard(); };
    pasteButton.onClick = [this] { pasteStateFromClipboard(); };
    addAndMakeVisible (copyButton);
    addAndMakeVisible (pasteButton);

    saveButton.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible (saveButton);

    deletePresetButton.onClick = [this] { promptDeletePreset(); };
    addAndMakeVisible (deletePresetButton);

    addChildComponent (presetBrowser);
    presetBrowser.setVisible (false);
    presetBrowser.onPresetChosen = [this] (int index)
    {
        if (index < 0 || index >= (int) presets.size())
            return;
        currentPresetIndex = index;
        applyPresetData (presets[(size_t) currentPresetIndex]);
        refreshPresetDisplay();
    };

    // IMPORTANT : on n'applique PAS de preset au demarrage -- une session
    // deja sauvegardee (avec ses propres reglages) ne doit jamais etre
    // ecrasee silencieusement a l'ouverture de l'editeur. Le nom affiche
    // au depart est purement indicatif ; choisir un preset (clic sur le
    // nom, ou < / >) l'applique reellement.
    presetNameButton.setButtonText ("Default");
    presetAuthorLabel.setText ({}, juce::dontSendNotification);
    deletePresetButton.setEnabled (false);

    startTimerHz (10);

    setSize (baseWidth, baseHeight);
}

SuppressorEditorContent::~SuppressorEditorContent()
{
    setLookAndFeel (nullptr);
}

void SuppressorEditorContent::timerCallback()
{
    const bool powered = *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idPower) > 0.5f;
    if (powered != lookAndFeel.isPowered())
    {
        lookAndFeel.setPowered (powered);
        repaint();
    }

    // Bouton AI : des que l'analyse en tache de fond est terminee, on
    // applique le resultat puis on acquitte pour permettre un nouvel
    // apprentissage. AiLearnButton se rafraichit lui-meme via son propre
    // Timer (clignotement), on n'a besoin de "poller" ici que pour savoir
    // QUAND appliquer le resultat.
    if (processorRef.aiLearnAnalyzer.getState() == AiLearnAnalyzer::State::Done)
    {
        applyAiLearnResult (processorRef.aiLearnAnalyzer.getResult());
        processorRef.aiLearnAnalyzer.acknowledge();
    }
}

void SuppressorEditorContent::applyAiLearnResult (const AiLearnAnalyzer::Result& result)
{
    // Portee (menu Options, section AI, demande explicite : "AI ne
    // s'occupe que de la compression si on le souhaite") -- chaque
    // categorie n'est poussee vers les parametres QUE si son interrupteur
    // etait actif au moment du clic (voir AiLearnAnalyzer::Result).
    if (result.applyCrossoverScope)
    {
        rampParamTo (SuppressorAudioProcessor::idCrossoverLow,  result.crossoverLowHz);
        rampParamTo (SuppressorAudioProcessor::idCrossoverHigh, result.crossoverHighHz);
    }

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SuppressorAudioProcessor::bandParamId (base, band); };

        // Comp et Expand ont des jeux de parametres INDEPENDANTS
        // (idInput/idInputExpand, etc.) et sont TOUS LES DEUX calcules et
        // pousses A CHAQUE Learn (demande explicite : "je veux que AI
        // regle en meme temps le comp et l'expand ... je veux pas cliquer
        // une fois pour les compresseurs et une fois pour les
        // expandeurs") -- plus de bascule automatique du bouton Comp/
        // Expand : les 2 jeux de valeurs sont deja prets, basculer une
        // bande d'un mode a l'autre pour comparer n'exige plus de
        // relancer un Learn.
        if (result.applyCompressionScope)
        {
            // Chaque axe a son propre "Off" (demande explicite : "on doit
            // pouvoir agir que sur les points AI qui ne vont pas") -- ne
            // pousse SA cible que si son axe n'est pas sur Off. Les 2
            // triades (Compression Mode/Gain Reduction/Character et
            // Expand Mode/Amount/Character) sont INDEPENDANTES, chacune
            // avec ses propres drapeaux.
            if (result.pushInput)
                rampParamTo (id (SuppressorAudioProcessor::idInput), result.inputDb[(size_t) band]);
            if (result.pushAttackRelease)
            {
                rampParamTo (id (SuppressorAudioProcessor::idAttack),  result.attackMs[(size_t) band]);
                rampParamTo (id (SuppressorAudioProcessor::idRelease), result.releaseMs[(size_t) band]);
            }
            if (result.pushRatio)
                setParamValue (id (SuppressorAudioProcessor::idRatioIndex),
                               (float) juce::roundToInt (result.ratioIndex[(size_t) band]));

            if (result.pushExpandInput)
                rampParamTo (id (SuppressorAudioProcessor::idInputExpand), result.expandInputDb[(size_t) band]);
            if (result.pushExpandAttackRelease)
            {
                rampParamTo (id (SuppressorAudioProcessor::idAttackExpand),  result.expandAttackMs[(size_t) band]);
                rampParamTo (id (SuppressorAudioProcessor::idReleaseExpand), result.expandReleaseMs[(size_t) band]);
            }
            if (result.pushExpandRatio)
                setParamValue (id (SuppressorAudioProcessor::idRatioIndexExpand),
                               (float) juce::roundToInt (result.expandRatioIndex[(size_t) band]));

            rampParamTo (id (SuppressorAudioProcessor::idBandMix),   result.mixPercent[(size_t) band]);
            rampParamTo (id (SuppressorAudioProcessor::idBandWidth), result.widthPercent[(size_t) band]);
        }

        if (result.applyDetectorsScope)
        {
            setParamValue (id (SuppressorAudioProcessor::idDetectorBell), result.detectorBellOn[(size_t) band] ? 1.0f : 0.0f);
            setParamValue (id (SuppressorAudioProcessor::idDetectorBellFreq), result.detectorBellFreqHz[(size_t) band]);
            setParamValue (id (SuppressorAudioProcessor::idDetectorBellGain), result.detectorBellGainDb[(size_t) band]);
        }

        // Output : UN SEUL knob partage entre Comp et Expand desormais
        // (retour utilisateur : "j'ai l'impression que je clique sur comp
        // et expand" -- deux Output independants faisaient sauter le
        // niveau au clic entre les deux modes, puisque Above et Below
        // tournent maintenant TOUJOURS ensemble -- voir doc de
        // FeedbackCompressor::AboveBelowMode). Jusqu'a 4 sources
        // INDEPENDANTES peuvent y contribuer -- la cible de mastering
        // (LUFS), le Style musical (offset statique par bande), Fidelity
        // (correction dynamique de balance) et le makeup Expand (voir
        // AiLearnAnalyzer::Result) -- chacune activable separement. On les
        // additionne et on pousse une seule fois si au moins une est
        // active, plutot qu'un rampParamTo par source (qui n'appliquerait
        // que la derniere).
        if (result.applyLoudnessTarget || result.applyStyle || result.applyFidelity || result.applyExpandOutput)
        {
            float outputDb = 0.0f;
            if (result.applyLoudnessTarget) outputDb += result.outputDb[(size_t) band];
            if (result.applyStyle)          outputDb += result.styleOutputDb[(size_t) band];
            if (result.applyFidelity)       outputDb += result.fidelityOutputDb[(size_t) band];
            if (result.applyExpandOutput)   outputDb += result.expandOutputDb[(size_t) band];
            rampParamTo (id (SuppressorAudioProcessor::idOutput), outputDb);
        }
    }

    if (result.applyLoudnessTarget && result.recommendSoftClipOn)
    {
        setParamValue (SuppressorAudioProcessor::idSoftClip, 1.0f);
    }
}

void SuppressorEditorContent::setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                                                 const juce::String& unitSuffix, int decimalPlaces)
{
    addAndMakeVisible (knob.slider);
    knob.slider.setTextValueSuffix (unitSuffix);
    knob.slider.setNumDecimalPlacesToDisplay (decimalPlaces);

    if (auto* param = processorRef.apvts.getParameter (paramID))
        knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, paramID, knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centredLeft);
    knob.label.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
    knob.label.setColour (juce::Label::textColourId, SuppressorColours::textBright.withAlpha (0.78f));
    addAndMakeVisible (knob.label);
}

void SuppressorEditorContent::setupFader (FaderControl& fader, const juce::String& paramID, double neutralValue,
                                                  const juce::String& topText, const juce::String& bottomText, juce::Colour accent,
                                                  juce::Colour accentBelowNeutral)
{
    fader.lookAndFeel.setAccentColour (accent);
    if (! accentBelowNeutral.isTransparent())
        fader.lookAndFeel.setAccentColourBelowNeutral (accentBelowNeutral);
    FaderVerticalLookAndFeel::setNeutralValue (fader.slider, neutralValue);

    addAndMakeVisible (fader.slider);
    fader.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, paramID, fader.slider);

    auto setupEndLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.05f));
        l.setColour (juce::Label::textColourId, SuppressorColours::textBright.withAlpha (0.75f));
        addAndMakeVisible (l);
    };
    setupEndLabel (fader.topLabel, topText);
    setupEndLabel (fader.bottomLabel, bottomText);
}

void SuppressorEditorContent::buildBandPanel (int band, juce::Colour accent, const juce::String& tag)
{
    auto& bp = bandPanels[(size_t) band];
    bp.accentColour = accent;

    const auto pid = [band] (const char* base) { return SuppressorAudioProcessor::bandParamId (base, band); };

    addAndMakeVisible (bp.opticalButton);
    bp.opticalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idOptical), bp.opticalButton);

    addAndMakeVisible (bp.britishButton);
    bp.britishAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idBritish), bp.britishButton);

    auto* opticalBtn = &bp.opticalButton;
    auto* britishBtn = &bp.britishButton;
    opticalBtn->onClick = [opticalBtn, britishBtn] { if (opticalBtn->getToggleState()) britishBtn->setToggleState (false, juce::sendNotificationSync); };
    britishBtn->onClick = [opticalBtn, britishBtn] { if (britishBtn->getToggleState()) opticalBtn->setToggleState (false, juce::sendNotificationSync); };

    // Comp/Expand (demande explicite : "on va juste mettre expand et comp.
    // quand on est en mode comp c'est comme si on etait en both ... le
    // mode expand passe sur l'expander") -- choix a 2 valeurs SEULEMENT
    // desormais (etait 3 -- Above/Below/Both), cycle au clic (COMP ->
    // EXPAND -> COMP...), meme mecanique que ratioSelector ci-dessus
    // (ParameterAttachment manuel, pas de ButtonAttachment). La diode
    // reste toujours allumee, seule sa couleur indique le mode actif.
    addAndMakeVisible (bp.aboveBelowButton);
    // 2 etats a faire cycler manuellement au clic -- on desactive le
    // toggle automatique du clic (ToggleSwitch l'active par defaut) pour
    // eviter qu'il devine un etat incorrect avant que le callback de
    // l'attachment (ci-dessous) ne vienne le corriger.
    bp.aboveBelowButton.setClickingTogglesState (false);
    static const char* aboveBelowLabels[2] = { "COMP", "EXPAND" };
    // EXPAND en violet -- coherent avec le reste du theme Expand (knobs
    // teintes violet, rangee GR dediee bleu->cyan->violet, voir
    // SuppressorLookAndFeel::ledGradientColourExpand()) -- retour
    // utilisateur : la diode restait verte alors que tout le reste de
    // l'habillage Expand est deja violet.
    static const juce::Colour aboveBelowColours[2] =
        { SuppressorColours::ledBlue, SuppressorColours::ledPurple };
    auto* aboveBelowParam = processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idAboveBelow));
    auto* aboveBelowBtn = &bp.aboveBelowButton;
    bp.aboveBelowAttachment = std::make_unique<juce::ParameterAttachment> (
        *aboveBelowParam,
        [this, band, aboveBelowBtn] (float v)
        {
            const int idx = juce::jlimit (0, 1, juce::roundToInt (v));
            aboveBelowBtn->setText (aboveBelowLabels[idx]);
            aboveBelowBtn->setAccentColour (aboveBelowColours[idx]);
            aboveBelowBtn->setToggleState (true, juce::dontSendNotification);
            // Comp et Expand partagent la meme bande mais des reglages
            // Input/Ratio/Attack/Release/Output INDEPENDANTS (demande
            // explicite) -- reattache les knobs/RatioSelector de cette
            // bande au bon jeu de parametres. Comp tourne desormais en
            // interne comme l'ancien Both (voir PluginProcessor.cpp),
            // mais garde les reglages COMPRESSION -- rebindBandKnobsForMode()
            // n'a pas besoin de changer, idx==1 signifie toujours Expand.
            rebindBandKnobsForMode (band, idx);
        },
        nullptr);
    aboveBelowBtn->onClick = [this, band, aboveBelowParam]
    {
        auto& attachment = *bandPanels[(size_t) band].aboveBelowAttachment;
        const int current = juce::roundToInt (aboveBelowParam->convertFrom0to1 (aboveBelowParam->getValue()));
        attachment.setValueAsCompleteGesture ((float) ((current + 1) % 2));
    };

    addAndMakeVisible (bp.sidechainToggle);
    bp.sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idSidechainEnable), bp.sidechainToggle);

    static const char* soloIds[kNumBands] = { SuppressorAudioProcessor::idSoloLow, SuppressorAudioProcessor::idSoloMid, SuppressorAudioProcessor::idSoloHigh };
    bp.soloSwitch = std::make_unique<SoloSwitch> (accent);
    addAndMakeVisible (*bp.soloSwitch);
    bp.soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, soloIds[band], bp.soloSwitch->lever);

    bp.grMeter = std::make_unique<GRMeter> (processorRef.grMeterDb[(size_t) band],
                                             processorRef.grMeterExpandDb[(size_t) band],
                                             *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idPower),
                                             *processorRef.apvts.getRawParameterValue (pid (SuppressorAudioProcessor::idAboveBelow)));
    addAndMakeVisible (*bp.grMeter);

    addAndMakeVisible (bp.ratioSelector);
    auto* ratioParam = processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idRatioIndex));
    auto* ratioSelectorPtr = &bp.ratioSelector;
    bp.ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        *ratioParam,
        [ratioSelectorPtr] (float v) { ratioSelectorPtr->setSelectedIndex ((int) std::round (v)); },
        nullptr);
    bp.ratioSelector.onSelect = [this, band] (int idx) { bandPanels[(size_t) band].ratioAttachment->setValueAsCompleteGesture ((float) idx); };
    bp.ratioAttachment->sendInitialUpdate();

    for (auto* t : { &bp.detectorHpToggle, &bp.detectorBellToggle, &bp.detectorBell2Toggle, &bp.detectorLinkToggle,
                     &bp.audioHpToggle, &bp.audioDist2Toggle, &bp.audioDist3Toggle })
        addAndMakeVisible (*t);

    // AUDIO HP et DETECTOR LINK : caches (pas supprimes -- parametre/DSP
    // intacts, juste plus affiches) pour degager de la place et agrandir
    // le graphe XY du detecteur, comme demande.
    bp.audioHpToggle.setVisible (false);
    bp.detectorLinkToggle.setVisible (false);

    // Bande MID : BELL 2 remplace HP dans l'UI (deja bornee par le
    // crossover, un HP y est redondant) -- HP reste techniquement cable
    // (parametre/DSP intacts) mais son bouton est cache pour cette bande.
    // Low/High gardent HP/LP normalement, BELL 2 y reste cache.
    const bool isMid = (band == SuppressorAudioProcessor::Mid);
    bp.detectorHpToggle.setVisible (! isMid);
    bp.detectorBell2Toggle.setVisible (isMid);

    bp.detectorHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idDetectorHp), bp.detectorHpToggle);
    bp.detectorBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idDetectorBell), bp.detectorBellToggle);
    bp.detectorBell2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idDetectorBell2), bp.detectorBell2Toggle);
    bp.detectorLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idDetectorLink), bp.detectorLinkToggle);
    bp.audioHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idAudioHp), bp.audioHpToggle);
    bp.audioDist2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idAudioDist2), bp.audioDist2Toggle);
    bp.audioDist3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SuppressorAudioProcessor::idAudioDist3), bp.audioDist3Toggle);

    // Bornes de l'axe X (voir doc de classe DetectorFilterGraph) : LOW va
    // de 20Hz a XLO, MID de XLO a XHI, HIGH de XHI a 20000Hz -- LOW/HIGH
    // partagent le meme atomic XLO/XHI que MID a leur frontiere commune,
    // c'est voulu (continuite visuelle entre bandes adjacentes).
    auto& crossoverLowAtom  = *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idCrossoverLow);
    auto& crossoverHighAtom = *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idCrossoverHigh);
    auto& viewMin = (band == SuppressorAudioProcessor::Low) ? kFilterGraphMinHz
                  : (band == SuppressorAudioProcessor::Mid) ? crossoverLowAtom
                                                              : crossoverHighAtom;
    auto& viewMax = (band == SuppressorAudioProcessor::Low) ? crossoverLowAtom
                  : (band == SuppressorAudioProcessor::Mid) ? crossoverHighAtom
                                                              : kFilterGraphMaxHz;

    bp.filterGraph = std::make_unique<DetectorFilterGraph> (
        *processorRef.apvts.getRawParameterValue (pid (SuppressorAudioProcessor::idDetectorHp)),
        *processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorHpFreq)),
        *processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorHpSlope)),
        *processorRef.apvts.getRawParameterValue (pid (SuppressorAudioProcessor::idDetectorBell)),
        *processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorBellFreq)),
        *processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorBellGain)),
        band == SuppressorAudioProcessor::High, viewMin, viewMax,
        isMid ? processorRef.apvts.getRawParameterValue (pid (SuppressorAudioProcessor::idDetectorBell2)) : nullptr,
        isMid ? processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorBell2Freq)) : nullptr,
        isMid ? processorRef.apvts.getParameter (pid (SuppressorAudioProcessor::idDetectorBell2Gain)) : nullptr);
    addAndMakeVisible (*bp.filterGraph);

    // Bande HIGH : le point HP devient LP (meme mecanique) -- relabelle le
    // bouton en consequence.
    if (band == SuppressorAudioProcessor::High)
        bp.detectorHpToggle.setText ("LP");

    // 2 decimales (retour utilisateur : "pour que je puisse calibrer
    // proprement" -- calibration fine du trim cache Input via Plugin
    // Doctor, 1 decimale ne suffit pas pour lire les 1/100e de dB).
    setupKnob (bp.inputKnob,   pid (SuppressorAudioProcessor::idInput),   "IN",  " dB", 2);
    setupKnob (bp.attackKnob,  pid (SuppressorAudioProcessor::idAttack),  "ATT", " ms", 2);
    setupKnob (bp.releaseKnob, pid (SuppressorAudioProcessor::idRelease), "REL", " ms", 1);
    setupKnob (bp.outputKnob,  pid (SuppressorAudioProcessor::idOutput),  "OUT", " dB", 1);

    // Envoye APRES setupKnob/ratioSelector ci-dessus (et non juste apres
    // la creation de aboveBelowAttachment plus haut) : le callback appelle
    // rebindBandKnobsForMode(), qui doit trouver des knobs deja attaches
    // (par setupKnob) pour pouvoir les reattacher correctement si une
    // session sauvegardee restaure une bande deja en Expand/Both.
    bp.aboveBelowAttachment->sendInitialUpdate();

    setupFader (bp.mixFader, pid (SuppressorAudioProcessor::idBandMix), 0.0,
                "WET", "DRY", SuppressorColours::ledCyan);
    setupFader (bp.widthFader, pid (SuppressorAudioProcessor::idBandWidth), 100.0,
                "SPREAD", "MONO", SuppressorColours::ledAmber, SuppressorColours::ledCyan);

    // "tag" (LOW/MID/HIGH) n'est plus dessine en texte -- voir le logo
    // image correspondant peint directement dans paint() -- conserve comme
    // parametre pour le nom accessible du composant.
    juce::ignoreUnused (tag);
}

void SuppressorEditorContent::rebindBandKnobsForMode (int band, int mode)
{
    auto& bp = bandPanels[(size_t) band];
    const auto pid = [band] (const char* base) { return SuppressorAudioProcessor::bandParamId (base, band); };
    const bool useExpand = (mode == 1); // 0=Comp, 2=Both -> reglages COMPRESSION (demande explicite)

    // Garde anti-cascade : recreer un SliderAttachment pousse la valeur
    // COURANTE du parametre dans le slider (via sendNotificationSync),
    // exactement comme un "drag" pour le mecanisme Link In/Out (voir
    // wireInputLink/wireOutputLink) -- sans ce garde, rebasculer une bande
    // pourrait entrainer les 2 autres bandes sur un delta parasite.
    const bool wasLinkInGuard = linkInGuard;
    const bool wasLinkOutGuard = linkOutGuard;
    linkInGuard = true;
    linkOutGuard = true;

    auto rebindKnob = [this] (KnobControl& knob, const juce::String& newParamId)
    {
        knob.attachment.reset();
        // Meme comportement double-clic que setupKnob() -- sans ce
        // re-appel, le double-clic remettrait toujours vers l'ancien
        // parametre (ex : 0dB d'Input au lieu de 0dB de Range).
        if (auto* param = processorRef.apvts.getParameter (newParamId))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, newParamId, knob.slider);
    };

    // Le knob "IN" est REAFFECTE en "RANGE" pour Expand pur (demande
    // explicite : "le knob input du mode comp passe en mode range" --
    // Comp/Both gardent "IN" normal, voir FeedbackCompressor::
    // setExpandRangeDb()).
    bp.inputKnob.label.setText (useExpand ? "RANGE" : "IN", juce::dontSendNotification);

    rebindKnob (bp.inputKnob,   pid (useExpand ? SuppressorAudioProcessor::idInputExpand   : SuppressorAudioProcessor::idInput));
    rebindKnob (bp.attackKnob,  pid (useExpand ? SuppressorAudioProcessor::idAttackExpand  : SuppressorAudioProcessor::idAttack));
    rebindKnob (bp.releaseKnob, pid (useExpand ? SuppressorAudioProcessor::idReleaseExpand : SuppressorAudioProcessor::idRelease));
    // Output : PAS de rebind -- knob PARTAGE entre Comp et Expand desormais
    // (retour utilisateur : "j'ai l'impression que je clique sur comp et
    // expand" -- deux Output independants faisaient sauter le niveau au
    // clic entre les deux modes ; Above et Below tournant maintenant
    // TOUJOURS ensemble, un seul makeup gain a du sens). Reste attache a
    // idOutput en permanence.

    // Degrade violet sur les 3 knobs mode-dependants quand la bande est en
    // Expand pur (demande explicite : "un peu comme les crossover" -- meme
    // mecanisme que setKnobAccentGradient() utilise pour XLO/XHI) -- retire
    // (retour au gris neutre d'origine) des qu'on repasse sur Comp/Both,
    // sinon la teinte resterait collee sur le meme Slider physique. Output
    // exclu (knob partage, jamais teinte violet).
    for (auto* knob : { &bp.inputKnob, &bp.attackKnob, &bp.releaseKnob })
    {
        if (useExpand)
            SuppressorLookAndFeel::setKnobAccentGradient (knob->slider,
                juce::Colour (0xff5a1fb8), SuppressorColours::ledPurple);
        else
            SuppressorLookAndFeel::clearKnobAccentGradient (knob->slider);
        knob->slider.repaint();
    }

    // Meme mecanique manuelle que le cablage initial du RatioSelector
    // (voir buildBandPanel()) -- pas de ButtonAttachment/SliderAttachment
    // possible pour ce composant custom.
    auto* ratioParam = processorRef.apvts.getParameter (
        pid (useExpand ? SuppressorAudioProcessor::idRatioIndexExpand : SuppressorAudioProcessor::idRatioIndex));
    auto* ratioSelectorPtr = &bp.ratioSelector;
    bp.ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        *ratioParam,
        [ratioSelectorPtr] (float v) { ratioSelectorPtr->setSelectedIndex ((int) std::round (v)); },
        nullptr);
    bp.ratioSelector.onSelect = [this, band] (int idx) { bandPanels[(size_t) band].ratioAttachment->setValueAsCompleteGesture ((float) idx); };
    bp.ratioAttachment->sendInitialUpdate();

    linkInGuard = wasLinkInGuard;
    linkOutGuard = wasLinkOutGuard;
}

// Lien RELATIF (delta), pas absolu : si Low=10dB et Mid=5dB et qu'on bouge
// Low de +3dB, Mid passe a 8dB (l'ecart de 5dB entre les 2 est preserve)
// plutot que de forcer tout le monde a la meme valeur. Installe UNE FOIS
// (pas a chaque clic sur le bouton Link) -- le handler consulte l'etat du
// bouton a chaque mouvement. Meme principe que Nuke Mext.
void SuppressorEditorContent::wireInputLink()
{
    for (int band = 0; band < kNumBands; ++band)
        lastInputValue[(size_t) band] = bandPanels[(size_t) band].inputKnob.slider.getValue();

    for (int band = 0; band < kNumBands; ++band)
    {
        auto& slider = bandPanels[(size_t) band].inputKnob.slider;
        auto previous = slider.onValueChange;

        slider.onValueChange = [this, band, previous]
        {
            if (previous)
                previous();

            auto& thisSlider = bandPanels[(size_t) band].inputKnob.slider;
            const double newValue = thisSlider.getValue();

            if (linkInSwitch.getToggleState() && ! linkInGuard)
            {
                const double delta = newValue - lastInputValue[(size_t) band];

                if (std::abs (delta) > 1.0e-9)
                {
                    // Ne link QUE les bandes dans le MEME mode Comp/Expand
                    // (retour utilisateur : "ça ne doit pas link quand par
                    // exemple une bande est sur expand et le link est
                    // active sur comp ... actuellement si je tourne le
                    // range alors que link in est active, ça tourne tous
                    // les knobs") -- ce knob peut representer Input (Comp)
                    // OU Range (Expand) selon le mode de CETTE bande (voir
                    // rebindBandKnobsForMode()), deux grandeurs de nature
                    // differente qui ne doivent jamais se linker entre elles.
                    const int modeOfDraggedBand = (int) std::round (
                        processorRef.apvts.getRawParameterValue (
                            SuppressorAudioProcessor::bandParamId (SuppressorAudioProcessor::idAboveBelow, band))->load());

                    linkInGuard = true;
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        if (b == band)
                            continue;
                        const int modeOfOtherBand = (int) std::round (
                            processorRef.apvts.getRawParameterValue (
                                SuppressorAudioProcessor::bandParamId (SuppressorAudioProcessor::idAboveBelow, b))->load());
                        if (modeOfOtherBand != modeOfDraggedBand)
                            continue;
                        auto& other = bandPanels[(size_t) b].inputKnob.slider;
                        const double target = juce::jlimit (other.getMinimum(), other.getMaximum(), other.getValue() + delta);
                        other.setValue (target, juce::sendNotificationSync);
                    }
                    linkInGuard = false;
                }
            }

            lastInputValue[(size_t) band] = thisSlider.getValue();
        };
    }
}

void SuppressorEditorContent::wireOutputLink()
{
    for (int band = 0; band < kNumBands; ++band)
        lastOutputValue[(size_t) band] = bandPanels[(size_t) band].outputKnob.slider.getValue();

    for (int band = 0; band < kNumBands; ++band)
    {
        auto& slider = bandPanels[(size_t) band].outputKnob.slider;
        auto previous = slider.onValueChange;

        slider.onValueChange = [this, band, previous]
        {
            if (previous)
                previous();

            auto& thisSlider = bandPanels[(size_t) band].outputKnob.slider;
            const double newValue = thisSlider.getValue();

            if (linkOutSwitch.getToggleState() && ! linkOutGuard)
            {
                const double delta = newValue - lastOutputValue[(size_t) band];

                if (std::abs (delta) > 1.0e-9)
                {
                    // Meme garde que wireInputLink() -- ne link que les
                    // bandes dans le MEME mode Comp/Expand (retour
                    // utilisateur, voir la doc la-bas).
                    const int modeOfDraggedBand = (int) std::round (
                        processorRef.apvts.getRawParameterValue (
                            SuppressorAudioProcessor::bandParamId (SuppressorAudioProcessor::idAboveBelow, band))->load());

                    linkOutGuard = true;
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        if (b == band)
                            continue;
                        const int modeOfOtherBand = (int) std::round (
                            processorRef.apvts.getRawParameterValue (
                                SuppressorAudioProcessor::bandParamId (SuppressorAudioProcessor::idAboveBelow, b))->load());
                        if (modeOfOtherBand != modeOfDraggedBand)
                            continue;
                        auto& other = bandPanels[(size_t) b].outputKnob.slider;
                        const double target = juce::jlimit (other.getMinimum(), other.getMaximum(), other.getValue() + delta);
                        other.setValue (target, juce::sendNotificationSync);
                    }
                    linkOutGuard = false;
                }
            }

            lastOutputValue[(size_t) band] = thisSlider.getValue();
        };
    }
}

void SuppressorEditorContent::updateInputLinkVisuals()
{
    const bool linked = linkInSwitch.getToggleState();
    for (int band = 0; band < kNumBands; ++band)
        bandPanels[(size_t) band].inputKnob.label.setColour (
            juce::Label::textColourId, linked ? SuppressorColours::ledGreen : SuppressorColours::textDim);
}

void SuppressorEditorContent::updateOutputLinkVisuals()
{
    const bool linked = linkOutSwitch.getToggleState();
    for (int band = 0; band < kNumBands; ++band)
        bandPanels[(size_t) band].outputKnob.label.setColour (
            juce::Label::textColourId, linked ? SuppressorColours::ledRed : SuppressorColours::textDim);
}

void SuppressorEditorContent::showOptionsOverlay()
{
    // Trouve l'index d'echelle le plus proche parmi { 75, 80, 85, 90, 95, 100 }%
    // -- plus robuste qu'une chaine de seuils si la liste change.
    static constexpr float kScaleValues[6] = { 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.0f };
    const float currentScale = processorRef.uiScale.load();
    int scaleIndex = 5; // 100%
    float bestDelta = std::abs (currentScale - kScaleValues[5]);
    for (int i = 0; i < 6; ++i)
    {
        const float delta = std::abs (currentScale - kScaleValues[i]);
        if (delta < bestDelta) { bestDelta = delta; scaleIndex = i; }
    }
    optionsOverlay.setCurrentScaleIndex (scaleIndex);

    int latencyIndex = 0;
    if (auto* param = processorRef.apvts.getParameter (SuppressorAudioProcessor::idLinearLatencyMode))
        latencyIndex = (int) std::round (param->convertFrom0to1 (param->getValue()));
    optionsOverlay.setCurrentLatencyIndex (latencyIndex);

    optionsOverlay.setCurrentAiTargetIndex ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiMasteringTarget)));
    optionsOverlay.setCurrentAiCompressionMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiCompressionMode)));
    optionsOverlay.setCurrentAiCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiAttackReleaseCharacter)));
    optionsOverlay.setCurrentAiGainReduction ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiGainReductionMode)));
    optionsOverlay.setAiScopeStates (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiScopeCrossover) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiScopeDetectors) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiScopeCompression) > 0.5f);
    optionsOverlay.setCurrentAiStyle ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiStyle)));
    optionsOverlay.setAiFidelityState (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiFidelity) > 0.5f);
    optionsOverlay.setCurrentAiExpandMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiExpandMode)));
    optionsOverlay.setCurrentAiExpandCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiExpandCharacter)));
    optionsOverlay.setCurrentAiExpandAmount ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiExpandAmount)));

    optionsOverlay.setVisible (true);
    optionsOverlay.toFront (true);
}

void SuppressorEditorContent::resized()
{
    // Barre de controle globale : meme hauteur que les autres plaques
    // (144), pour loger les VU a aiguille (deplacees ici, a gauche du
    // knob XLO) en plus des knobs Xover/Wide-Linear/Link/Power.
    controlBarBounds = juce::Rectangle<float> (3.0f, 3.0f, 1394.0f, 144.0f);

    // Ordre des plaques : barre de controle, PUIS les 3 bandes, PUIS la
    // plaque THD Valve tout en bas (comme demande).
    for (int band = 0; band < kNumBands; ++band)
        bandPanelBounds[(size_t) band] = juce::Rectangle<float> (3.0f, 153.0f + (float) band * 150.0f, 1394.0f, 144.0f);

    vuPlateBounds = juce::Rectangle<float> (3.0f, 153.0f + (float) kNumBands * 150.0f, 1394.0f, 144.0f); // THD Valve seul, tout en bas

    const int barTop = (int) controlBarBounds.getY();

    // Monogramme SK retire -- le logo SUPPRESSOR lui-meme est desormais le
    // declencheur de la page About (voir SuppressorLogoButton), et peut
    // utiliser toute la largeur/hauteur auparavant partagee avec le
    // monogramme -- nettement plus grand.
    // Agrandi (demande explicite : "au moins le double") -- l'image fait un
    // ratio 1536x1024 (1.5:1), donc c'est la HAUTEUR de la boite qui pilote
    // sa taille de rendu (la largeur 170 avait deja de la marge). Plafonne a
    // 84 (1.5x, pas 2x plein) car Bypass + A/B occupent toute la place
    // restante sous le logo dans cette meme colonne, jusqu'au bas de la
    // plaque (barTop+144) -- un vrai doublement (112) les ferait deborder
    // de la plaque. Prevenir l'utilisateur si un doublement complet est
    // vraiment voulu (implique de deplacer Bypass/A-B ailleurs).
    suppressorLogoButton.setBounds (8, barTop + 4, 170, 84);

    // BYPASS + A/B -- sous le logo/wordmark, meme colonne de gauche.
    bypassButton.setBounds (10, barTop + 90, 150, 26);
    abButton.setBounds     (10, barTop + 118, 150, 26);

    // VU a aiguille L/R -- taille D'ORIGINE (326/332 x 122, comme sur la
    // plaque VU/THD du Suppressor mono-bande), a GAUCHE du knob XLO. Tout
    // le reste de la barre est decale/resserre a droite pour leur laisser
    // cette place, SANS reduire la taille des autres fonctions (knobs
    // Xover toujours 124px, boutons a leur taille normale).
    auto layoutNeedle = [] (NeedleVU& needle, NeedleModeSelector& modes, int x, int y, int unitW, int h)
    {
        constexpr int columnW = 54, gap = 6;
        modes.setBounds (x, y, columnW, h);
        needle.setBounds (x + columnW + gap, y, unitW - columnW - gap, h);
    };
    const int needleTop = barTop + 12;
    layoutNeedle (needleVu1, needleVu1Modes, 170, needleTop, 326, 122);
    layoutNeedle (needleVu2, needleVu2Modes, 502, needleTop, 332, 122);

    // Knobs Xover : MEME taille que les knobs de bande (124px), comme demande.
    auto layoutKnob124 = [] (KnobControl& knob, int cx, int cy)
    {
        constexpr int diameter = 124;
        knob.slider.setBounds (cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        const int lw = 40, lh = 12;
        knob.label.setBounds (cx - diameter / 2 + 2, cy + diameter / 2 - lh - 2, lw, lh);
    };
    // Link In/Out -- desormais AVANT le groupe Xover (section inversee,
    // comme demande), juste apres les VU a aiguille.
    linkInSwitch.setBounds  (846, barTop + 30, 90, 30);
    linkOutSwitch.setBounds (846, barTop + 68, 90, 30);

    const int knobCy = barTop + 69;
    layoutKnob124 (crossoverLowKnob,  1006, knobCy);
    layoutKnob124 (crossoverHighKnob, 1134, knobCy);

    // WIDE/LINEAR + engrenage : integres dans le groupe Xover (juste apres
    // XHI), retrecis pour ne pas mordre sur la ligne blanche suivante.
    // Bouton AI juste au-dessus du groupe Analog/Linear (demande explicite).
    aiLearnButton.setBounds (1204, barTop + 4, 70, 22);

    xoverModeButton.setBounds (1204, barTop + 30, 70, 84);
    latencyModeButton.setBounds (1280, barTop + 30, 20, 20);

    powerButton.setBounds (1312, barTop + 26, 70, 92);

    // --- THD Valve : plus de chassis propre (retire, demande explicite) --
    // occupe desormais l'INTEGRALITE de la plaque rack 1U (vuPlateBounds),
    // pour que les 3 aiguilles Low/Mid/High prennent toute la hauteur
    // disponible.
    thdValve.setBounds (vuPlateBounds.toNearestInt());

    // --- 3 plaques de bande ---
    for (int band = 0; band < kNumBands; ++band)
        layoutBandRow (bandPanels[(size_t) band], bandPanelBounds[(size_t) band].toNearestInt());

    // --- Barre de presets, tout en bas (34px, meme mecanisme que Nuke Mext) ---
    auto presetBar = juce::Rectangle<int> (14, 757, 1372, 26);

    presetSectionLabel.setBounds (presetBar.removeFromLeft (46));
    presetBar.removeFromLeft (6);
    prevPresetButton.setBounds (presetBar.removeFromLeft (22));
    presetBar.removeFromLeft (4);
    presetNameButton.setBounds (presetBar.removeFromLeft (160));
    presetBar.removeFromLeft (4);
    nextPresetButton.setBounds (presetBar.removeFromLeft (22));
    presetBar.removeFromLeft (8);
    presetAuthorLabel.setBounds (presetBar.removeFromLeft (100));

    deletePresetButton.setBounds (presetBar.removeFromRight (26));
    presetBar.removeFromRight (8);
    saveButton.setBounds (presetBar.removeFromRight (60));
    presetBar.removeFromRight (8);
    pasteButton.setBounds (presetBar.removeFromRight (64));
    presetBar.removeFromRight (6);
    copyButton.setBounds (presetBar.removeFromRight (64));

    aboutOverlay.setBounds (getLocalBounds());
    optionsOverlay.setBounds (getLocalBounds());
    presetBrowser.setBounds (getLocalBounds());

    rebuildBackgroundCache();
}

void SuppressorEditorContent::layoutBandRow (BandPanel& bp, juce::Rectangle<int> area)
{
    const int top = area.getY();

    // Bouton SOLO -- sous le tag LOW/MID/HIGH, dans la meme colonne de
    // gauche (avant le 1er divider a l'offset 97), assez grand pour rester
    // lisible (voir SoloSwitch.h : LED + levier switch_metal.png + libelle).
    bp.soloSwitch->setBounds (area.getX() + 14, top + 34, 68, 90);

    // Hauteur +9px (etait 36) pour loger la rangee Expand dediee, MEME
    // taille de diode que la rangee compression (demande explicite) donc
    // plus de place requise que l'ancienne lecture numerique -- le haut ne
    // bouge pas (top+17, juste sous le label "GAIN REDUCTION / RATIO" a
    // top+4..16), la croissance se fait vers le bas (jusqu'a top+62).
    bp.grMeter->setBounds (area.getX() + 105, top + 17, 280, 45);

    // Above/Below/Both, a gauche d'Optical (demande explicite) -- rangee
    // decalee a y=64 (etait 61, +3px pour laisser un peu d'air sous le GR
    // meter agrandi ci-dessus), toujours avec de la marge avant le
    // RatioSelector (y93+).
    bp.aboveBelowButton.setBounds (area.getX() + 105, top + 64, 64, 24);
    bp.opticalButton.setBounds  (area.getX() + 175, top + 64, 76, 24);
    bp.britishButton.setBounds  (area.getX() + 257, top + 64, 76, 24);
    bp.sidechainToggle.setBounds (area.getX() + 341, top + 64, 44, 24);

    bp.ratioSelector.setBounds (area.getX() + 105, top + 93, 280, 44);

    // AUDIO HP et DETECTOR LINK retires de la mise en page (caches, voir
    // buildBandPanel()) -- DIST 2/DIST 3 remontent d'une rangee chacun, ce
    // qui degage toute la moitie basse de la zone pour agrandir le graphe XY.
    bp.detectorHpToggle.setBounds   (area.getX() + 410, top + 21, 86, 24);
    bp.audioDist2Toggle.setBounds   (area.getX() + 502, top + 21, 86, 24);
    bp.audioDist3Toggle.setBounds   (area.getX() + 502, top + 49, 86, 24);

    // BAND MID : BELL au-dessus de BELL 2 (plus logique -- le 1er point
    // avant le 2e), inverse par rapport a l'emplacement HP/LP+BELL des
    // autres bandes. detectorBell2Toggle n'est visible que sur MID (voir
    // buildBandPanel()), donc ce test reste sans effet sur Low/High.
    if (bp.detectorBell2Toggle.isVisible())
    {
        bp.detectorBellToggle.setBounds  (area.getX() + 410, top + 21, 86, 24);
        bp.detectorBell2Toggle.setBounds (area.getX() + 410, top + 49, 86, 24);
    }
    else
    {
        bp.detectorBellToggle.setBounds  (area.getX() + 410, top + 49, 86, 24);
        bp.detectorBell2Toggle.setBounds (area.getX() + 410, top + 21, 86, 24);
    }

    // Graphe XY agrandi, centre dans la colonne Detector/Audio (dividers a
    // +389 et +609, voir rebuildBackgroundCache()) -- ne doit PAS deborder
    // sur la ligne blanche de droite (+609).
    bp.filterGraph->setBounds (area.getX() + 399, top + 81, 200, 56);

    auto layoutKnob = [] (KnobControl& knob, int cx, int cy, int diameter)
    {
        knob.slider.setBounds (cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        const int lw = 32, lh = 12;
        knob.label.setBounds (cx - diameter / 2 + 2, cy + diameter / 2 - lh - 2, lw, lh);
    };

    // Pas de knob resserre (155 au lieu de 170, MEME diametre 124) pour
    // degager plus de place a droite pour les faders Mix/Width.
    layoutKnob (bp.inputKnob,   area.getX() + 699,  top + 69, 124);
    layoutKnob (bp.attackKnob,  area.getX() + 854,  top + 69, 124);
    layoutKnob (bp.releaseKnob, area.getX() + 1009, top + 69, 124);
    layoutKnob (bp.outputKnob,  area.getX() + 1164, top + 69, 124);

    // Faders Mix + Width, a droite du dernier knob (Output), au-dela de la
    // ligne blanche -- inset vertical/horizontal pour ne PAS chevaucher les
    // vis de coin de la plaque (renderCornerScrews, inset 15 depuis chaque
    // coin), mais aussi larges que possible dans l'espace regagne ci-dessus.
    auto layoutFader = [] (FaderControl& fader, juce::Rectangle<int> slot)
    {
        constexpr int endLabelH = 11;
        fader.topLabel.setBounds (slot.removeFromTop (endLabelH));
        fader.bottomLabel.setBounds (slot.removeFromBottom (endLabelH));
        fader.slider.setBounds (slot.reduced (4, 2));
    };

    // Plus etroits (34px, au lieu de la moitie de la zone) et plus hauts
    // (top+5..top+139, quasi toute la plaque) -- l'inset horizontal les
    // garde bien clairs des vis de coin (voir renderCornerScrews).
    constexpr int faderW = 34, faderGap = 14;
    const int faderTotalW = faderW * 2 + faderGap;
    const int faderZoneX = area.getX() + 1240, faderZoneW = 1370 - 1240;
    const int faderStartX = faderZoneX + (faderZoneW - faderTotalW) / 2;
    const int faderTop = top + 5, faderH = 134;

    layoutFader (bp.mixFader,   { faderStartX, faderTop, faderW, faderH });
    layoutFader (bp.widthFader, { faderStartX + faderW + faderGap, faderTop, faderW, faderH });
}

void SuppressorEditorContent::rebuildBackgroundCache()
{
    const int w = getWidth();
    const int h = getHeight();

    backgroundCache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (backgroundCache);

    SuppressorLookAndFeel::renderMetalPanel (g, backgroundCache.getBounds().toFloat());

    // --- Barre de controle globale ---
    SuppressorLookAndFeel::renderCornerScrews (g, controlBarBounds, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (controlBarBounds.reduced (0.5f), 6.0f, 1.0f);
    {
        const float barTop = controlBarBounds.getY(), barBottom = controlBarBounds.getBottom();
        SuppressorLookAndFeel::renderDividerLine (g, 164.0f,  barTop + 7.0f, barBottom - 7.0f);
        SuppressorLookAndFeel::renderDividerLine (g, 838.0f,  barTop + 7.0f, barBottom - 7.0f); // apres les VU, avant Link In/Out
        SuppressorLookAndFeel::renderDividerLine (g, 944.0f,  barTop + 7.0f, barBottom - 7.0f); // apres Link In/Out, avant Xover
        SuppressorLookAndFeel::renderDividerLine (g, 1308.0f, barTop + 7.0f, barBottom - 7.0f); // apres Xover + Wide/Linear + engrenage, avant Power

        for (int cx : { 1006, 1134 })
            SuppressorLookAndFeel::renderKnobSocket (g, { (float) cx, barTop + 69.0f }, 63.0f);
    }

    // --- Plaque THD seule (plaque ARRIERE du rack -- chassis metal sombre
    // d'origine, inchange). Le plastique blanc brillant est sur la FACE
    // AVANT du module THD Valve lui-meme, voir ThdValveModule::rebuildStaticCache().
    SuppressorLookAndFeel::renderCornerScrews (g, vuPlateBounds, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (vuPlateBounds.reduced (0.5f), 6.0f, 1.0f);

    // --- 3 plaques de bande ---
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto& pb = bandPanelBounds[(size_t) band];
        SuppressorLookAndFeel::renderCornerScrews (g, pb, 15.0f, 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (pb.reduced (0.5f), 6.0f, 1.0f);

        const float top = pb.getY();
        SuppressorLookAndFeel::renderDividerLine (g, pb.getX() + 97.0f,  top + 7.0f,  top + 55.0f);
        SuppressorLookAndFeel::renderDividerLine (g, pb.getX() + 97.0f,  top + 79.0f, top + 137.0f);
        SuppressorLookAndFeel::renderDividerLine (g, pb.getX() + 389.0f, top + 7.0f,  top + 137.0f);
        SuppressorLookAndFeel::renderDividerLine (g, pb.getX() + 609.0f, top + 7.0f,  top + 137.0f);
        SuppressorLookAndFeel::renderDividerLine (g, pb.getX() + 1229.0f, top + 7.0f, top + 137.0f);

        for (int cx : { 699, 854, 1009, 1164 })
            SuppressorLookAndFeel::renderKnobSocket (g, { pb.getX() + (float) cx, top + 69.0f }, 63.0f);
    }
}

void SuppressorEditorContent::paint (juce::Graphics& g)
{
    if (backgroundCache.isValid())
        g.drawImageAt (backgroundCache, 0, 0);
    else
        SuppressorLookAndFeel::renderMetalPanel (g, getLocalBounds().toFloat());

    const int barTop = (int) controlBarBounds.getY();

    // Logo "SUPPRESSOR" (LogoSuppressor.png, remplace le wordmark texte) --
    // ajuste par CONTIENT (aspect ratio preserve) dans le meme espace que
    // l'ancien texte, aligne a gauche.
    auto drawLogo = [&g] (const void* data, int dataSize, juce::Rectangle<int> box, juce::Justification just = juce::Justification::centredLeft)
    {
        auto img = juce::ImageCache::getFromMemory (data, dataSize);
        if (img.isNull())
            return;
        const float scale = juce::jmin ((float) box.getWidth() / (float) img.getWidth(),
                                         (float) box.getHeight() / (float) img.getHeight());
        const int w = juce::roundToInt (img.getWidth() * scale);
        const int h = juce::roundToInt (img.getHeight() * scale);
        auto dest = juce::Rectangle<int> (w, h);
        dest = just.testFlags (juce::Justification::horizontallyCentred) ? dest.withCentre (box.getCentre())
             : dest.withX (box.getX()).withCentre ({ dest.getCentreX(), box.getCentreY() });
        g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(), 0, 0, img.getWidth(), img.getHeight());
    };

    // Logo SUPPRESSOR desormais dessine par SuppressorLogoButton (vrai
    // composant cliquable, ouvre About) -- plus dessine ici.

    g.setColour (SuppressorColours::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    g.drawFittedText ("POWER", { 1313, barTop + 4, 70, 12 }, juce::Justification::centred, 1);

    static const std::tuple<const char*, int> bandLogos[kNumBands] =
    {
        { BinaryData::LogoLow_png,  BinaryData::LogoLow_pngSize },
        { BinaryData::LogoMid_png,  BinaryData::LogoMid_pngSize },
        { BinaryData::LogoHigh_png, BinaryData::LogoHigh_pngSize },
    };

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto& pb = bandPanelBounds[(size_t) band];
        const int x = (int) pb.getX();
        const int top = (int) pb.getY();

        // Logo LOW/MID/HIGH (remplace bandTagLabel) -- encore agrandi et
        // decale (bas + droite) pour degager largement la vis de coin
        // (voir renderCornerScrews, inset 15 rayon 5). Meme taille de
        // boite que le logo THD (voir ThdValveModule::resized()).
        drawLogo (std::get<0> (bandLogos[band]), std::get<1> (bandLogos[band]), { x + 34, top + 18, 104, 32 });

        g.setColour (SuppressorColours::textBright);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("GAIN REDUCTION / RATIO", { x + 105, top + 4, 280, 12 }, juce::Justification::centred, 1);

        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("DETECTOR", { x + 410, top + 4, 86, 14 }, juce::Justification::centred, 1);
        g.drawFittedText ("COLOR",    { x + 502, top + 4, 86, 14 }, juce::Justification::centred, 1);
    }
}

// ============================================================================
// Presets -- meme mecanisme que Nuke Mext (voir PluginEditor.h::PresetData) :
// une barre rapide (Prev/Next/nom/Copy/Paste/Save/Delete) + un navigateur
// plein ecran (PresetBrowserOverlay). Presets d'usine propres au moteur
// Suppressor (Ratio/Attack/Release/Detector HP+Bell+Link/Audio Dist2+Dist3/
// Optical/British/Mix/Width), pas ceux de Mext qui ne correspondent a rien
// ici (moteur DSP different).
// ============================================================================

void SuppressorEditorContent::setParamValue (const juce::String& paramID, float rawValue)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
}

void SuppressorEditorContent::rampParamTo (const juce::String& paramID, float rawValue, int durationMs)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        knobRamper.rampTo (p, p->convertTo0to1 (rawValue), durationMs);
}

void SuppressorEditorContent::buildPresetList()
{
    // Presets PORTES DEPUIS NUKE MEXT (memes noms, memes valeurs Input/
    // Ratio/Attack/Release/Output/Xover/Linear que le PluginEditor.cpp de
    // Mext, voir buildPresetList() la-bas) -- demande explicite de
    // l'utilisateur ("le meme compresseur, je veux les memes presets").
    //
    // Mext et Suppressor n'ont PAS le meme jeu de parametres (Mext a
    // softKnee/colour/sidechain EQ Freq-Gain-Q/Limit/Low-High Cut, absents
    // ici ; Suppressor a Detector HP/Bell/Link, Audio Dist2/Dist3, Optical/
    // British, Mix/Width, absents de Mext) -- seuls les champs QUI EXISTENT
    // REELLEMENT DANS LES DEUX moteurs sont portes : Input, Ratio, Attack,
    // Release, Output par bande, plus Crossover Lo/Hi, mode WIDE/LINEAR, et
    // Soft Clip (equivalent du Clip Stage de Mext). Le reste (Optical/
    // British/Dist2-3/Detector/Mix/Width) reste a sa valeur neutre par
    // defaut, faute d'equivalent chez Mext.
    auto makeBand = [] (float input, int ratio, float attack, float release, float output)
    {
        PresetData::Band b;
        b.input = input; b.ratioIndex = ratio; b.attack = attack; b.release = release; b.output = output;
        return b;
    };

    // Table de ratios Mext (0..8 : 1.1/2.1/3.1/4.1/6.1/8.1/10.1/20.1/NUKE)
    // -> index le plus proche dans la table Suppressor actuelle (0..6 :
    // 2/3/4/6/8/10/20, voir PluginProcessor.h::ratioTable -- NUKE retire,
    // se rabat sur 20:1, le cran le plus extreme restant).
    auto mapRatio = [] (int mextIndex)
    {
        static constexpr int table[9] = { 0, 0, 1, 2, 3, 4, 5, 6, 6 };
        return table[juce::jlimit (0, 8, mextIndex)];
    };

    auto addPreset = [this] (PresetData p) { presets.push_back (std::move (p)); };

    {
        // "Default" -- vraies valeurs d'USINE (celles de createParameterLayout()
        // dans PluginProcessor.cpp), pas juste des zeros approximatifs --
        // demande explicite : selectionner ce preset doit remettre le
        // plugin exactement dans son etat de sortie d'usine, HP inclus
        // (50Hz Low/Mid, 16000Hz High -- pas le meme sur les 3 bandes).
        PresetData p; p.name = "Default"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.linearMode = false; p.softClipOn = true; p.thdMode = 0;
        // Range Expand a plat 0.0f (demande explicite : "les ranges des
        // expand doivent etre a 0 a l'ouverture du plug in") -- deja le
        // defaut du champ PresetData::Band::rangeExpand, Band{} suffit.
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& band = p.bands[b];
            band = PresetData::Band {};
            band.detectorHpFreq = (b == SuppressorAudioProcessor::High) ? 16000.0f : 50.0f;
            band.detectorHpSlope = 8;
        }
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f; p.softClipOn = true;
        p.bands[0] = makeBand (0.0f, mapRatio (3), 27.927f, 306.9f, 6.91f);
        p.bands[1] = makeBand (0.0f, mapRatio (3), 10.971f, 204.3f, 6.91f);
        p.bands[2] = makeBand (0.0f, mapRatio (2), 5.891f,  106.1f, 4.03f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Linear"; p.crossoverLow = 125.0f; p.crossoverHigh = 2930.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.29f,  mapRatio (3), 27.927f, 50.0f,  8.93f);
        p.bands[1] = makeBand (-6.62f, mapRatio (3), 10.971f, 50.0f,  8.93f);
        p.bands[2] = makeBand (-3.17f, mapRatio (2), 5.891f,  106.1f, 6.05f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Metal"; p.crossoverLow = 125.0f; p.crossoverHigh = 2553.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-7.79f, mapRatio (2), 23.261f, 50.0f, 13.42f);
        p.bands[1] = makeBand (-8.34f, mapRatio (2), 20.473f, 50.0f, 11.58f);
        p.bands[2] = makeBand (-8.65f, mapRatio (2), 13.770f, 50.0f, 11.52f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Fake OTT"; p.crossoverLow = 195.3f; p.crossoverHigh = 2521.0f; p.softClipOn = true;
        p.bands[0] = makeBand (25.08f, mapRatio (6), 13.272f, 50.0f, 9.09f);
        p.bands[1] = makeBand (19.03f, mapRatio (6), 19.171f, 50.0f, 9.09f);
        p.bands[2] = makeBand (19.59f, mapRatio (6), 8.242f,  50.0f, 9.09f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Kick"; p.crossoverLow = 89.1f; p.crossoverHigh = 2653.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (8.64f,  mapRatio (5), 2.718f, 50.0f, 11.52f);
        p.bands[1] = makeBand (28.80f, mapRatio (4), 5.048f, 50.0f, -4.32f);
        p.bands[2] = makeBand (12.39f, mapRatio (2), 5.891f, 50.0f, 8.35f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Drumbus"; p.crossoverLow = 195.3f; p.crossoverHigh = 2521.0f; p.softClipOn = true;
        p.bands[0] = makeBand (25.08f, mapRatio (6), 13.272f, 50.0f, 13.99f);
        p.bands[1] = makeBand (11.25f, mapRatio (6), 19.171f, 50.0f, 9.09f);
        p.bands[2] = makeBand (7.21f,  mapRatio (6), 8.242f,  50.0f, 11.11f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Vocal"; p.crossoverLow = 121.5f; p.crossoverHigh = 4511.0f; p.softClipOn = true;
        p.bands[0] = makeBand (19.89f, mapRatio (3), 35.121f, 172.3f, 8.81f);
        p.bands[1] = makeBand (9.52f,  mapRatio (4), 13.519f, 85.1f,  11.11f);
        p.bands[2] = makeBand (14.99f, mapRatio (5), 2.025f,  51.6f,  11.11f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Smashed Snare"; p.crossoverLow = 225.4f; p.crossoverHigh = 3582.0f; p.softClipOn = true;
        p.bands[0] = makeBand (12.11f, mapRatio (3), 13.769f, 59.2f, 14.27f);
        p.bands[1] = makeBand (10.09f, mapRatio (5), 10.971f, 78.5f, 14.27f);
        p.bands[2] = makeBand (13.84f, mapRatio (2), 5.891f,  50.8f, 12.83f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Synth Bass"; p.crossoverLow = 125.0f; p.crossoverHigh = 1824.0f; p.softClipOn = true;
        p.bands[0] = makeBand (2.90f, mapRatio (2), 35.121f, 185.5f, 8.80f);
        p.bands[1] = makeBand (0.01f, mapRatio (3), 15.341f, 90.7f,  8.80f);
        p.bands[2] = makeBand (9.51f, mapRatio (1), 5.891f,  51.2f,  8.80f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Neuro Bass"; p.crossoverLow = 190.6f; p.crossoverHigh = 1824.0f; p.softClipOn = true;
        p.bands[0] = makeBand (2.90f, mapRatio (2), 35.121f, 185.5f, 27.82f);
        p.bands[1] = makeBand (0.01f, mapRatio (3), 15.341f, 90.7f,  21.77f);
        p.bands[2] = makeBand (9.51f, mapRatio (1), 5.891f,  51.2f,  27.82f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Boogie"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.softClipOn = true; p.linearMode = false;
        p.bands[0] = makeBand (4.31f, mapRatio (4), 27.927f, 50.0f, 1.09f);
        p.bands[1] = makeBand (0.57f, mapRatio (4), 10.971f, 50.0f, 1.66f);
        p.bands[2] = makeBand (1.15f, mapRatio (2), 5.891f,  50.0f, 4.03f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Jazz Club"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-4.90f, mapRatio (3), 27.927f, 50.0f, 5.36f);
        p.bands[1] = makeBand (-5.76f, mapRatio (3), 10.971f, 50.0f, 5.93f);
        p.bands[2] = makeBand (0.00f,  mapRatio (2), 5.891f,  50.0f, 4.27f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Rock Razor"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.86f, mapRatio (3), 27.927f, 50.0f,  2.31f);
        p.bands[1] = makeBand (1.38f, mapRatio (3), 10.971f, 50.0f,  2.65f);
        p.bands[2] = makeBand (6.05f, mapRatio (2), 5.891f,  106.1f, 2.02f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Future Bass"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.86f, mapRatio (3), 27.927f, 50.0f, 2.31f);
        p.bands[1] = makeBand (1.38f, mapRatio (3), 10.971f, 50.0f, 2.65f);
        p.bands[2] = makeBand (6.05f, mapRatio (2), 5.891f,  50.0f, 2.02f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Death Metal"; p.crossoverLow = 125.0f; p.crossoverHigh = 2553.0f;
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-8.36f, mapRatio (2), 23.261f, 50.0f, 10.95f);
        p.bands[1] = makeBand (-7.48f, mapRatio (2), 20.473f, 50.0f, 9.92f);
        p.bands[2] = makeBand (-8.65f, mapRatio (2), 13.770f, 50.0f, 8.94f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Jazz Fallout"; p.crossoverLow = 135.8f; p.crossoverHigh = 2521.0f;
        p.softClipOn = false; p.linearMode = true;
        p.bands[0] = makeBand (1.44f,  mapRatio (2), 23.997f, 77.0f, 6.73f);
        p.bands[1] = makeBand (-5.75f, mapRatio (2), 13.770f, 50.0f, 6.50f);
        p.bands[2] = makeBand (-4.61f, mapRatio (2), 2.751f,  50.0f, 3.57f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Synthwave"; p.crossoverLow = 135.8f; p.crossoverHigh = 2521.0f;
        p.softClipOn = false; p.linearMode = true;
        p.bands[0] = makeBand (1.44f,  mapRatio (2), 23.997f, 77.0f, 6.73f);
        p.bands[1] = makeBand (-5.75f, mapRatio (2), 13.770f, 50.0f, 6.50f);
        p.bands[2] = makeBand (-4.61f, mapRatio (2), 2.751f,  50.0f, 3.57f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        // Mastering Pop Rock -- valeurs EXACTES fournies par capture
        // d'ecran (mode ANALOG, XLO 121.5Hz / XHI 2521Hz) : Low 6:1
        // (index 3), Mid 6:1 (index 3), High 3:1 (index 1) dans la table
        // native Suppressor (pas de mapRatio, ce n'est PAS un preset porte
        // de Mext).
        PresetData p; p.name = "Mastering Pop Rock"; p.crossoverLow = 121.5f; p.crossoverHigh = 2521.0f;
        p.softClipOn = true; p.linearMode = false;
        p.bands[0] = makeBand (6.0f, 3, 40.0f, 50.0f, 6.0f);
        p.bands[1] = makeBand (6.0f, 3, 40.0f, 70.1f, -0.6f);
        p.bands[2] = makeBand (4.8f, 1, 40.0f, 50.0f, 0.0f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }

    // Presets UTILISATEUR (Save/Delete) geres a part, voir loadUserPresets().
    loadUserPresets();
}

void SuppressorEditorContent::loadUserPresets()
{
    auto dir = getUserPresetsDirectory();
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        PresetData p;
        if (readPresetXml (file, p))
            presets.push_back (p);
    }
}

void SuppressorEditorContent::goToPreset (int delta)
{
    if (presets.empty())
        return;

    const int newIndex = juce::jlimit (0, (int) presets.size() - 1, currentPresetIndex + delta);
    if (newIndex == currentPresetIndex)
        return;

    currentPresetIndex = newIndex;
    applyPresetData (presets[(size_t) currentPresetIndex]);
    refreshPresetDisplay();
}

void SuppressorEditorContent::syncPresetBrowserEntries()
{
    std::vector<PresetBrowserOverlay::Entry> entries;
    entries.reserve (presets.size());
    for (auto& p : presets)
        entries.push_back ({ p.name, p.author, p.isFactory });

    presetBrowser.setEntries (std::move (entries));
    presetBrowser.setCurrentIndex (currentPresetIndex);
}

void SuppressorEditorContent::openPresetBrowser()
{
    syncPresetBrowserEntries();
    presetBrowser.setBounds (getLocalBounds());
    presetBrowser.show();
}

void SuppressorEditorContent::refreshPresetDisplay()
{
    if (currentPresetIndex < 0 || currentPresetIndex >= (int) presets.size())
    {
        presetNameButton.setButtonText ("Default");
        presetAuthorLabel.setText ({}, juce::dontSendNotification);
        deletePresetButton.setEnabled (false);
        return;
    }

    const auto& p = presets[(size_t) currentPresetIndex];
    presetNameButton.setButtonText (p.name);
    presetAuthorLabel.setText (p.author, juce::dontSendNotification);
    deletePresetButton.setEnabled (! p.isFactory);
}

void SuppressorEditorContent::applyPresetData (const PresetData& preset)
{
    // Knobs continus -- ANIMES (voir ParamRamper/rampParamTo()) pour que
    // les knobs tournent visiblement au lieu de teleporter (demande
    // explicite). Tout le reste (booleens/discrets : ratio, toggles
    // Detector/Audio, Mix/Width...) reste instantane, pas d'equivalent
    // visuel a animer.
    rampParamTo (SuppressorAudioProcessor::idCrossoverLow,  preset.crossoverLow);
    rampParamTo (SuppressorAudioProcessor::idCrossoverHigh, preset.crossoverHigh);
    setParamValue (SuppressorAudioProcessor::idDigitalCrossover, preset.linearMode ? 1.0f : 0.0f);
    setParamValue (SuppressorAudioProcessor::idSoftClip, preset.softClipOn ? 1.0f : 0.0f);
    setParamValue (SuppressorAudioProcessor::idThdMode, (float) preset.thdMode);

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SuppressorAudioProcessor::bandParamId (base, band); };
        const auto& bp = preset.bands[band];

        rampParamTo (id (SuppressorAudioProcessor::idInput), bp.input);
        setParamValue (id (SuppressorAudioProcessor::idRatioIndex), (float) bp.ratioIndex);
        rampParamTo (id (SuppressorAudioProcessor::idAttack), bp.attack);
        rampParamTo (id (SuppressorAudioProcessor::idRelease), bp.release);
        rampParamTo (id (SuppressorAudioProcessor::idOutput), bp.output);

        // Above/Below/Both + jeu Expand dedie (demande explicite :
        // "recalibre les valeurs range").
        setParamValue (id (SuppressorAudioProcessor::idAboveBelow), (float) bp.aboveBelow);
        rampParamTo (id (SuppressorAudioProcessor::idInputExpand), bp.rangeExpand);
        setParamValue (id (SuppressorAudioProcessor::idRatioIndexExpand), (float) bp.ratioIndexExpand);
        rampParamTo (id (SuppressorAudioProcessor::idAttackExpand), bp.attackExpand);
        rampParamTo (id (SuppressorAudioProcessor::idReleaseExpand), bp.releaseExpand);
        rampParamTo (id (SuppressorAudioProcessor::idOutputExpand), bp.outputExpand);

        setParamValue (id (SuppressorAudioProcessor::idDetectorHp), bp.detectorHp ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idDetectorHpFreq), bp.detectorHpFreq);
        setParamValue (id (SuppressorAudioProcessor::idDetectorHpSlope), (float) bp.detectorHpSlope);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBell), bp.detectorBell ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBellFreq), bp.detectorBellFreq);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBellGain), bp.detectorBellGain);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBell2), bp.detectorBell2 ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBell2Freq), bp.detectorBell2Freq);
        setParamValue (id (SuppressorAudioProcessor::idDetectorBell2Gain), bp.detectorBell2Gain);
        setParamValue (id (SuppressorAudioProcessor::idDetectorLink), bp.detectorLink ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idSidechainEnable), bp.sidechainEnable ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idAudioHp), bp.audioHp ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idAudioDist2), bp.audioDist2 ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idAudioDist3), bp.audioDist3 ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idOptical), bp.optical ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idBritish), bp.british ? 1.0f : 0.0f);
        setParamValue (id (SuppressorAudioProcessor::idBandMix), bp.bandMix);
        setParamValue (id (SuppressorAudioProcessor::idBandWidth), bp.bandWidth);
    }
}

SuppressorEditorContent::PresetData SuppressorEditorContent::captureCurrentPresetData (
    const juce::String& name, const juce::String& author, bool isFactory)
{
    PresetData p;
    p.name = name;
    p.author = author;
    p.isFactory = isFactory;

    p.crossoverLow  = processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idCrossoverLow)->load();
    p.crossoverHigh = processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idCrossoverHigh)->load();
    p.linearMode    = processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idDigitalCrossover)->load() > 0.5f;
    p.softClipOn    = processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idSoftClip)->load() > 0.5f;
    p.thdMode       = (int) std::round (processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idThdMode)->load());

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SuppressorAudioProcessor::bandParamId (base, band); };
        auto& bp = p.bands[band];

        bp.input           = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idInput))->load();
        bp.ratioIndex       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idRatioIndex))->load());
        bp.attack           = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAttack))->load();
        bp.release          = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idRelease))->load();
        bp.output           = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idOutput))->load();

        bp.aboveBelow       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAboveBelow))->load());
        bp.rangeExpand      = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idInputExpand))->load();
        bp.ratioIndexExpand = (int) std::round (processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idRatioIndexExpand))->load());
        bp.attackExpand     = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAttackExpand))->load();
        bp.releaseExpand    = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idReleaseExpand))->load();
        bp.outputExpand     = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idOutputExpand))->load();

        bp.detectorHp       = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorHp))->load() > 0.5f;
        bp.detectorHpFreq   = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorHpFreq))->load();
        bp.detectorHpSlope  = (int) std::round (processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorHpSlope))->load());
        bp.detectorBell     = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBell))->load() > 0.5f;
        bp.detectorBellFreq = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBellFreq))->load();
        bp.detectorBellGain = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBellGain))->load();
        bp.detectorBell2     = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBell2))->load() > 0.5f;
        bp.detectorBell2Freq = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBell2Freq))->load();
        bp.detectorBell2Gain = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorBell2Gain))->load();
        bp.detectorLink     = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idDetectorLink))->load() > 0.5f;
        bp.sidechainEnable  = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idSidechainEnable))->load() > 0.5f;
        bp.audioHp          = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAudioHp))->load() > 0.5f;
        bp.audioDist2       = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAudioDist2))->load() > 0.5f;
        bp.audioDist3       = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idAudioDist3))->load() > 0.5f;
        bp.optical          = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idOptical))->load() > 0.5f;
        bp.british          = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idBritish))->load() > 0.5f;
        bp.bandMix          = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idBandMix))->load();
        bp.bandWidth        = processorRef.apvts.getRawParameterValue (id (SuppressorAudioProcessor::idBandWidth))->load();
    }

    return p;
}

void SuppressorEditorContent::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Enter a name and creator for this preset:",
                                       juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("presetName", "My Preset", "Name");
    aw->addTextEditor ("presetAuthor", "", "Creator");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            if (result == 1)
            {
                const auto name = aw->getTextEditorContents ("presetName").trim();
                if (name.isNotEmpty())
                {
                    // "Side Key" reste reserve aux presets d'usine -- un
                    // preset utilisateur qui porterait ce nom serait trompeur.
                    const auto safeName = name.equalsIgnoreCase ("Side Key") ? name + " (mine)" : name;

                    auto author = aw->getTextEditorContents ("presetAuthor").trim();
                    if (author.isEmpty())
                        author = "User";
                    else if (author.equalsIgnoreCase ("Side Key"))
                        author += " (mine)";

                    auto newPreset = captureCurrentPresetData (safeName, author, false);
                    writePresetXml (newPreset, getUserPresetsDirectory().getChildFile (safeName + ".xml"));

                    presets.push_back (newPreset);
                    currentPresetIndex = (int) presets.size() - 1;
                    refreshPresetDisplay();
                }
            }
        }), true); // true = JUCE supprime la fenetre automatiquement apres le callback
}

void SuppressorEditorContent::promptDeletePreset()
{
    if (currentPresetIndex < 0 || currentPresetIndex >= (int) presets.size())
        return;

    const auto& p = presets[(size_t) currentPresetIndex];
    if (p.isFactory)
        return; // securite -- le bouton est de toute facon desactive pour un preset Side Key

    const auto name = p.name;
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::WarningIcon, "Delete Preset",
        "Delete preset \"" + name + "\"? This cannot be undone.", "Delete", "Cancel");

    juce::AlertWindow::showAsync (options, [this, name] (int result)
    {
        if (result != 1)
            return;

        getUserPresetsDirectory().getChildFile (name + ".xml").deleteFile();

        const auto it = std::find_if (presets.begin(), presets.end(),
                                       [&name] (const PresetData& p) { return p.name == name; });
        if (it != presets.end())
            presets.erase (it);

        currentPresetIndex = presets.empty() ? -1 : juce::jlimit (0, (int) presets.size() - 1, currentPresetIndex);
        if (currentPresetIndex >= 0)
            applyPresetData (presets[(size_t) currentPresetIndex]);
        refreshPresetDisplay();
    });
}

// Copy/Paste : serialise tout l'etat APVTS (tous les parametres, comme un
// mini-preset) via le presse-papier systeme -- permet de coller les
// reglages d'une instance a une autre, ou entre pistes/sessions.
void SuppressorEditorContent::copyStateToClipboard()
{
    juce::SystemClipboard::copyTextToClipboard (processorRef.apvts.copyState().toXmlString());
}

void SuppressorEditorContent::pasteStateFromClipboard()
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (text.isEmpty())
        return;

    auto xml = juce::XmlDocument::parse (text);
    if (xml == nullptr)
        return;

    auto newState = juce::ValueTree::fromXml (*xml);
    if (! newState.isValid())
        return;

    processorRef.apvts.replaceState (newState);
    currentPresetIndex = -1;
    refreshPresetDisplay();
}
