#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "PluginProcessor.h"
#include "KnobSlider.h"
#include "RatioSelector.h"
#include "ToggleSwitch.h"
#include "RackPushButton.h"

/**
    HorizontalLedBarLookAndFeel
    ---------------------------
    Variante HORIZONTALE de FaderVerticalLookAndFeel (FaderControl.h) pour
    Dry/Wet et Spread dans les tranches -- meme principe : puits en creux,
    segments allumes en couleur d'accent entre la valeur NEUTRE (voir
    setNeutralValue()) et la valeur courante, 2 tons de part et d'autre du
    neutre (Spread : cyan cote mono, ambre cote stereo), eteints hors
    tension (POWER).
*/
class HorizontalLedBarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                            float, float, float, const juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto outer = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const bool dos = SevenpackLookAndFeel::isDosThemeStatic();
        const float corner = dos ? 0.0f : 3.0f;

        g.setColour (dos ? juce::Colours::black : juce::Colour (0xff08090a));
        g.fillRoundedRectangle (outer, corner);
        g.setColour (dos ? accentColour : juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (outer.reduced (0.5f), corner, 1.0f);

        const auto bounds = outer.reduced (3.0f, 3.0f);
        constexpr int numSegs = 14;
        constexpr float gap = 2.0f;
        const float segW = (bounds.getWidth() - gap * (float) (numSegs - 1)) / (float) numSegs;

        const double neutral = slider.getProperties().contains (neutralValuePropertyId)
                                    ? (double) slider.getProperties()[neutralValuePropertyId]
                                    : slider.getMinimum();
        const double range = slider.getMaximum() - slider.getMinimum();
        const float neutralFrac = range > 0.0 ? (float) ((neutral - slider.getMinimum()) / range) : 0.0f;
        const float valueFrac   = range > 0.0 ? (float) ((slider.getValue() - slider.getMinimum()) / range) : 0.0f;
        const float loFrac = juce::jmin (neutralFrac, valueFrac);
        const float hiFrac = juce::jmax (neutralFrac, valueFrac);

        for (int i = 0; i < numSegs; ++i)
        {
            const float segCentre = ((float) i + 0.5f) / (float) numSegs;
            const bool lit = powered && segCentre >= loFrac && segCentre <= hiFrac;
            const auto litColour = (segCentre < neutralFrac) ? accentColourBelowNeutral : accentColour;
            auto seg = juce::Rectangle<float> (bounds.getX() + (float) i * (segW + gap), bounds.getY(), segW, bounds.getHeight());

            g.setColour (lit ? litColour : SevenpackColours::gridLine);
            g.fillRoundedRectangle (seg, dos ? 0.0f : 1.5f);
        }
    }

    // Pas de pouce a dessiner -- sans ca, JUCE retrecit la zone de la barre
    // de ce rayon de chaque cote (bord gauche/droit decolles du puits).
    int getSliderThumbRadius (juce::Slider&) override { return 0; }

    void setAccentColour (juce::Colour c) { accentColour = c; if (! twoTone) accentColourBelowNeutral = c; }
    void setAccentColourBelowNeutral (juce::Colour c) { accentColourBelowNeutral = c; twoTone = true; }
    void setPowered (bool shouldBePowered) noexcept { powered = shouldBePowered; }

    static void setNeutralValue (juce::Slider& slider, double value)
    {
        slider.getProperties().set (neutralValuePropertyId, value);
    }

private:
    static constexpr const char* neutralValuePropertyId = "ledBarNeutralValue";
    juce::Colour accentColour = SevenpackColours::ledCyan;
    juce::Colour accentColourBelowNeutral = SevenpackColours::ledCyan;
    bool twoTone = false;
    bool powered = true;
};

/**
    PreciseHorizontalSlider
    -----------------------
    Meme drag "maison" que PreciseVerticalSlider (FaderControl.h), mais sur
    l'axe X : jamais de saut a la position du clic, deplacement relatif,
    sensibilite 4x plus fine pendant que Ctrl est enfonce (reancrage au
    changement d'etat de Ctrl pour ne jamais sauter).
*/
class PreciseHorizontalSlider : public juce::Slider
{
public:
    PreciseHorizontalSlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    {
        setSliderSnapsToMousePosition (false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartProportion = valueToProportionOfLength (getValue());
        dragStartX = e.position.x;
        ctrlWasDown = e.mods.isCtrlDown();
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const bool ctrlDown = e.mods.isCtrlDown();
        if (ctrlDown != ctrlWasDown)
        {
            dragStartProportion = valueToProportionOfLength (getValue());
            dragStartX = e.position.x;
            ctrlWasDown = ctrlDown;
        }

        const float normalPixels = juce::jmax (120.0f, (float) getWidth());
        const float pixelsForFullRange = ctrlDown ? normalPixels * 4.0f : normalPixels;
        const double deltaProportion = (double) (e.position.x - dragStartX) / (double) pixelsForFullRange;
        setValue (proportionOfLengthToValue (juce::jlimit (0.0, 1.0, dragStartProportion + deltaProportion)),
                  juce::sendNotificationSync);
    }

private:
    double dragStartProportion = 0.0;
    float dragStartX = 0.0f;
    bool ctrlWasDown = false;
};

/**
    BandStrip
    ---------
    UNE tranche complete par bande (Sevenpack, retour utilisateur explicite :
    "je souhaite que les compresseurs soient visibles par tranche ... voir
    les parametres de compression et d'expansion sur toutes les tranches
    avec les gain reductions et expansion et vu metre. On abandonne le fait
    de selectionner pour voir les compresseurs" -- maquette "Tranches
    Sevenpack" validee). Remplace a la fois l'ancien panneau de detail
    unique (BandPanel + selectedBand) et le rack du bas (BandMetersRack :
    faders de volume, SEL).

    De haut en bas :
      - tete : nom de la bande (couleur d'accent), plage de frequences
        (suit les crossovers en direct, "MERGED INTO ..." si le crossover
        du dessous est eteint -- la tranche est alors grisee), SOLO ;
      - COMP : ratio (7 boutons compacts), OPTICAL / OVERKILL / SC, puis
        IN / ATT / REL / OUT ;
      - EXPANDER : RANGE / ATT (knobs teintes violet, comme avant) ;
      - a droite du corps : 3 metres avec leur valeur chiffree -- GR
        (diodes, degrade vert->rouge), EXP (diodes, degrade bleu->violet),
        OUT (barre lisse, niveau de sortie de la bande) ;
      - bas : Dry/Wet et Spread en barres LED horizontales.

    Les knobs sont en mode "compact" (KnobSlider::setCompact()) : plus
    d'ecran LCD au centre (illisible a ce diametre), la valeur est ecrite
    SOUS le knob par cette classe. Double-clic sur une valeur ecrite =
    saisie au clavier ; double-clic sur le knob = valeur par defaut.
*/
class BandStrip : public juce::Component,
                  private juce::Timer
{
public:
    BandStrip (SevenpackAudioProcessor& p, int bandIndex, juce::Colour accentColour)
        : processorRef (p), band (bandIndex), accent (accentColour),
          powerParam (*p.apvts.getRawParameterValue (SevenpackAudioProcessor::idPower))
    {
        const auto pid = [this] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };

        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            crossoverHz[(size_t) x]      = p.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossover[x]);
            crossoverEnabled[(size_t) x] = p.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossoverEnabled[x]);
        }

        // --- SOLO (toggle persistant, idSolo) -- ButtonAttachment ne pose
        // PAS setClickingTogglesState() (voir BandMetersRack, meme piege). ---
        soloButton.setClickingTogglesState (true);
        soloButton.setHaloMargin (soloHalo);
        addAndMakeVisible (soloButton);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, SevenpackAudioProcessor::idSolo[band], soloButton);

        // --- Ratio ---
        ratioSelector.setCompact (true);
        addAndMakeVisible (ratioSelector);
        if (auto* ratioParam = processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idRatioIndex)))
        {
            ratioAttachment = std::make_unique<juce::ParameterAttachment> (
                *ratioParam, [this] (float v) { ratioSelector.setSelectedIndex ((int) std::round (v)); }, nullptr);
            ratioSelector.onSelect = [this] (int idx) { ratioAttachment->setValueAsCompleteGesture ((float) idx); };
            ratioAttachment->sendInitialUpdate();
        }

        // --- OPTICAL / OVERKILL (exclusifs) / SC ---
        for (auto* t : { &opticalButton, &overkillButton, &sidechainButton })
        {
            t->setCompact (true);
            t->setHaloMargin (toggleHalo);
            addAndMakeVisible (*t);
        }
        opticalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idOptical), opticalButton);
        overkillAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idBritish), overkillButton);
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idSidechainEnable), sidechainButton);
        opticalButton.onClick  = [this] { if (opticalButton.getToggleState())  overkillButton.setToggleState (false, juce::sendNotificationSync); };
        overkillButton.onClick = [this] { if (overkillButton.getToggleState()) opticalButton.setToggleState (false, juce::sendNotificationSync); };

        // --- Knobs (memes decimales qu'avant : IN/ATT sur 2 pour la
        // calibration fine, voir l'ancien buildBandPanel()) ---
        setupKnob (knobs[kIn],       pid (SevenpackAudioProcessor::idInput),        "IN",    "dB", 2, true);
        setupKnob (knobs[kAtt],      pid (SevenpackAudioProcessor::idAttack),       "ATT",   "ms", 2, false);
        setupKnob (knobs[kRel],      pid (SevenpackAudioProcessor::idRelease),      "REL",   "ms", 1, false);
        setupKnob (knobs[kOut],      pid (SevenpackAudioProcessor::idOutput),       "OUT",   "dB", 1, true);
        setupKnob (knobs[kExpRange], pid (SevenpackAudioProcessor::idInputExpand),  "RANGE", "dB", 1, false);
        setupKnob (knobs[kExpAtt],   pid (SevenpackAudioProcessor::idAttackExpand), "ATT",   "ms", 2, false);
        for (auto k : { kExpRange, kExpAtt })
            SevenpackLookAndFeel::setKnobAccentGradient (knobs[(size_t) k].slider, SevenpackColours::ledPurple, SevenpackColours::ledPurple);

        // --- Dry/Wet (neutre = 0, tout sec) + Spread (neutre = 100%) ---
        mixLaf.setAccentColour (SevenpackColours::ledCyan);
        widthLaf.setAccentColour (SevenpackColours::ledAmber);
        widthLaf.setAccentColourBelowNeutral (SevenpackColours::ledCyan);
        HorizontalLedBarLookAndFeel::setNeutralValue (mixSlider, 0.0);
        HorizontalLedBarLookAndFeel::setNeutralValue (widthSlider, 100.0);
        mixSlider.setLookAndFeel (&mixLaf);
        widthSlider.setLookAndFeel (&widthLaf);
        for (auto* s : { &mixSlider, &widthSlider })
            addAndMakeVisible (*s);
        mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idBandMix), mixSlider);
        widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idBandWidth), widthSlider);
        for (auto* s : { &mixSlider, &widthSlider })
        {
            if (auto* param = processorRef.apvts.getParameter (s == &mixSlider ? pid (SevenpackAudioProcessor::idBandMix)
                                                                                : pid (SevenpackAudioProcessor::idBandWidth)))
                s->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        }

        // --- Saisie clavier d'une valeur (double-clic sur le texte) ---
        valueEditor.setJustification (juce::Justification::centred);
        valueEditor.setInputRestrictions (0, "0123456789.-+");
        valueEditor.setSelectAllWhenFocused (true);
        valueEditor.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        valueEditor.setColour (juce::TextEditor::backgroundColourId, SevenpackColours::lcdBg);
        valueEditor.setColour (juce::TextEditor::textColourId, SevenpackColours::lcdText);
        valueEditor.setColour (juce::TextEditor::outlineColourId, accent);
        valueEditor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
        valueEditor.onReturnKey = [this] { commitValueEdit(); };
        valueEditor.onFocusLost = [this] { commitValueEdit(); };
        valueEditor.onEscapeKey = [this] { cancelValueEdit(); };
        addChildComponent (valueEditor);

        updateRangeState();
        startTimerHz (60);
    }

    ~BandStrip() override
    {
        stopTimer();
        mixSlider.setLookAndFeel (nullptr);
        widthSlider.setLookAndFeel (nullptr);
    }

    // Lien relatif LINK IN / LINK OUT (voir SevenpackEditorContent::
    // wireInputLink()/wireOutputLink()) -- l'editeur installe ses handlers
    // directement sur ces 2 sliders.
    juce::Slider& getInputSlider() noexcept  { return knobs[kIn].slider; }
    juce::Slider& getOutputSlider() noexcept { return knobs[kOut].slider; }

    // Libelle IN en vert quand LINK IN est actif, OUT en rouge pour LINK OUT
    // (meme code couleur que les boutons du header).
    void setLinkStates (bool inputLinked, bool outputLinked)
    {
        if (inputLinked == inLinked && outputLinked == outLinked)
            return;
        inLinked = inputLinked;
        outLinked = outputLinked;
        repaint (knobs[kIn].nameArea);
        repaint (knobs[kOut].nameArea);
    }

    // A rappeler apres un changement de theme (le cache de plaque se
    // reconstruit tout seul a la prochaine peinture, voir paint()).
    void refreshTheme()
    {
        backgroundCache = {};
        repaint();
    }

    void resized() override
    {
        auto plate = getLocalBounds().toFloat().reduced (plateInset);
        auto content = plate.reduced (8.0f, 6.0f);

        // Tete -- 6px de retrait lateral pour degager les vis de coin.
        auto header = content.removeFromTop (32.0f);
        headerArea = header.toNearestInt();
        auto headerInner = header.reduced (6.0f, 0.0f);
        auto soloRect = headerInner.removeFromRight (48.0f).withSizeKeepingCentre (48.0f, 22.0f);
        soloButton.setBounds (soloRect.expanded (soloHalo).toNearestInt());
        headerInner.removeFromRight (4.0f);
        nameArea = headerInner.removeFromTop (17.0f).toNearestInt();
        rangeArea = headerInner.toNearestInt();

        content.removeFromTop (4.0f);
        compLabelArea = content.removeFromTop (12.0f).toNearestInt();
        content.removeFromTop (3.0f);
        ratioSelector.setBounds (content.removeFromTop (24.0f).expanded (RatioSelector::compactButtonMargin).toNearestInt());
        content.removeFromTop (4.0f);
        {
            auto row = content.removeFromTop (22.0f);
            constexpr float gap = 4.0f;
            const float total = row.getWidth() - gap * 2.0f;
            opticalButton.setBounds (row.removeFromLeft (total * 1.0f / 2.7f).expanded (toggleHalo).toNearestInt());
            row.removeFromLeft (gap);
            overkillButton.setBounds (row.removeFromLeft (total * 1.15f / 2.7f).expanded (toggleHalo).toNearestInt());
            row.removeFromLeft (gap);
            sidechainButton.setBounds (row.expanded (toggleHalo).toNearestInt());
        }
        content.removeFromTop (6.0f);

        // Bas : Dry/Wet + Spread.
        {
            auto mixArea = content.removeFromBottom (40.0f);
            content.removeFromBottom (6.0f);
            layoutBarRow (mixArea.removeFromTop (18.0f), mixSlider, mixLabelArea, mixValueArea);
            mixArea.removeFromTop (4.0f);
            layoutBarRow (mixArea.removeFromTop (18.0f), widthSlider, widthLabelArea, widthValueArea);
        }

        // Corps : knobs a gauche, 3 metres a droite.
        auto body = content;
        metersArea = body.removeFromRight (58.0f);
        body.removeFromRight (5.0f);

        constexpr float rowGap = 8.0f, expLabelH = 12.0f, expLabelGap = 3.0f, expBlockGap = 10.0f;
        const float cellH = std::floor ((body.getHeight() - rowGap - expBlockGap - expLabelH - expLabelGap) / 3.0f);
        const float cellW = body.getWidth() * 0.5f;

        auto row1 = body.removeFromTop (cellH);
        body.removeFromTop (rowGap);
        auto row2 = body.removeFromTop (cellH);
        body.removeFromTop (expBlockGap);
        expLabelArea = body.removeFromTop (expLabelH).toNearestInt();
        body.removeFromTop (expLabelGap);
        auto row3 = body.removeFromTop (cellH);

        layoutKnobCell (knobs[kIn],       row1.removeFromLeft (cellW));
        layoutKnobCell (knobs[kAtt],      row1);
        layoutKnobCell (knobs[kRel],      row2.removeFromLeft (cellW));
        layoutKnobCell (knobs[kOut],      row2);
        layoutKnobCell (knobs[kExpRange], row3.removeFromLeft (cellW));
        layoutKnobCell (knobs[kExpAtt],   row3);

        // 3 colonnes de metres : valeur en haut, libelle en bas.
        {
            auto m = metersArea.reduced (2.0f, 3.0f);
            constexpr float colGap = 1.0f;
            const float colW = (m.getWidth() - colGap * 2.0f) / 3.0f;
            for (int c = 0; c < 3; ++c)
            {
                auto col = m.removeFromLeft (colW);
                m.removeFromLeft (colGap);
                meterReadoutArea[(size_t) c] = col.removeFromTop (13.0f);
                meterLabelArea[(size_t) c] = col.removeFromBottom (12.0f);
                meterTrack[(size_t) c] = col.reduced (0.0f, 3.0f);
            }
        }

        if (valueEditor.isVisible())
            cancelValueEdit();
        backgroundCache = {};
    }

    void paint (juce::Graphics& g) override
    {
        rebuildBackgroundCacheIfNeeded();
        g.drawImageAt (backgroundCache, 0, 0);

        const bool powered = powerParam.load() > 0.5f;
        const bool light = SevenpackLookAndFeel::isLightThemeStatic();

        // Plage de frequences / etat fusionne.
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.03f));
        SevenpackLookAndFeel::drawEngravedText (g, rangeText, rangeArea, SevenpackColours::textBright.withAlpha (0.6f),
                                                  juce::Justification::centredLeft);

        // Noms + valeurs des knobs.
        for (int k = 0; k < kNumKnobs; ++k)
        {
            const auto& knob = knobs[(size_t) k];
            juce::Colour nameColour = SevenpackColours::textBright.withAlpha (0.78f);
            if (k == kIn && inLinked)   nameColour = light ? SevenpackColours::ledGreen.darker (0.5f) : SevenpackColours::ledGreen;
            if (k == kOut && outLinked) nameColour = SevenpackColours::ledRed;
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)).withExtraKerningFactor (0.08f));
            SevenpackLookAndFeel::drawEngravedText (g, knob.name, knob.nameArea, nameColour);

            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            SevenpackLookAndFeel::drawEngravedText (g, formatKnobValue (knob), knob.valueArea,
                                                      SevenpackColours::textBright.withAlpha (powered ? 0.95f : 0.3f));
        }

        // Valeurs Dry/Wet + Spread.
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        SevenpackLookAndFeel::drawEngravedText (g, juce::String (juce::roundToInt (mixSlider.getValue())) + "%", mixValueArea,
                                                  SevenpackColours::textBright.withAlpha (powered ? 0.9f : 0.3f), juce::Justification::centredRight);
        SevenpackLookAndFeel::drawEngravedText (g, juce::String (juce::roundToInt (widthSlider.getValue())) + "%", widthValueArea,
                                                  SevenpackColours::textBright.withAlpha (powered ? 0.9f : 0.3f), juce::Justification::centredRight);

        // Metres.
        drawMeters (g, powered, false);

        // "Neon a l'extinction" (voir SevenpackLookAndFeel::getPowerTransitionMix()) :
        // passe superposee a pleine charge, opacite = powerFlickerMix.
        if (! powered && powerFlickerMix > 0.001f)
        {
            g.beginTransparencyLayer (powerFlickerMix);
            drawMeters (g, true, true);
            g.endTransparencyLayer();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        for (auto& knob : knobs)
        {
            if (knob.valueArea.contains (pos))
            {
                beginValueEdit (knob.slider, knob.valueArea, knob.decimals);
                return;
            }
        }
        if (mixValueArea.contains (pos))   { beginValueEdit (mixSlider, mixValueArea, 0);   return; }
        if (widthValueArea.contains (pos)) { beginValueEdit (widthSlider, widthValueArea, 0); return; }
    }

private:
    enum KnobId { kIn, kAtt, kRel, kOut, kExpRange, kExpAtt, kNumKnobs };

    struct StripKnob
    {
        KnobSlider slider;
        juce::String name, unit;
        int decimals = 1;
        bool showSign = false;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<int> nameArea, valueArea;
        double lastShownValue = std::numeric_limits<double>::quiet_NaN();
    };

    static constexpr float plateInset = 3.0f;
    static constexpr float soloHalo = 3.0f;
    static constexpr float toggleHalo = 2.0f;
    static constexpr int numLeds = 18;

    static const char* bandName (int b) noexcept
    {
        static const char* names[SevenpackAudioProcessor::NumBands] = { "LOW", "LOW-MID", "MID", "HIGH-MID", "PRESENCE", "HIGH", "AIR" };
        return names[juce::jlimit (0, (int) SevenpackAudioProcessor::NumBands - 1, b)];
    }

    void setupKnob (StripKnob& knob, const juce::String& paramID, const juce::String& name,
                    const juce::String& unit, int decimals, bool showSign)
    {
        knob.name = name;
        knob.unit = unit;
        knob.decimals = decimals;
        knob.showSign = showSign;
        knob.slider.setCompact (true);
        // Halo/ombre du knob laisses deborder dans la tranche (voir
        // SevenpackLookAndFeel::drawRotarySlider, "il faut la laisser
        // par-dessus le tout").
        knob.slider.setPaintingIsUnclipped (true);
        knob.slider.setNumDecimalPlacesToDisplay (decimals);
        knob.slider.setTextValueSuffix (" " + unit);
        if (auto* param = processorRef.apvts.getParameter (paramID))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        addAndMakeVisible (knob.slider);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, paramID, knob.slider);
    }

    static void layoutKnobCell (StripKnob& knob, juce::Rectangle<float> cell)
    {
        knob.nameArea = cell.removeFromTop (13.0f).toNearestInt();
        knob.valueArea = cell.removeFromBottom (14.0f).toNearestInt();
        const float d = juce::jmin (cell.getWidth() - 4.0f, cell.getHeight());
        knob.slider.setBounds (cell.withSizeKeepingCentre (d, d).toNearestInt());
    }

    static void layoutBarRow (juce::Rectangle<float> row, juce::Slider& slider,
                              juce::Rectangle<int>& labelArea, juce::Rectangle<int>& valueArea)
    {
        row = row.reduced (6.0f, 0.0f); // degage les vis de coin du bas
        labelArea = row.removeFromLeft (46.0f).toNearestInt();
        valueArea = row.removeFromRight (32.0f).toNearestInt();
        slider.setBounds (row.reduced (3.0f, 2.0f).toNearestInt());
    }

    static juce::String formatValue (double v, int decimals, bool showSign)
    {
        const int d = (std::abs (v) >= 100.0) ? 0 : decimals;
        auto text = juce::String (v, d);
        if (showSign && v > 0.0 && text.getDoubleValue() != 0.0)
            text = "+" + text;
        return text;
    }

    static juce::String formatKnobValue (const StripKnob& knob)
    {
        return formatValue (knob.slider.getValue(), knob.decimals, knob.showSign) + " " + knob.unit;
    }

    static juce::String formatHz (float hz)
    {
        if (hz < 1000.0f)
            return juce::String (juce::roundToInt (hz));
        const float k = hz / 1000.0f;
        const float rounded = std::round (k * 10.0f) / 10.0f;
        return (std::abs (rounded - std::round (rounded)) < 0.05f) ? juce::String ((int) std::round (rounded))
                                                                   : juce::String (rounded, 1);
    }

    static juce::String formatRange (float loHz, float hiHz)
    {
        const juce::String dash = juce::String::fromUTF8 (" \xe2\x80\x93 ");
        if (hiHz < 1000.0f)  return formatHz (loHz) + dash + formatHz (hiHz) + " Hz";
        if (loHz >= 1000.0f) return formatHz (loHz) + dash + formatHz (hiHz) + " kHz";
        return formatHz (loHz) + " Hz" + dash + formatHz (hiHz) + " kHz";
    }

    bool isCrossoverOn (int x) const noexcept { return crossoverEnabled[(size_t) x]->load() > 0.5f; }

    // Meme regle que SevenpackAudioProcessor::computeGroupOwners() : un
    // crossover eteint fait rejoindre la bande du dessus au groupe de sa
    // voisine du dessous -- la tranche "fusionnee" n'est alors plus traitee
    // separement (grisee ici), la tranche proprietaire affiche la plage
    // complete du groupe.
    void updateRangeState()
    {
        int owner = band;
        while (owner > 0 && ! isCrossoverOn (owner - 1))
            --owner;
        int groupEnd = owner;
        while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && ! isCrossoverOn (groupEnd))
            ++groupEnd;

        const bool isMerged = owner != band;
        juce::String text;
        if (isMerged)
            text = "MERGED INTO " + juce::String (bandName (owner));
        else
        {
            const float lo = (owner == 0) ? 20.0f : crossoverHz[(size_t) (owner - 1)]->load();
            const float hi = (groupEnd == SevenpackAudioProcessor::NumBands - 1) ? 20000.0f : crossoverHz[(size_t) groupEnd]->load();
            text = formatRange (lo, hi);
        }

        if (text != rangeText)
        {
            rangeText = text;
            repaint (rangeArea);
        }
        if (isMerged != merged)
        {
            merged = isMerged;
            setAlpha (merged ? 0.4f : 1.0f);
        }
    }

    void beginValueEdit (juce::Slider& target, juce::Rectangle<int> area, int decimals)
    {
        editTarget = &target;
        valueEditor.setBounds (area.expanded (3, 2));
        valueEditor.setText (juce::String (target.getValue(), decimals), juce::dontSendNotification);
        valueEditor.setVisible (true);
        valueEditor.toFront (true);
        valueEditor.grabKeyboardFocus();
        valueEditor.selectAll();
    }

    void commitValueEdit()
    {
        auto* target = editTarget;
        if (target == nullptr)
            return;
        editTarget = nullptr; // avant setVisible(false) : evite une 2e validation via onFocusLost
        const auto text = valueEditor.getText().trim();
        if (text.isNotEmpty())
            target->setValue (juce::jlimit (target->getMinimum(), target->getMaximum(), text.getDoubleValue()),
                              juce::sendNotificationSync);
        valueEditor.setVisible (false);
    }

    void cancelValueEdit()
    {
        editTarget = nullptr;
        valueEditor.setVisible (false);
    }

    // Meme echelle non-lineaire que GRMeter/BandMetersRack (resolution fine
    // pres de 0 dB).
    static float grDbToFraction (float db) noexcept
    {
        static constexpr float marks[] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
        constexpr int numMarks = 10;
        db = juce::jlimit (0.0f, marks[numMarks - 1], db);
        for (int i = 0; i < numMarks - 1; ++i)
        {
            if (db <= marks[i + 1])
            {
                const float segFrac = (marks[i + 1] > marks[i]) ? (db - marks[i]) / (marks[i + 1] - marks[i]) : 0.0f;
                return ((float) i + segFrac) / (float) (numMarks - 1);
            }
        }
        return 1.0f;
    }

    void drawMeters (juce::Graphics& g, bool powered, bool forceFullyLit)
    {
        constexpr float kExpandLedDb = 0.25f; // meme calibrage que BandMetersRack (~4.5 dB pleine echelle)
        const float grDb = juce::jlimit (0.0f, 30.0f, processorRef.grMeterDb[(size_t) band].load());
        const float exDb = juce::jlimit (0.0f, kExpandLedDb * (float) numLeds, processorRef.grMeterExpandDb[(size_t) band].load());
        const int litGr = forceFullyLit ? numLeds : (powered ? (int) std::round (grDbToFraction (grDb) * (float) numLeds) : 0);
        const int litEx = forceFullyLit ? numLeds : (powered ? (int) std::round (exDb / kExpandLedDb) : 0);

        // GR puis EXP : diodes rondes, remplies du HAUT vers le bas (la
        // reduction "pend" depuis 0 dB, lecture classique d'un GR meter).
        for (int c = 0; c < 2; ++c)
        {
            const auto& track = meterTrack[(size_t) c];
            const float step = track.getHeight() / (float) numLeds;
            const float dot = juce::jmin (9.0f, step - 3.0f, track.getWidth() - 2.0f);
            const int lit = (c == 0) ? litGr : litEx;
            for (int i = 0; i < numLeds; ++i)
            {
                const float t = (float) i / (float) (numLeds - 1);
                auto r = juce::Rectangle<float> (dot, dot).withCentre ({ track.getCentreX(), track.getY() + step * ((float) i + 0.5f) });
                if (i < lit)
                    g.setColour (c == 0 ? SevenpackLookAndFeel::ledGradientColour (t) : SevenpackLookAndFeel::ledGradientColourExpand (t));
                else
                    g.setColour (SevenpackColours::ledOff);
                g.fillEllipse (r);
            }
        }

        // OUT : barre lisse (style Ableton, voir BandMetersRack::drawMasterMeter()).
        {
            const auto& col = meterTrack[2];
            auto track = col.withSizeKeepingCentre (juce::jmin (8.0f, col.getWidth()), col.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (track, 2.0f);

            constexpr float minDb = -48.0f, maxDb = 3.0f;
            const float outDb = powered ? processorRef.bandOutputLevelDb[(size_t) band].load() : minDb;
            const float frac = forceFullyLit ? 1.0f : juce::jlimit (0.0f, 1.0f, (outDb - minDb) / (maxDb - minDb));
            const float litY = track.getBottom() - frac * track.getHeight();
            if (litY < track.getBottom())
            {
                juce::ColourGradient grad (SevenpackColours::ledGreen, track.getX(), track.getBottom(),
                                           SevenpackColours::ledRed, track.getX(), track.getY(), false);
                grad.addColour (0.72, SevenpackColours::ledGreen);
                grad.addColour (0.90, SevenpackColours::ledYellow);
                g.saveState();
                g.reduceClipRegion (track.withTop (litY).toNearestInt());
                g.setGradientFill (grad);
                g.fillRoundedRectangle (track, 2.0f);
                g.restoreState();
            }
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawRoundedRectangle (track, 2.0f, 1.0f);
        }

        // Valeurs chiffrees (rafraichies a ~7.5 Hz, voir timerCallback()).
        if (powered && ! forceFullyLit)
        {
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            for (int c = 0; c < 3; ++c)
                SevenpackLookAndFeel::drawEngravedText (g, readoutText[(size_t) c], meterReadoutArea[(size_t) c].toNearestInt(),
                                                          SevenpackColours::textBright.withAlpha (0.92f));
        }
    }

    void rebuildBackgroundCacheIfNeeded()
    {
        const int themeKey = SevenpackLookAndFeel::getUiThemeIndex();
        if (backgroundCache.isValid() && backgroundCache.getWidth() == getWidth()
            && backgroundCache.getHeight() == getHeight() && cachedThemeIndex == themeKey)
            return;

        backgroundCache = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics g (backgroundCache);
        const bool light = SevenpackLookAndFeel::isLightThemeStatic();
        const bool dos = SevenpackLookAndFeel::isDosThemeStatic();
        auto plate = getLocalBounds().toFloat().reduced (plateInset);

        // Plaque vissee teintee couleur de bande -- meme rendu que les
        // plaques de l'ancien rack du bas (BandMetersRack), deja valide.
        if (dos)
        {
            g.setColour (juce::Colours::black);
            g.fillRect (plate);
            g.setColour (accent);
            g.drawRect (plate, 1.5f);
        }
        else
        {
            juce::Path platePath;
            platePath.addRoundedRectangle (plate, 4.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 5, { 0, 2 }).drawForPath (g, platePath);

            juce::ColourGradient base (light ? SevenpackColours::lightPanelGradTop : juce::Colour (0xff262422), plate.getX(), plate.getY(),
                                        light ? SevenpackColours::lightPanelGradBottom : juce::Colour (0xff0e0d0c), plate.getX(), plate.getBottom(), false);
            g.setGradientFill (base);
            g.fillPath (platePath);
            g.setColour (accent.withAlpha (0.16f));
            g.fillPath (platePath);

            juce::Path outerBevel; outerBevel.addRoundedRectangle (plate.expanded (0.5f), 4.5f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.strokePath (outerBevel, juce::PathStrokeType (1.2f));
            juce::Path innerBevel; innerBevel.addRoundedRectangle (plate.reduced (1.2f), 3.3f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.strokePath (innerBevel, juce::PathStrokeType (1.0f));

            // Liseret haut plus epais, couleur de bande (reperage rapide).
            g.saveState();
            g.reduceClipRegion (platePath);
            g.setColour (accent);
            g.fillRect (plate.withHeight (3.0f));
            g.restoreState();

            g.setColour (accent.withAlpha (0.55f));
            g.strokePath (platePath, juce::PathStrokeType (1.0f));
        }

        constexpr float screwInset = 9.0f, screwRadius = 3.4f;
        SevenpackLookAndFeel::drawScrew (g, { plate.getX() + screwInset,     plate.getY() + screwInset },     screwRadius);
        SevenpackLookAndFeel::drawScrew (g, { plate.getRight() - screwInset, plate.getY() + screwInset },     screwRadius);
        SevenpackLookAndFeel::drawScrew (g, { plate.getX() + screwInset,     plate.getBottom() - screwInset }, screwRadius);
        SevenpackLookAndFeel::drawScrew (g, { plate.getRight() - screwInset, plate.getBottom() - screwInset }, screwRadius);

        // Nom de bande.
        const auto nameColour = (light && ! dos) ? accent.darker (0.45f) : accent;
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.06f));
        SevenpackLookAndFeel::drawEngravedText (g, bandName (band), nameArea, nameColour, juce::Justification::centredLeft);

        // Libelles de section + filet.
        auto drawSectionLabel = [&g] (juce::Rectangle<int> area, const juce::String& text, juce::Colour colour)
        {
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.16f));
            const int textW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 4;
            SevenpackLookAndFeel::drawEngravedText (g, text, area.withWidth (textW), colour, juce::Justification::centredLeft);
            g.setColour (colour.withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> ((float) (area.getX() + textW + 2), (float) area.getCentreY(),
                                                (float) (area.getRight() - area.getX() - textW - 2), 1.0f));
        };
        drawSectionLabel (compLabelArea, "COMP", SevenpackColours::textBright.withAlpha (0.62f));
        drawSectionLabel (expLabelArea.withRight (juce::roundToInt (metersArea.getX()) - 5), "EXPANDER",
                          light ? SevenpackColours::ledPurple.darker (0.4f) : SevenpackColours::ledPurple);

        // Puits des metres + libelles GR / EXP / OUT.
        if (! dos)
        {
            g.setColour (juce::Colours::black.withAlpha (light ? 0.18f : 0.35f));
            g.fillRoundedRectangle (metersArea, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (metersArea.reduced (0.5f), 4.0f, 1.0f);
        }
        else
        {
            g.setColour (accent.withAlpha (0.6f));
            g.drawRect (metersArea, 1.0f);
        }
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.04f));
        static const char* meterNames[3] = { "GR", "EXP", "OUT" };
        for (int c = 0; c < 3; ++c)
            SevenpackLookAndFeel::drawEngravedText (g, meterNames[c], meterLabelArea[(size_t) c].toNearestInt(),
                                                      SevenpackColours::textBright.withAlpha (0.6f));

        // Libelles Dry/Wet + Spread.
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.05f));
        SevenpackLookAndFeel::drawEngravedText (g, "DRY/WET", mixLabelArea, SevenpackColours::textBright.withAlpha (0.65f),
                                                  juce::Justification::centredLeft);
        SevenpackLookAndFeel::drawEngravedText (g, "SPREAD", widthLabelArea, SevenpackColours::textBright.withAlpha (0.65f),
                                                  juce::Justification::centredLeft);

        cachedThemeIndex = themeKey;
    }

    void timerCallback() override
    {
        const bool powered = powerParam.load() > 0.5f;
        const bool poweredChanged = powered != lastPowered;
        lastPowered = powered;
        if (poweredChanged)
        {
            mixLaf.setPowered (powered);
            widthLaf.setPowered (powered);
            mixSlider.repaint();
            widthSlider.repaint();
            repaint();
        }

        const auto* laf = dynamic_cast<SevenpackLookAndFeel*> (&getLookAndFeel());
        powerFlickerMix = laf != nullptr ? laf->getPowerTransitionMix() : 1.0f;
        bool metersDirty = false;
        if (std::abs (powerFlickerMix - lastPaintedFlickerMix) > 0.01f)
        {
            lastPaintedFlickerMix = powerFlickerMix;
            metersDirty = true;
        }

        const float gr  = processorRef.grMeterDb[(size_t) band].load();
        const float ex  = processorRef.grMeterExpandDb[(size_t) band].load();
        const float out = processorRef.bandOutputLevelDb[(size_t) band].load();
        if (std::abs (gr - lastGrDb) > 0.05f || std::abs (ex - lastExDb) > 0.02f || std::abs (out - lastOutDb) > 0.1f)
        {
            lastGrDb = gr; lastExDb = ex; lastOutDb = out;
            metersDirty = true;
        }

        // Chiffres des metres a ~7.5 Hz (a 60 Hz ils defilent trop vite
        // pour etre lus).
        if (++readoutTick >= 8)
        {
            readoutTick = 0;
            const std::array<juce::String, 3> fresh {
                juce::String (juce::jmax (0.0f, gr), 1),
                juce::String (juce::jmax (0.0f, ex), 1),
                out <= -60.0f ? juce::String ("-inf")
                              : (std::abs (out) >= 10.0f ? juce::String (juce::roundToInt (out)) : juce::String (out, 1))
            };
            if (fresh != readoutText)
            {
                readoutText = fresh;
                metersDirty = true;
            }
        }

        if (metersDirty || poweredChanged)
            repaint (metersArea.getSmallestIntegerContainer());

        for (auto& knob : knobs)
        {
            const double v = knob.slider.getValue();
            if (! (v == knob.lastShownValue))
            {
                knob.lastShownValue = v;
                repaint (knob.valueArea);
            }
        }

        const int mixShown = juce::roundToInt (mixSlider.getValue()), widthShown = juce::roundToInt (widthSlider.getValue());
        if (mixShown != lastMixShown)     { lastMixShown = mixShown;     repaint (mixValueArea); }
        if (widthShown != lastWidthShown) { lastWidthShown = widthShown; repaint (widthValueArea); }

        updateRangeState();
    }

    SevenpackAudioProcessor& processorRef;
    const int band;
    const juce::Colour accent;
    std::atomic<float>& powerParam;
    std::array<std::atomic<float>*, (size_t) SevenpackAudioProcessor::NumCrossovers> crossoverHz {}, crossoverEnabled {};

    RackPushButton soloButton { "SOLO", juce::Colour (0xff8c1c2b), true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;

    RatioSelector ratioSelector;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    ToggleSwitch opticalButton   { "OPTICAL",  SevenpackColours::ledAmber };
    ToggleSwitch overkillButton  { "OVERKILL", SevenpackColours::ledRed };
    ToggleSwitch sidechainButton { "SC",       SevenpackColours::ledCyan };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> opticalAttachment, overkillAttachment, sidechainAttachment;

    std::array<StripKnob, (size_t) kNumKnobs> knobs;

    HorizontalLedBarLookAndFeel mixLaf, widthLaf;
    PreciseHorizontalSlider mixSlider, widthSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, widthAttachment;

    juce::TextEditor valueEditor;
    juce::Slider* editTarget = nullptr;

    juce::Rectangle<int> headerArea, nameArea, rangeArea, compLabelArea, expLabelArea;
    juce::Rectangle<int> mixLabelArea, mixValueArea, widthLabelArea, widthValueArea;
    juce::Rectangle<float> metersArea;
    std::array<juce::Rectangle<float>, 3> meterReadoutArea, meterLabelArea, meterTrack;

    juce::String rangeText;
    bool merged = false;
    bool inLinked = false, outLinked = false;

    bool lastPowered = true;
    float powerFlickerMix = 1.0f, lastPaintedFlickerMix = 1.0f;
    float lastGrDb = 0.0f, lastExDb = 0.0f, lastOutDb = -100.0f;
    int readoutTick = 0;
    std::array<juce::String, 3> readoutText { "0.0", "0.0", "-inf" };
    int lastMixShown = -1, lastWidthShown = -1;

    juce::Image backgroundCache;
    int cachedThemeIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandStrip)
};
