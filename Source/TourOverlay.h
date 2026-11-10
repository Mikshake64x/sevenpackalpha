#pragma once

#include <JuceHeader.h>
#include "AlphaSkin.h"
#include "OptionsOverlay.h" // OptionsUi::font (texte courant, comme les Options et la bulle LEARN)

/*
    TourOverlay -- tutoriel d'accueil (v2.23.0)
    -------------------------------------------
    Demande du user : "un petit tuto a l'ouverture du soft et du vst. Avec des
    carres qui affichent les fonctions et un voile gris sur le reste du plug
    in". Apercu valide : claude.ai/artifact/Xg2A3AqGazm6UdTMUzcBz3, variante
    A "Bulle", textes en anglais, sans "Step x of y".

    - Voile gris sur tout l'editeur, decoupe autour des elements expliques
      (contour bleu + anneau qui pulse), bulle flechee a cote.
    - Pendant les etapes THD / GR / GONIO, la vue correspondante est affichee
      (setMeterMode) et son bouton en haut est encadre ; la vue d'origine
      revient a la fin.
    - Bloque les clics sur le plugin pendant le tour. Echap = "Skip tour"
      (va a la derniere etape, qui dit comment le relancer).
    - Vu une seule fois par ordinateur et par utilisateur, Standalone et VST
      confondus (TourSettings). Options > General > Help > Tour le relance.
*/

// Positions des elements a encadrer, en coordonnees de l'editeur (fournies
// par SevenpackEditorContent a chaque mise en page).
struct TourTargets
{
    juce::Rectangle<float> learn, gear, options, linkIn, linkOut, thdButton;
    std::array<juce::Rectangle<float>, 3> meterModes {};
    std::array<juce::Rectangle<float>, 4> thdModes {};
    struct Band { juce::Rectangle<float> needle, out, in, att, rel, range, mix, spread; };
    std::array<Band, 7> bands {};
};

// "Deja vu" : un petit fichier a cote des presets utilisateur, partage par le
// Standalone et le VST (meme dossier SideKeySystem/SevenpackAlpha).
namespace TourSettings
{
    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("SideKeySystem").getChildFile ("SevenpackAlpha").getChildFile ("tour.settings");
    }

    inline bool hasBeenSeen()
    {
        juce::PropertiesFile::Options o;
        juce::PropertiesFile pf (file(), o);
        return pf.getBoolValue ("tourSeen", false);
    }

    inline void setSeen (bool seen)
    {
        file().getParentDirectory().createDirectory();
        juce::PropertiesFile::Options o;
        juce::PropertiesFile pf (file(), o);
        pf.setValue ("tourSeen", seen);
        pf.saveIfNeeded();
    }
}

class TourOverlay : public juce::Component, private juce::Timer
{
public:
    std::function<TourTargets()> getTargets;
    std::function<void (int)> setMeterMode; // 0 THD, 1 GR, 2 GONIO
    std::function<int()> getMeterMode;

    TourOverlay()
    {
        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
        for (auto* b : { &skipButton, &backButton, &nextButton })
            addAndMakeVisible (*b);
        skipButton.onClick = [this] { goTo (lastStep()); };
        backButton.onClick = [this] { goTo (step - 1); };
        nextButton.onClick = [this] { if (step >= lastStep()) finish(); else goTo (step + 1); };
        setVisible (false);
    }

    ~TourOverlay() override { stopTimer(); }

    void start()
    {
        originalMode = getMeterMode ? getMeterMode() : 0;
        setVisible (true);
        toFront (true);
        startTimerHz (30);
        goTo (0);
        grabKeyboardFocus();
    }

    bool isRunning() const noexcept { return isVisible(); }

    // A rappeler apres une mise en page de l'editeur (taille, theme).
    void refreshLayout()
    {
        if (isVisible())
            layoutStep();
    }

    void resized() override { refreshLayout(); }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();

        // Voile gris, decoupe autour des elements expliques.
        juce::Path veil;
        veil.addRectangle (getLocalBounds().toFloat());
        for (const auto& h : holes)
            veil.addRoundedRectangle (h, kHoleCorner);
        veil.setUsingNonZeroWinding (false);
        g.setColour (pal.light ? juce::Colour (0xa3343a42) : juce::Colour (0xa80a0e13));
        g.fillPath (veil);

        // Contours + anneau qui pulse.
        const auto ring = pal.light ? juce::Colour (0xff2c95ef) : juce::Colour (0xff4fb6ff);
        const float t = pulsePhase;
        for (const auto& h : holes)
        {
            const float grow = 6.0f * t;
            g.setColour (ring.withAlpha (0.55f * (1.0f - t)));
            g.drawRoundedRectangle (h.expanded (grow), kHoleCorner + grow, 2.0f + 6.0f * t);
            g.setColour (ring);
            g.drawRoundedRectangle (h, kHoleCorner, 2.0f);
        }

        // Bulle : ombre (image en cache), feuille, fleche, textes.
        g.drawImage (shadowImage, shadowArea, juce::RectanglePlacement::stretchToFit);
        const auto sheetCol = pal.sheet();
        g.setColour (sheetCol);
        g.fillRoundedRectangle (card, kCardCorner);
        g.setColour (pal.cardLine);
        g.drawRoundedRectangle (card.reduced (0.5f), kCardCorner, 1.0f);
        if (arrowSide != Side::none)
            drawArrow (g, sheetCol, pal.cardLine);

        g.setColour (pal.heading());
        g.setFont (titleFont());
        g.drawText (steps()[(size_t) step].title.toUpperCase(), titleArea, juce::Justification::centredLeft, true);
        // Couleur lue AU DESSIN (theme courant) : les TextLayout de
        // layoutStep() ne servent qu'a mesurer.
        const auto& body = steps()[(size_t) step].body;
        for (int i = 0; i < body.size() && i < (int) paragraphAreas.size(); ++i)
            makeParagraph (body[i], pal.sheetText()).draw (g, paragraphAreas[(size_t) i]);
    }

    void mouseDown (const juce::MouseEvent&) override {} // clics sur le voile : rien (le plugin est bloque)

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)  { if (step < lastStep()) goTo (lastStep()); else finish(); return true; }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey) { nextButton.triggerClick(); return true; }
        if (key == juce::KeyPress::leftKey && step > 0) { goTo (step - 1); return true; }
        return false;
    }

private:
    enum class Place { center, right, below, belowLeft, above };
    enum class Side { none, left, top, bottom };

    struct Step
    {
        juce::String title;
        juce::StringArray body;
        int mode;      // vue a afficher (0 THD, 1 GR, 2 GONIO), -1 = vue d'origine
        Place place;
    };

    static constexpr float kCardW = 318.0f, kGap = 18.0f, kMargin = 16.0f;
    static constexpr float kHoleCorner = 9.0f, kCardCorner = 12.0f;

    // Textes valides sur l'apercu (anglais, ton de manuel).
    static const std::vector<Step>& steps()
    {
        static const std::vector<Step> s {
            { "Welcome to 7PACK Alpha",
              { "This short tour covers LEARN, its settings, the three console views and band linking. It takes about a minute." },
              0, Place::center },
            { "LEARN",
              { "Start playback, then click LEARN. 7PACK listens to eight seconds of your mix and calibrates the crossovers and the dynamics of each band.",
                "Use the result as a starting point for your master." },
              0, Place::right },
            { "LEARN settings",
              { "The gear opens the Calculator. Set the loudness target, choose what LEARN may change, and how much compression and expansion it applies." },
              0, Place::below },
            { "Console views",
              { "THD, GR and GONIO change what the band meters display and which controls are active." },
              0, Place::below },
            { "THD view",
              { "Each meter shows the saturation of its band. To change it, raise or lower the band's OUT fader." },
              0, Place::above },
            { "THD modules",
              { "Choose the saturation character: SOFT, TAPE, IRON or AIR. The THD button switches the stage on and off." },
              0, Place::belowLeft },
            { "GR view",
              { "GR is the compression and expansion section. Each band has IN, ATT, REL, RANGE and DRY/WET.",
                "RANGE sets the expander threshold. The expander takes over whenever the compressor is not working." },
              1, Place::right },
            { "Gain reduction meter",
              { "Compressor gain reduction reads to the right of zero, expansion to the left." },
              1, Place::right },
            { "GONIO view",
              { "GONIO leaves only SPREAD active, to set the stereo width of each band. The meter becomes a vectorscope." },
              2, Place::right },
            { "LINK IN / LINK OUT",
              { "LINK IN moves the IN controls of all seven bands together. LINK OUT does the same for the OUT faders.",
                "Each band keeps its offset, so the balance between bands is preserved." },
              1, Place::below },
            { "End of the tour",
              { "The tour won't open again. To replay it, go to Options > General > Tour." },
              -1, Place::belowLeft },
        };
        return s;
    }

    int lastStep() const { return (int) steps().size() - 1; }

    static juce::Font titleFont() { return AlphaDraw::font (15.0f, true).withExtraKerningFactor (0.1f); }
    static juce::Font bodyFont()  { return OptionsUi::font (13.5f); }

    static juce::AttributedString makeParagraph (const juce::String& text, juce::Colour colour)
    {
        juce::AttributedString as;
        as.setText (text);
        as.setFont (bodyFont());
        as.setColour (colour);
        as.setLineSpacing (2.0f);
        as.setJustification (juce::Justification::topLeft);
        as.setWordWrap (juce::AttributedString::byWord);
        return as;
    }

    static juce::Rectangle<float> pad (juce::Rectangle<float> r, float t, float side, float b)
    {
        return { r.getX() - side, r.getY() - t, r.getWidth() + 2.0f * side, r.getHeight() + t + b };
    }
    static juce::Rectangle<float> pad (juce::Rectangle<float> r, float all) { return r.expanded (all); }
    // Knob : libelle au-dessus, valeur en dessous. Marges reglees knob par
    // knob pour que les cadres d'une tranche ne se chevauchent pas (voile en
    // pair-impair : une zone couverte par deux decoupes serait revoilee).
    static juce::Rectangle<float> knob (juce::Rectangle<float> r, float top = 12.0f, float bottom = 14.0f)
    {
        return pad (r, top, 5.0f, bottom);
    }

    // Securite : deux decoupes qui se chevauchent sont fusionnees.
    static void mergeOverlaps (std::vector<juce::Rectangle<float>>& rs)
    {
        for (bool merged = true; merged;)
        {
            merged = false;
            for (size_t i = 0; i < rs.size() && ! merged; ++i)
                for (size_t j = i + 1; j < rs.size() && ! merged; ++j)
                    if (rs[i].intersects (rs[j]))
                    {
                        rs[i] = rs[i].getUnion (rs[j]);
                        rs.erase (rs.begin() + (std::ptrdiff_t) j);
                        merged = true;
                    }
        }
    }

    static juce::Rectangle<float> unionOf (const std::vector<juce::Rectangle<float>>& rs)
    {
        juce::Rectangle<float> u;
        for (const auto& r : rs)
            u = u.isEmpty() ? r : u.getUnion (r);
        return u;
    }

    // Decoupes (holes) et elements qui placent la bulle (anchor) de l'etape.
    void computeTargets (const TourTargets& t, std::vector<juce::Rectangle<float>>& outHoles,
                         std::vector<juce::Rectangle<float>>& outAnchor) const
    {
        const auto& M = t.bands[2]; // bande MID pour les exemples d'une tranche
        auto view = [&t] (int i) { return pad (t.meterModes[(size_t) i], 5.0f); };
        std::vector<juce::Rectangle<float>> needles, outs, ins;
        for (const auto& b : t.bands) { needles.push_back (b.needle); outs.push_back (pad (b.out, 6.0f)); ins.push_back (knob (b.in, 16.0f)); }
        const auto needleRow = pad (unionOf (needles), 5.0f);
        const auto links = pad (t.linkIn.getUnion (t.linkOut), 6.0f);
        auto thdBlock = t.thdButton;
        for (const auto& r : t.thdModes) thdBlock = thdBlock.getUnion (r);

        outHoles.clear(); outAnchor.clear();
        switch (step)
        {
            case 1: outHoles = { pad (t.learn, 6.0f) }; break;
            case 2: outHoles = { pad (t.gear, 6.0f) }; break;
            case 3: outHoles = { pad (unionOf ({ t.meterModes[0], t.meterModes[1], t.meterModes[2] }), 6.0f) }; break;
            case 4: outHoles = { view (0), needleRow }; outHoles.insert (outHoles.end(), outs.begin(), outs.end());
                    outAnchor = { needleRow }; break;
            case 5: outHoles = { pad (thdBlock, 6.0f) }; break;
            case 6: outAnchor = { knob (M.in, 16.0f), knob (M.att, 11.0f), knob (M.rel), knob (M.range), knob (M.mix) };
                    outHoles = outAnchor; outHoles.insert (outHoles.begin(), view (1)); break;
            case 7: outAnchor = { pad (M.needle, 5.0f) }; outHoles = { view (1), outAnchor[0] }; break;
            case 8: outAnchor = { pad (M.needle, 5.0f), knob (M.spread) };
                    outHoles = outAnchor; outHoles.insert (outHoles.begin(), view (2)); break;
            case 9: outAnchor = { links }; outHoles = { links };
                    outHoles.insert (outHoles.end(), ins.begin(), ins.end());
                    outHoles.insert (outHoles.end(), outs.begin(), outs.end()); break;
            case 10: outHoles = { pad (t.options, 6.0f) }; break;
            default: break; // 0 : accueil, sans decoupe
        }
        if (outAnchor.empty())
            outAnchor = outHoles;
        mergeOverlaps (outHoles);
    }

    void goTo (int newStep)
    {
        step = juce::jlimit (0, lastStep(), newStep);
        const int mode = steps()[(size_t) step].mode;
        if (setMeterMode)
            setMeterMode (mode >= 0 ? mode : originalMode);
        if (step == lastStep())
            TourSettings::setSeen (true); // vu jusqu'au bout (ou passe) : ne revient plus
        layoutStep();
        grabKeyboardFocus();
    }

    void finish()
    {
        TourSettings::setSeen (true);
        if (setMeterMode)
            setMeterMode (originalMode);
        stopTimer();
        setVisible (false);
    }

    void layoutStep()
    {
        const auto& st = steps()[(size_t) step];
        std::vector<juce::Rectangle<float>> anchorRects;
        holes.clear();
        if (getTargets)
            computeTargets (getTargets(), holes, anchorRects);

        // Textes.
        const float innerW = kCardW - 36.0f;
        paragraphs.clear();
        float textH = 0.0f;
        for (const auto& p : st.body)
        {
            juce::TextLayout tl;
            tl.createLayout (makeParagraph (p, juce::Colours::black), innerW);
            textH += tl.getHeight() + (paragraphs.empty() ? 0.0f : 7.0f);
            paragraphs.push_back (std::move (tl));
        }
        const float cardH = 18.0f + 22.0f + 8.0f + textH + 14.0f + 30.0f + 14.0f;

        // Placement de la bulle.
        const float W = (float) getWidth(), H = (float) getHeight();
        auto clampX = [&] (float v) { return juce::jlimit (kMargin, W - kCardW - kMargin, v); };
        auto clampY = [&] (float v) { return juce::jlimit (kMargin, H - cardH - kMargin, v); };
        arrowSide = Side::none;
        if (anchorRects.empty() || st.place == Place::center)
        {
            card = { (W - kCardW) * 0.5f, (H - cardH) * 0.5f, kCardW, cardH };
        }
        else
        {
            const auto a = unionOf (anchorRects);
            const float focusY = a.getY() + juce::jmin (a.getHeight(), 120.0f) * 0.5f;
            switch (st.place)
            {
                case Place::right:     card = { a.getRight() + kGap, clampY (focusY - 40.0f), kCardW, cardH };
                                       arrowSide = Side::left; arrowPos = focusY; break;
                case Place::below:     card = { clampX (a.getCentreX() - kCardW * 0.5f), a.getBottom() + kGap, kCardW, cardH };
                                       arrowSide = Side::top; arrowPos = a.getCentreX(); break;
                case Place::belowLeft: card = { clampX (a.getRight() - kCardW), a.getBottom() + kGap, kCardW, cardH };
                                       arrowSide = Side::top; arrowPos = a.getCentreX(); break;
                case Place::above:     card = { clampX (a.getCentreX() - kCardW * 0.5f), clampY (a.getY() - cardH - kGap), kCardW, cardH };
                                       arrowSide = Side::bottom; arrowPos = a.getCentreX(); break;
                case Place::center:    break;
            }
            arrowPos = arrowSide == Side::left ? juce::jlimit (card.getY() + 22.0f, card.getBottom() - 22.0f, arrowPos)
                                               : juce::jlimit (card.getX() + 24.0f, card.getRight() - 24.0f, arrowPos);
        }

        auto in = card.reduced (18.0f, 0.0f).withTrimmedTop (18.0f).withTrimmedBottom (14.0f);
        titleArea = in.removeFromTop (22.0f);
        in.removeFromTop (8.0f);
        paragraphAreas.clear();
        for (size_t i = 0; i < paragraphs.size(); ++i)
        {
            if (i > 0) in.removeFromTop (7.0f);
            paragraphAreas.push_back (in.removeFromTop (paragraphs[i].getHeight()));
        }

        // Boutons.
        const bool first = step == 0, last = step == lastStep();
        skipButton.setVisible (! last);
        backButton.setVisible (! first);
        nextButton.setButtonText (first ? "Start tour" : last ? "Done" : "Next");
        auto row = card.reduced (18.0f, 0.0f).removeFromBottom (14.0f + 30.0f).withTrimmedBottom (14.0f);
        nextButton.setBounds (row.removeFromRight ((float) nextButton.getIdealWidth()).toNearestInt());
        row.removeFromRight (8.0f);
        if (! first)
            backButton.setBounds (row.removeFromRight ((float) backButton.getIdealWidth()).toNearestInt());
        if (! last)
            skipButton.setBounds (row.removeFromLeft ((float) skipButton.getIdealWidth()).toNearestInt());

        rebuildShadow();
        repaint();
    }

    void drawArrow (juce::Graphics& g, juce::Colour fill, juce::Colour edge) const
    {
        juce::Path tri, sides;
        const float s = 9.0f;
        if (arrowSide == Side::left)
        {
            const float x = card.getX();
            tri.addTriangle (x + 1.5f, arrowPos - 11.0f, x - s, arrowPos, x + 1.5f, arrowPos + 11.0f);
            sides.startNewSubPath (x + 0.5f, arrowPos - 10.5f); sides.lineTo (x - s, arrowPos); sides.lineTo (x + 0.5f, arrowPos + 10.5f);
        }
        else if (arrowSide == Side::top)
        {
            const float y = card.getY();
            tri.addTriangle (arrowPos - 11.0f, y + 1.5f, arrowPos, y - s, arrowPos + 11.0f, y + 1.5f);
            sides.startNewSubPath (arrowPos - 10.5f, y + 0.5f); sides.lineTo (arrowPos, y - s); sides.lineTo (arrowPos + 10.5f, y + 0.5f);
        }
        else
        {
            const float y = card.getBottom();
            tri.addTriangle (arrowPos - 11.0f, y - 1.5f, arrowPos, y + s, arrowPos + 11.0f, y - 1.5f);
            sides.startNewSubPath (arrowPos - 10.5f, y - 0.5f); sides.lineTo (arrowPos, y + s); sides.lineTo (arrowPos + 10.5f, y - 0.5f);
        }
        g.setColour (fill);
        g.fillPath (tri);
        g.setColour (edge);
        g.strokePath (sides, juce::PathStrokeType (1.0f));
    }

    // Ombre de la bulle calculee une fois par etape (pas a chaque image de
    // l'anneau qui pulse).
    void rebuildShadow()
    {
        shadowArea = card.expanded (34.0f);
        const auto pal = AlphaPalette::get();
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) shadowArea.getWidth()), juce::jmax (1, (int) shadowArea.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (card.translated (-shadowArea.getX(), -shadowArea.getY()), kCardCorner);
        juce::DropShadow (juce::Colours::black.withAlpha (pal.light ? 0.30f : 0.55f), 24, { 0, 8 }).drawForPath (sg, p);
    }

    void timerCallback() override
    {
        pulsePhase = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1800.0, 1.0);
        for (const auto& h : holes)
            repaint (h.expanded (12.0f).getSmallestIntegerContainer());
    }

    // Boutons de la bulle : lien discret, contour, ou bleu plein.
    class TourButton : public juce::Button
    {
    public:
        enum class Kind { link, plain, primary };
        TourButton (const juce::String& text, Kind k) : juce::Button (text), kind (k)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (false);
        }

        int getIdealWidth() const
        {
            const float pad = kind == Kind::link ? 4.0f : 30.0f;
            return (int) std::ceil (juce::GlyphArrangement::getStringWidth (OptionsUi::font (13.0f, 1), getButtonText()) + pad);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto pal = AlphaPalette::get();
            const auto b = getLocalBounds().toFloat();
            if (kind == Kind::primary)
            {
                auto bg = pal.light ? pal.accentDeep : pal.accent;
                if (down) bg = bg.darker (0.15f); else if (over) bg = bg.brighter (0.08f);
                g.setColour (bg);
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (pal.light ? juce::Colours::white : juce::Colour (0xff0b1520));
            }
            else if (kind == Kind::plain)
            {
                g.setColour (pal.sheetText().withAlpha (down ? 0.12f : over ? 0.07f : 0.0f));
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (pal.cardLine);
                g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);
                g.setColour (pal.sheetText());
            }
            else
            {
                g.setColour (over ? pal.sheetText() : pal.sheetTextSoft());
            }
            g.setFont (OptionsUi::font (13.0f, kind == Kind::link ? 0 : 1));
            g.drawText (getButtonText(), getLocalBounds(), kind == Kind::link ? juce::Justification::centredLeft
                                                                                : juce::Justification::centred, false);
        }

    private:
        Kind kind;
    };

    TourButton skipButton { "Skip tour", TourButton::Kind::link };
    TourButton backButton { "Back", TourButton::Kind::plain };
    TourButton nextButton { "Next", TourButton::Kind::primary };

    int step = 0, originalMode = 0;
    float pulsePhase = 0.0f, arrowPos = 0.0f;
    Side arrowSide = Side::none;
    std::vector<juce::Rectangle<float>> holes;
    juce::Rectangle<float> card, titleArea, shadowArea;
    std::vector<juce::TextLayout> paragraphs;
    std::vector<juce::Rectangle<float>> paragraphAreas;
    juce::Image shadowImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourOverlay)
};
