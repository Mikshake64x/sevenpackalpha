#include "PluginEditor.h"

static constexpr int kNumBands = (int) SixpackAudioProcessor::NumBands;

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
                       .getChildFile ("SideKeySystem").getChildFile ("Sixpack").getChildFile ("Presets");
        dir.createDirectory();
        return dir;
    }

    // Migration des presets d'usine HERITES DE TRIPRESSOR (3 bandes, 2
    // crossovers Low/High) vers sixpack (5 bandes, 4 crossovers) : les 2
    // frequences d'origine deviennent les bornes EXTERIEURES (Low/LowMid et
    // HighMid/High), l'ancienne plage "Mid" est subdivisee log-uniformement
    // en 3 pour loger LowMid/Mid/HighMid. Ces presets n'ont pas encore ete
    // retunes pour les 2 nouvelles bandes intermediaires (voir buildPresetList()
    // -- LowMid/HighMid restent a leurs valeurs neutres par defaut) ; sur
    // quelques presets extremes, la frequence d'origine peut aussi deborder
    // legerement des nouvelles bornes Crossover::kSplitMinHz/MaxHz[0]/[3] et
    // se retrouver clampee -- acceptable pour une premiere migration, a
    // affiner a la main une fois le nouvel editeur de crossover en place.
    std::array<float, (size_t) SixpackAudioProcessor::NumCrossovers> legacyCrossoverHz (float lowHz, float highHz)
    {
        const double lo = juce::jlimit ((double) Crossover::kSplitMinHz[0], (double) Crossover::kSplitMaxHz[0], (double) lowHz);
        const double hi = juce::jlimit ((double) Crossover::kSplitMinHz[4], (double) Crossover::kSplitMaxHz[4], (double) highHz);
        // 5 points (lo..hi), donc 4 pas geometriques -- etait 1/3 (4 points/
        // 3 pas) avant l'ajout de la 6e bande "Presence", corrige en
        // consequence pour continuer a remplir EXACTEMENT les 5 elements du
        // tableau (un 5e point manquant restait sinon a 0.0f par defaut).
        const double ratio = std::pow (hi / lo, 1.0 / 4.0);
        return { { (float) lo, (float) (lo * ratio), (float) (lo * ratio * ratio), (float) (lo * ratio * ratio * ratio), (float) hi } };
    }

    void writePresetXml (const SixpackEditorContent::PresetData& p, const juce::File& file)
    {
        juce::XmlElement xml ("SixpackPreset");
        xml.setAttribute ("name", p.name);
        xml.setAttribute ("author", p.author);
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
        {
            xml.setAttribute ("crossoverHz" + juce::String (x), (double) p.crossoverHz[(size_t) x]);
            xml.setAttribute ("crossoverEnabled" + juce::String (x), p.crossoverEnabled[(size_t) x]);
        }
        xml.setAttribute ("linearMode", p.linearMode);
        xml.setAttribute ("softClipOn", p.softClipOn);
        xml.setAttribute ("thdMode", p.thdMode);

        for (int b = 0; b < kNumBands; ++b)
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

    bool readPresetXml (const juce::File& file, SixpackEditorContent::PresetData& out)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || xml->getTagName() != "SixpackPreset")
            return false;

        out.name          = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        out.author        = xml->getStringAttribute ("author", "User");
        out.isFactory      = false;
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
        {
            out.crossoverHz[(size_t) x] = (float) xml->getDoubleAttribute ("crossoverHz" + juce::String (x), (double) Crossover::kSplitDefaultHz[x]);
            out.crossoverEnabled[(size_t) x] = xml->getBoolAttribute ("crossoverEnabled" + juce::String (x), true);
        }
        out.linearMode    = xml->getBoolAttribute ("linearMode", false);
        out.softClipOn    = xml->getBoolAttribute ("softClipOn", true);
        out.thdMode       = xml->getIntAttribute ("thdMode", 0);

        for (auto* bandXml = xml->getChildByName ("Band"); bandXml != nullptr;
             bandXml = bandXml->getNextElementWithTagName ("Band"))
        {
            const int idx = bandXml->getIntAttribute ("index", 0);
            if (idx < 0 || idx >= kNumBands)
                continue;

            auto& bp = out.bands[idx];
            bp.input            = (float) bandXml->getDoubleAttribute ("input", 0.0);
            bp.ratioIndex        = bandXml->getIntAttribute ("ratioIndex", 3);
            bp.attack            = (float) bandXml->getDoubleAttribute ("attack", SixpackAudioProcessor::defaultAttackMs);
            bp.release           = (float) bandXml->getDoubleAttribute ("release", SixpackAudioProcessor::defaultReleaseMs);
            bp.output            = (float) bandXml->getDoubleAttribute ("output", 0.0);
            bp.aboveBelow        = bandXml->getIntAttribute ("aboveBelow", 0);
            bp.rangeExpand       = (float) bandXml->getDoubleAttribute ("rangeExpand", 0.0);
            bp.ratioIndexExpand  = bandXml->getIntAttribute ("ratioIndexExpand", 0);
            bp.attackExpand      = (float) bandXml->getDoubleAttribute ("attackExpand", SixpackAudioProcessor::defaultAttackMs);
            bp.releaseExpand     = (float) bandXml->getDoubleAttribute ("releaseExpand", SixpackAudioProcessor::defaultReleaseMs);
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

SixpackEditorContent::SixpackEditorContent (SixpackAudioProcessor& p)
    : processorRef (p)
{
    // Opaque -- paint() couvre deja tout le fond (backgroundCache ou
    // renderMetalPanel, voir plus bas), donc pas besoin que JUCE efface
    // en dessous. Aide aussi a eviter le clignotement blanc au
    // redimensionnement (voir SixpackAudioProcessorEditor::setOpaque).
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    // Theme deja applique aux globales SixpackColours AVANT que ce
    // constructeur (et tous les membres de cette classe) ne commence a
    // tourner -- voir le membre sentinelle juste avant `content` dans
    // SixpackAudioProcessorEditor (PluginEditor.h) et
    // SixpackLookAndFeel::applyGlobalTheme(). `lookAndFeel` (juste
    // au-dessus) a deja capture la bonne teinte a SA construction via
    // refreshFixedColours(), rien d'autre a faire ici.

    // --- Barre de controle globale ---
    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SixpackAudioProcessor::idPower, powerButton);

    // A/B : bascule persistante entre 2 jeux de reglages (voir
    // SixpackAudioProcessor::toggleABCompare()/selectABSlot()) -- pas de
    // ButtonAttachment (etat gere par le processeur, pas un parametre APVTS).
    addAndMakeVisible (abButton);
    abButton.setIsB (processorRef.isUsingSlotB());
    abButton.onSelect = [this] (bool wantB)
    {
        processorRef.selectABSlot (wantB);
        abButton.setIsB (processorRef.isUsingSlotB());
    };

    // Spectre + 4 lignes de crossover draggables/activables + selection de
    // bande (remplace les anciens knobs XLO/XHI de Tripressor) -- voir
    // CrossoverSpectrumEditor.h.
    addAndMakeVisible (crossoverSpectrum);
    crossoverSpectrum.setSelectedBand (selectedBand);
    crossoverSpectrum.onBandSelected = [this] (int band)
    {
        selectedBand = band;
        refreshBandPanelVisibility();
    };

    addAndMakeVisible (xoverModeButton);
    xoverModeButton.setAnalogTooltip ("Crossover 1er ordre, 0 latence (comportement par defaut).");
    xoverModeButton.setLinearTooltip ("Crossover FIR a phase lineaire, latence reglable (icone engrenage). Tres gourmand en CPU.");
    xoverModeButton.onSelect = [this] (XoverModeButton::Mode mode)
    {
        auto* linearParam = processorRef.apvts.getParameter (SixpackAudioProcessor::idDigitalCrossover);
        if (linearParam == nullptr)
            return;

        const bool wantLinear = mode == XoverModeButton::Mode::Linear;
        const bool alreadyLinear = linearParam->convertFrom0to1 (linearParam->getValue()) > 0.5f;

        if (! wantLinear || alreadyLinear)
        {
            linearParam->setValueNotifyingHost (wantLinear ? 1.0f : 0.0f);
            return;
        }

        // Avertissement CPU en passant ANALOG -> LINEAR (retour utilisateur
        // explicite) -- meme mecanisme OK/Cancel que le CPU Budget LINEAR
        // (onLatencySelected plus haut) : Cancel laisse le bouton sur
        // ANALOG (rien n'a encore ete ecrit au parametre).
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, "Higher CPU usage",
            "LINEAR crossover uses a linear-phase FIR filter, which is significantly more "
            "CPU-intensive than ANALOG. An octa-core CPU is strongly recommended. Continue?",
            "Continue", "Cancel");

        juce::AlertWindow::showAsync (options, [this, linearParam] (int alertResult)
        {
            if (alertResult == 1) // "Continue" est le 1er bouton ajoute
                linearParam->setValueNotifyingHost (1.0f);
            // Cancel : rien a faire, le Timer interne de xoverModeButton
            // relit le parametre (toujours ANALOG) et remet le bouton
            // visuellement a sa place tout seul.
        });
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

    // --- 6 bandes -- progression spectrale cyan (Low) -> rouge (High),
    // Presence (nouvelle 6e bande, 1-5kHz) inseree en violet entre
    // High-Mid (orange) et High (rouge) pour rester visuellement distincte
    // de ses 2 voisines. ---
    static const juce::Colour bandAccents[kNumBands] = { SixpackColours::ledCyan, SixpackColours::ledGreen, SixpackColours::ledAmber, SixpackColours::ledOrange, SixpackColours::ledPurple, SixpackColours::ledRed };
    static const char* bandTags[kNumBands] = { "LOW", "LOW-MID", "MID", "HIGH-MID", "PRESENCE", "HIGH" };
    for (int band = 0; band < kNumBands; ++band)
        buildBandPanel (band, bandAccents[band], bandTags[band]);
    refreshBandPanelVisibility(); // n'affiche que bandPanels[selectedBand] -- voir doc de classe BandPanel

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

    addAndMakeVisible (thdHeader);
    addAndMakeVisible (bandMetersRack);
    bandMetersRack.onBandSelectRequested = [this] (int band)
    {
        selectedBand = band;
        refreshBandPanelVisibility();
    };
    softClipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SixpackAudioProcessor::idSoftClip, thdHeader.engageSwitch);

    // 4 boutons de mode THD (SOFT/TAPE/IRON/AIR, voir ThdHeaderModule) --
    // radio exclusif sur idThdMode (0..3), TOUJOURS exactement un actif
    // (SOFT = comportement d'origine, plus un etat "rien de selectionne").
    // Meme mecanique que RatioSelector (ParameterAttachment manuel, pas de
    // ButtonAttachment -- ce n'est pas un booleen).
    if (auto* thdModeParamPtr = processorRef.apvts.getParameter (SixpackAudioProcessor::idThdMode))
    {
        thdModeAttachment = std::make_unique<juce::ParameterAttachment> (
            *thdModeParamPtr,
            [this] (float v)
            {
                const int mode = (int) std::round (v);
                thdHeader.softToggle.setToggleState (mode == 0, juce::dontSendNotification);
                thdHeader.tapeToggle.setToggleState (mode == 1, juce::dontSendNotification);
                thdHeader.ironToggle.setToggleState (mode == 2, juce::dontSendNotification);
                thdHeader.airToggle.setToggleState  (mode == 3, juce::dontSendNotification);
            },
            nullptr);

        auto wireThdModeButton = [this] (ToggleSwitch& btn, int modeIndex)
        {
            btn.onClick = [this, modeIndex] { thdModeAttachment->setValueAsCompleteGesture ((float) modeIndex); };
        };
        wireThdModeButton (thdHeader.softToggle, 0);
        wireThdModeButton (thdHeader.tapeToggle, 1);
        wireThdModeButton (thdHeader.ironToggle, 2);
        wireThdModeButton (thdHeader.airToggle, 3);

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
    // Theme SOMBRE/CLAIR -- retour utilisateur explicite : "je souhaite
    // faire un design plus clair qu'on peut changer dans l'engrenage".
    // Persiste dans processorRef.lightTheme (meme pattern que uiScale),
    // repousse vers le LookAndFeel partage, puis invalide le cache de
    // fond statique (rebuildBackgroundCache() -- renderMetalPanel() lit
    // SixpackLookAndFeel::isLightThemeStatic()) et repaint tout.
    optionsOverlay.onThemeToggled = [this] (bool light)
    {
        processorRef.lightTheme.store (light ? 1.0f : 0.0f);
        lookAndFeel.setLightTheme (light);
        refreshThemeReactiveColours();
        rebuildBackgroundCache();
        aboutOverlay.refreshTheme();
        repaint();
    };
    // setLightTheme() deja applique tout en haut du constructeur (voir
    // commentaire la-bas) -- juste synchroniser l'etat visuel du bouton ici.
    optionsOverlay.setLightThemeState (processorRef.lightTheme.load() > 0.5f);
    // Meme avertissement que Nuke Mext (showLatencyModeMenu()) : Low CPU
    // s'applique immediatement, mais Medium/High CPU demandent confirmation
    // (cout CPU/latence nettement plus eleve).
    optionsOverlay.onLatencySelected = [this] (int index)
    {
        auto* param = processorRef.apvts.getParameter (SixpackAudioProcessor::idLinearLatencyMode);
        if (param == nullptr)
            return;

        const int currentIndex = (int) std::round (param->convertFrom0to1 (param->getValue()));

        if (index == 0)
        {
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
            return;
        }

        const juce::String modeLabel = (index == 1) ? "Medium CPU" : "High CPU";
        const juce::String recommendedCores = (index == 1) ? "16" : "32";

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
        setParamValue (SixpackAudioProcessor::idAiMasteringTarget, (float) index);
    };
    optionsOverlay.onAiCompressionModeSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiCompressionMode, (float) index);
    };
    optionsOverlay.onAiCharacterSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiAttackReleaseCharacter, (float) index);
    };
    optionsOverlay.onAiGainReductionSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiGainReductionMode, (float) index);
    };
    optionsOverlay.onAiScopeChanged = [this] (int scopeIndex, bool enabled)
    {
        static constexpr const char* scopeIds[3] =
        {
            SixpackAudioProcessor::idAiScopeCrossover,
            SixpackAudioProcessor::idAiScopeDetectors,
            SixpackAudioProcessor::idAiScopeCompression,
        };
        if (scopeIndex >= 0 && scopeIndex < 3)
            setParamValue (scopeIds[scopeIndex], enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiStyleSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiStyle, (float) index);
    };
    optionsOverlay.onAiFidelityChanged = [this] (bool enabled)
    {
        setParamValue (SixpackAudioProcessor::idAiFidelity, enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiExpandModeSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiExpandMode, (float) index);
    };
    optionsOverlay.onAiExpandCharacterSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiExpandCharacter, (float) index);
    };
    optionsOverlay.onAiExpandAmountSelected = [this] (int index)
    {
        setParamValue (SixpackAudioProcessor::idAiExpandAmount, (float) index);
    };

    // --- Presets rapides (meme mecanisme que Nuke Mext) ---
    buildPresetList();

    presetSectionLabel.setText ("preset", juce::dontSendNotification);
    presetSectionLabel.setJustificationType (juce::Justification::centredLeft);
    presetSectionLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    presetSectionLabel.setColour (juce::Label::textColourId, SixpackColours::textDim);
    addAndMakeVisible (presetSectionLabel);

    prevPresetButton.onClick = [this] { goToPreset (-1); };
    nextPresetButton.onClick = [this] { goToPreset (1); };
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    presetNameButton.onClick = [this] { openPresetBrowser(); };
    addAndMakeVisible (presetNameButton);

    presetAuthorLabel.setJustificationType (juce::Justification::centred);
    presetAuthorLabel.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::italic)));
    presetAuthorLabel.setColour (juce::Label::textColourId, SixpackColours::textDim);
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

    // Garantit que le rack du bas (et ses faders) reste TOUJOURS au-dessus
    // de tout sibling ajoute apres lui (barre de presets, etc.), quel que
    // soit l'ordre d'ajout -- retour utilisateur explicite : "les fader ne
    // doivent pas passer sous le canvas mais par dessus".
    bandMetersRack.toFront (false);

    setSize (baseWidth, baseHeight);
}

SixpackEditorContent::~SixpackEditorContent()
{
    setLookAndFeel (nullptr);
}

void SixpackEditorContent::timerCallback()
{
    const bool powered = *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idPower) > 0.5f;
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

void SixpackEditorContent::applyAiLearnResult (const AiLearnAnalyzer::Result& result)
{
    // Portee (menu Options, section AI, demande explicite : "AI ne
    // s'occupe que de la compression si on le souhaite") -- chaque
    // categorie n'est poussee vers les parametres QUE si son interrupteur
    // etait actif au moment du clic (voir AiLearnAnalyzer::Result).
    if (result.applyCrossoverScope)
    {
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
            rampParamTo (SixpackAudioProcessor::idCrossover[x], result.crossoverHz[(size_t) x]);
    }

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SixpackAudioProcessor::bandParamId (base, band); };

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
                rampParamTo (id (SixpackAudioProcessor::idInput), result.inputDb[(size_t) band]);
            if (result.pushAttackRelease)
            {
                rampParamTo (id (SixpackAudioProcessor::idAttack),  result.attackMs[(size_t) band]);
                rampParamTo (id (SixpackAudioProcessor::idRelease), result.releaseMs[(size_t) band]);
            }
            if (result.pushRatio)
                setParamValue (id (SixpackAudioProcessor::idRatioIndex),
                               (float) juce::roundToInt (result.ratioIndex[(size_t) band]));

            if (result.pushExpandInput)
                rampParamTo (id (SixpackAudioProcessor::idInputExpand), result.expandInputDb[(size_t) band]);
            if (result.pushExpandAttackRelease)
            {
                rampParamTo (id (SixpackAudioProcessor::idAttackExpand),  result.expandAttackMs[(size_t) band]);
                rampParamTo (id (SixpackAudioProcessor::idReleaseExpand), result.expandReleaseMs[(size_t) band]);
            }
            if (result.pushExpandRatio)
                setParamValue (id (SixpackAudioProcessor::idRatioIndexExpand),
                               (float) juce::roundToInt (result.expandRatioIndex[(size_t) band]));

            rampParamTo (id (SixpackAudioProcessor::idBandMix),   result.mixPercent[(size_t) band]);
            rampParamTo (id (SixpackAudioProcessor::idBandWidth), result.widthPercent[(size_t) band]);
        }

        if (result.applyDetectorsScope)
        {
            setParamValue (id (SixpackAudioProcessor::idDetectorBell), result.detectorBellOn[(size_t) band] ? 1.0f : 0.0f);
            setParamValue (id (SixpackAudioProcessor::idDetectorBellFreq), result.detectorBellFreqHz[(size_t) band]);
            setParamValue (id (SixpackAudioProcessor::idDetectorBellGain), result.detectorBellGainDb[(size_t) band]);
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
            rampParamTo (id (SixpackAudioProcessor::idOutput), outputDb);
        }
    }

    if (result.applyLoudnessTarget && result.recommendSoftClipOn)
    {
        setParamValue (SixpackAudioProcessor::idSoftClip, 1.0f);
    }
}

void SixpackEditorContent::setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                                                 const juce::String& unitSuffix, int decimalPlaces)
{
    addAndMakeVisible (knob.slider);
    // Non clippe a ses propres bounds (retour utilisateur : l'ombre portee
    // du knob etait tronquee au bord de son petit composant carre -- "il
    // faut la laisser par-dessus le tout") -- laisse le halo/l'ombre de
    // SixpackLookAndFeel::drawRotarySlider deborder dans le parent
    // (le BandPanel, dont le clip couvre toute la plaque) au lieu d'etre
    // coupes au bord du knob lui-meme.
    knob.slider.setPaintingIsUnclipped (true);
    knob.slider.setTextValueSuffix (unitSuffix);
    knob.slider.setNumDecimalPlacesToDisplay (decimalPlaces);

    if (auto* param = processorRef.apvts.getParameter (paramID))
        knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, paramID, knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centredLeft);
    knob.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
    knob.label.setColour (juce::Label::textColourId, SixpackColours::textBright.withAlpha (0.78f));
    addAndMakeVisible (knob.label);
}

void SixpackEditorContent::buildBandPanel (int band, juce::Colour accent, const juce::String& tag)
{
    auto& bp = bandPanels[(size_t) band];
    bp.accentColour = accent;

    const auto pid = [band] (const char* base) { return SixpackAudioProcessor::bandParamId (base, band); };

    addAndMakeVisible (bp.opticalButton);
    bp.opticalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idOptical), bp.opticalButton);

    addAndMakeVisible (bp.britishButton);
    bp.britishAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idBritish), bp.britishButton);

    auto* opticalBtn = &bp.opticalButton;
    auto* britishBtn = &bp.britishButton;
    opticalBtn->onClick = [opticalBtn, britishBtn] { if (opticalBtn->getToggleState()) britishBtn->setToggleState (false, juce::sendNotificationSync); };
    britishBtn->onClick = [opticalBtn, britishBtn] { if (britishBtn->getToggleState()) opticalBtn->setToggleState (false, juce::sendNotificationSync); };

    // Comp/Expand : bouton RETIRE DU VISUEL (retour utilisateur explicite :
    // "dans les bandes... on va faire apparaitre les expandeurs" -- Comp et
    // Expand ont maintenant CHACUN leurs propres knobs affiches en
    // permanence, voir BandPanel::expandRangeKnob/expandAttackKnob et
    // updateDspFromParams(), qui n'alterne plus entre les 2 jeux de
    // parametres -- ce bouton n'a donc plus d'utilite. addChildComponent
    // (pas addAndMakeVisible) : le parametre idAboveBelow reste dans
    // l'APVTS pour la compat des presets/automations existants, juste plus
    // lu par le DSP ni affiche.
    addChildComponent (bp.aboveBelowButton);

    addAndMakeVisible (bp.sidechainToggle);
    bp.sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idSidechainEnable), bp.sidechainToggle);

    bp.soloSwitch = std::make_unique<SoloSwitch> (accent);
    addAndMakeVisible (*bp.soloSwitch);
    bp.soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SixpackAudioProcessor::idSolo[band], bp.soloSwitch->lever);

    bp.grMeter = std::make_unique<GRMeter> (processorRef.grMeterDb[(size_t) band],
                                             processorRef.grMeterExpandDb[(size_t) band],
                                             *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idPower),
                                             *processorRef.apvts.getRawParameterValue (pid (SixpackAudioProcessor::idAboveBelow)));
    addAndMakeVisible (*bp.grMeter);

    addAndMakeVisible (bp.ratioSelector);
    auto* ratioParam = processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idRatioIndex));
    auto* ratioSelectorPtr = &bp.ratioSelector;
    bp.ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        *ratioParam,
        [ratioSelectorPtr] (float v) { ratioSelectorPtr->setSelectedIndex ((int) std::round (v)); },
        nullptr);
    bp.ratioSelector.onSelect = [this, band] (int idx) { bandPanels[(size_t) band].ratioAttachment->setValueAsCompleteGesture ((float) idx); };
    bp.ratioAttachment->sendInitialUpdate();

    // DETECTOR/COLOR retires du VISUEL (retour utilisateur explicite : "je
    // souhaite retirer detector, color et le XY. laisse dans le code mais
    // retire du visuel si un jour je veux remettre") -- addChildComponent
    // (pas addAndMakeVisible) : les attachements/le DSP restent intacts,
    // ces composants existent juste hors-ecran en permanence.
    for (auto* t : { &bp.detectorHpToggle, &bp.detectorBellToggle, &bp.detectorBell2Toggle, &bp.detectorLinkToggle,
                     &bp.audioHpToggle, &bp.audioDist2Toggle, &bp.audioDist3Toggle })
        addChildComponent (*t);

    // useBell2 ne pilote plus que le routage DetectorFilterGraph ci-dessous
    // (bell2FreqP/GainP transmis ou non) -- les boutons eux-memes ne
    // s'affichent plus jamais (voir ci-dessus), mais le parametre reste
    // fonctionnel bande par bande (LowMid/Mid/HighMid) si jamais l'UI
    // Detector est remise un jour.
    const bool useBell2 = (band != SixpackAudioProcessor::Low && band != SixpackAudioProcessor::High);

    bp.detectorHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idDetectorHp), bp.detectorHpToggle);
    bp.detectorBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idDetectorBell), bp.detectorBellToggle);
    bp.detectorBell2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idDetectorBell2), bp.detectorBell2Toggle);
    bp.detectorLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idDetectorLink), bp.detectorLinkToggle);
    bp.audioHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idAudioHp), bp.audioHpToggle);
    bp.audioDist2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idAudioDist2), bp.audioDist2Toggle);
    bp.audioDist3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SixpackAudioProcessor::idAudioDist3), bp.audioDist3Toggle);

    // Bornes de l'axe X (voir doc de classe DetectorFilterGraph) : la bande
    // 0 (Low) part de 20Hz, la derniere (High) finit a 20000Hz, chaque
    // bande intermediaire va du crossover qui la precede a celui qui la
    // suit -- les bandes voisines partagent le meme atomic de crossover a
    // leur frontiere commune, c'est voulu (continuite visuelle).
    auto& viewMin = (band == 0) ? kFilterGraphMinHz
                  : *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idCrossover[band - 1]);
    auto& viewMax = (band == kNumBands - 1) ? kFilterGraphMaxHz
                  : *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idCrossover[band]);

    bp.filterGraph = std::make_unique<DetectorFilterGraph> (
        *processorRef.apvts.getRawParameterValue (pid (SixpackAudioProcessor::idDetectorHp)),
        *processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorHpFreq)),
        *processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorHpSlope)),
        *processorRef.apvts.getRawParameterValue (pid (SixpackAudioProcessor::idDetectorBell)),
        *processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorBellFreq)),
        *processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorBellGain)),
        band == SixpackAudioProcessor::High, viewMin, viewMax,
        useBell2 ? processorRef.apvts.getRawParameterValue (pid (SixpackAudioProcessor::idDetectorBell2)) : nullptr,
        useBell2 ? processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorBell2Freq)) : nullptr,
        useBell2 ? processorRef.apvts.getParameter (pid (SixpackAudioProcessor::idDetectorBell2Gain)) : nullptr);
    addChildComponent (*bp.filterGraph);

    // Bande HIGH : le point HP devient LP (meme mecanique) -- relabelle le
    // bouton en consequence.
    if (band == SixpackAudioProcessor::High)
        bp.detectorHpToggle.setText ("LP");

    // 2 decimales (retour utilisateur : "pour que je puisse calibrer
    // proprement" -- calibration fine du trim cache Input via Plugin
    // Doctor, 1 decimale ne suffit pas pour lire les 1/100e de dB).
    setupKnob (bp.inputKnob,   pid (SixpackAudioProcessor::idInput),   "IN",  " dB", 2);
    setupKnob (bp.attackKnob,  pid (SixpackAudioProcessor::idAttack),  "ATT", " ms", 2);
    setupKnob (bp.releaseKnob, pid (SixpackAudioProcessor::idRelease), "REL", " ms", 1);
    setupKnob (bp.outputKnob,  pid (SixpackAudioProcessor::idOutput),  "OUT", " dB", 1);

    // Expandeur -- knobs PERMANENTS et INDEPENDANTS du compresseur (retour
    // utilisateur explicite, voir doc de classe BandPanel), plus de mode
    // Comp/Expand a choisir (voir updateDspFromParams()). RANGE = idInputExpand
    // (meme parametre que la ligne RANGE de CrossoverSpectrumEditor). Pas de
    // knob Release ("pas besoin du reglage release sur les expandeurs") ni
    // Output (partage avec le compresseur, voir doc de classe).
    setupKnob (bp.expandRangeKnob,  pid (SixpackAudioProcessor::idInputExpand),  "RANGE", " dB", 2);
    setupKnob (bp.expandAttackKnob, pid (SixpackAudioProcessor::idAttackExpand), "ATT",   " ms", 2);
    // Libelle RANGE/ATT desormais IDENTIQUE aux autres knobs (retour
    // utilisateur explicite : "meme chose pour les noms sous l'expand.
    // range et att il faut que ce soit grave en noir" -- le violet etait
    // peu lisible sur le panneau, seule la bague du knob (juste en dessous)
    // reste teintee violette pour identifier visuellement l'expandeur.
    for (auto* knob : { &bp.expandRangeKnob, &bp.expandAttackKnob })
        SixpackLookAndFeel::setKnobAccentGradient (knob->slider, SixpackColours::ledPurple, SixpackColours::ledPurple);

    // "tag" (LOW/MID/HIGH) n'est plus dessine en texte -- voir le logo
    // image correspondant peint directement dans paint() -- conserve comme
    // parametre pour le nom accessible du composant.
    juce::ignoreUnused (tag);
}

void SixpackEditorContent::setBandPanelVisible (int band, bool visible)
{
    auto& bp = bandPanels[(size_t) band];

    bp.opticalButton.setVisible (visible);
    bp.britishButton.setVisible (visible);
    bp.sidechainToggle.setVisible (visible);
    if (bp.soloSwitch != nullptr)  bp.soloSwitch->setVisible (visible);
    if (bp.grMeter != nullptr)     bp.grMeter->setVisible (visible);
    bp.ratioSelector.setVisible (visible);

    // Ancien bouton Comp/Expand, AUDIO HP, DETECTOR (HP/BELL/BELL2), COLOR
    // (TUBE/FUZZ) et XY (filterGraph) restent TOUJOURS caches (voir
    // buildBandPanel() -- addChildComponent, jamais addAndMakeVisible) --
    // jamais reaffiches, meme quand le reste de la bande redevient visible.
    bp.aboveBelowButton.setVisible (false);
    bp.audioHpToggle.setVisible (false);
    bp.detectorLinkToggle.setVisible (false);
    bp.detectorHpToggle.setVisible (false);
    bp.detectorBell2Toggle.setVisible (false);
    bp.detectorBellToggle.setVisible (false);
    bp.audioDist2Toggle.setVisible (false);
    bp.audioDist3Toggle.setVisible (false);
    if (bp.filterGraph != nullptr) bp.filterGraph->setVisible (false);

    for (auto* knob : { &bp.inputKnob, &bp.attackKnob, &bp.releaseKnob, &bp.outputKnob,
                         &bp.expandRangeKnob, &bp.expandAttackKnob })
    {
        knob->slider.setVisible (visible);
        knob->label.setVisible (visible);
    }

    // Repositionne dans la zone de detail UNIQUE a chaque fois qu'une bande
    // redevient visible -- les 5 BandPanel partagent la meme zone (voir
    // bandPanelBounds), pas besoin de la recalculer par bande.
    if (visible)
        layoutBandRow (bp, bandPanelBounds.toNearestInt());
}

void SixpackEditorContent::refreshBandPanelVisibility()
{
    for (int band = 0; band < kNumBands; ++band)
        setBandPanelVisible (band, band == selectedBand);
    crossoverSpectrum.setSelectedBand (selectedBand);
    repaint();
}

// Lien RELATIF (delta), pas absolu : si Low=10dB et Mid=5dB et qu'on bouge
// Low de +3dB, Mid passe a 8dB (l'ecart de 5dB entre les 2 est preserve)
// plutot que de forcer tout le monde a la meme valeur. Installe UNE FOIS
// (pas a chaque clic sur le bouton Link) -- le handler consulte l'etat du
// bouton a chaque mouvement. Meme principe que Nuke Mext.
void SixpackEditorContent::wireInputLink()
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
                    // Ce knob represente TOUJOURS Input (Comp) -- plus de
                    // mode Comp/Expand a rebinder desormais (retour
                    // utilisateur : compresseur et expandeur ont chacun
                    // leurs propres knobs en permanence, voir BandPanel::
                    // expandRangeKnob/expandAttackKnob), donc plus besoin de
                    // garde "meme mode" ici.
                    linkInGuard = true;
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        if (b == band)
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

void SixpackEditorContent::wireOutputLink()
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
                    // Output est desormais un knob PARTAGE (jamais rebinde,
                    // voir doc de classe BandPanel) -- plus besoin de garde
                    // "meme mode" ici non plus (voir wireInputLink()).
                    linkOutGuard = true;
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        if (b == band)
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

void SixpackEditorContent::updateInputLinkVisuals()
{
    const bool linked = linkInSwitch.getToggleState();
    for (int band = 0; band < kNumBands; ++band)
        bandPanels[(size_t) band].inputKnob.label.setColour (
            juce::Label::textColourId, linked ? SixpackColours::ledGreen : SixpackColours::textDim);
}

void SixpackEditorContent::updateOutputLinkVisuals()
{
    const bool linked = linkOutSwitch.getToggleState();
    for (int band = 0; band < kNumBands; ++band)
        bandPanels[(size_t) band].outputKnob.label.setColour (
            juce::Label::textColourId, linked ? SixpackColours::ledRed : SixpackColours::textDim);
}

void SixpackEditorContent::refreshThemeReactiveColours()
{
    // setupKnob()/le constructeur "bakent" Label::textColourId a partir de
    // SixpackColours::textBright/textDim au moment de leur creation (une
    // simple copie de valeur, voir doc de Component::setColour) -- un
    // changement de theme EN COURS D'EXECUTION (bouton LIGHT THEME dans
    // l'engrenage) ne les met donc pas a jour tout seul. Retour utilisateur
    // explicite : "quand on desactive le light theme les ecritures sont
    // blanches" (texte reste bloque sur la couleur du theme precedent).
    // Reapplique ici toutes les couleurs "bakees" avec les valeurs
    // ACTUELLES des globales -- a appeler juste apres tout changement de
    // theme.
    const auto knobLabelColour = SixpackColours::textBright.withAlpha (0.78f);
    for (auto& bp : bandPanels)
    {
        bp.attackKnob.label.setColour (juce::Label::textColourId, knobLabelColour);
        bp.releaseKnob.label.setColour (juce::Label::textColourId, knobLabelColour);
        bp.expandRangeKnob.label.setColour (juce::Label::textColourId, knobLabelColour);
        bp.expandAttackKnob.label.setColour (juce::Label::textColourId, knobLabelColour);
    }

    // inputKnob/outputKnob ont une couleur conditionnelle (lien actif ou
    // non) -- ces deux fonctions relisent deja les globales EN DIRECT,
    // il suffit de les rappeler pour qu'elles rebakent avec les valeurs a jour.
    updateInputLinkVisuals();
    updateOutputLinkVisuals();

    presetSectionLabel.setColour (juce::Label::textColourId, SixpackColours::textDim);
    presetAuthorLabel.setColour (juce::Label::textColourId, SixpackColours::textDim);

    bandMetersRack.refreshThemeReactiveColours();
}

void SixpackEditorContent::showOptionsOverlay()
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
    if (auto* param = processorRef.apvts.getParameter (SixpackAudioProcessor::idLinearLatencyMode))
        latencyIndex = (int) std::round (param->convertFrom0to1 (param->getValue()));
    optionsOverlay.setCurrentLatencyIndex (latencyIndex);

    optionsOverlay.setCurrentAiTargetIndex ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiMasteringTarget)));
    optionsOverlay.setCurrentAiCompressionMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiCompressionMode)));
    optionsOverlay.setCurrentAiCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiAttackReleaseCharacter)));
    optionsOverlay.setCurrentAiGainReduction ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiGainReductionMode)));
    optionsOverlay.setAiScopeStates (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiScopeCrossover) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiScopeDetectors) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiScopeCompression) > 0.5f);
    optionsOverlay.setCurrentAiStyle ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiStyle)));
    optionsOverlay.setAiFidelityState (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiFidelity) > 0.5f);
    optionsOverlay.setCurrentAiExpandMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiExpandMode)));
    optionsOverlay.setCurrentAiExpandCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiExpandCharacter)));
    optionsOverlay.setCurrentAiExpandAmount ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idAiExpandAmount)));

    optionsOverlay.setVisible (true);
    optionsOverlay.toFront (true);
}

void SixpackEditorContent::resized()
{
    // Barre de controle globale : meme hauteur que les autres plaques
    // (144), pour loger les VU a aiguille (deplacees ici, a gauche du
    // knob XLO) en plus des knobs Xover/Wide-Linear/Link/Power.
    controlBarBounds = juce::Rectangle<float> (3.0f, 3.0f, 1394.0f, 144.0f);

    // Ordre des plaques : barre de controle (THD desormais compact dedans,
    // voir thdHeader plus bas), PUIS l'editeur de crossover (spectre +
    // selection de bande), PUIS le panneau de detail de la bande
    // selectionnee (UN SEUL a la fois, voir BandPanel/selectedBand), PUIS
    // la plaque du bas -- gardee VIDE pour l'instant (retour utilisateur :
    // "il faut garder le rack vide en bas pour le moment"), reservee pour
    // un usage futur.
    // Hauteur 144 REVERTEE a l'original (tentative 200 rejetee, retour
    // utilisateur : "tu a agrandi la fenetre du plugin" -- la fenetre ne
    // doit pas grandir). Ecarts de 10px avant/apres SUPPRIMES (retour
    // utilisateur explicite : "il reste une bande grise en haut et en bas
    // du compresseur" -- le fond du chassis se voyait dans ces 2 ecarts
    // entre crossoverEditorBounds/bandPanelBounds et bandPanelBounds/
    // vuPlateBounds) : les 3 plaques se touchent desormais.
    // Haut chaine sur controlBarBounds.getBottom() (etait un Y fixe=153,
    // laissant un ecart de 6px -- retour utilisateur : "une bande grise au
    // dessus du bandeau crossover ... [il] s'agrandisse ... pour ne plus
    // voir cette bande grise"). Le BAS reste fixe (293, comme avant) pour
    // ne rien decaler en dessous -- seule la hauteur grandit (140 -> 146)
    // pour absorber l'ecart, la fenetre ne grandit pas.
    crossoverEditorBounds = juce::Rectangle<float> (3.0f, controlBarBounds.getBottom(), 1394.0f, 146.0f);
    bandPanelBounds       = juce::Rectangle<float> (3.0f, crossoverEditorBounds.getBottom(), 1394.0f, 144.0f);
    vuPlateBounds         = juce::Rectangle<float> (3.0f, bandPanelBounds.getBottom(), 1394.0f, 328.0f);

    crossoverSpectrum.setBounds (crossoverEditorBounds.toNearestInt());
    layoutBandRow (bandPanels[(size_t) selectedBand], bandPanelBounds.toNearestInt());
    // Les vis d'angle (renderCornerScrews, inset 15 + rayon 5) ne sont
    // QUE dans les 4 coins -- decaler tout le rack de 24px a gauche suffit
    // a degager les 2 vis de GAUCHE (le contenu ne va pas jusqu'a droite,
    // voir compactBandW dans BandMetersRack), inutile de rogner la hauteur
    // (ca ecrasait les vu-metres pour rien, retour utilisateur : "c'est
    // trop petit en hauteur la") -- juste une petite marge haut/bas pour
    // ne pas toucher le trait de bordure de la plaque.
    auto rackArea = vuPlateBounds.reduced (0.0f, 4.0f);
    rackArea.removeFromLeft (24.0f);
    // Meme marge a DROITE qu'a gauche (retour utilisateur : "j'ai une
    // plaque a gauche derriere la console mais elle n'est pas a droite du
    // plugin") -- avant l'ajout de la 6e bande, le rack ne remplissait pas
    // toute la largeur de vuPlateBounds a droite (marge reservee "pour
    // d'autres fonctions"), donc son propre bord droit ne s'approchait
    // jamais du bord de la plaque. Depuis que compactBandW*6 remplit
    // presque toute la largeur, le dernier bandeau va desormais jusqu'au
    // bord droit de vuPlateBounds SANS la meme marge que la gauche --
    // asymetrie visible (bande de plaque de fond visible a gauche,
    // absente a droite). Voir BandMetersRack::compactBandW, reduit en
    // consequence pour que les 6 bandes tiennent toujours.
    rackArea.removeFromRight (24.0f);
    bandMetersRack.setBounds (rackArea.toNearestInt());

    const int barTop = (int) controlBarBounds.getY();

    // --- Header redessine, v4 -- CENTRAGE EXACT du logo (retour
    // utilisateur explicite : "reduis les boutons soft, iron tape et air
    // en largeur pour placer au centre le logo"). Les versions precedentes
    // avaient etabli qu'un centrage pixel-parfait etait mathematiquement
    // impossible SANS toucher a un module -- l'utilisateur a alors
    // explicitement autorise a retrecir la grille SOFT/TAPE/IRON/AIR (2x2)
    // du module THD : ThdHeaderModule::resized() dimensionne cette grille
    // PROPORTIONNELLEMENT a la largeur totale qu'on lui donne (pas de
    // valeurs en dur a l'interieur), donc reduire thdHeader.setBounds()
    // ici suffit, sans toucher a ThdHeaderModule.h. Largeur 228 -> 168
    // (-60px), cellules SOFT/TAPE/IRON/AIR ~86px -> ~56px chacune.
    //
    // Avec ce -60px cote droit (Link+THD), le calcul rend le residu de
    // centrage a 0 : logo exactement a x=700 (centre de la barre de
    // 1394px). Zones : Power+A/B+module AI a gauche (pas de separateur
    // interne) ; Link+THD a droite (idem) ; 2 lignes de demarcation
    // seulement, aux frontieres avec le groupe central VU-logo-VU.

    // Espacement redistribue (retour utilisateur : "espace mal reparti" +
    // "elements mal groupes", preview Artifact "Header — pistes de layout",
    // variante B choisie) -- l'ecart Power/AB -> module AI n'etait que de
    // 4px (179 a 183), a peine plus que l'ecart INTERIEUR a un groupe (ex.
    // 4px entre needleVu1Modes et needleVu1), alors que ce sont 2 groupes
    // fonctionnels distincts : ca lisait comme un seul bloc. Tous les
    // ecarts INTER-groupes sont maintenant proches de 6-9px (au lieu du
    // 4/10/6/6/10 incoherent d'avant), les ecarts INTERIEURS a un groupe
    // restent a 2-4px (inchanges) -- le rythme espace/groupe devient net
    // sans avoir besoin des lignes de separation (retirees, voir
    // rebuildBackgroundCache()). Meme span total qu'avant (29 a 1393).

    // POWER (recule de +8px en Y pour laisser la place au label "POWER"
    // repositionne juste au-dessus, voir paint()) + A/B, puis module AI.
    powerButton.setBounds (29, barTop + 18, 70, 92);
    abButton.setBounds    (29, barTop + 110, 150, 26);

    aiLearnButton.setBounds (188, barTop + 7, 70, 19);
    xoverModeButton.setBounds (188, barTop + 30, 70, 84);
    latencyModeButton.setBounds (260, barTop + 30, 20, 20);

    // VU gauche : module de mode (exterieur, cote module AI), puis
    // aiguille (interieure, pres du logo).
    needleVu1Modes.setBounds (287, barTop + 15, 54, 122);
    needleVu1.setBounds      (345, barTop + 15, 266, 122);

    // Logo -- proche du centre (x=617, centre ~702, marge de 2px negligeable
    // vs le centrage pixel-parfait d'avant pour absorber le nouvel
    // espacement), remonte de 13px (retour utilisateur : "monter un peu le
    // logo Sixpack vers le haut") pour degager la place necessaire a
    // l'agrandissement de la signature mikshake juste en dessous (voir
    // paint()).
    suppressorLogoButton.setBounds (617, barTop + 20, 170, 84);

    // VU droite : MIROIR de la gauche.
    needleVu2.setBounds      (793, barTop + 15, 272, 122);
    needleVu2Modes.setBounds (1069, barTop + 15, 54, 122);

    linkInSwitch.setBounds  (1131, barTop + 30, 90, 30);
    linkOutSwitch.setBounds (1131, barTop + 68, 90, 30);

    // Largeur reduite 228 -> 168 (voir commentaire ci-dessus) -- seul
    // module dont la TAILLE change dans ce header, a la demande explicite
    // de l'utilisateur.
    thdHeader.setBounds (1225, barTop + 28, 168, 88);

    // Panneau de detail de la bande selectionnee -- deja repositionne plus
    // haut (voir crossoverEditorBounds/bandPanelBounds ci-dessus), rien de
    // plus a faire ici (une seule bande visible a la fois).

    // --- Barre de presets, tout en bas (34px, meme mecanisme que Nuke Mext) ---
    // Position calculee depuis le bas de vuPlateBounds (plutot qu'un nombre
    // en dur) -- suit automatiquement la mise en page au-dessus. Centree
    // verticalement dans la zone libre entre vuPlateBounds et le bord bas
    // de la plaque de fond unique (outerPlate, marge 6px, voir
    // rebuildBackgroundCache()) : zone disponible 37px (785->822), rangee
    // de 26px -> ~5-6px de marge de chaque cote (etait +10 en haut / ~1px
    // en bas, tout collait vers le bas -- retour utilisateur explicite).
    const int presetBarTop = (int) vuPlateBounds.getBottom() + 6;
    // x=32 (pas 14) -- la plaque de fond unique (voir rebuildBackgroundCache())
    // pose une vis de coin en bas a gauche vers x=21 qui, a x=14, mordait
    // sur le "P" de "PRESET" (aucune sous-plaque dediee ici pour l'eviter,
    // contrairement aux autres rangees). Largeur reduite d'autant pour
    // garder le bord droit aligne avec le reste.
    auto presetBar = juce::Rectangle<int> (32, presetBarTop, 1354, 26);

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

// Retour utilisateur explicite : "le compresseur doit prendre toute la
// plaque sur laquelle il est" -- le contenu laissait ~50px de vide mort a
// droite (l'espacement des 6 knobs, 155px, n'utilisait pas toute la
// largeur disponible apres le decalage d'alignement +24) et un peu a
// gauche. Redistribue pour REMPLIR la plaque (144x1394, INCHANGEE -- la
// fenetre ne doit pas grandir) : espacement des knobs elargi 155 -> 164/177
// (177 pour l'ecart Comp/Expand, plus large) pour que le dernier knob
// touche (a la marge des vis pres) le bord droit de la plaque, coordonne
// avec rebuildBackgroundCache() (chrome statique : dividers/douilles).
void SixpackEditorContent::layoutBandRow (BandPanel& bp, juce::Rectangle<int> area)
{
    const int top = area.getY();

    bp.soloSwitch->setBounds (area.getX() + 24, top + 34, 68, 90);

    bp.grMeter->setBounds (area.getX() + 115, top + 17, 280, 45);

    bp.opticalButton.setBounds  (area.getX() + 115, top + 64, 76, 24);
    bp.britishButton.setBounds  (area.getX() + 197, top + 64, 76, 24);
    bp.sidechainToggle.setBounds (area.getX() + 279, top + 64, 44, 24);

    bp.ratioSelector.setBounds (area.getX() + 115, top + 93, 280, 44);

    // DETECTOR/COLOR/XY retires de la mise en page (caches en permanence,
    // voir buildBandPanel()/setBandPanelVisible()) -- plus de bounds a leur
    // donner, l'espace qu'ils occupaient sert desormais au Compresseur et a
    // l'Expandeur cote a cote (voir ci-dessous).

    auto layoutKnob = [] (KnobControl& knob, int cx, int cy, int diameter)
    {
        knob.slider.setBounds (cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        const int lw = 44, lh = 14;
        // Decale vers la gauche (retour utilisateur : "il faut decaller un
        // peu sur la gauche les ecritures sous les knobs ... surtout range
        // car il est dans le knob", puis "decale encore un peu range", puis
        // "c'est un tout petit peu trop pour range") -- a +2 la boite de
        // label mordait sur le cercle du knob pres du bas (rayon local ~42px
        // a cette hauteur, boite large de 44px). -12 insuffisant, -24 trop,
        // -18 au final.
        knob.label.setBounds (cx - diameter / 2 - 18, cy + diameter / 2 - lh - 2, lw, lh);
    };

    // Compresseur ET Expandeur : MEME diametre 124 pour les 6 knobs --
    // espacement elargi (164, 177 pour l'ecart Comp/Expand) pour occuper
    // toute la largeur disponible jusqu'au bord droit de la plaque.
    layoutKnob (bp.inputKnob,   area.getX() + 485, top + 69, 124);
    layoutKnob (bp.attackKnob,  area.getX() + 649, top + 69, 124);
    layoutKnob (bp.releaseKnob, area.getX() + 813, top + 69, 124);
    layoutKnob (bp.outputKnob,  area.getX() + 977, top + 69, 124);

    layoutKnob (bp.expandRangeKnob,  area.getX() + 1154, top + 69, 124);
    layoutKnob (bp.expandAttackKnob, area.getX() + 1318, top + 69, 124);
}

void SixpackEditorContent::rebuildBackgroundCache()
{
    const int w = getWidth();
    const int h = getHeight();

    backgroundCache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (backgroundCache);

    // Plaque de fond UNIQUE derriere TOUTES les autres plaques (barre de
    // controle, crossover, bande, rack) -- retour utilisateur explicite :
    // "remettre une plaque derriere toutes les plaques qui prend tout le
    // VST, avec des petites marges sur les cotes". Rail de montage sombre
    // (fixe, jamais retheme -- comme un vrai chassis rack) visible sur la
    // petite marge, avant la plaque metal (elle, theme-reactive) inseree
    // dedans avec sa propre bordure/vis de coin -- les plaques existantes
    // (controlBarBounds, vuPlateBounds, bandPanelBounds) continuent de
    // flotter par-dessus avec leurs propres marges, inchangees.
    g.setColour (juce::Colour (0xff08090a));
    g.fillRect (backgroundCache.getBounds());

    constexpr float outerMargin = 6.0f;
    const auto outerPlate = backgroundCache.getBounds().toFloat().reduced (outerMargin);
    {
        // renderMetalPanel() remplit un RECTANGLE (coins carres) -- clip sur
        // un rectangle arrondi pour que le degrade brosse ne deborde pas des
        // coins de la plaque (sinon de petits triangles de texture depassent
        // de la bordure arrondie dessinee juste apres).
        juce::Path clip;
        clip.addRoundedRectangle (outerPlate, 6.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        SixpackLookAndFeel::renderMetalPanel (g, outerPlate);
        g.restoreState();
    }
    SixpackLookAndFeel::renderCornerScrews (g, outerPlate, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (outerPlate.reduced (0.5f), 6.0f, 1.0f);

    // --- Barre de controle globale ---
    // PAS de renderCornerScrews ici (retour utilisateur : "j'ai
    // l'impression qu'il y a deux vis au dessus de tape") -- controlBarBounds
    // n'est inset que de 3px par rapport a outerPlate (lui-meme inset de
    // 6px du bord du canevas), donc ses vis de coin (inset 15px chacune)
    // ne sont qu'a 3px de celles d'outerPlate juste au-dessus -- quasi
    // superposees. C'etait cache jusqu'ici sous l'ancien bouton AI Learn
    // en haut a droite ; expose depuis que ce coin est redevenu vide.
    // Les vis d'outerPlate suffisent pour cette zone.
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (controlBarBounds.reduced (0.5f), 6.0f, 1.0f);
    {
        const float barTop = controlBarBounds.getY();
        // Les 2 lignes de separation (apres Power/AB/AI, avant Link/THD)
        // sont RETIREES (retour utilisateur, variante B de la preview
        // Artifact "Header — pistes de layout" : "meme groupement,
        // espacement redistribue" -- l'espacement inter-groupes reequilibre
        // dans resized() suffit desormais a lire les zones sans un trait).

        // Plus de sockets de knobs ici -- l'ancien groupe XLO/XHI est
        // remplace par CrossoverSpectrumEditor (son propre cadre/fond, voir
        // CrossoverSpectrumEditor::paint()), qui dessine sa propre plaque.

        // Signature, fond transparent, sous le logo SIXPACK (retour
        // utilisateur explicite : "je souhaite que tu la mettes en fond
        // transparent sous le logo sixpack", puis plusieurs retours -- "pas
        // assez noire", "degage cette opacite", "ou de la transparence" --
        // jusqu'a "elle doit etre completement noire. pas de transparence,
        // pas d'opacite"). Cause racine : g.drawImage(...,destW,destH,...)
        // demande TOUJOURS un resample a JUCE des que destW/destH different
        // de la taille source -- meme avec une source deja a alpha binaire
        // et lowResamplingQuality, un ratio d'echelle non trivial (ex.
        // 180x120 -> 39x26) reintroduit des pixels de bord semi-opaques.
        // Fix definitif : le PNG est desormais genere DIRECTEMENT a sa
        // taille d'AFFICHAGE finale (39x26, min-pooling + seuil binaire,
        // voir script de conversion) et dessine ici en 1:1 EXACT via
        // drawImageAt (pas de destW/destH => aucun resample possible).
        // Statique -> cache de fond, comme le reste de ce chrome. Le logo
        // (suppressorLogoButton, SixpackLogoButton) est affiche ~170x84
        // dans sa boite (617, barTop+20, 170x84), centre horizontal x=700
        // -- meme centre ici, juste sous le bas de sa boite (barTop+104).
        // Signature agrandie 39x26 -> 85x39 (retour utilisateur : "trop
        // petite", puis "minimise la zone morte du png") -- le master
        // "Signature Mikshake.png" (1536x1024) a une signature qui n'occupe
        // que 1447x644 en son centre (bbox mesuree), le reste n'est que de
        // la marge transparente/blanche mise a l'echelle pour rien. Recadre
        // sur cette bbox (+15px de marge de securite) AVANT le pipeline
        // min-pooling + seuil binaire habituel (aucun pixel semi-opaque),
        // ce qui double la densite d'encre utile dans le PNG final au lieu
        // d'agrandir une image majoritairement vide.
        {
            static const juce::Image sigImage = juce::ImageCache::getFromMemory (
                BinaryData::signature_mikshake_png, BinaryData::signature_mikshake_pngSize);
            if (! sigImage.isNull())
            {
                const float sigX = 700.0f - (float) sigImage.getWidth() * 0.5f;
                const float sigY = barTop + 104.0f;
                g.drawImageAt (sigImage, (int) sigX, (int) sigY);
            }
        }
    }

    // --- Plaque rack du bas -- chassis + vis, le contenu (BandMetersRack,
    // vu-metres GR/Expand par bande) se dessine par-dessus comme enfant. ---
    SixpackLookAndFeel::renderCornerScrews (g, vuPlateBounds, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (vuPlateBounds.reduced (0.5f), 6.0f, 1.0f);

    // --- Panneau de detail de bande -- UN SEUL emplacement chrome desormais
    // (voir BandPanel/selectedBand), quelle que soit la bande qui l'occupe. ---
    {
        const auto& pb = bandPanelBounds;
        SixpackLookAndFeel::renderCornerScrews (g, pb, 15.0f, 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (pb.reduced (0.5f), 6.0f, 1.0f);

        const float top = pb.getY();
        // Chrome recale (retour utilisateur : "le compresseur doit prendre
        // toute la plaque" -- voir doc de layoutBandRow()) : contenu
        // redistribue pour remplir toute la largeur de la plaque
        // (1394x144, INCHANGEE), plus d'espace mort a droite.
        SixpackLookAndFeel::renderDividerLine (g, pb.getX() + 107.0f,  top + 7.0f,  top + 55.0f);
        SixpackLookAndFeel::renderDividerLine (g, pb.getX() + 107.0f,  top + 79.0f, top + 137.0f);
        SixpackLookAndFeel::renderDividerLine (g, pb.getX() + 405.0f, top + 7.0f,  top + 137.0f);

        // Separation Compresseur/Expandeur -- style "design" (halo lumineux,
        // preview "style A" valide) au lieu du simple trait plat, retour
        // utilisateur explicite (voir doc de classe BandPanel).
        SixpackLookAndFeel::renderGlowDividerLine (g, pb.getX() + 1066.0f, top + 17.0f, top + 127.0f);

        // 6 knobs, TOUS au meme diametre 124 -- espacement elargi (voir
        // layoutBandRow()) pour occuper toute la largeur jusqu'au bord
        // droit de la plaque.
        for (int cx : { 485, 649, 813, 977, 1154, 1318 })
            SixpackLookAndFeel::renderKnobSocket (g, { pb.getX() + (float) cx, top + 69.0f }, 63.0f);
    }
}

void SixpackEditorContent::paint (juce::Graphics& g)
{
    if (backgroundCache.isValid())
        g.drawImageAt (backgroundCache, 0, 0);
    else
        SixpackLookAndFeel::renderMetalPanel (g, getLocalBounds().toFloat());

    const int barTop = (int) controlBarBounds.getY();

    // Logo SUPPRESSOR desormais dessine par SixpackLogoButton (vrai
    // composant cliquable, ouvre About) -- plus dessine ici.

    // Repositionne au-dessus de powerButton (header v3, Power est passe a
    // gauche) -- ce label texte fixe restait a son ancienne position en
    // haut a droite, orpheline (retour utilisateur : capture montrant
    // "POWER" flottant au-dessus de SOFT/TAPE/IRON/AIR sans plus aucun
    // interrupteur a proximite).
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    SixpackLookAndFeel::drawEngravedText (g, "POWER", { 29, barTop + 2, 70, 14 }, SixpackColours::textDim);

    static const juce::Colour bandTintColours[kNumBands] =
        { SixpackColours::ledCyan, SixpackColours::ledGreen, SixpackColours::ledAmber, SixpackColours::ledOrange, SixpackColours::ledPurple, SixpackColours::ledRed };

    // Un seul en-tete a dessiner -- celui de la bande actuellement
    // selectionnee (voir BandPanel/selectedBand), redessine a CHAQUE
    // paint() (contrairement au chrome statique de rebuildBackgroundCache()
    // ci-dessus) puisqu'il doit changer avec la selection.
    {
        const int band = selectedBand;
        const auto& pb = bandPanelBounds;
        const int x = (int) pb.getX();
        const int top = (int) pb.getY();

        // Plaque acier noir brosse TEINTEE couleur de bande, a la place du
        // logo/texte LOW/MID/HIGH -- retour utilisateur explicite : "au
        // lieu de mettre low, mid ... je souhaite avoir la couleur de la
        // plaque en acier noir brosse ... avec la couleur de la bande"
        // (preview validee via mcp__visualize avant implementation). Lavis
        // + halo radial discrets (alpha faible) pour ne pas noyer le
        // chrome statique (vis/bordure/dividers/sockets, deja dans
        // backgroundCache) dessine juste en dessous.
        {
            const auto tint = bandTintColours[band];
            // Lavis clippe a un rectangle ARRONDI (meme rayon 6 que la
            // bordure dessinee juste au-dessus, drawRoundedRectangle (pb,
            // 6.0f)) -- retour utilisateur : "la couleur depasse un peu a
            // gauche". La bordure du plateau n'est qu'un TRAIT decoratif
            // (pas un masque), le fond sous pb reste un rectangle A COINS
            // CARRES ; le lavis (fillRect (pb), coins carres aussi)
            // depassait donc legerement le trait arrondi a chaque coin. Un
            // essai precedent avait resserre le lavis de 24px de chaque
            // cote (pour s'aligner sur la marge de BandMetersRack en
            // dessous) -- rejete ("tu as coupe trop court ... je demandais
            // d'etre bien raccord avec le bord") : le lavis doit rester
            // PLEINE largeur, seuls les 4 coins doivent etre arrondis.
            juce::Path roundedClip;
            roundedClip.addRoundedRectangle (pb.reduced (0.5f), 6.0f);
            g.saveState();
            g.reduceClipRegion (roundedClip);
            g.setColour (tint.withAlpha (0.10f));
            g.fillRect (pb);
            const float cx = pb.getX() + pb.getWidth() * 0.15f;
            const float cy = pb.getY() + pb.getHeight() * 0.35f;
            juce::ColourGradient highlight (tint.withAlpha (0.30f), cx, cy,
                                             tint.withAlpha (0.0f), cx + pb.getWidth() * 0.5f, cy, true);
            g.setGradientFill (highlight);
            g.fillRect (pb);
            g.restoreState();
            // Vis REDESSINEES par-dessus le lavis (retour utilisateur : "les
            // vis des compresseurs doivent etre noire, pas de la couleur des
            // bandes", puis "on dirait qu'il y a un cadre autour des vis...
            // c'est pas beau" suite a une 1ere tentative par excludeClipRegion
            // -- un rectangle dur autour d'une vis ronde laissait un carre
            // visible). Repeindre la vis (cercle anti-aliase naturel) par-
            // dessus le lavis evite tout artefact de decoupe. Memes positions
            // que renderCornerScrews (g, pb, 15, 5) dans rebuildBackgroundCache().
            {
                constexpr float screwInset = 15.0f, screwRadius = 5.0f;
                SixpackLookAndFeel::drawScrew (g, { pb.getX() + screwInset,     pb.getY() + screwInset },     screwRadius);
                SixpackLookAndFeel::drawScrew (g, { pb.getRight() - screwInset, pb.getY() + screwInset },     screwRadius);
                SixpackLookAndFeel::drawScrew (g, { pb.getX() + screwInset,     pb.getBottom() - screwInset }, screwRadius);
                SixpackLookAndFeel::drawScrew (g, { pb.getRight() - screwInset, pb.getBottom() - screwInset }, screwRadius);
            }
        }
        // Retour utilisateur : "pas besoin d'ecrire low mid ... sur les
        // bandes" -- plus de label texte du tout, la couleur de la teinte
        // suffit a identifier la bande.

        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        SixpackLookAndFeel::drawEngravedText (g, "GAIN REDUCTION / RATIO", { x + 115, top + 4, 280, 14 },
                                                  SixpackColours::textBright);

        // Libelles "DETECTOR"/"COLOR" retires -- retiraient dans le vide
        // depuis que les boutons correspondants sont caches (voir doc de
        // classe BandPanel dans PluginEditor.h).
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

void SixpackEditorContent::setParamValue (const juce::String& paramID, float rawValue)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
}

void SixpackEditorContent::rampParamTo (const juce::String& paramID, float rawValue, int durationMs)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        knobRamper.rampTo (p, p->convertTo0to1 (rawValue), durationMs);
}

void SixpackEditorContent::buildPresetList()
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
        // crossoverHz : pas d'override ici (etait legacyCrossoverHz(121.5,
        // 2521), un ancien reglage herite de Tripressor qui ne correspondait
        // plus a l'etat d'ouverture reel du plugin) -- le membre par defaut
        // de PresetData reprend directement Crossover::kSplitDefaultHz
        // (60/110/300/1000/2500 Hz, retour utilisateur explicite), qui EST
        // la vraie valeur de sortie d'usine de createParameterLayout().
        PresetData p; p.name = "Default";
        p.linearMode = false; p.softClipOn = true; p.thdMode = 0;
        // Range Expand a plat 0.0f (demande explicite : "les ranges des
        // expand doivent etre a 0 a l'ouverture du plug in") -- deja le
        // defaut du champ PresetData::Band::rangeExpand, Band{} suffit.
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& band = p.bands[b];
            band = PresetData::Band {};
            band.detectorHpFreq = (b == SixpackAudioProcessor::High) ? 16000.0f : 50.0f;
            band.detectorHpSlope = 8;
        }
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f); p.softClipOn = true;
        p.bands[0] = makeBand (0.0f, mapRatio (3), 27.927f, 306.9f, 6.91f);
        p.bands[2] = makeBand (0.0f, mapRatio (3), 10.971f, 204.3f, 6.91f);
        p.bands[5] = makeBand (0.0f, mapRatio (2), 5.891f,  106.1f, 4.03f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Linear"; p.crossoverHz = legacyCrossoverHz (125.0f, 2930.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.29f,  mapRatio (3), 27.927f, 50.0f,  8.93f);
        p.bands[2] = makeBand (-6.62f, mapRatio (3), 10.971f, 50.0f,  8.93f);
        p.bands[5] = makeBand (-3.17f, mapRatio (2), 5.891f,  106.1f, 6.05f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Metal"; p.crossoverHz = legacyCrossoverHz (125.0f, 2553.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-7.79f, mapRatio (2), 23.261f, 50.0f, 13.42f);
        p.bands[2] = makeBand (-8.34f, mapRatio (2), 20.473f, 50.0f, 11.58f);
        p.bands[5] = makeBand (-8.65f, mapRatio (2), 13.770f, 50.0f, 11.52f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Fake OTT"; p.crossoverHz = legacyCrossoverHz (195.3f, 2521.0f); p.softClipOn = true;
        p.bands[0] = makeBand (25.08f, mapRatio (6), 13.272f, 50.0f, 9.09f);
        p.bands[2] = makeBand (19.03f, mapRatio (6), 19.171f, 50.0f, 9.09f);
        p.bands[5] = makeBand (19.59f, mapRatio (6), 8.242f,  50.0f, 9.09f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Kick"; p.crossoverHz = legacyCrossoverHz (89.1f, 2653.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (8.64f,  mapRatio (5), 2.718f, 50.0f, 11.52f);
        p.bands[2] = makeBand (28.80f, mapRatio (4), 5.048f, 50.0f, -4.32f);
        p.bands[5] = makeBand (12.39f, mapRatio (2), 5.891f, 50.0f, 8.35f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Drumbus"; p.crossoverHz = legacyCrossoverHz (195.3f, 2521.0f); p.softClipOn = true;
        p.bands[0] = makeBand (25.08f, mapRatio (6), 13.272f, 50.0f, 13.99f);
        p.bands[2] = makeBand (11.25f, mapRatio (6), 19.171f, 50.0f, 9.09f);
        p.bands[5] = makeBand (7.21f,  mapRatio (6), 8.242f,  50.0f, 11.11f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Vocal"; p.crossoverHz = legacyCrossoverHz (121.5f, 4511.0f); p.softClipOn = true;
        p.bands[0] = makeBand (19.89f, mapRatio (3), 35.121f, 172.3f, 8.81f);
        p.bands[2] = makeBand (9.52f,  mapRatio (4), 13.519f, 85.1f,  11.11f);
        p.bands[5] = makeBand (14.99f, mapRatio (5), 2.025f,  51.6f,  11.11f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Smashed Snare"; p.crossoverHz = legacyCrossoverHz (225.4f, 3582.0f); p.softClipOn = true;
        p.bands[0] = makeBand (12.11f, mapRatio (3), 13.769f, 59.2f, 14.27f);
        p.bands[2] = makeBand (10.09f, mapRatio (5), 10.971f, 78.5f, 14.27f);
        p.bands[5] = makeBand (13.84f, mapRatio (2), 5.891f,  50.8f, 12.83f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Synth Bass"; p.crossoverHz = legacyCrossoverHz (125.0f, 1824.0f); p.softClipOn = true;
        p.bands[0] = makeBand (2.90f, mapRatio (2), 35.121f, 185.5f, 8.80f);
        p.bands[2] = makeBand (0.01f, mapRatio (3), 15.341f, 90.7f,  8.80f);
        p.bands[5] = makeBand (9.51f, mapRatio (1), 5.891f,  51.2f,  8.80f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Neuro Bass"; p.crossoverHz = legacyCrossoverHz (190.6f, 1824.0f); p.softClipOn = true;
        p.bands[0] = makeBand (2.90f, mapRatio (2), 35.121f, 185.5f, 27.82f);
        p.bands[2] = makeBand (0.01f, mapRatio (3), 15.341f, 90.7f,  21.77f);
        p.bands[5] = makeBand (9.51f, mapRatio (1), 5.891f,  51.2f,  27.82f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Boogie"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f);
        p.softClipOn = true; p.linearMode = false;
        p.bands[0] = makeBand (4.31f, mapRatio (4), 27.927f, 50.0f, 1.09f);
        p.bands[2] = makeBand (0.57f, mapRatio (4), 10.971f, 50.0f, 1.66f);
        p.bands[5] = makeBand (1.15f, mapRatio (2), 5.891f,  50.0f, 4.03f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Jazz Club"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-4.90f, mapRatio (3), 27.927f, 50.0f, 5.36f);
        p.bands[2] = makeBand (-5.76f, mapRatio (3), 10.971f, 50.0f, 5.93f);
        p.bands[5] = makeBand (0.00f,  mapRatio (2), 5.891f,  50.0f, 4.27f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Rock Razor"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.86f, mapRatio (3), 27.927f, 50.0f,  2.31f);
        p.bands[2] = makeBand (1.38f, mapRatio (3), 10.971f, 50.0f,  2.65f);
        p.bands[5] = makeBand (6.05f, mapRatio (2), 5.891f,  106.1f, 2.02f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Future Bass"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (0.86f, mapRatio (3), 27.927f, 50.0f, 2.31f);
        p.bands[2] = makeBand (1.38f, mapRatio (3), 10.971f, 50.0f, 2.65f);
        p.bands[5] = makeBand (6.05f, mapRatio (2), 5.891f,  50.0f, 2.02f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Death Metal"; p.crossoverHz = legacyCrossoverHz (125.0f, 2553.0f);
        p.softClipOn = true; p.linearMode = true;
        p.bands[0] = makeBand (-8.36f, mapRatio (2), 23.261f, 50.0f, 10.95f);
        p.bands[2] = makeBand (-7.48f, mapRatio (2), 20.473f, 50.0f, 9.92f);
        p.bands[5] = makeBand (-8.65f, mapRatio (2), 13.770f, 50.0f, 8.94f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Jazz Fallout"; p.crossoverHz = legacyCrossoverHz (135.8f, 2521.0f);
        p.softClipOn = false; p.linearMode = true;
        p.bands[0] = makeBand (1.44f,  mapRatio (2), 23.997f, 77.0f, 6.73f);
        p.bands[2] = makeBand (-5.75f, mapRatio (2), 13.770f, 50.0f, 6.50f);
        p.bands[5] = makeBand (-4.61f, mapRatio (2), 2.751f,  50.0f, 3.57f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        PresetData p; p.name = "Mastering Synthwave"; p.crossoverHz = legacyCrossoverHz (135.8f, 2521.0f);
        p.softClipOn = false; p.linearMode = true;
        p.bands[0] = makeBand (1.44f,  mapRatio (2), 23.997f, 77.0f, 6.73f);
        p.bands[2] = makeBand (-5.75f, mapRatio (2), 13.770f, 50.0f, 6.50f);
        p.bands[5] = makeBand (-4.61f, mapRatio (2), 2.751f,  50.0f, 3.57f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }
    {
        // Mastering Pop Rock -- valeurs EXACTES fournies par capture
        // d'ecran (mode ANALOG, XLO 121.5Hz / XHI 2521Hz) : Low 6:1
        // (index 3), Mid 6:1 (index 3), High 3:1 (index 1) dans la table
        // native Suppressor (pas de mapRatio, ce n'est PAS un preset porte
        // de Mext).
        PresetData p; p.name = "Mastering Pop Rock"; p.crossoverHz = legacyCrossoverHz (121.5f, 2521.0f);
        p.softClipOn = true; p.linearMode = false;
        p.bands[0] = makeBand (6.0f, 3, 40.0f, 50.0f, 6.0f);
        p.bands[2] = makeBand (6.0f, 3, 40.0f, 70.1f, -0.6f);
        p.bands[5] = makeBand (4.8f, 1, 40.0f, 50.0f, 0.0f);
        p.author = "Side Key"; p.isFactory = true; addPreset (p);
    }

    // Presets UTILISATEUR (Save/Delete) geres a part, voir loadUserPresets().
    loadUserPresets();
}

void SixpackEditorContent::loadUserPresets()
{
    auto dir = getUserPresetsDirectory();
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        PresetData p;
        if (readPresetXml (file, p))
            presets.push_back (p);
    }
}

void SixpackEditorContent::goToPreset (int delta)
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

void SixpackEditorContent::syncPresetBrowserEntries()
{
    std::vector<PresetBrowserOverlay::Entry> entries;
    entries.reserve (presets.size());
    for (auto& p : presets)
        entries.push_back ({ p.name, p.author, p.isFactory });

    presetBrowser.setEntries (std::move (entries));
    presetBrowser.setCurrentIndex (currentPresetIndex);
}

void SixpackEditorContent::openPresetBrowser()
{
    syncPresetBrowserEntries();
    presetBrowser.setBounds (getLocalBounds());
    presetBrowser.show();
}

void SixpackEditorContent::refreshPresetDisplay()
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

void SixpackEditorContent::applyPresetData (const PresetData& preset)
{
    // Knobs continus -- ANIMES (voir ParamRamper/rampParamTo()) pour que
    // les knobs tournent visiblement au lieu de teleporter (demande
    // explicite). Tout le reste (booleens/discrets : ratio, toggles
    // Detector/Audio, Mix/Width...) reste instantane, pas d'equivalent
    // visuel a animer.
    for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
    {
        rampParamTo (SixpackAudioProcessor::idCrossover[x], preset.crossoverHz[(size_t) x]);
        setParamValue (SixpackAudioProcessor::idCrossoverEnabled[x], preset.crossoverEnabled[(size_t) x] ? 1.0f : 0.0f);
    }
    setParamValue (SixpackAudioProcessor::idDigitalCrossover, preset.linearMode ? 1.0f : 0.0f);
    setParamValue (SixpackAudioProcessor::idSoftClip, preset.softClipOn ? 1.0f : 0.0f);
    setParamValue (SixpackAudioProcessor::idThdMode, (float) preset.thdMode);

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SixpackAudioProcessor::bandParamId (base, band); };
        const auto& bp = preset.bands[band];

        rampParamTo (id (SixpackAudioProcessor::idInput), bp.input);
        setParamValue (id (SixpackAudioProcessor::idRatioIndex), (float) bp.ratioIndex);
        rampParamTo (id (SixpackAudioProcessor::idAttack), bp.attack);
        rampParamTo (id (SixpackAudioProcessor::idRelease), bp.release);
        rampParamTo (id (SixpackAudioProcessor::idOutput), bp.output);

        // Above/Below/Both + jeu Expand dedie (demande explicite :
        // "recalibre les valeurs range").
        setParamValue (id (SixpackAudioProcessor::idAboveBelow), (float) bp.aboveBelow);
        rampParamTo (id (SixpackAudioProcessor::idInputExpand), bp.rangeExpand);
        setParamValue (id (SixpackAudioProcessor::idRatioIndexExpand), (float) bp.ratioIndexExpand);
        rampParamTo (id (SixpackAudioProcessor::idAttackExpand), bp.attackExpand);
        rampParamTo (id (SixpackAudioProcessor::idReleaseExpand), bp.releaseExpand);
        rampParamTo (id (SixpackAudioProcessor::idOutputExpand), bp.outputExpand);

        setParamValue (id (SixpackAudioProcessor::idDetectorHp), bp.detectorHp ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idDetectorHpFreq), bp.detectorHpFreq);
        setParamValue (id (SixpackAudioProcessor::idDetectorHpSlope), (float) bp.detectorHpSlope);
        setParamValue (id (SixpackAudioProcessor::idDetectorBell), bp.detectorBell ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idDetectorBellFreq), bp.detectorBellFreq);
        setParamValue (id (SixpackAudioProcessor::idDetectorBellGain), bp.detectorBellGain);
        setParamValue (id (SixpackAudioProcessor::idDetectorBell2), bp.detectorBell2 ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idDetectorBell2Freq), bp.detectorBell2Freq);
        setParamValue (id (SixpackAudioProcessor::idDetectorBell2Gain), bp.detectorBell2Gain);
        setParamValue (id (SixpackAudioProcessor::idDetectorLink), bp.detectorLink ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idSidechainEnable), bp.sidechainEnable ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idAudioHp), bp.audioHp ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idAudioDist2), bp.audioDist2 ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idAudioDist3), bp.audioDist3 ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idOptical), bp.optical ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idBritish), bp.british ? 1.0f : 0.0f);
        setParamValue (id (SixpackAudioProcessor::idBandMix), bp.bandMix);
        setParamValue (id (SixpackAudioProcessor::idBandWidth), bp.bandWidth);
    }
}

SixpackEditorContent::PresetData SixpackEditorContent::captureCurrentPresetData (
    const juce::String& name, const juce::String& author, bool isFactory)
{
    PresetData p;
    p.name = name;
    p.author = author;
    p.isFactory = isFactory;

    for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
    {
        p.crossoverHz[(size_t) x] = processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idCrossover[x])->load();
        p.crossoverEnabled[(size_t) x] = processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idCrossoverEnabled[x])->load() > 0.5f;
    }
    p.linearMode    = processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idDigitalCrossover)->load() > 0.5f;
    p.softClipOn    = processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idSoftClip)->load() > 0.5f;
    p.thdMode       = (int) std::round (processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idThdMode)->load());

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SixpackAudioProcessor::bandParamId (base, band); };
        auto& bp = p.bands[band];

        bp.input           = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idInput))->load();
        bp.ratioIndex       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idRatioIndex))->load());
        bp.attack           = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAttack))->load();
        bp.release          = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idRelease))->load();
        bp.output           = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idOutput))->load();

        bp.aboveBelow       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAboveBelow))->load());
        bp.rangeExpand      = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idInputExpand))->load();
        bp.ratioIndexExpand = (int) std::round (processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idRatioIndexExpand))->load());
        bp.attackExpand     = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAttackExpand))->load();
        bp.releaseExpand    = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idReleaseExpand))->load();
        bp.outputExpand     = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idOutputExpand))->load();

        bp.detectorHp       = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorHp))->load() > 0.5f;
        bp.detectorHpFreq   = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorHpFreq))->load();
        bp.detectorHpSlope  = (int) std::round (processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorHpSlope))->load());
        bp.detectorBell     = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBell))->load() > 0.5f;
        bp.detectorBellFreq = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBellFreq))->load();
        bp.detectorBellGain = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBellGain))->load();
        bp.detectorBell2     = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBell2))->load() > 0.5f;
        bp.detectorBell2Freq = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBell2Freq))->load();
        bp.detectorBell2Gain = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorBell2Gain))->load();
        bp.detectorLink     = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idDetectorLink))->load() > 0.5f;
        bp.sidechainEnable  = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idSidechainEnable))->load() > 0.5f;
        bp.audioHp          = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAudioHp))->load() > 0.5f;
        bp.audioDist2       = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAudioDist2))->load() > 0.5f;
        bp.audioDist3       = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idAudioDist3))->load() > 0.5f;
        bp.optical          = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idOptical))->load() > 0.5f;
        bp.british          = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idBritish))->load() > 0.5f;
        bp.bandMix          = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idBandMix))->load();
        bp.bandWidth        = processorRef.apvts.getRawParameterValue (id (SixpackAudioProcessor::idBandWidth))->load();
    }

    return p;
}

void SixpackEditorContent::promptSavePreset()
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

void SixpackEditorContent::promptDeletePreset()
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
void SixpackEditorContent::copyStateToClipboard()
{
    juce::SystemClipboard::copyTextToClipboard (processorRef.apvts.copyState().toXmlString());
}

void SixpackEditorContent::pasteStateFromClipboard()
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
