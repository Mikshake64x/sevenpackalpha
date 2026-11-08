#pragma once
#include <JuceHeader.h>
#include "AlphaSkin.h"
#include "AlphaMeters.h"
#include "AlphaAttRel.h"
#include "PluginProcessor.h"
#include "FaderControl.h" // PreciseVerticalSlider
#include "KnobSlider.h"

/**
    AlphaStrip
    ----------
    Tranche d'UNE bande dans l'interface v2 "Sevenpack Alpha" (maquette
    7PACK validee). Remplace BandStrip dans l'editeur (BandStrip.h reste
    dans le projet, inutilise).

    De haut en bas :
      - aiguille THD de la bande (AlphaThdNeedle) ;
      - SOLO / SEL sous l'aiguille (demande explicite) -- SEL selectionne la
        bande, Ctrl/Cmd+clic l'ajoute au groupe lie (comme Sixpack), voir
        onSelectClicked / SevenpackEditorContent::selectBand() ;
      - carte : bandeau pastel sur une ligne (nom a gauche, plage a droite,
        "MERGED" si fusionnee), ratio (7 cases LCD) ;
      - corps : fader OUT a gauche (diodes GR et EXP derriere le capuchon,
        comme Sixpack), a droite le gros knob IN en tete puis ATT / REL
        (compresseur) ;
      - bas, pleine largeur : RANGE (expandeur), DRY/WET et SPREAD.
    Retires de l'interface (retour utilisateur : "inutile" / "je ne
    l'utilise jamais") mais GARDES dans le code, attachements compris :
    OPTICAL / OVERKILL / SC par bande et l'ATT de l'expandeur -- voir
    kShowModeButtons / kShowExpanderAttack pour les remettre.
    Knobs neumorphiques (AlphaKnob) : la place de la couronne de traits
    est reservee autour de chaque knob (AlphaKnob::kExtent).

    Knobs = KnobSlider (glisser vertical, Ctrl = reglage fin) -- retour
    utilisateur : les petits faders horizontaux n'etaient pas pratiques.
    Le THD etant un etage GLOBAL (pas de reglage THD par bande dans le
    DSP), le gros knob pilote IN et le fader pilote OUT.
    Double-clic sur une valeur ecrite = saisie au clavier ; double-clic sur
    un controle = valeur par defaut.
*/
class AlphaStrip : public juce::Component, private juce::Timer
{
public:
    AlphaStrip (SevenpackAudioProcessor& p, int bandIndex)
        : processorRef (p), band (bandIndex), needle (p, bandIndex),
          powerParam (*p.apvts.getRawParameterValue (SevenpackAudioProcessor::idPower))
    {
        const auto pid = [this] (const char* base) { return SevenpackAudioProcessor::bandParamId (base, band); };

        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            crossoverHz[(size_t) x]      = p.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossover[x]);
            crossoverEnabled[(size_t) x] = p.apvts.getRawParameterValue (SevenpackAudioProcessor::idCrossoverEnabled[x]);
        }
        for (int b = 0; b < SevenpackAudioProcessor::NumBands; ++b)
            soloState[(size_t) b] = p.apvts.getRawParameterValue (SevenpackAudioProcessor::idSolo[b]);

        addAndMakeVisible (needle);

        // --- SOLO (toggle persistant) / SEL (selection d'affichage) ---
        // Plus hauts et lisibles (demande explicite) : 28 px, texte 9.5 px
        // bleu nuit sur fond blanc (style Ghost, voir AlphaButton).
        for (auto* btn : { &soloButton, &selButton })
        {
            btn->setStyle (AlphaButton::Style::Ghost);
            btn->setFontSize (AlphaType::label);
            addAndMakeVisible (*btn);
        }
        soloButton.setOnColour (juce::Colour (0xfff5c04a));
        soloButton.setClickingTogglesState (true);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, SevenpackAudioProcessor::idSolo[band], soloButton);
        // Ctrl (Cmd sur Mac) + clic = ajoute/retire la bande du groupe lie,
        // comme Sixpack (voir SevenpackEditorContent::selectBand()).
        selButton.onClick = [this]
        {
            if (onSelectClicked)
                onSelectClicked (band, juce::ModifierKeys::getCurrentModifiers().isCommandDown());
        };

        // --- Ratio (7 cases, table reelle du DSP : 2 3 4 6 8 10 20) ---
        static const char* ratioLabels[7] = { "2", "3", "4", "6", "8", "10", "20" };
        for (int i = 0; i < 7; ++i)
        {
            ratioButtons[(size_t) i] = std::make_unique<AlphaLcdButton> (ratioLabels[i]);
            ratioButtons[(size_t) i]->setTooltip (juce::String (ratioLabels[i]) + ":1");
            addAndMakeVisible (*ratioButtons[(size_t) i]);
        }
        if (auto* ratioParam = processorRef.apvts.getParameter (pid (SevenpackAudioProcessor::idRatioIndex)))
        {
            ratioAttachment = std::make_unique<juce::ParameterAttachment> (*ratioParam, [this] (float v)
            {
                const int idx = (int) std::round (v);
                for (int i = 0; i < 7; ++i)
                    ratioButtons[(size_t) i]->setToggleState (i == idx, juce::dontSendNotification);
            }, nullptr);
            for (int i = 0; i < 7; ++i)
                ratioButtons[(size_t) i]->onClick = [this, i] { ratioAttachment->setValueAsCompleteGesture ((float) i); };
            ratioAttachment->sendInitialUpdate();
        }

        // --- OPTICAL / OVERKILL (exclusifs) / SC -- masques (kShowModeButtons) ---
        for (auto* t : { &opticalButton, &overkillButton, &scButton })
        {
            t->setClickingTogglesState (true);
            addChildComponent (*t);
            t->setVisible (kShowModeButtons);
        }
        opticalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idOptical), opticalButton);
        overkillAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idBritish), overkillButton);
        scAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, pid (SevenpackAudioProcessor::idSidechainEnable), scButton);
        opticalButton.onClick  = [this] { if (opticalButton.getToggleState())  overkillButton.setToggleState (false, juce::sendNotificationSync); };
        overkillButton.onClick = [this] { if (overkillButton.getToggleState()) opticalButton.setToggleState (false, juce::sendNotificationSync); };

        // --- Diodes GR/EXP (composant opaque a part, SOUS le fader) ---
        addAndMakeVisible (grExpMeter);

        // --- Fader OUT ---
        outFader.setSliderStyle (juce::Slider::LinearVertical);
        outFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        AlphaLookAndFeel::markAsFader (outFader);
        setupSlider (outFader, pid (SevenpackAudioProcessor::idOutput), outAttachment);

        // --- Knobs (retour utilisateur : "les petits faders horizontaux ...
        // pas faciles a la souris. des knobs ?") -- KnobSlider, meme prise
        // en main que Sixpack : glisser vertical, Ctrl = reglage fin,
        // double-clic = valeur par defaut. ---
        setupSlider (inKnob,    pid (SevenpackAudioProcessor::idInput),        inAttachment);
        setupSlider (attKnob,   pid (SevenpackAudioProcessor::idAttack),       attAttachment);
        setupSlider (relKnob,   pid (SevenpackAudioProcessor::idRelease),      relAttachment);
        setupSlider (rangeKnob, pid (SevenpackAudioProcessor::idInputExpand),  rangeAttachment);
        setupSlider (eattKnob,  pid (SevenpackAudioProcessor::idAttackExpand), eattAttachment);
        setupSlider (mixKnob,   pid (SevenpackAudioProcessor::idBandMix),      mixAttachment);
        setupSlider (widthKnob, pid (SevenpackAudioProcessor::idBandWidth),    widthAttachment);
        AlphaLookAndFeel::setNeutral (widthKnob, 100.0);
        AlphaLookAndFeel::setNeutral (inKnob, 0.0);
        eattKnob.setVisible (kShowExpanderAttack);

        rows = { Row { &attKnob,   "ATT",     "ms", 2, false },
                 Row { &relKnob,   "REL",     "ms", 0, false },
                 Row { &rangeKnob, "RANGE",   "dB", 1, false, LabelTint::expander },
                 Row { &eattKnob,  "ATT",     "ms", 2, false, LabelTint::expander },
                 Row { &mixKnob,   "DRY/WET", "%",  0, false },
                 Row { &widthKnob, "SPREAD",  "%",  0, false },
                 Row { &inKnob,    "IN",      "dB", 1, true,  LabelTint::input },
                 Row { &outFader,  "OUT",     "dB", 1, true } };

        // --- Saisie clavier d'une valeur (double-clic sur le texte) ---
        valueEditor.setJustification (juce::Justification::centred);
        valueEditor.setInputRestrictions (0, "0123456789.-+");
        valueEditor.setSelectAllWhenFocused (true);
        valueEditor.setFont (AlphaDraw::font (AlphaType::value));
        valueEditor.onReturnKey = [this] { commitValueEdit(); };
        valueEditor.onFocusLost = [this] { commitValueEdit(); };
        valueEditor.onEscapeKey = [this] { cancelValueEdit(); };
        addChildComponent (valueEditor);

        updateRangeState();
        startTimerHz (30);
    }

    ~AlphaStrip() override
    {
        stopTimer();
        for (auto* s : allSliders())
            s->setLookAndFeel (nullptr);
    }

    juce::Slider& getInputSlider() noexcept  { return inKnob; }
    juce::Slider& getOutputSlider() noexcept { return outFader; }

    // (bande, Ctrl/Cmd enfonce)
    std::function<void (int, bool)> onSelectClicked;

    void setSelected (bool shouldBeSelected)
    {
        selButton.setToggleState (shouldBeSelected, juce::dontSendNotification);
        if (selected != shouldBeSelected) { selected = shouldBeSelected; repaint(); }
    }

    void setLinkStates (bool inputLinked, bool outputLinked)
    {
        if (inputLinked == inLinked && outputLinked == outLinked)
            return;
        inLinked = inputLinked;
        outLinked = outputLinked;
        repaint();
    }

    void refreshTheme()
    {
        resized(); // le theme sombre (design de Sunny) a sa propre mise en page
        needle.refreshTheme();
        valueEditor.setColour (juce::TextEditor::backgroundColourId, AlphaPalette::get().lcd);
        valueEditor.setColour (juce::TextEditor::textColourId, AlphaPalette::get().lcdInk);
        valueEditor.setColour (juce::TextEditor::outlineColourId, AlphaPalette::get().accent);
        valueEditor.setColour (juce::TextEditor::focusedOutlineColourId, AlphaPalette::get().accent);
        repaint();
    }

    void resized() override
    {
        if (AlphaPalette::get().sunny)
        {
            layoutSunny();
            return;
        }
        pictoBox = {};
        auto plate = getLocalBounds().toFloat().reduced (1.0f);
        plateRect = plate;
        auto inner = plate.reduced (7.0f);

        // Aiguille THD : 72 px (audit -- etait 84, la place va a SOLO/SEL
        // et aux knobs).
        needle.setBounds (inner.removeFromTop (72.0f).toNearestInt());
        needle.setPlateArea (plateRect - needle.getPosition().toFloat()); // plaque repeinte derriere le cadran (finitions)
        inner.removeFromTop (6.0f);
        {
            auto ss = inner.removeFromTop (28.0f);
            const float half = (ss.getWidth() - 6.0f) * 0.5f;
            soloButton.setBounds (ss.removeFromLeft (half).toNearestInt());
            ss.removeFromLeft (6.0f);
            selButton.setBounds (ss.toNearestInt());
        }
        inner.removeFromTop (6.0f);
        cardRect = inner;

        auto c = cardRect.reduced (8.0f);
        // Bandeau sur UNE ligne (demande explicite : "prendre qu'une ligne
        // pour gagner de la place") : nom a gauche, plage a droite.
        bandeauRect = c.removeFromTop (22.0f);
        nameArea = hzArea = bandeauRect.reduced (8.0f, 0.0f);
        c.removeFromTop (6.0f);
        {
            auto row = c.removeFromTop (20.0f);
            const float w = (row.getWidth() - 2.0f * 6.0f) / 7.0f;
            for (int i = 0; i < 7; ++i)
            {
                ratioButtons[(size_t) i]->setBounds (row.removeFromLeft (w).toNearestInt());
                row.removeFromLeft (2.0f);
            }
        }
        if constexpr (kShowModeButtons)
        {
            c.removeFromTop (5.0f);
            auto row = c.removeFromTop (22.0f);
            const float total = row.getWidth() - 6.0f;
            opticalButton.setBounds (row.removeFromLeft (total * 1.25f / 3.2f).toNearestInt());
            row.removeFromLeft (3.0f);
            overkillButton.setBounds (row.removeFromLeft (total * 1.35f / 3.2f).toNearestInt());
            row.removeFromLeft (3.0f);
            scButton.setBounds (row.toNearestInt());
        }
        c.removeFromTop (8.0f);

        layoutCardBody (c, sunnyKnobs(), true);

        if (valueEditor.isVisible())
            cancelValueEdit();
    }

    // Disposition des knobs (demande explicite : "les knobs ne sont pas
    // positionne pareil en sombre et en clair") : 0 = chaque theme la sienne
    // (avant), 1 = celle du theme sombre (design de Sunny) partout, 2 = celle
    // du theme clair partout.
    static inline int knobLayout = 1; // choix du user (QCM) : celle du sombre partout
    static bool sunnyKnobs() { return knobLayout == 0 ? AlphaPalette::get().sunny : knobLayout == 1; }

    // Corps de la carte : en bas RANGE | DRY/WET | SPREAD, le fader OUT a
    // gauche, puis ATT / REL et le knob IN.
    //  - compact (design de Sunny) : knobs de 30 px, ATT (knob + case picto)
    //    puis REL (case picto + knob) en quinconce ;
    //  - sinon (d'origine) : knobs de 37 px, ATT | REL cote a cote, picto a
    //    cote du libelle.
    // linkPills : pastilles LINK du theme clair (le sombre a son cadenas).
    void layoutCardBody (juce::Rectangle<float> c, bool compact, bool linkPills)
    {
        pictoBox = {};
        // Knobs neumorphiques : diametre de la face ; la cellule reserve la
        // couronne de traits autour (AlphaKnob::kExtent).
        const float smallD = compact ? 30.0f : 37.0f;
        constexpr float inMaxD = 56.0f;
        const float cellH = compact ? 56.0f : 12.0f + std::ceil (37.0f * AlphaKnob::kExtent) + 13.0f;
        const auto cell = [this, compact, smallD] (Row& row, juce::Rectangle<float> area)
        {
            if (compact) layoutKnobCellCompact (row, area, smallD);
            else         layoutKnobCell (row, area, smallD);
        };

        // Bas de carte, pleine largeur : RANGE (expandeur) | DRY/WET | SPREAD
        // (la cellule du milieu un peu plus large pour "DRY/WET").
        {
            auto bottom = c.removeFromBottom (cellH);
            bottomLineY = bottom.getY() - 5.0f;
            const float side = (bottom.getWidth() - 56.0f) * 0.5f;
            cell (rows[2], bottom.removeFromLeft (side));
            cell (rows[4], bottom.removeFromLeft (56.0f));
            cell (rows[5], bottom);
        }
        c.removeFromBottom (10.0f);
        expLabelArea = {};

        // Colonne du fader OUT : diodes GR/EXP (composant a part) derriere le
        // capuchon, pastille LINK (clair) puis valeur en dessous.
        auto body = c;
        auto faderCol = body.removeFromLeft (44.0f);
        body.removeFromLeft (6.0f);
        rows[7].valueArea = faderCol.removeFromBottom (13.0f).expanded (6.0f, 0.0f).toNearestInt(); // deborde un peu : "-11.2dB" tient
        rows[7].nameArea = {};
        outLinkPill = linkPills ? faderCol.removeFromBottom (14.0f).withSizeKeepingCentre (34.0f, 12.0f) : juce::Rectangle<float>();
        meterArea = faderCol.reduced (0.0f, 1.0f);
        grExpMeter.setBounds (meterArea.toNearestInt());
        grExpMeter.setCardArea (cardRect - grExpMeter.getPosition().toFloat()); // carte repeinte derriere les diodes (finitions)
        outFader.setBounds (meterArea.withTrimmedTop (12.0f).toNearestInt());

        if (compact)
        {
            // REL : case picto a gauche, knob a droite.
            {
                auto row = body.removeFromBottom (cellH);
                cell (rows[1], row.removeFromRight (50.0f));
                pictoBox[1] = juce::Rectangle<float> (juce::jmin (46.0f, row.getWidth() - 4.0f), 24.0f)
                                  .withCentre ({ row.getCentreX(), (float) rows[1].slider->getBounds().getCentreY() });
            }
            // ATT : knob a gauche, case picto a droite.
            {
                auto row = body.removeFromBottom (cellH);
                cell (rows[0], row.removeFromLeft (50.0f));
                pictoBox[0] = juce::Rectangle<float> (juce::jmin (46.0f, row.getWidth() - 4.0f), 24.0f)
                                  .withCentre ({ row.getCentreX(), (float) rows[0].slider->getBounds().getCentreY() });
            }
            body.removeFromBottom (2.0f);
        }
        else
        {
            // Compresseur : ATT | REL, sous le knob IN.
            auto row = body.removeFromBottom (cellH);
            cell (rows[0], row.removeFromLeft (row.getWidth() * 0.5f));
            cell (rows[1], row);
            body.removeFromBottom (4.0f);
        }

        // IN en TETE de tranche (demande explicite : "les knobs Input doivent
        // etre celui qui est en haut par bande") -- gros knob, libelle (et
        // pastille LINK en clair) au-dessus, valeur en dessous.
        {
            auto inCell = body;
            auto nameRow = inCell.removeFromTop (12.0f);
            rows[6].nameArea = nameRow.withSizeKeepingCentre (20.0f, 12.0f).toNearestInt();
            inLinkPill = linkPills ? juce::Rectangle<float> ((float) rows[6].nameArea.getRight() + 4.0f, nameRow.getY(), 34.0f, 12.0f)
                                   : juce::Rectangle<float>();
            rows[6].valueArea = inCell.removeFromBottom (14.0f).toNearestInt();
            const float d = juce::jmin (inMaxD, inCell.getWidth() / AlphaKnob::kExtent, inCell.getHeight() / AlphaKnob::kExtent);
            inKnob.setBounds (inCell.withSizeKeepingCentre (d * AlphaKnob::kExtent, d * AlphaKnob::kExtent).toNearestInt());
        }

        // ATT de l'expandeur : masque (kShowExpanderAttack), plus de place.
        rows[3].nameArea = rows[3].valueArea = {};
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();
        const bool powered = powerParam.load() > 0.5f;
        const int colourMode = juce::jlimit (0, 2, processorRef.bandColourMode.load());
        const auto pastel = AlphaPalette::bandPastel (band);

        // Plaque bleue de la tranche. Halo de selection = contours
        // superposes (plus de flou recalcule a chaque repaint, audit perf).
        if (selected && ! pal.sunny)
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (pal.accent.withAlpha (0.14f * (float) (4 - i)));
                g.drawRoundedRectangle (plateRect.reduced (1.0f).expanded ((float) i * 1.3f), 10.0f + (float) i, 1.6f);
            }
        AlphaDraw::finishDropShadow (g, plateRect, 10.0f, pal.finish);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (plateRect.translated (0.0f, 2.0f), 10.0f);
        AlphaDraw::paintPlate (g, plateRect, pal); // finition MATTE/SATIN/GLOSS/MOLDED
        if (pal.finish == 0 && ! pal.sunny)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawLine (plateRect.getX() + 10.0f, plateRect.getY() + 0.8f, plateRect.getRight() - 10.0f, plateRect.getY() + 0.8f, 1.0f);
        }
        if (selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (plateRect.reduced (pal.sunny ? 1.5f : 1.0f), 9.5f, pal.sunny ? 3.0f : 2.0f);
        }
        if (pal.sunny)
        {
            // Bac sombre sous les 7 cases de ratio (design de Sunny).
            g.setColour (juce::Colour (0xff121f33));
            g.fillRoundedRectangle (ratioRowRect, 4.0f);
        }

        // Carte (finition : voir AlphaDraw::paintCard()).
        AlphaDraw::paintCard (g, cardRect, pal.cardFor (band, colourMode), pal);

        // Bandeau pastel.
        const bool pastelBand = colourMode != 2;
        AlphaDraw::finishDropShadow (g, bandeauRect, 5.0f, pal.finish);
        g.setColour (pastelBand ? pastel : pal.pill);
        g.fillRoundedRectangle (bandeauRect, 5.0f);
        AlphaDraw::finishShine (g, bandeauRect, 5.0f, pal.finish);
        const auto bandInk = pastelBand ? juce::Colour (0xff26303a) : pal.pillInk;
        AlphaDraw::spacedText (g, AlphaPalette::bandName (band), nameArea, bandInk, AlphaType::title, AlphaType::kerning, juce::Justification::centredLeft);
        g.setColour (bandInk.withAlpha (0.85f));
        g.setFont (AlphaDraw::lcdFont (AlphaType::value));
        g.drawText (rangeText, hzArea, juce::Justification::centredRight, false);

        // Lignes de valeurs.
        for (int i = 0; i < 6; ++i)
            paintRowText (g, rows[(size_t) i], pal, powered);

        {
            // Pictogrammes ATT / REL dans leur case, a cote des knobs
            // (disposition compacte, voir layoutCardBody()) ; case bleu nuit
            // en sombre, gris perle en clair (couleur des pilules).
            const int pictoStyle = processorRef.attRelIcons.load();
            if (pictoStyle != AlphaAttRel::off)
                for (int i = 0; i < 2; ++i)
                {
                    const auto box = pictoBox[(size_t) i];
                    if (box.isEmpty())
                        continue;
                    g.setColour (pal.sunny ? juce::Colour (0xff253649) : pal.pill);
                    g.fillRoundedRectangle (box, 2.0f);
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    const auto& anim = pictoAnim[(size_t) i];
                    const float t = now < anim.activeUntil ? (float) ((now - anim.start) / 1000.0) : -1.0f;
                    const auto& row = rows[(size_t) i];
                    AlphaAttRel::paint (g, box.reduced (4.0f, 3.0f), pictoStyle, i == 0,
                                        AlphaAttRel::slowness (i == 0, row.slider->getValue()), t,
                                        pal, (row.greyed ? 0.35f : 1.0f) * (powered ? 1.0f : 0.5f));
                }
        }

        if (pal.sunny)
        {
            // Cadenas en haut a droite de la carte : bleu quand la bande est
            // liee (LINK IN / LINK OUT), gris sinon.
            const auto lockCol = (inLinked || outLinked) ? pal.accent : juce::Colour (0xff4a5a6c);
            const auto lb = lockRect;
            juce::Path shackle;
            shackle.addCentredArc (lb.getCentreX(), lb.getY() + 4.5f, 3.2f, 3.6f, 0.0f,
                                   -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.setColour (lockCol);
            g.strokePath (shackle, juce::PathStrokeType (1.6f));
            g.fillRoundedRectangle (lb.withTop (lb.getY() + 5.0f), 1.5f);
        }

        // EXPANDER (l'activite d'expansion s'affiche dans la colonne EXP,
        // derriere le fader). Plus de titre de section depuis le retrait de
        // l'ATT de l'expandeur : RANGE porte la couleur de l'expandeur.
        if (! expLabelArea.isEmpty())
        {
            auto area = expLabelArea;
            g.setColour (pal.cardLine);
            g.fillRect (area.getX(), area.getY() - 3.0f, area.getWidth(), 1.0f);
            AlphaDraw::spacedText (g, "EXPANDER", area, pal.light ? juce::Colour (0xff6a2fb0) : juce::Colour (0xffb98cff),
                                   AlphaType::label, AlphaType::kerning, juce::Justification::centredLeft);
        }

        // (Diodes GR/EXP : composant AlphaGrExpMeter, derriere le fader.)

        // Valeur du fader OUT + libelle/valeur du knob IN, avec pastille
        // LINK quand LINK OUT / LINK IN sont actifs (audit : etat peu
        // visible avant, simple changement de couleur en 7.5 px).
        {
            g.setColour (pal.cardInk.withAlpha (powered ? 1.0f : 0.4f));
            g.setFont (AlphaDraw::lcdFont (AlphaType::value));
            g.drawText (formatValue (rows[7]), rows[7].valueArea.toFloat(), juce::Justification::centred, false);
            if (outLinked && ! outLinkPill.isEmpty())
                drawLinkPill (g, outLinkPill, pal);

            const float inGrey = rows[6].greyed ? 0.35f : 1.0f;
            AlphaDraw::spacedText (g, "IN", rows[6].nameArea.toFloat(), labelColour (rows[6], pal).withMultipliedAlpha (inGrey),
                                   AlphaType::label, AlphaType::kerning);
            if (inLinked && ! inLinkPill.isEmpty())
                drawLinkPill (g, inLinkPill, pal);
            g.setColour (pal.cardInk.withAlpha ((powered ? 1.0f : 0.4f) * inGrey));
            g.setFont (AlphaDraw::lcdFont (AlphaType::title));
            g.drawText (formatValue (rows[6]), rows[6].valueArea.toFloat(), juce::Justification::centred, false);
        }

        g.setColour (pal.cardLine);
        g.fillRect (cardRect.getX() + 8.0f, bottomLineY, cardRect.getWidth() - 16.0f, 1.0f);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        for (auto& row : rows)
            if (! row.valueArea.isEmpty() && row.valueArea.contains (pos))
            {
                beginValueEdit (*row.slider, row.valueArea, row.decimals);
                return;
            }
    }

private:
    // Controles retires de l'interface mais gardes dans le code (retour
    // utilisateur) : passer a true pour les remettre.
    static constexpr bool kShowModeButtons = false;     // OPTICAL / OVERKILL / SC
    static constexpr bool kShowExpanderAttack = false;  // ATT de l'expandeur

    enum class LabelTint { plain, input, expander };

    struct Row
    {
        juce::Slider* slider = nullptr;
        juce::String name, unit;
        int decimals = 1;
        bool showSign = false;
        LabelTint tint = LabelTint::plain;
        bool greyed = false; // grise par le mode des aiguilles (voir updateMeterModeGreying())
        juce::Rectangle<int> nameArea, valueArea;
        double lastShown = std::numeric_limits<double>::quiet_NaN();
    };

    static juce::Colour labelColour (const Row& row, const AlphaPalette& pal)
    {
        switch (row.tint)
        {
            case LabelTint::input:    return pal.light ? pal.accentDeep : pal.accent;
            case LabelTint::expander: return pal.light ? juce::Colour (0xff6a2fb0) : juce::Colour (0xffb98cff);
            case LabelTint::plain:    break;
        }
        return pal.cardInk.withAlpha (0.85f);
    }

    // Demande explicite : en GONIO, tous les knobs sauf SPREAD sont grises ;
    // en GR, seul SPREAD l'est. Purement visuel : ils restent reglables.
    void updateMeterModeGreying()
    {
        const int mode = processorRef.meterMode.load();
        for (int i = 0; i < 7; ++i) // 0..6 = knobs (7 = fader OUT, jamais grise)
        {
            auto& row = rows[(size_t) i];
            const bool isSpread = row.slider == &widthKnob;
            const bool greyed = (mode == 2 && ! isSpread) || (mode == 1 && isSpread);
            if (greyed == row.greyed)
                continue;
            row.greyed = greyed;
            row.slider->getProperties().set ("alphaGreyed", greyed);
            row.slider->setAlpha (greyed ? 0.45f : 1.0f);
            row.slider->repaint();
            repaint (row.nameArea.getUnion (row.valueArea).expanded (4));
        }
    }

    std::array<juce::Slider*, 8> allSliders() noexcept
    {
        return { &outFader, &inKnob, &attKnob, &relKnob, &rangeKnob, &eattKnob, &mixKnob, &widthKnob };
    }

    void setupSlider (juce::Slider& s, const juce::String& paramID,
                      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        if (auto* knob = dynamic_cast<KnobSlider*> (&s))
        {
            knob->setCompact (true);
            knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        }
        s.setLookAndFeel (&laf);
        addAndMakeVisible (s);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processorRef.apvts, paramID, s);
        if (auto* param = processorRef.apvts.getParameter (paramID))
            s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    // Mise en page du design de Sunny (theme sombre) : aiguille, bandeau,
    // SOLO / SEL, ratios dans leur bac, puis la carte -- fader a gauche,
    // IN en grand, ATT (knob + case picto) puis REL (case picto + knob) en
    // quinconce, et en bas RANGE / DRY/WET / SPREAD. Cadenas en haut a
    // droite de la carte (bande liee).
    void layoutSunny()
    {
        auto plate = getLocalBounds().toFloat().reduced (1.0f);
        plateRect = plate;
        auto inner = plate.reduced (8.0f);

        needle.setBounds (inner.removeFromTop (70.0f).toNearestInt());
        needle.setPlateArea (plateRect - needle.getPosition().toFloat());
        inner.removeFromTop (6.0f);
        bandeauRect = inner.removeFromTop (20.0f);
        nameArea = hzArea = bandeauRect.reduced (8.0f, 0.0f);
        inner.removeFromTop (6.0f);
        {
            auto ss = inner.removeFromTop (24.0f);
            const float half = (ss.getWidth() - 6.0f) * 0.5f;
            soloButton.setBounds (ss.removeFromLeft (half).toNearestInt());
            ss.removeFromLeft (6.0f);
            selButton.setBounds (ss.toNearestInt());
        }
        inner.removeFromTop (6.0f);
        ratioRowRect = inner.removeFromTop (24.0f);
        {
            auto row = ratioRowRect.reduced (3.0f);
            const float w = (row.getWidth() - 2.0f * 6.0f) / 7.0f;
            for (int i = 0; i < 7; ++i)
            {
                ratioButtons[(size_t) i]->setBounds (row.removeFromLeft (w).toNearestInt());
                row.removeFromLeft (2.0f);
            }
        }
        inner.removeFromTop (6.0f);
        cardRect = inner;
        lockRect = juce::Rectangle<float> (9.0f, 12.0f).withPosition (cardRect.getRight() - 16.0f, cardRect.getY() + 6.0f);

        layoutCardBody (cardRect.reduced (8.0f), sunnyKnobs(), false);

        if (valueEditor.isVisible())
            cancelValueEdit();
    }

    // Cellule compacte (design de Sunny) : la marge du knob deborde un peu
    // sur le libelle et la valeur (l'arc bleu ne touche pas le texte).
    void layoutKnobCellCompact (Row& row, juce::Rectangle<float> area, float d)
    {
        row.nameArea  = area.removeFromTop (11.0f).toNearestInt();
        row.valueArea = area.removeFromBottom (12.0f).toNearestInt();
        const float foot = d * AlphaKnob::kExtent;
        row.slider->setBounds (juce::Rectangle<float> (foot, foot).withCentre (area.getCentre()).toNearestInt());
    }

    // Cellule de knob : libelle en haut, knob au centre (face de diametre
    // d + couronne de traits), valeur en bas.
    void layoutKnobCell (Row& row, juce::Rectangle<float> area, float d)
    {
        row.nameArea  = area.removeFromTop (12.0f).toNearestInt();
        row.valueArea = area.removeFromBottom (13.0f).toNearestInt();
        const float foot = juce::jmin (d * AlphaKnob::kExtent, area.getWidth(), area.getHeight());
        row.slider->setBounds (area.withSizeKeepingCentre (foot, foot).toNearestInt());
    }

    static juce::String formatValue (const Row& row)
    {
        const double v = row.slider->getValue();
        const int d = (std::abs (v) >= 100.0) ? 0 : row.decimals;
        auto text = juce::String (v, d);
        if (row.showSign && v > 0.0 && text.getDoubleValue() != 0.0)
            text = "+" + text;
        return text + row.unit;
    }

    void paintRowText (juce::Graphics& g, const Row& row, const AlphaPalette& pal, bool powered)
    {
        if (! row.slider->isVisible())
            return;
        // Libelle resserre s'il deborde de sa cellule (DRY/WET entre RANGE
        // et SPREAD, rangee de 3 knobs).
        auto labelArea = row.nameArea.toFloat();
        const float greyAlpha = row.greyed ? 0.35f : 1.0f;

        // Pictogramme anime ATT / REL a gauche du libelle (Options >
        // Appearance), le groupe pictogramme + libelle reste centre.
        const int pictoIndex = row.slider == &attKnob ? 0 : (row.slider == &relKnob ? 1 : -1);
        const int pictoStyle = processorRef.attRelIcons.load();
        const bool withPicto = pictoIndex >= 0 && pictoStyle != AlphaAttRel::off && pictoBox[(size_t) juce::jmax (0, pictoIndex)].isEmpty();
        const float pictoGap = 3.0f;
        const float textRoom = labelArea.getWidth() - (withPicto ? AlphaAttRel::kWidth + pictoGap : 0.0f);

        float kerning = AlphaType::kerning;
        auto textW = juce::GlyphArrangement::getStringWidth (AlphaDraw::font (AlphaType::label).withExtraKerningFactor (kerning), row.name);
        if (textW > textRoom - 6.0f)
        {
            kerning = 0.0f;
            textW = juce::GlyphArrangement::getStringWidth (AlphaDraw::font (AlphaType::label), row.name);
        }

        if (withPicto)
        {
            const float total = AlphaAttRel::kWidth + pictoGap + textW;
            auto group = labelArea.withSizeKeepingCentre (total, labelArea.getHeight());
            const auto pictoArea = group.removeFromLeft (AlphaAttRel::kWidth);
            group.removeFromLeft (pictoGap);
            labelArea = group.withWidth (textW + 2.0f);

            const double now = juce::Time::getMillisecondCounterHiRes();
            const auto& anim = pictoAnim[(size_t) pictoIndex];
            const float t = now < anim.activeUntil ? (float) ((now - anim.start) / 1000.0) : -1.0f;
            AlphaAttRel::paint (g, pictoArea, pictoStyle, pictoIndex == 0,
                                AlphaAttRel::slowness (pictoIndex == 0, row.slider->getValue()), t,
                                pal, greyAlpha * (powered ? 1.0f : 0.5f));
            AlphaDraw::spacedText (g, row.name, labelArea, labelColour (row, pal).withMultipliedAlpha (greyAlpha), AlphaType::label, kerning,
                                   juce::Justification::centredLeft);
        }
        else
        {
            AlphaDraw::spacedText (g, row.name, labelArea, labelColour (row, pal).withMultipliedAlpha (greyAlpha), AlphaType::label, kerning);
        }
        g.setColour (pal.cardInk.withAlpha ((powered ? 1.0f : 0.4f) * greyAlpha));
        g.setFont (AlphaDraw::lcdFont (AlphaType::value));
        g.drawText (formatValue (row), row.valueArea.toFloat(), juce::Justification::centred, false);
    }

    // Pastille "LINK" (LINK IN pres du knob IN, LINK OUT pres du fader).
    static void drawLinkPill (juce::Graphics& g, juce::Rectangle<float> r, const AlphaPalette& pal)
    {
        g.setColour (pal.accent);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::white);
        g.setFont (AlphaDraw::font (9.0f).withExtraKerningFactor (0.05f));
        g.drawText ("LINK", r, juce::Justification::centred, false);
    }

    bool isCrossoverOn (int x) const noexcept { return crossoverEnabled[(size_t) x]->load() > 0.5f; }

    static juce::String formatHz (float hz)
    {
        if (hz < 1000.0f)
            return juce::String (juce::roundToInt (hz));
        const float k = std::round (hz / 100.0f) / 10.0f;
        return (std::abs (k - std::round (k)) < 0.05f) ? juce::String ((int) std::round (k)) + "K" : juce::String (k, 1) + "K";
    }

    // Meme regle que SevenpackAudioProcessor::computeGroupOwners().
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
            text = "MERGED"; // la bande d'accueil se voit sur l'ecran des crossovers (bandeau sur une ligne)
        else
        {
            const float lo = (owner == 0) ? 20.0f : crossoverHz[(size_t) (owner - 1)]->load();
            const float hi = (groupEnd == SevenpackAudioProcessor::NumBands - 1) ? 20000.0f : crossoverHz[(size_t) groupEnd]->load();
            // Sans " HZ" : la plage tient sur la ligne du nom.
            text = formatHz (lo) + juce::String::fromUTF8 ("\xe2\x80\x93") + formatHz (hi);
        }
        if (text != rangeText)
        {
            rangeText = text;
            repaint (bandeauRect.getSmallestIntegerContainer());
        }

        bool anySolo = false;
        for (auto* s : soloState)
            anySolo = anySolo || s->load() > 0.5f;
        const bool soloDimmed = anySolo && soloState[(size_t) band]->load() <= 0.5f;
        const float alpha = isMerged ? 0.4f : (soloDimmed ? 0.45f : 1.0f);
        if (std::abs (alpha - getAlpha()) > 0.001f)
            setAlpha (alpha);
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
        editTarget = nullptr;
        const auto text = valueEditor.getText().trim();
        if (text.isNotEmpty())
            target->setValue (juce::jlimit (target->getMinimum(), target->getMaximum(), text.getDoubleValue()), juce::sendNotificationSync);
        valueEditor.setVisible (false);
    }

    void cancelValueEdit()
    {
        editTarget = nullptr;
        valueEditor.setVisible (false);
    }

    void timerCallback() override
    {
        const bool powered = powerParam.load() > 0.5f;
        if (powered != lastPowered)
        {
            lastPowered = powered;
            for (auto* s : allSliders())
            {
                s->getProperties().set ("alphaPowered", powered);
                s->repaint();
            }
            repaint();
        }

        const int colourMode = processorRef.bandColourMode.load();
        if (colourMode != lastColourMode) { lastColourMode = colourMode; repaint(); }

        updateMeterModeGreying();

        const int pictoStyle = processorRef.attRelIcons.load();
        if (pictoStyle != lastPictoStyle)
        {
            lastPictoStyle = pictoStyle;
            repaint (rows[0].nameArea);
            repaint (rows[1].nameArea);
            for (const auto& box : pictoBox)
                repaint (box.getSmallestIntegerContainer());
        }
        const double now = juce::Time::getMillisecondCounterHiRes();

        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& row = rows[i];
            const double v = row.slider->getValue();
            if (! (v == row.lastShown))
            {
                // ATT / REL tournes a la main (souris, molette, double-clic) :
                // le pictogramme s'anime ; pas sur un chargement de preset ou
                // une automation.
                if (i < 2 && ! std::isnan (row.lastShown) && row.slider->isMouseOverOrDragging())
                {
                    auto& anim = pictoAnim[i];
                    if (now >= anim.activeUntil)
                        anim.start = now;
                    anim.activeUntil = now + AlphaAttRel::kAnimMs;
                }
                row.lastShown = v;
                repaint (row.valueArea);
            }
        }

        // Pictogrammes en cours d'animation (plus une image figee a la fin).
        bool animating = false;
        for (size_t i = 0; i < pictoAnim.size(); ++i)
        {
            auto& anim = pictoAnim[i];
            const bool active = now < anim.activeUntil;
            if (pictoStyle != AlphaAttRel::off && (active || anim.wasActive))
                repaint (pictoBox[i].isEmpty() ? rows[i].nameArea : pictoBox[i].getSmallestIntegerContainer());
            anim.wasActive = active;
            animating = animating || active;
        }
        // 60 images/s pendant l'animation, 30 sinon.
        const int wantedHz = animating && pictoStyle != AlphaAttRel::off ? 60 : 30;
        if (getTimerInterval() != 1000 / wantedHz)
            startTimer (1000 / wantedHz);

        updateRangeState();
    }

    SevenpackAudioProcessor& processorRef;
    const int band;
    AlphaLookAndFeel laf;
    AlphaThdNeedle needle;
    AlphaGrExpMeter grExpMeter { processorRef, band };
    std::atomic<float>& powerParam;
    std::array<std::atomic<float>*, (size_t) SevenpackAudioProcessor::NumCrossovers> crossoverHz {}, crossoverEnabled {};
    std::array<std::atomic<float>*, (size_t) SevenpackAudioProcessor::NumBands> soloState {};

    AlphaButton soloButton { "SOLO" }, selButton { "SEL" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;

    std::array<std::unique_ptr<AlphaLcdButton>, 7> ratioButtons;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    AlphaChipButton opticalButton { "OPTICAL" }, overkillButton { "OVERKILL" }, scButton { "SC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> opticalAttachment, overkillAttachment, scAttachment;

    PreciseVerticalSlider outFader;
    KnobSlider inKnob, attKnob, relKnob, rangeKnob, eattKnob, mixKnob, widthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        outAttachment, inAttachment, attAttachment, relAttachment, rangeAttachment, eattAttachment, mixAttachment, widthAttachment;

    std::array<Row, 8> rows;

    juce::TextEditor valueEditor;
    juce::Slider* editTarget = nullptr;

    juce::Rectangle<float> plateRect, cardRect, bandeauRect, nameArea, hzArea, meterArea, expLabelArea, inLinkPill, outLinkPill;
    float bottomLineY = 0.0f;

    juce::String rangeText;
    bool selected = false, inLinked = false, outLinked = false, lastPowered = true;
    int lastColourMode = -1;

    // Animation des pictogrammes ATT (0) / REL (1), en ms
    // (Time::getMillisecondCounterHiRes).
    struct PictoAnim { double start = 0.0, activeUntil = 0.0; bool wasActive = false; };
    std::array<PictoAnim, 2> pictoAnim;
    // Design de Sunny : cases des pictos ATT / REL, bac des ratios, cadenas.
    std::array<juce::Rectangle<float>, 2> pictoBox {};
    juce::Rectangle<float> ratioRowRect, lockRect;
    int lastPictoStyle = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaStrip)
};
