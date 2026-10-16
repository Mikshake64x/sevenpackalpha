#include "PluginEditor.h"

static constexpr int kNumBands = (int) SevenpackAudioProcessor::NumBands;

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
                       .getChildFile ("SideKeySystem").getChildFile ("SevenpackAlpha").getChildFile ("Presets");
        dir.createDirectory();
        return dir;
    }

    // Migration des presets d'usine HERITES DE TRIPRESSOR (3 bandes, 2
    // crossovers Low/High) vers sevenpack (5 bandes, 4 crossovers) : les 2
    // frequences d'origine deviennent les bornes EXTERIEURES (Low/LowMid et
    // HighMid/High), l'ancienne plage "Mid" est subdivisee log-uniformement
    // en 3 pour loger LowMid/Mid/HighMid. Ces presets n'ont pas encore ete
    // retunes pour les 2 nouvelles bandes intermediaires (voir buildPresetList()
    // -- LowMid/HighMid restent a leurs valeurs neutres par defaut) ; sur
    // quelques presets extremes, la frequence d'origine peut aussi deborder
    // legerement des nouvelles bornes Crossover::kSplitMinHz/MaxHz[0]/[3] et
    // se retrouver clampee -- acceptable pour une premiere migration, a
    // affiner a la main une fois le nouvel editeur de crossover en place.
    std::array<float, (size_t) SevenpackAudioProcessor::NumCrossovers> legacyCrossoverHz (float lowHz, float highHz)
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

    void writePresetXml (const SevenpackEditorContent::PresetData& p, const juce::File& file)
    {
        juce::XmlElement xml ("SevenpackPreset");
        xml.setAttribute ("name", p.name);
        xml.setAttribute ("author", p.author);
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
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

    bool readPresetXml (const juce::File& file, SevenpackEditorContent::PresetData& out)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || xml->getTagName() != "SevenpackPreset")
            return false;

        out.name          = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        out.author        = xml->getStringAttribute ("author", "User");
        out.isFactory      = false;
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
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
            bp.attack            = (float) bandXml->getDoubleAttribute ("attack", SevenpackAudioProcessor::defaultAttackMs);
            bp.release           = (float) bandXml->getDoubleAttribute ("release", SevenpackAudioProcessor::defaultReleaseMs);
            bp.output            = (float) bandXml->getDoubleAttribute ("output", 0.0);
            bp.aboveBelow        = bandXml->getIntAttribute ("aboveBelow", 0);
            bp.rangeExpand       = (float) bandXml->getDoubleAttribute ("rangeExpand", 0.0);
            bp.ratioIndexExpand  = bandXml->getIntAttribute ("ratioIndexExpand", 0);
            bp.attackExpand      = (float) bandXml->getDoubleAttribute ("attackExpand", SevenpackAudioProcessor::defaultAttackMs);
            bp.releaseExpand     = (float) bandXml->getDoubleAttribute ("releaseExpand", SevenpackAudioProcessor::defaultReleaseMs);
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

SevenpackEditorContent::SevenpackEditorContent (SevenpackAudioProcessor& p)
    : processorRef (p)
{
    // Opaque -- paint() couvre deja tout le fond (cache bois + panneau).
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    // Theme deja applique aux globales SevenpackColours AVANT que ce
    // constructeur ne commence a tourner -- voir le membre sentinelle juste
    // avant `content` dans SevenpackAudioProcessorEditor (PluginEditor.h).

    woodImage = juce::ImageCache::getFromMemory (BinaryData::bois_wood_jpg, BinaryData::bois_wood_jpgSize);

    // ========================================================================
    // Interface v2 "Sevenpack Alpha" (maquette 7PACK validee) -- voir
    // AlphaSkin.h / AlphaStrip.h / AlphaMeters.h.
    // ========================================================================

    // --- POWER / A/B / theme (interrupteurs a pilule) ---
    addAndMakeVisible (powerToggle);
    powerToggle.setTooltip ("Power");
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SevenpackAudioProcessor::idPower, powerToggle);

    addAndMakeVisible (abToggle);
    abToggle.setTooltip ("A/B compare");
    abToggle.setToggleState (processorRef.isUsingSlotB(), juce::dontSendNotification);
    abToggle.setLetter (processorRef.isUsingSlotB() ? "B" : "A");
    abToggle.onClick = [this]
    {
        processorRef.selectABSlot (abToggle.getToggleState());
        abToggle.setToggleState (processorRef.isUsingSlotB(), juce::dontSendNotification);
        abToggle.setLetter (processorRef.isUsingSlotB() ? "B" : "A");
    };

    addAndMakeVisible (themeToggle);
    themeToggle.setTooltip ("Dark theme");
    themeToggle.setToggleState (processorRef.uiTheme.load() != 1, juce::dontSendNotification);
    themeToggle.onClick = [this] { applyUiTheme (themeToggle.getToggleState() ? 0 : 1); };

    // Contraste renforce (sous A/B) -- persiste dans processorRef.highContrast.
    // N'agit que sur les knobs (variante C "silhouette", choisie par
    // l'utilisateur). kShowContrastToggle = false le masque et le force a OFF.
    constexpr bool kShowContrastToggle = true;
    addChildComponent (contrastToggle);
    contrastToggle.setVisible (kShowContrastToggle);
    contrastToggle.setTooltip ("High contrast (for washed-out screens)");
    AlphaPalette::highContrastActive = kShowContrastToggle && processorRef.highContrast.load();
    AlphaPalette::finishActive = juce::jlimit (0, 3, processorRef.uiFinish.load()); // Options > FINISH
    AlphaPalette::faderStyleActive = juce::jlimit (0, 2, processorRef.faderStyle.load()); // Options > BAND FADERS
    contrastToggle.setToggleState (AlphaPalette::highContrastActive, juce::dontSendNotification);
    contrastToggle.onClick = [this] { applyHighContrast (contrastToggle.getToggleState()); };

    // --- LEARN (bouton AI) ---
    addAndMakeVisible (learnButton);
    learnButton.setStyle (AlphaButton::Style::Primary); // action principale : lisere lumineux + pulsation pendant LEARN
    learnButton.setFontSize (AlphaType::title);
    learnButton.setHaloMargin (8.0f);
    learnButton.setTooltip ("Listens to a few seconds of audio, then sets the crossovers and each band's Input/Ratio/Attack/Release.");
    learnButton.onClick = [this] { processorRef.startAiLearn(); };

    // --- ANALOG / LINEAR (idDigitalCrossover) ---
    analogButton.setStyle (AlphaButton::Style::Mode); // boutons de mode : discrets quand inactifs (audit)
    linearButton.setStyle (AlphaButton::Style::Mode);
    addAndMakeVisible (analogButton);
    addAndMakeVisible (linearButton);
    analogButton.setTooltip ("1st-order crossover, zero latency (default).");
    linearButton.setTooltip ("Linear-phase FIR crossover, adjustable latency (Options). Very CPU hungry.");
    analogButton.onClick = [this]
    {
        if (auto* linearParam = processorRef.apvts.getParameter (SevenpackAudioProcessor::idDigitalCrossover))
            linearParam->setValueNotifyingHost (0.0f);
    };
    linearButton.onClick = [this]
    {
        auto* linearParam = processorRef.apvts.getParameter (SevenpackAudioProcessor::idDigitalCrossover);
        if (linearParam == nullptr || linearParam->getValue() > 0.5f)
            return;

        // Avertissement CPU en passant ANALOG -> LINEAR (retour utilisateur
        // explicite) -- Cancel laisse le crossover sur ANALOG.
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, "Higher CPU usage",
            "LINEAR crossover uses a linear-phase FIR filter, which is significantly more "
            "CPU-intensive than ANALOG. An octa-core CPU is strongly recommended. Continue?",
            "Continue", "Cancel");

        juce::AlertWindow::showAsync (options.withAssociatedComponent (this), [linearParam] (int alertResult)
        {
            if (alertResult == 1)
                linearParam->setValueNotifyingHost (1.0f);
        });
    };

    // --- LINK IN / LINK OUT (etat sur le processeur, voir PluginProcessor.h) ---
    linkInSwitch.setClickingTogglesState (true);
    addAndMakeVisible (linkInSwitch);
    linkInSwitch.setTooltip ("Moves all IN knobs together (relative)");
    linkInSwitch.setToggleState (processorRef.linkInEnabled.load(), juce::dontSendNotification);
    linkInSwitch.onClick = [this]
    {
        processorRef.linkInEnabled.store (linkInSwitch.getToggleState());
        updateInputLinkVisuals();
    };

    linkOutSwitch.setClickingTogglesState (true);
    addAndMakeVisible (linkOutSwitch);
    linkOutSwitch.setTooltip ("Moves all OUT faders together (relative)");
    linkOutSwitch.setToggleState (processorRef.linkOutEnabled.load(), juce::dontSendNotification);
    linkOutSwitch.onClick = [this]
    {
        // Comme Sixpack : LINK OUT et un groupe SEL (>= 2 bandes) ne sont
        // jamais actifs en meme temps -- allumer LINK OUT ramene la
        // selection a la seule bande principale.
        if (linkOutSwitch.getToggleState())
        {
            bool anyLinked = false;
            for (bool b : linkedBands)
                anyLinked = anyLinked || b;
            if (anyLinked)
            {
                linkedBands.fill (false);
                refreshSelectionVisuals();
            }
        }

        processorRef.linkOutEnabled.store (linkOutSwitch.getToggleState());
        updateOutputLinkVisuals();
    };

    // --- Logo (ouvre About). Mini spectrums IN/OUT supprimes (retour
    // utilisateur : "c'est moche"). ---
    addAndMakeVisible (logoButton);
    logoButton.onClick = [this] { aboutOverlay.showAnimated(); };

    // --- Graphe des crossovers (style Alpha) -- clic sur une zone = SEL ---
    addAndMakeVisible (crossoverSpectrum);
    crossoverSpectrum.setAlphaStyle (true);
    crossoverSpectrum.setBandSelectionEnabled (true);
    crossoverSpectrum.onBandSelected = [this] (int band) { selectBand (band, false); };

    // --- 7 tranches ---
    for (int band = 0; band < kNumBands; ++band)
    {
        buildHiddenBandControls (band);
        bandStrips[(size_t) band] = std::make_unique<AlphaStrip> (processorRef, band);
        bandStrips[(size_t) band]->onSelectClicked = [this] (int b, bool multi) { selectBand (b, multi); };
        bandStrips[(size_t) band]->refreshTheme();
        addAndMakeVisible (*bandStrips[(size_t) band]);
    }

    // Installe les handlers onValueChange (une seule fois -- voir
    // wireInputLink()/wireOutputLink(), qui consultent l'etat des
    // interrupteurs Link a CHAQUE mouvement).
    wireInputLink();
    wireOutputLink();
    updateInputLinkVisuals();
    updateOutputLinkVisuals();
    refreshSelectionVisuals();

    // Calque d'animation LEARN, au-dessus du graphe et des tranches (sous
    // les overlays About/Options/Presets, ramenes devant en fin de constructeur).
    addAndMakeVisible (learnFx);

    // --- THD global + 4 modes (radio exclusif sur idThdMode, toujours un actif) ---
    thdButton.setClickingTogglesState (true);
    thdButton.setFontSize (AlphaType::title);
    thdButton.setTooltip ("THD saturation stage (global, after the bands)");
    addAndMakeVisible (thdButton);
    softClipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SevenpackAudioProcessor::idSoftClip, thdButton);

    static const char* thdModeLabels[4] = { "SOFT", "TAPE", "IRON", "AIR" };
    for (int i = 0; i < 4; ++i)
    {
        thdModeButtons[(size_t) i] = std::make_unique<AlphaButton> (thdModeLabels[i]);
        thdModeButtons[(size_t) i]->setStyle (AlphaButton::Style::Mode);
        addAndMakeVisible (*thdModeButtons[(size_t) i]);
    }
    if (auto* thdModeParamPtr = processorRef.apvts.getParameter (SevenpackAudioProcessor::idThdMode))
    {
        thdModeAttachment = std::make_unique<juce::ParameterAttachment> (
            *thdModeParamPtr,
            [this] (float v)
            {
                const int mode = (int) std::round (v);
                for (int i = 0; i < 4; ++i)
                    thdModeButtons[(size_t) i]->setToggleState (mode == i, juce::dontSendNotification);
            },
            nullptr);
        for (int i = 0; i < 4; ++i)
            thdModeButtons[(size_t) i]->onClick = [this, i] { thdModeAttachment->setValueAsCompleteGesture ((float) i); };
        thdModeAttachment->sendInitialUpdate();
    }

    // --- Mode des aiguilles THD / GR / GONIO, a gauche des tranches
    // (demande explicite : change les aiguilles des 7 bandes) ---
    {
        static const char* labels[3] = { "THD", "GR", "GONIO" };
        static const char* tips[3] = { "Needles show how much THD each band receives.",
                                       "Needles show gain reduction: compression to the right, expansion to the left.",
                                       "Needles become a stereo goniometer of each band, with phase correlation." };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = meterModeButtons[(size_t) i];
            b = std::make_unique<AlphaButton> (labels[i]);
            b->setStyle (AlphaButton::Style::Mode);
            b->setTooltip (tips[i]);
            b->onClick = [this, i] { processorRef.meterMode.store (i); syncMeterModeButtons(); };
            addAndMakeVisible (*b);
        }
        syncMeterModeButtons();
    }

    // --- Menu "3 points" : remplace l'engrenage (demande explicite) ---
    addAndMakeVisible (optionsButton);
    optionsButton.setTooltip ("Options");
    optionsButton.onClick = [this] { showOptionsOverlay(); };

    addChildComponent (aboutOverlay);
    aboutOverlay.setVisible (false);
    aboutOverlay.setLicenseManager (processorRef.licenseManager);
    aboutOverlay.onDismiss = [this] { aboutOverlay.hideAnimated(); };
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
    // Theme SOMBRE/CLAIR/DOS retro -- retour utilisateur explicite : "je
    // souhaite faire un design plus clair qu'on peut changer dans
    // l'engrenage", puis "on peut tester un 3 eme UI... vieille machine
    // MSDOS". Persiste dans processorRef.uiTheme (meme pattern que
    // uiScale), repousse vers le LookAndFeel partage, puis invalide le
    // cache de fond statique (rebuildBackgroundCache() -- renderMetalPanel()
    // lit SevenpackLookAndFeel::isLightThemeStatic()/isDosThemeStatic()) et
    // repaint tout.
    optionsOverlay.onUiThemeSelected = [this] (int themeIndex) { applyUiTheme (themeIndex); };
    // applyGlobalTheme() deja applique tout en haut du constructeur (voir
    // commentaire la-bas) -- juste synchroniser l'etat visuel des boutons ici.
    optionsOverlay.setCurrentUiThemeIndex (processorRef.uiTheme.load());
    // Meme avertissement que Nuke Mext (showLatencyModeMenu()) : Low CPU
    // s'applique immediatement, mais Medium/High CPU demandent confirmation
    // (cout CPU/latence nettement plus eleve).
    optionsOverlay.onLatencySelected = [this] (int index)
    {
        auto* param = processorRef.apvts.getParameter (SevenpackAudioProcessor::idLinearLatencyMode);
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

        juce::AlertWindow::showAsync (options.withAssociatedComponent (this), [this, param, index, currentIndex] (int alertResult)
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
        setParamValue (SevenpackAudioProcessor::idAiMasteringTarget, (float) index);
    };
    optionsOverlay.onAiCompressionModeSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiCompressionMode, (float) index);
    };
    optionsOverlay.onAiCharacterSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiAttackReleaseCharacter, (float) index);
    };
    optionsOverlay.onAiGainReductionSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiGainReductionMode, (float) index);
    };
    optionsOverlay.onAiScopeChanged = [this] (int scopeIndex, bool enabled)
    {
        // "Detectors" retire du visuel (retour utilisateur : "tu peux
        // supprimer detector car ils n'y sont plus") -- idAiScopeDetectors
        // reste declare dans l'APVTS (compat presets), juste plus reglable
        // depuis cette liste.
        static constexpr const char* scopeIds[2] =
        {
            SevenpackAudioProcessor::idAiScopeCrossover,
            SevenpackAudioProcessor::idAiScopeCompression,
        };
        if (scopeIndex >= 0 && scopeIndex < 2)
            setParamValue (scopeIds[scopeIndex], enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiStyleSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiStyle, (float) index);
    };
    optionsOverlay.onAiFidelityChanged = [this] (bool enabled)
    {
        setParamValue (SevenpackAudioProcessor::idAiFidelity, enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiExpandModeSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiExpandMode, (float) index);
    };
    optionsOverlay.onAiExpandCharacterSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiExpandCharacter, (float) index);
    };
    optionsOverlay.onAiExpandAmountSelected = [this] (int index)
    {
        setParamValue (SevenpackAudioProcessor::idAiExpandAmount, (float) index);
    };

    optionsOverlay.onMainOutPairChanged = [this] (int pairIndex)
    {
        processorRef.mainOutPairIndex.store (juce::jmax (0, pairIndex));
    };

    optionsOverlay.onBandOutRoutingChanged = [this] (int band, int comboIndex)
    {
        if (band < 0 || band >= (int) SevenpackAudioProcessor::NumBands)
            return;
        // comboIndex 0 = reste dans le mix ; >=1 = routee (et, en standalone,
        // sur la paire comboIndex-1).
        const bool routed = comboIndex >= 1;
        setParamValue (SevenpackAudioProcessor::bandParamId (SevenpackAudioProcessor::idBandRouteOut, band),
                       routed ? 1.0f : 0.0f);
        if (routed)
            processorRef.bandOutPairIndex[(size_t) band].store (comboIndex - 1);
    };

    // Section INTERFACE des options (Sevenpack Alpha) -- preferences
    // d'affichage persistees sur le processeur (voir PluginProcessor.h).
    optionsOverlay.onNeedleSettingsChanged = [this] (float bounce, float ret)
    {
        processorRef.needleBounce.store (bounce);
        processorRef.needleReturn.store (ret);
    };
    optionsOverlay.onNeedleGlassSelected = [this] (int glass)
    {
        processorRef.needleGlass.store (juce::jlimit (0, 1, glass));
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onGonioLinesSelected = [this] (bool lines)
    {
        processorRef.gonioLines.store (lines);
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onGonioRedPhaseSelected = [this] (bool red)
    {
        processorRef.gonioRedPhase.store (red);
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onFaderStyleSelected = [this] (int styleIdx)
    {
        processorRef.faderStyle.store (juce::jlimit (0, 2, styleIdx));
        AlphaPalette::faderStyleActive = processorRef.faderStyle.load();
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onAttRelIconsSelected = [this] (int style)
    {
        processorRef.attRelIcons.store (juce::jlimit (0, 3, style));
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onSpectrumStyleSelected = [this] (int style)
    {
        processorRef.spectrumStyle.store (juce::jlimit (0, 4, style));
        crossoverSpectrum.repaint();
    };
    optionsOverlay.onSpectrumSmoothSelected = [this] (int smooth)
    {
        processorRef.spectrumSmooth.store (juce::jlimit (0, 1, smooth));
        crossoverSpectrum.repaint();
    };
    optionsOverlay.onSpectrumPeakChanged = [this] (bool on)
    {
        processorRef.spectrumPeak.store (on);
        crossoverSpectrum.repaint();
    };
    optionsOverlay.onFinishSelected = [this] (int finish)
    {
        processorRef.uiFinish.store (juce::jlimit (0, 3, finish));
        AlphaPalette::finishActive = processorRef.uiFinish.load();
        applyUiTheme (processorRef.uiTheme.load()); // memes rafraichissements qu'un changement de theme
    };
    optionsOverlay.onNeedleTipSelected = [this] (bool red)
    {
        processorRef.needleRedTip.store (red);
        for (auto& strip : bandStrips)
            strip->repaint();
    };
    optionsOverlay.onBandColourModeSelected = [this] (int mode)
    {
        processorRef.bandColourMode.store (juce::jlimit (0, 2, mode));
        crossoverSpectrum.repaint();
        for (auto& strip : bandStrips)
            strip->repaint();
    };

    // --- Presets rapides (meme mecanisme que Nuke Mext) ---
    buildPresetList();

    prevPresetButton.onClick = [this] { goToPreset (-1); };
    nextPresetButton.onClick = [this] { goToPreset (1); };
    presetNameButton.onClick = [this] { openPresetBrowser(); };
    copyButton.onClick  = [this] { copyStateToClipboard(); };
    pasteButton.onClick = [this] { pasteStateFromClipboard(); };
    saveButton.onClick  = [this] { promptSavePreset(); };
    deletePresetButton.onClick = [this] { promptDeletePreset(); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    for (juce::Component* c : { (juce::Component*) &prevPresetButton, (juce::Component*) &nextPresetButton, (juce::Component*) &presetNameButton,
                                (juce::Component*) &copyButton, (juce::Component*) &pasteButton, (juce::Component*) &saveButton,
                                (juce::Component*) &deletePresetButton, (juce::Component*) &undoButton, (juce::Component*) &redoButton })
        addAndMakeVisible (*c);

    // Plafond de sortie THD (-0.5 -> -3 dB) -- retour utilisateur : "je
    // souhaite quand meme pouvoir gerer le niveau de sortie apres le THD".
    if (auto* ceilParam = processorRef.apvts.getParameter (SevenpackAudioProcessor::idThdOutput))
    {
        thdCeiling = std::make_unique<AlphaCeilControl> (*ceilParam);
        addAndMakeVisible (*thdCeiling);
    }

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
    presetNameButton.setButtonText ("Presets");
    presetAuthorLabel.setText ({}, juce::dontSendNotification);
    deletePresetButton.setEnabled (false);

    // Overlays au premier plan (ajoutes avant certains controles ci-dessus).
    aboutOverlay.toFront (false);
    optionsOverlay.toFront (false);
    presetBrowser.toFront (false);

    // Boites de dialogue (avertissements CPU, sauvegarde/suppression de
    // preset) aux couleurs Alpha.
    AlphaDraw::styleAlertWindows (lookAndFeel);

    // Annuler/retablir : tout geste souris dans l'editeur (y compris sur
    // les enfants) marque "changement utilisateur" ; premier instantane =
    // etat a l'ouverture.
    addMouseListener (this, true);
    lastSeenParams = captureParamValues();
    undoHistory.push_back (lastSeenParams);
    undoIndex = 0;
    refreshUndoButtons();

    startTimerHz (10);

    setSize (baseWidth, baseHeight);
}

SevenpackEditorContent::~SevenpackEditorContent()
{
    setLookAndFeel (nullptr);
}

void SevenpackEditorContent::timerCallback()
{
    const bool powered = *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idPower) > 0.5f;
    if (powered != lookAndFeel.isPowered())
    {
        lookAndFeel.setPowered (powered);
        repaint();
    }

    // LEARN clignote pendant l'ecoute/l'analyse (meme role que l'ancien
    // AiLearnButton), le graphe anime les crossovers en meme temps.
    {
        const auto st = processorRef.aiLearnAnalyzer.getState();
        const bool busy = st == AiLearnAnalyzer::State::Capturing || st == AiLearnAnalyzer::State::Analyzing;
        learnButton.setForceLit (busy); // enfonce tant que le plugin ecoute/analyse (animation : AlphaLearnFx)
        learnButton.setPulsing (busy);  // halo + texte clignotant, comme la preview HTML
    }

    // Etats lus sur les parametres (automation, presets, A/B...).
    {
        const bool linear = *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idDigitalCrossover) > 0.5f;
        analogButton.setToggleState (! linear, juce::dontSendNotification);
        linearButton.setToggleState (linear, juce::dontSendNotification);
        const bool thdOn = *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idSoftClip) > 0.5f;
        for (auto& b : thdModeButtons)
            b->setDimmed (! thdOn);
        abToggle.setToggleState (processorRef.isUsingSlotB(), juce::dontSendNotification);
        abToggle.setLetter (processorRef.isUsingSlotB() ? "B" : "A");
        syncMeterModeButtons(); // etat restaure par l'hote pendant que l'editeur est ouvert
    }

    // Bouton AI : des que l'analyse en tache de fond est terminee, on
    // applique le resultat puis on acquitte pour permettre un nouvel
    // apprentissage.
    if (processorRef.aiLearnAnalyzer.getState() == AiLearnAnalyzer::State::Done)
    {
        applyAiLearnResult (processorRef.aiLearnAnalyzer.getResult());
        processorRef.aiLearnAnalyzer.acknowledge();
        userTouchedParams = true; // le resultat LEARN est annulable
    }

    pushUndoSnapshotIfChanged();
}

// ============================================================================
// Annuler / retablir -- historique par INSTANTANES de tous les parametres
// (le plugin n'a pas d'UndoManager APVTS). Un instantane est pris quand les
// parametres ont change APRES une action de l'utilisateur dans l'editeur
// (clic, molette, preset, LEARN, coller, A/B) et sont stables depuis 400 ms
// -- un glisser de knob = une seule etape. L'automation de l'hote seule
// (sans geste dans l'editeur) n'alimente pas l'historique.
// ============================================================================

std::vector<float> SevenpackEditorContent::captureParamValues() const
{
    std::vector<float> values;
    for (auto* p : processorRef.getParameters())
        values.push_back (p->getValue());
    return values;
}

void SevenpackEditorContent::pushUndoSnapshotIfChanged()
{
    const auto now = juce::Time::getMillisecondCounter();
    auto current = captureParamValues();

    if (current != lastSeenParams)
    {
        lastSeenParams = std::move (current);
        lastParamChangeMs = now;
        return; // on attend que ca se stabilise
    }

    if (! userTouchedParams || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        return;
    if (now - lastParamChangeMs < 400)
        return;
    if (undoIndex >= 0 && undoHistory[(size_t) undoIndex] == lastSeenParams)
    {
        userTouchedParams = false;
        return;
    }

    // Nouvelle etape : on coupe la branche "retablir" et on borne a 60 etapes.
    undoHistory.resize ((size_t) (undoIndex + 1));
    undoHistory.push_back (lastSeenParams);
    if (undoHistory.size() > 60)
        undoHistory.erase (undoHistory.begin());
    undoIndex = (int) undoHistory.size() - 1;
    userTouchedParams = false;
    refreshUndoButtons();
}

void SevenpackEditorContent::restoreUndoSnapshot (int index)
{
    if (index < 0 || index >= (int) undoHistory.size())
        return;
    const auto& snap = undoHistory[(size_t) index];
    const auto& params = processorRef.getParameters();
    for (size_t i = 0; i < snap.size() && i < (size_t) params.size(); ++i)
    {
        auto* p = params[(int) i];
        if (std::abs (p->getValue() - snap[i]) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (snap[i]);
            p->endChangeGesture();
        }
    }
    undoIndex = index;
    lastSeenParams = snap;
    userTouchedParams = false;
    refreshUndoButtons();
}

void SevenpackEditorContent::undo()
{
    pushUndoSnapshotIfChanged(); // fige un reglage en cours avant de revenir
    restoreUndoSnapshot (undoIndex - 1);
}

void SevenpackEditorContent::redo()
{
    restoreUndoSnapshot (undoIndex + 1);
}

void SevenpackEditorContent::refreshUndoButtons()
{
    undoButton.setEnabled (undoIndex > 0);
    redoButton.setEnabled (undoIndex >= 0 && undoIndex < (int) undoHistory.size() - 1);
}

void SevenpackEditorContent::mouseDown (const juce::MouseEvent&)
{
    userTouchedParams = true;
}

void SevenpackEditorContent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&)
{
    userTouchedParams = true;
}

bool SevenpackEditorContent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (mods.isShiftDown()) redo(); else undo();
        return true;
    }
    if (mods.isCommandDown() && key.getKeyCode() == 'Y')
    {
        redo();
        return true;
    }
    return false;
}

void SevenpackEditorContent::applyUiTheme (int themeIndex)
{
    // Theme SOMBRE/CLAIR/DOS -- persiste dans processorRef.uiTheme, repousse
    // vers le LookAndFeel partage (overlays historiques), puis l'interface
    // Alpha relit AlphaPalette::get() a sa prochaine peinture.
    themeIndex = juce::jlimit (0, 2, themeIndex);
    processorRef.uiTheme.store (themeIndex);
    lookAndFeel.setUiTheme (themeIndex);
    optionsOverlay.setCurrentUiThemeIndex (themeIndex);
    themeToggle.setToggleState (themeIndex != 1, juce::dontSendNotification);
    refreshThemeReactiveColours();
    rebuildBackgroundCache();
    aboutOverlay.refreshTheme();
    optionsOverlay.applyAlphaStyle();
    AlphaDraw::styleAlertWindows (lookAndFeel);
    repaint();
}

void SevenpackEditorContent::applyHighContrast (bool shouldBeOn)
{
    processorRef.highContrast.store (shouldBeOn);
    AlphaPalette::highContrastActive = shouldBeOn;
    contrastToggle.setToggleState (shouldBeOn, juce::dontSendNotification);
    applyUiTheme (processorRef.uiTheme.load()); // memes rafraichissements qu'un changement de theme
}

void SevenpackEditorContent::selectBand (int band, bool isMultiSelectModifier)
{
    if (band < 0 || band >= kNumBands)
        return;

    if (isMultiSelectModifier)
    {
        // Ctrl/Cmd+clic : ajoute/retire la bande du groupe lie, SANS changer
        // la bande principale (meme convention qu'un multi-select fichier).
        if (band != selectedBand)
            linkedBands[(size_t) band] = ! linkedBands[(size_t) band];

        // Des qu'un groupe d'au moins 2 bandes existe, LINK OUT se coupe
        // (deux mecanismes de liaison sur le meme fader -- voir Sixpack).
        bool anyLinked = false;
        for (bool b : linkedBands)
            anyLinked = anyLinked || b;
        if (anyLinked && linkOutSwitch.getToggleState())
        {
            linkOutSwitch.setToggleState (false, juce::dontSendNotification);
            processorRef.linkOutEnabled.store (false);
            updateOutputLinkVisuals();
        }
    }
    else
    {
        selectedBand = band;
        linkedBands.fill (false);
    }

    refreshSelectionVisuals();
}

void SevenpackEditorContent::refreshSelectionVisuals()
{
    for (int b = 0; b < kNumBands; ++b)
        bandStrips[(size_t) b]->setSelected (isInSelectionGroup (b));
    crossoverSpectrum.setSelectedBand (selectedBand);
}

void SevenpackEditorContent::applyAiLearnResult (const AiLearnAnalyzer::Result& result)
{
    // Portee (menu Options, section AI, demande explicite : "AI ne
    // s'occupe que de la compression si on le souhaite") -- chaque
    // categorie n'est poussee vers les parametres QUE si son interrupteur
    // etait actif au moment du clic (voir AiLearnAnalyzer::Result).
    if (result.applyCrossoverScope)
    {
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
            // Strictement identique a Sixpack (demande explicite : "elle
            // doit etre identique au sixpack") -- rampe standard 260 ms.
            rampParamTo (SevenpackAudioProcessor::idCrossover[x], result.crossoverHz[(size_t) x]);
    }

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };

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
                rampParamTo (id (SevenpackAudioProcessor::idInput), result.inputDb[(size_t) band]);
            if (result.pushAttackRelease)
            {
                rampParamTo (id (SevenpackAudioProcessor::idAttack),  result.attackMs[(size_t) band]);
                rampParamTo (id (SevenpackAudioProcessor::idRelease), result.releaseMs[(size_t) band]);
            }
            if (result.pushRatio)
                setParamValue (id (SevenpackAudioProcessor::idRatioIndex),
                               (float) juce::roundToInt (result.ratioIndex[(size_t) band]));

            if (result.pushExpandInput)
                rampParamTo (id (SevenpackAudioProcessor::idInputExpand), result.expandInputDb[(size_t) band]);
            if (result.pushExpandAttackRelease)
            {
                rampParamTo (id (SevenpackAudioProcessor::idAttackExpand),  result.expandAttackMs[(size_t) band]);
                rampParamTo (id (SevenpackAudioProcessor::idReleaseExpand), result.expandReleaseMs[(size_t) band]);
            }
            if (result.pushExpandRatio)
                setParamValue (id (SevenpackAudioProcessor::idRatioIndexExpand),
                               (float) juce::roundToInt (result.expandRatioIndex[(size_t) band]));

            rampParamTo (id (SevenpackAudioProcessor::idBandMix),   result.mixPercent[(size_t) band]);
            rampParamTo (id (SevenpackAudioProcessor::idBandWidth), result.widthPercent[(size_t) band]);
        }

        if (result.applyDetectorsScope)
        {
            setParamValue (id (SevenpackAudioProcessor::idDetectorBell), result.detectorBellOn[(size_t) band] ? 1.0f : 0.0f);
            setParamValue (id (SevenpackAudioProcessor::idDetectorBellFreq), result.detectorBellFreqHz[(size_t) band]);
            setParamValue (id (SevenpackAudioProcessor::idDetectorBellGain), result.detectorBellGainDb[(size_t) band]);
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
            rampParamTo (id (SevenpackAudioProcessor::idOutput), outputDb);
        }
    }

    if (result.applyLoudnessTarget && result.recommendSoftClipOn)
    {
        setParamValue (SevenpackAudioProcessor::idSoftClip, 1.0f);
    }
}

void SevenpackEditorContent::buildHiddenBandControls (int band)
{
    auto& hc = hiddenBandControls[(size_t) band];
    const auto pid = [band] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };

    // DETECTOR/COLOR/XY retires du VISUEL (retour utilisateur explicite :
    // "je souhaite retirer detector, color et le XY. laisse dans le code mais
    // retire du visuel si un jour je veux remettre") -- addChildComponent
    // (jamais addAndMakeVisible) : les attachements restent intacts, ces
    // composants existent juste hors-ecran en permanence.
    for (auto* t : { &hc.detectorHpToggle, &hc.detectorBellToggle, &hc.detectorBell2Toggle, &hc.detectorLinkToggle,
                     &hc.audioHpToggle, &hc.audioDist2Toggle, &hc.audioDist3Toggle })
        addChildComponent (*t);

    // useBell2 ne pilote que le routage DetectorFilterGraph ci-dessous
    // (bell2FreqP/GainP transmis ou non) -- parametre fonctionnel bande par
    // bande (LowMid..High) si jamais l'UI Detector est remise un jour.
    const bool useBell2 = (band != SevenpackAudioProcessor::Low && band != kNumBands - 1);

    hc.detectorHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idDetectorHp), hc.detectorHpToggle);
    hc.detectorBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idDetectorBell), hc.detectorBellToggle);
    hc.detectorBell2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idDetectorBell2), hc.detectorBell2Toggle);
    hc.detectorLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idDetectorLink), hc.detectorLinkToggle);
    hc.audioHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idAudioHp), hc.audioHpToggle);
    hc.audioDist2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idAudioDist2), hc.audioDist2Toggle);
    hc.audioDist3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, pid (SevenpackAudioProcessor::idAudioDist3), hc.audioDist3Toggle);

    // Bornes de l'axe X (voir doc de classe DetectorFilterGraph) : la bande
    // 0 (Low) part de 20Hz, la derniere (Air) finit a 20000Hz, chaque bande
    // intermediaire va du crossover qui la precede a celui qui la suit.
    auto& viewMin = (band == 0) ? kFilterGraphMinHz
                  : *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossover[band - 1]);
    auto& viewMax = (band == kNumBands - 1) ? kFilterGraphMaxHz
                  : *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossover[band]);

    hc.filterGraph = std::make_unique<DetectorFilterGraph> (
        *processorRef.apvts.getRawParameterValue (pid (SevenpackAudioProcessor::idDetectorHp)),
        *processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorHpFreq)),
        *processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorHpSlope)),
        *processorRef.apvts.getRawParameterValue (pid (SevenpackAudioProcessor::idDetectorBell)),
        *processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorBellFreq)),
        *processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorBellGain)),
        band == kNumBands - 1, viewMin, viewMax,
        useBell2 ? processorRef.apvts.getRawParameterValue (pid (SevenpackAudioProcessor::idDetectorBell2)) : nullptr,
        useBell2 ? processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorBell2Freq)) : nullptr,
        useBell2 ? processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idDetectorBell2Gain)) : nullptr);
    addChildComponent (*hc.filterGraph);

    // Bande la plus haute (Air) : le point HP devient LP (meme mecanique).
    if (band == kNumBands - 1)
        hc.detectorHpToggle.setText ("LP");
}

// Lien RELATIF (delta), pas absolu : si Low=10dB et Mid=5dB et qu'on bouge
// Low de +3dB, Mid passe a 8dB (l'ecart de 5dB entre les 2 est preserve)
// plutot que de forcer tout le monde a la meme valeur. Installe UNE FOIS
// (pas a chaque clic sur le bouton Link) -- le handler consulte l'etat du
// bouton a chaque mouvement. Meme principe que Nuke Mext.
void SevenpackEditorContent::wireInputLink()
{
    for (int band = 0; band < kNumBands; ++band)
        lastInputValue[(size_t) band] = bandStrips[(size_t) band]->getInputSlider().getValue();

    for (int band = 0; band < kNumBands; ++band)
    {
        auto& slider = bandStrips[(size_t) band]->getInputSlider();
        auto previous = slider.onValueChange;

        slider.onValueChange = [this, band, previous]
        {
            if (previous)
                previous();

            auto& thisSlider = bandStrips[(size_t) band]->getInputSlider();
            const double newValue = thisSlider.getValue();

            if (linkInSwitch.getToggleState() && ! linkInGuard)
            {
                const double delta = newValue - lastInputValue[(size_t) band];

                if (std::abs (delta) > 1.0e-9)
                {
                    linkInGuard = true;
                    for (int b = 0; b < kNumBands; ++b)
                    {
                        if (b == band)
                            continue;
                        auto& other = bandStrips[(size_t) b]->getInputSlider();
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

void SevenpackEditorContent::wireOutputLink()
{
    for (int band = 0; band < kNumBands; ++band)
        lastOutputValue[(size_t) band] = bandStrips[(size_t) band]->getOutputSlider().getValue();

    for (int band = 0; band < kNumBands; ++band)
    {
        auto& slider = bandStrips[(size_t) band]->getOutputSlider();
        auto previous = slider.onValueChange;

        slider.onValueChange = [this, band, previous]
        {
            if (previous)
                previous();

            auto& thisSlider = bandStrips[(size_t) band]->getOutputSlider();
            const double newValue = thisSlider.getValue();
            const double delta = newValue - lastOutputValue[(size_t) band];

            // LINK OUT = les 7 bandes ; sinon groupe SEL (Ctrl/Cmd+clic, voir
            // selectBand()) = seulement les bandes du groupe -- "pairage
            // console" de Sixpack, delta relatif. Jamais les deux a la fois.
            const bool allLinked = linkOutSwitch.getToggleState();
            int groupSize = 0;
            for (int b = 0; b < kNumBands; ++b)
                if (isInSelectionGroup (b))
                    ++groupSize;
            const bool groupLinked = ! allLinked && groupSize > 1 && isInSelectionGroup (band);

            if ((allLinked || groupLinked) && ! linkOutGuard && std::abs (delta) > 1.0e-9)
            {
                linkOutGuard = true;
                for (int b = 0; b < kNumBands; ++b)
                {
                    if (b == band || (groupLinked && ! isInSelectionGroup (b)))
                        continue;
                    auto& other = bandStrips[(size_t) b]->getOutputSlider();
                    const double target = juce::jlimit (other.getMinimum(), other.getMaximum(), other.getValue() + delta);
                    other.setValue (target, juce::sendNotificationSync);
                }
                linkOutGuard = false;
            }

            lastOutputValue[(size_t) band] = thisSlider.getValue();
        };
    }
}

void SevenpackEditorContent::updateInputLinkVisuals()  { updateLinkVisuals(); }
void SevenpackEditorContent::updateOutputLinkVisuals() { updateLinkVisuals(); }

// Libelles IN (vert) / OUT (rouge) de chaque tranche quand LINK IN / LINK
// OUT sont actifs -- meme code couleur que les 2 boutons du header.
void SevenpackEditorContent::updateLinkVisuals()
{
    for (auto& strip : bandStrips)
        if (strip != nullptr)
            strip->setLinkStates (linkInSwitch.getToggleState(), linkOutSwitch.getToggleState());
}

void SevenpackEditorContent::refreshThemeReactiveColours()
{
    // Les juce::Label "bakent" Label::textColourId au moment de leur
    // creation (simple copie de valeur) -- un changement de theme EN COURS
    // D'EXECUTION ne les met donc pas a jour tout seul (retour utilisateur :
    // "quand on desactive le light theme les ecritures sont blanches").
    // Les tranches, elles, relisent les globales a chaque paint() : seul
    // leur cache de plaque doit etre invalide.
    presetAuthorLabel.setColour (juce::Label::textColourId, SevenpackColours::textDim);

    for (auto& strip : bandStrips)
        if (strip != nullptr)
            strip->refreshTheme();
}

void SevenpackEditorContent::showOptionsOverlay()
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
    if (auto* param = processorRef.apvts.getParameter (SevenpackAudioProcessor::idLinearLatencyMode))
        latencyIndex = (int) std::round (param->convertFrom0to1 (param->getValue()));
    optionsOverlay.setCurrentLatencyIndex (latencyIndex);

    optionsOverlay.setCurrentAiTargetIndex ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiMasteringTarget)));
    optionsOverlay.setCurrentAiCompressionMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiCompressionMode)));
    optionsOverlay.setCurrentAiCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiAttackReleaseCharacter)));
    optionsOverlay.setCurrentAiGainReduction ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiGainReductionMode)));
    optionsOverlay.setAiScopeStates (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiScopeCrossover) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiScopeCompression) > 0.5f);
    optionsOverlay.setCurrentAiStyle ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiStyle)));
    optionsOverlay.setAiFidelityState (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiFidelity) > 0.5f);
    optionsOverlay.setCurrentAiExpandMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiExpandMode)));
    optionsOverlay.setCurrentAiExpandCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiExpandCharacter)));
    optionsOverlay.setCurrentAiExpandAmount ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idAiExpandAmount)));

    {
        const bool standalone = processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;

        juce::StringArray pairLabels;
        if (standalone)
        {
            const auto names = processorRef.getStandaloneOutputChannelNames();
            const int pairs = names.size() / 2;
            for (int p = 0; p < pairs; ++p)
                pairLabels.add (juce::String (p * 2 + 1) + "-" + juce::String (p * 2 + 2));
        }
        optionsOverlay.setOutputRoutingOptions (standalone, pairLabels);

        const int pairCount = juce::jmax (1, pairLabels.size());
        optionsOverlay.setMainOutPair (juce::jlimit (0, pairCount - 1, processorRef.mainOutPairIndex.load()));

        for (int b = 0; b < (int) SevenpackAudioProcessor::NumBands; ++b)
        {
            const bool routed = *processorRef.apvts.getRawParameterValue (
                SevenpackAudioProcessor::bandParamId (SevenpackAudioProcessor::idBandRouteOut, b)) > 0.5f;
            int comboIndex = 0; // dans le mix
            if (routed)
                comboIndex = standalone
                    ? juce::jlimit (0, pairCount - 1, processorRef.bandOutPairIndex[(size_t) b].load()) + 1
                    : 1; // VST3 : "sur son bus"
            optionsOverlay.setBandOutRouting (b, comboIndex);
        }
    }

    optionsOverlay.setNeedleSettings (processorRef.needleBounce.load(), processorRef.needleReturn.load());
    optionsOverlay.setBandColourMode (processorRef.bandColourMode.load());
    optionsOverlay.setNeedleGlass (processorRef.needleGlass.load());
    optionsOverlay.setNeedleRedTip (processorRef.needleRedTip.load());
    optionsOverlay.setGonioLines (processorRef.gonioLines.load());
    optionsOverlay.setGonioRedPhase (processorRef.gonioRedPhase.load());
    optionsOverlay.setFinish (processorRef.uiFinish.load());
    optionsOverlay.setFaderStyle (processorRef.faderStyle.load());
    optionsOverlay.setAttRelIcons (processorRef.attRelIcons.load());
    optionsOverlay.setSpectrumStyle (processorRef.spectrumStyle.load());
    optionsOverlay.setSpectrumSmooth (processorRef.spectrumSmooth.load());
    optionsOverlay.setSpectrumPeak (processorRef.spectrumPeak.load());

    optionsOverlay.applyAlphaStyle();
    optionsOverlay.setVisible (true);
    optionsOverlay.toFront (true);
}

void SevenpackEditorContent::resized()
{
    // Interface Alpha (maquette 7PACK) : cadre bois sur tout le pourtour,
    // panneau metal arrondi pose dessus (14px de bois visible), puis de
    // haut en bas : en-tete (96), graphe des crossovers (130), 7 tranches,
    // barre du bas (44). Taille de fenetre INCHANGEE (1400x808).
    panelBounds = getLocalBounds().toFloat().reduced (14.0f);
    auto area = panelBounds;

    headerBounds = area.removeFromTop (96.0f);
    footerBounds = area.removeFromBottom (44.0f);
    area.removeFromTop (8.0f);
    crossoverEditorBounds = area.removeFromTop (130.0f).reduced (22.0f, 0.0f);
    area.removeFromTop (10.0f);
    area.removeFromBottom (8.0f);
    stripsBounds = area.reduced (22.0f, 0.0f);

    crossoverSpectrum.setBounds (crossoverEditorBounds.toNearestInt());

    {
        constexpr float gap = 6.0f;
        const float stripW = (stripsBounds.getWidth() - gap * (float) (kNumBands - 1)) / (float) kNumBands;
        for (int band = 0; band < kNumBands; ++band)
        {
            const int x0 = juce::roundToInt (stripsBounds.getX() + (float) band * (stripW + gap));
            const int x1 = juce::roundToInt (stripsBounds.getX() + (float) band * (stripW + gap) + stripW);
            bandStrips[(size_t) band]->setBounds (x0, juce::roundToInt (stripsBounds.getY()), x1 - x0, juce::roundToInt (stripsBounds.getHeight()));
        }
    }

    // --- En-tete ---
    {
        auto h = headerBounds.reduced (22.0f, 0.0f);
        const float cy = h.getCentreY();

        // Groupes separes par un filet grave (demande explicite) :
        // [ ON/OFF  A/B / THEME CONTRASTE ] | [ LEARN ] | [ ANALOG / LINEAR ]
        // | [ THD / GR / GONIO ] ... logo ... [ LINK IN / OUT ] | [ THD |
        // modes ] | [ options ]
        // (LINK passe a droite : "surcharge a gauche").
        constexpr float groupGap = 12.0f;
        float x = h.getX();

        powerToggle.setBounds (juce::Rectangle<float> (x, cy - 30.0f, 56.0f, 26.0f).toNearestInt());
        abToggle.setBounds    (juce::Rectangle<float> (x + 70.0f, cy - 30.0f, 56.0f, 26.0f).toNearestInt());
        themeToggle.setBounds (juce::Rectangle<float> (x, cy + 4.0f, 56.0f, 26.0f).toNearestInt());
        contrastToggle.setBounds (juce::Rectangle<float> (x + 70.0f, cy + 4.0f, 56.0f, 26.0f).toNearestInt());
        x += 126.0f + groupGap;
        headerSeparatorX[0] = x;
        x += groupGap;

        // LEARN plus grand que les autres (action principale, audit) ; les
        // bounds incluent la marge du halo (setHaloMargin (8)).
        learnButton.setBounds (juce::Rectangle<float> (x, cy - 41.0f, 72.0f, 82.0f).expanded (8.0f).toNearestInt());
        x += 72.0f + groupGap;
        headerSeparatorX[1] = x;
        x += groupGap;

        analogButton.setBounds (juce::Rectangle<float> (x, cy - 38.0f, 70.0f, 37.0f).toNearestInt());
        linearButton.setBounds (juce::Rectangle<float> (x, cy + 1.0f,  70.0f, 37.0f).toNearestInt());
        x += 70.0f + groupGap;
        headerSeparatorX[5] = x;
        x += groupGap;

        // Mode des aiguilles THD / GR / GONIO (demande explicite : dans
        // l'en-tete, plus a gauche des tranches) -- 3 boutons empiles.
        for (int i = 0; i < 3; ++i)
            meterModeButtons[(size_t) i]->setBounds (juce::Rectangle<float> (x, cy - 38.0f + 26.0f * (float) i, 70.0f, 24.0f).toNearestInt());

        // Logo centre + mini spectrums de part et d'autre (graves a gauche
        // pour les deux). La progression LEARN est en bas a droite (voir
        // la barre du bas).
        auto logo = juce::Rectangle<float> (220.0f, 52.0f).withCentre ({ panelBounds.getCentreX(), cy - 3.0f });
        logoButton.setBounds (logo.toNearestInt());

        // Cote droit structure comme le gauche (audit) : [ THD | modes ] | [ options ].
        optionsButton.setBounds (juce::Rectangle<float> (h.getRight() - 24.0f, cy - 22.0f, 24.0f, 44.0f).toNearestInt());
        headerSeparatorX[4] = h.getRight() - 24.0f - groupGap;
        const float tx = headerSeparatorX[4] - groupGap - 200.0f;
        headerSeparatorX[3] = tx - groupGap;
        const float lx = headerSeparatorX[3] - groupGap - 70.0f;
        linkInSwitch.setBounds  (juce::Rectangle<float> (lx, cy - 38.0f, 70.0f, 37.0f).toNearestInt());
        linkOutSwitch.setBounds (juce::Rectangle<float> (lx, cy + 1.0f,  70.0f, 37.0f).toNearestInt());
        headerSeparatorX[2] = lx - groupGap;
        thdButton.setBounds (juce::Rectangle<float> (tx, cy - 38.0f, 64.0f, 76.0f).toNearestInt());
        thdModeButtons[0]->setBounds (juce::Rectangle<float> (tx + 68.0f,  cy - 38.0f, 64.0f, 37.0f).toNearestInt());
        thdModeButtons[1]->setBounds (juce::Rectangle<float> (tx + 136.0f, cy - 38.0f, 64.0f, 37.0f).toNearestInt());
        thdModeButtons[2]->setBounds (juce::Rectangle<float> (tx + 68.0f,  cy + 1.0f,  64.0f, 37.0f).toNearestInt());
        thdModeButtons[3]->setBounds (juce::Rectangle<float> (tx + 136.0f, cy + 1.0f,  64.0f, 37.0f).toNearestInt());
    }

    // --- Barre du bas : THD CEIL a gauche, presets au centre, actions a droite ---
    {
        auto f = footerBounds.reduced (22.0f, 0.0f);
        const float cy = f.getCentreY();
        thdCeiling->setBounds (juce::Rectangle<float> (f.getX(), cy - 14.0f, 200.0f, 28.0f).toNearestInt());

        auto pill = juce::Rectangle<float> (380.0f, 24.0f).withCentre ({ panelBounds.getCentreX(), cy });
        presetNameButton.setBounds (pill.toNearestInt());
        prevPresetButton.setBounds (juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ pill.getX() - 18.0f, cy }).toNearestInt());
        nextPresetButton.setBounds (juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ pill.getRight() + 18.0f, cy }).toNearestInt());
        // Annuler / retablir a gauche des fleches de preset (maquette).
        redoButton.setBounds (juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ pill.getX() - 54.0f, cy }).toNearestInt());
        undoButton.setBounds (juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ pill.getX() - 84.0f, cy }).toNearestInt());

        // Copier / coller / enregistrer / corbeille juste a droite de la
        // barre des presets (demande explicite) ; le coin bas droit recoit
        // la progression LEARN.
        float x = pill.getRight() + 40.0f;
        for (auto* b : { &copyButton, &pasteButton, &saveButton, &deletePresetButton })
        {
            b->setBounds (juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ x, cy }).toNearestInt());
            x += 30.0f;
        }
        learnProgressBounds = juce::Rectangle<float> (f.getRight() - 240.0f, cy - 9.0f, 240.0f, 18.0f);
    }

    {
        std::array<juce::Rectangle<float>, 7> stripRects {};
        for (int band = 0; band < kNumBands; ++band)
            stripRects[(size_t) band] = bandStrips[(size_t) band]->getBounds().toFloat().reduced (1.0f);
        learnFx.setBounds (getLocalBounds());
        learnFx.setTargets (panelBounds, crossoverEditorBounds, stripRects, learnProgressBounds);
    }

    aboutOverlay.setBounds (getLocalBounds());
    optionsOverlay.setBounds (getLocalBounds());
    presetBrowser.setBounds (getLocalBounds());

    rebuildBackgroundCache();
}

void SevenpackEditorContent::rebuildBackgroundCache()
{
    const int w = juce::jmax (1, getWidth());
    const int h = juce::jmax (1, getHeight());
    backgroundCache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (backgroundCache);
    const auto pal = AlphaPalette::get();

    // Cadre bois (texture "boissunny", demande explicite : "on garde
    // l'integration bois en AP") -- cadrage "cover", vignette legere.
    if (woodImage.isValid())
        g.drawImage (woodImage, backgroundCache.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    else
        g.fillAll (juce::Colour (0xffb5803f));
    {
        juce::ColourGradient vignette (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.5f,
                                       juce::Colours::black.withAlpha (0.30f), 0.0f, 0.0f, true);
        g.setGradientFill (vignette);
        g.fillAll();
    }

    // Panneau metal (clair = argent brosse, sombre = graphite).
    constexpr float corner = 12.0f;
    {
        juce::Path p; p.addRoundedRectangle (panelBounds, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 18, { 0, 6 }).drawForPath (g, p);

        juce::ColourGradient grad (pal.panelTop, 0.0f, panelBounds.getY(), pal.panelBottom, 0.0f, panelBounds.getBottom(), false);
        grad.addColour (0.20, pal.panelUpper);
        grad.addColour (0.46, pal.panelMid);
        grad.addColour (0.68, pal.panelLower);
        g.setGradientFill (grad);
        g.fillPath (p);

        // Brossage horizontal fin.
        g.saveState();
        g.reduceClipRegion (p);
        juce::Random rng (11);
        for (int y = (int) panelBounds.getY(); y < (int) panelBounds.getBottom(); y += 2)
        {
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + rng.nextFloat() * 0.018f));
            g.fillRect (panelBounds.getX(), (float) y, panelBounds.getWidth(), 1.0f);
        }
        g.restoreState();

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (panelBounds.reduced (0.5f), corner, 1.0f);
    }

    // Filets de separation sous l'en-tete et au-dessus de la barre du bas.
    auto rule = [&g, &pal] (float y, float x0, float x1)
    {
        g.setColour (pal.sep);
        g.fillRect (x0, y, x1 - x0, 1.0f);
        g.setColour (pal.sepHi);
        g.fillRect (x0, y + 1.0f, x1 - x0, 1.0f);
    };
    rule (headerBounds.getBottom() - 5.0f, panelBounds.getX() + 22.0f, panelBounds.getRight() - 22.0f); // remonte : laisse la place a la cuvette de l'ecran
    rule (footerBounds.getY() - 1.0f, panelBounds.getX() + 22.0f, panelBounds.getRight() - 22.0f);

    // Ecran des crossovers ENFONCE dans le rack (demande explicite) : cuvette
    // biseautee autour de l'ecran -- haut dans l'ombre du rebord, bas en
    // levre claire. L'ombre interieure de l'ecran lui-meme est peinte par
    // CrossoverSpectrumEditor::paintAlpha().
    {
        const auto well = crossoverEditorBounds.expanded (7.0f);
        constexpr float wellCorner = 21.0f;
        g.setColour (juce::Colours::white.withAlpha (pal.light ? 0.85f : 0.10f)); // levre : 1 px sous la cuvette
        g.fillRoundedRectangle (well.translated (0.0f, 1.0f), wellCorner);
        juce::ColourGradient wg (pal.light ? juce::Colour (0xff80878e) : juce::Colour (0xff0b0d0f), 0.0f, well.getY(),
                                 pal.light ? juce::Colour (0xfff7f9fa) : juce::Colour (0xff3a4148), 0.0f, well.getBottom(), false);
        wg.addColour (0.22, pal.light ? juce::Colour (0xffa9afb5) : juce::Colour (0xff15191d));
        wg.addColour (0.70, pal.light ? juce::Colour (0xffd3d7db) : juce::Colour (0xff272d33));
        g.setGradientFill (wg);
        g.fillRoundedRectangle (well, wellCorner);
        g.saveState();
        juce::Path wp; wp.addRoundedRectangle (well, wellCorner);
        g.reduceClipRegion (wp);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, well.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, well.getY() + 6.0f, false));
        g.fillRect (well.withHeight (6.0f));
        g.restoreState();
    }

    // Filets verticaux graves entre les groupes de l'en-tete.
    for (float sx : headerSeparatorX)
    {
        const float y0 = headerBounds.getCentreY() - 36.0f, y1 = headerBounds.getCentreY() + 36.0f;
        g.setColour (pal.sep);
        g.fillRect (sx - 0.5f, y0, 1.0f, y1 - y0);
        g.setColour (pal.sepHi);
        g.fillRect (sx + 0.5f, y0, 1.0f, y1 - y0);
    }
}

void SevenpackEditorContent::paint (juce::Graphics& g)
{
    if (backgroundCache.isValid())
        g.drawImageAt (backgroundCache, 0, 0);
    else
        g.fillAll (juce::Colour (0xffb5803f));
}

// ============================================================================
// Presets -- meme mecanisme que Nuke Mext (voir PluginEditor.h::PresetData) :
// une barre rapide (Prev/Next/nom/Copy/Paste/Save/Delete) + un navigateur
// plein ecran (PresetBrowserOverlay). Presets d'usine propres au moteur
// Suppressor (Ratio/Attack/Release/Detector HP+Bell+Link/Audio Dist2+Dist3/
// Optical/British/Mix/Width), pas ceux de Mext qui ne correspondent a rien
// ici (moteur DSP different).
// ============================================================================

void SevenpackEditorContent::setParamValue (const juce::String& paramID, float rawValue)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
}

void SevenpackEditorContent::rampParamTo (const juce::String& paramID, float rawValue, int durationMs)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        knobRamper.rampTo (p, p->convertTo0to1 (rawValue), durationMs);
}

void SevenpackEditorContent::buildPresetList()
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
            band.detectorHpFreq = (b == kNumBands - 1) ? 16000.0f : 50.0f;
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

void SevenpackEditorContent::loadUserPresets()
{
    auto dir = getUserPresetsDirectory();
    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        PresetData p;
        if (readPresetXml (file, p))
            presets.push_back (p);
    }
}

void SevenpackEditorContent::goToPreset (int delta)
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

void SevenpackEditorContent::syncPresetBrowserEntries()
{
    std::vector<PresetBrowserOverlay::Entry> entries;
    entries.reserve (presets.size());
    for (auto& p : presets)
        entries.push_back ({ p.name, p.author, p.isFactory });

    presetBrowser.setEntries (std::move (entries));
    presetBrowser.setCurrentIndex (currentPresetIndex);
}

void SevenpackEditorContent::openPresetBrowser()
{
    syncPresetBrowserEntries();
    presetBrowser.setBounds (getLocalBounds());
    presetBrowser.show();
}

void SevenpackEditorContent::refreshPresetDisplay()
{
    if (currentPresetIndex < 0 || currentPresetIndex >= (int) presets.size())
    {
        presetNameButton.setButtonText ("Default");
        presetNameButton.setAuthor ({});
        presetAuthorLabel.setText ({}, juce::dontSendNotification);
        deletePresetButton.setEnabled (false);
        return;
    }

    const auto& p = presets[(size_t) currentPresetIndex];
    presetNameButton.setButtonText (p.name);
    presetNameButton.setAuthor (p.author);
    presetAuthorLabel.setText (p.author, juce::dontSendNotification);
    deletePresetButton.setEnabled (! p.isFactory);
}

void SevenpackEditorContent::applyPresetData (const PresetData& preset)
{
    // Knobs continus -- ANIMES (voir ParamRamper/rampParamTo()) pour que
    // les knobs tournent visiblement au lieu de teleporter (demande
    // explicite). Tout le reste (booleens/discrets : ratio, toggles
    // Detector/Audio, Mix/Width...) reste instantane, pas d'equivalent
    // visuel a animer.
    for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
    {
        rampParamTo (SevenpackAudioProcessor::idCrossover[x], preset.crossoverHz[(size_t) x]);
        setParamValue (SevenpackAudioProcessor::idCrossoverEnabled[x], preset.crossoverEnabled[(size_t) x] ? 1.0f : 0.0f);
    }
    setParamValue (SevenpackAudioProcessor::idDigitalCrossover, preset.linearMode ? 1.0f : 0.0f);
    setParamValue (SevenpackAudioProcessor::idSoftClip, preset.softClipOn ? 1.0f : 0.0f);
    setParamValue (SevenpackAudioProcessor::idThdMode, (float) preset.thdMode);

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };
        const auto& bp = preset.bands[band];

        rampParamTo (id (SevenpackAudioProcessor::idInput), bp.input);
        setParamValue (id (SevenpackAudioProcessor::idRatioIndex), (float) bp.ratioIndex);
        rampParamTo (id (SevenpackAudioProcessor::idAttack), bp.attack);
        rampParamTo (id (SevenpackAudioProcessor::idRelease), bp.release);
        rampParamTo (id (SevenpackAudioProcessor::idOutput), bp.output);

        // Above/Below/Both + jeu Expand dedie (demande explicite :
        // "recalibre les valeurs range").
        setParamValue (id (SevenpackAudioProcessor::idAboveBelow), (float) bp.aboveBelow);
        rampParamTo (id (SevenpackAudioProcessor::idInputExpand), bp.rangeExpand);
        setParamValue (id (SevenpackAudioProcessor::idRatioIndexExpand), (float) bp.ratioIndexExpand);
        rampParamTo (id (SevenpackAudioProcessor::idAttackExpand), bp.attackExpand);
        rampParamTo (id (SevenpackAudioProcessor::idReleaseExpand), bp.releaseExpand);
        rampParamTo (id (SevenpackAudioProcessor::idOutputExpand), bp.outputExpand);

        setParamValue (id (SevenpackAudioProcessor::idDetectorHp), bp.detectorHp ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idDetectorHpFreq), bp.detectorHpFreq);
        setParamValue (id (SevenpackAudioProcessor::idDetectorHpSlope), (float) bp.detectorHpSlope);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBell), bp.detectorBell ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBellFreq), bp.detectorBellFreq);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBellGain), bp.detectorBellGain);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBell2), bp.detectorBell2 ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBell2Freq), bp.detectorBell2Freq);
        setParamValue (id (SevenpackAudioProcessor::idDetectorBell2Gain), bp.detectorBell2Gain);
        setParamValue (id (SevenpackAudioProcessor::idDetectorLink), bp.detectorLink ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idSidechainEnable), bp.sidechainEnable ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idAudioHp), bp.audioHp ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idAudioDist2), bp.audioDist2 ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idAudioDist3), bp.audioDist3 ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idOptical), bp.optical ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idBritish), bp.british ? 1.0f : 0.0f);
        setParamValue (id (SevenpackAudioProcessor::idBandMix), bp.bandMix);
        setParamValue (id (SevenpackAudioProcessor::idBandWidth), bp.bandWidth);
    }
}

SevenpackEditorContent::PresetData SevenpackEditorContent::captureCurrentPresetData (
    const juce::String& name, const juce::String& author, bool isFactory)
{
    PresetData p;
    p.name = name;
    p.author = author;
    p.isFactory = isFactory;

    for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
    {
        p.crossoverHz[(size_t) x] = processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossover[x])->load();
        p.crossoverEnabled[(size_t) x] = processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossoverEnabled[x])->load() > 0.5f;
    }
    p.linearMode    = processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idDigitalCrossover)->load() > 0.5f;
    p.softClipOn    = processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idSoftClip)->load() > 0.5f;
    p.thdMode       = (int) std::round (processorRef.apvts.getRawParameterValue (SevenpackAudioProcessor::idThdMode)->load());

    for (int band = 0; band < kNumBands; ++band)
    {
        auto id = [band] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };
        auto& bp = p.bands[band];

        bp.input           = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idInput))->load();
        bp.ratioIndex       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idRatioIndex))->load());
        bp.attack           = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAttack))->load();
        bp.release          = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idRelease))->load();
        bp.output           = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idOutput))->load();

        bp.aboveBelow       = (int) std::round (processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAboveBelow))->load());
        bp.rangeExpand      = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idInputExpand))->load();
        bp.ratioIndexExpand = (int) std::round (processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idRatioIndexExpand))->load());
        bp.attackExpand     = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAttackExpand))->load();
        bp.releaseExpand    = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idReleaseExpand))->load();
        bp.outputExpand     = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idOutputExpand))->load();

        bp.detectorHp       = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorHp))->load() > 0.5f;
        bp.detectorHpFreq   = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorHpFreq))->load();
        bp.detectorHpSlope  = (int) std::round (processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorHpSlope))->load());
        bp.detectorBell     = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBell))->load() > 0.5f;
        bp.detectorBellFreq = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBellFreq))->load();
        bp.detectorBellGain = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBellGain))->load();
        bp.detectorBell2     = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBell2))->load() > 0.5f;
        bp.detectorBell2Freq = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBell2Freq))->load();
        bp.detectorBell2Gain = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorBell2Gain))->load();
        bp.detectorLink     = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idDetectorLink))->load() > 0.5f;
        bp.sidechainEnable  = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idSidechainEnable))->load() > 0.5f;
        bp.audioHp          = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAudioHp))->load() > 0.5f;
        bp.audioDist2       = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAudioDist2))->load() > 0.5f;
        bp.audioDist3       = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idAudioDist3))->load() > 0.5f;
        bp.optical          = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idOptical))->load() > 0.5f;
        bp.british          = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idBritish))->load() > 0.5f;
        bp.bandMix          = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idBandMix))->load();
        bp.bandWidth        = processorRef.apvts.getRawParameterValue (id (SevenpackAudioProcessor::idBandWidth))->load();
    }

    return p;
}

void SevenpackEditorContent::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Enter a name and creator for this preset:",
                                       juce::MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lookAndFeel); // couleurs Alpha (voir AlphaDraw::styleAlertWindows)
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

void SevenpackEditorContent::promptDeletePreset()
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

    juce::AlertWindow::showAsync (options.withAssociatedComponent (this), [this, name] (int result)
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
void SevenpackEditorContent::copyStateToClipboard()
{
    juce::SystemClipboard::copyTextToClipboard (processorRef.apvts.copyState().toXmlString());
}

void SevenpackEditorContent::pasteStateFromClipboard()
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
