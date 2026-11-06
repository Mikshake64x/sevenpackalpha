#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "AlphaSkin.h"

/**
    OptionsOverlay
    --------------
    Voile plein ecran affiche au clic sur l'engrenage (latencyModeButton).
    Style "Reglages" d'Apple (demande explicite : "le menu option ... plus
    style, dans l'esprit OS Apple dans les parametres ou Android, avec des
    pictogrammes, par categorie" -- apercu Options, variante Apple retenue,
    celle affichee par defaut) :
      - barre laterale a gauche : une categorie par ligne (pastille de
        couleur + pictogramme, nom, nombre de reglages) ;
      - page de la categorie a droite (juce::Viewport, scrollable) : groupes
        en cartes, une ligne par reglage (pastille + pictogramme, titre,
        sous-titre, controle cale a droite : selecteur segmente,
        interrupteur, curseur ou menu).
    Suit le theme du plugin : variante claire en theme clair, sombre en
    theme sombre (et DOS).

    Categories :
      - General    : taille de fenetre, theme, budget CPU du mode Linear.
      - Appearance : finition, couleurs des bandes, faders, pictogrammes
                     animes ATT / REL (voir AlphaAttRel.h).
      - Meters     : rebond/retour des aiguilles, vitre, bout des aiguilles,
                     goniometre (points/lignes, hors phase en rouge), spectre
                     de l'ecran des crossovers (style, lissage, cretes).
      - Outputs    : sorties separees (mix principal en standalone + une
                     ligne par bande).
      - THD stage  : surechantillonnage de l'etage THD (Off / x2 / x4 / x8,
                     filtre lineaire ou minimal), forme du clip (Soft /
                     Hard), ecretage dur par bande, plafond de securite.
      - (ancienne categorie "AI Learn" : deplacee dans CalculatorOverlay,
        engrenage a cote de LEARN, en bas de ce fichier. Le detail
        ci-dessous reste valable.) Reglages du bouton LEARN :
          - Cible de loudness par plateforme ("Off" = comportement
            d'origine ; "Side Key" = repere le plus agressif, -6 LUFS),
            style musical, fidelite (reprend la balance du mix apres
            compression).
          - Compression / Attack-release / Gain reduction : chacun a son
            propre "Off" (demande explicite : "on doit pouvoir agir que sur
            les points AI qui ne vont pas") -- Off n'ecrit QUE sa propre
            cible, le reste continue d'etre pousse normalement.
          - Expandeur : meme triade, segment choisi en violet comme la diode
            Expand (demande explicite).
          - Portee : 2 interrupteurs independants (Crossovers/Compression).
            "Detecteurs" retire (plus rien de visible a piloter) -- le
            parametre idAiScopeDetectors et le code AiLearnAnalyzer associe
            restent en place, juste plus exposes ici.
      - License    : version + gestionnaire de licence.
    Se ferme au clic hors du panneau, ou via la croix.
*/
namespace OptionsUi
{
    //==============================================================================
    // Couleurs "Reglages" Apple (clair / sombre), reprises de l'apercu.
    struct Theme
    {
        bool dark = false;
        juce::Colour bg, side, group, fg, muted, sep, edge, field, hover, accent, segOn, segOnFg, popupBg;

        static Theme current()
        {
            Theme t;
            t.dark = ! AlphaPalette::get().light;
            t.accent = juce::Colour (0xff0a84ff);
            if (t.dark)
            {
                t.bg      = juce::Colour (0xff1e1e20);
                t.side    = juce::Colour (0xff262628);
                t.group   = juce::Colour (0xff2c2c2e);
                t.fg      = juce::Colour (0xfff5f5f7);
                t.muted   = juce::Colour (0xff98989d);
                t.sep     = juce::Colours::white.withAlpha (0.09f);
                t.edge    = juce::Colours::white.withAlpha (0.12f);
                t.field   = juce::Colour (0xff3a3a3c);
                t.hover   = juce::Colours::white.withAlpha (0.06f);
                t.segOn   = juce::Colour (0xff636366);
                t.segOnFg = juce::Colours::white;
                t.popupBg = juce::Colour (0xff2c2c2e);
            }
            else
            {
                t.bg      = juce::Colour (0xfff5f5f7);
                t.side    = juce::Colour (0xffececee);
                t.group   = juce::Colours::white;
                t.fg      = juce::Colour (0xff1d1d1f);
                t.muted   = juce::Colour (0xff86868b);
                t.sep     = juce::Colours::black.withAlpha (0.09f);
                t.edge    = juce::Colours::black.withAlpha (0.20f);
                t.field   = juce::Colour (0xffe3e3e8);
                t.hover   = juce::Colours::black.withAlpha (0.05f);
                t.segOn   = juce::Colours::white;
                t.segOnFg = juce::Colour (0xff1d1d1f);
                t.popupBg = juce::Colours::white;
            }
            return t;
        }
    };

    // weight : 0 regular, 1 semibold, 2 bold. Segoe UI sous Windows (police
    // systeme, la plus proche de SF), police sans-serif par defaut ailleurs.
    inline juce::Font font (float pointHeight, int weight = 0)
    {
        static const char* const styles[] = { "Regular", "Semibold", "Bold" };
       #if JUCE_WINDOWS
        const juce::String name ("Segoe UI");
       #else
        const juce::String name (juce::Font::getDefaultSansSerifFontName());
       #endif
        return juce::Font (juce::FontOptions().withName (name)
                                              .withStyle (styles[juce::jlimit (0, 2, weight)])
                                              .withPointHeight (pointHeight));
    }

    //==============================================================================
    enum class Icon
    {
        gear, brush, gauge, split, spark, key, window, theme, cpu, finish, palette, fader,
        spring, back, glass, needle, gonio, phase, out, target, comp, timer, stack, scope,
        music, fidelity, expand, amount, info, close, check, spectrum, clip, calc, alert, count
    };

    // Pictogrammes de l'apercu (traits simples, viewBox 24), construits une
    // fois et partages par tous les composants (SharedResourcePointer).
    struct IconSet
    {
        struct Glyph { juce::Path stroke, fill; };

        IconSet()
        {
            for (int i = 0; i < (int) Icon::count; ++i)
                glyphs[(size_t) i] = make ((Icon) i);
        }

        const Glyph& operator[] (Icon icon) const   { return glyphs[(size_t) icon]; }

    private:
        static Glyph make (Icon icon)
        {
            Glyph g;
            const auto d = [&g] (const char* svg) { g.stroke.addPath (juce::Drawable::parseSVGPath (svg)); };
            const auto circle = [&g] (float cx, float cy, float r) { g.stroke.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f); };
            const auto rect = [&g] (float x, float y, float w, float h, float rx)
            {
                if (rx > 0.0f) g.stroke.addRoundedRectangle (x, y, w, h, rx);
                else           g.stroke.addRectangle (x, y, w, h);
            };

            switch (icon)
            {
                case Icon::gear:     circle (12, 12, 3.2f); circle (12, 12, 6.6f);
                                     d ("M12 2.8v2.4M12 18.8v2.4M2.8 12h2.4M18.8 12h2.4M5.5 5.5l1.7 1.7M16.8 16.8l1.7 1.7M5.5 18.5l1.7-1.7M16.8 7.2l1.7-1.7"); break;
                case Icon::brush:    d ("M14.5 4.5l5 5-8 8-5-5z"); d ("M6.5 12.5c-2 .5-3 2-3 4 0 1.5-.8 2.5-1.5 3 3 .5 6-.5 7-3"); break;
                case Icon::gauge:    d ("M4 16a8 8 0 1 1 16 0"); d ("M12 16l4-5"); circle (12, 16, 1.4f); break;
                case Icon::split:    d ("M4 12h6l4-5h6M14 17h6M10 12l4 5"); d ("M17 4l3 3-3 3M17 14l3 3-3 3"); break;
                case Icon::spark:    d ("M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z");
                                     d ("M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"); break;
                case Icon::key:      circle (8, 12, 3.5f); d ("M11.5 12H21M17 12v3M20 12v2.5"); break;
                case Icon::window:   rect (3, 5, 18, 14, 2); d ("M3 9h18M14 13l3 3M17 13v3h-3"); break;
                case Icon::theme:    circle (12, 12, 8); g.fill = juce::Drawable::parseSVGPath ("M12 4a8 8 0 0 0 0 16z"); break;
                case Icon::cpu:      rect (6.5f, 6.5f, 11, 11, 1.5f);
                                     d ("M9.5 3v3.5M14.5 3v3.5M9.5 17.5V21M14.5 17.5V21M3 9.5h3.5M3 14.5h3.5M17.5 9.5H21M17.5 14.5H21"); break;
                case Icon::finish:   d ("M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z"); d ("M9 14.5a3 3 0 0 0 3 3"); break;
                case Icon::palette:  d ("M12 3a9 9 0 1 0 0 18c1 0 1.6-.8 1.6-1.6 0-1-.8-1.5-.8-2.4 0-.9.8-1.6 1.7-1.6H17a4 4 0 0 0 4-4C21 6.8 17 3 12 3z");
                                     circle (7.5f, 11, 1); circle (10, 7, 1); circle (14.5f, 7, 1); break;
                case Icon::fader:    d ("M8 3v18M16 3v18"); rect (5, 7, 6, 4, 1); rect (13, 13, 6, 4, 1); break;
                case Icon::spring:   d ("M6 4h12M6 20h12M8 6.5l8 2.5-8 2.5 8 2.5-8 2.5"); break;
                case Icon::back:     d ("M9 7l-5 5 5 5"); d ("M4 12h9a6 6 0 0 1 6 6"); break;
                case Icon::glass:    rect (3, 6, 18, 12, 3); d ("M6 10.5c3-2 9-2 12 0"); break;
                case Icon::needle:   d ("M4 17a8 8 0 0 1 16 0"); d ("M12 17l5-8"); break;
                case Icon::gonio:    d ("M3 18a9 9 0 0 1 18 0z"); d ("M12 18l-6.3-6.3M12 18l6.3-6.3M12 18V9"); break;
                case Icon::phase:    d ("M2 12c2.5-6 5-6 7.5 0s5 6 7.5 0 3.5-4 5-3"); break;
                case Icon::out:      d ("M4 9h4l5-4v14l-5-4H4z"); d ("M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11"); break;
                case Icon::target:   circle (12, 12, 8.5f); circle (12, 12, 4.5f); circle (12, 12, 1); break;
                case Icon::comp:     d ("M3 17l6-6 4 4 8-8"); d ("M15 7h6v6"); break;
                case Icon::timer:    circle (12, 13, 7.5f); d ("M12 9v4l2.5 2M9.5 3h5"); break;
                case Icon::stack:    d ("M4 7h16M7 12h10M10 17h4"); break;
                case Icon::scope:    rect (4, 4, 16, 16, 3); d ("M9 12l2 2 4-4"); break;
                case Icon::music:    d ("M9 18V6l11-2v12"); circle (6.5f, 18, 2.5f); circle (17.5f, 16, 2.5f); break;
                case Icon::fidelity: d ("M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"); d ("M9 12l2 2 4-4"); break;
                case Icon::expand:   d ("M4 12h16M8 8l-4 4 4 4M16 8l4 4-4 4"); break;
                case Icon::amount:   d ("M4 18h16"); rect (6, 11, 3, 7, 0); rect (11, 7, 3, 11, 0); rect (16, 4, 3, 14, 0); break;
                case Icon::info:     circle (12, 12, 8.5f); d ("M12 11v5.5M12 7.6v.2"); break;
                case Icon::close:    d ("M6 6l12 12M18 6L6 18"); break;
                case Icon::check:    d ("M5 12.5l4.5 4.5L19 7.5"); break;
                case Icon::spectrum: d ("M3 19h18"); d ("M3 16l3-6 3 3 3-8 3 6 3-3 3 4"); break;
                case Icon::clip:     d ("M2 18c2-6 3.5-10 5.5-10h9c2 0 3.5 4 5.5 10"); d ("M2 8h20"); break;
                case Icon::alert:    d ("M12 4l9 16H3z"); d ("M12 10v4.5M12 17.5v.01"); break;
                case Icon::calc:     rect (5, 2.5f, 14, 19, 2.5f); rect (8, 5.5f, 8, 4, 1);
                                     d ("M9 13.5h.01M12 13.5h.01M15 13.5h.01M9 17h.01M12 17h.01M15 17h.01"); break;
                case Icon::count:    break;
            }
            return g;
        }

        std::array<Glyph, (size_t) Icon::count> glyphs;
    };

    // Pictogramme centre dans `area` ; strokeUnits = epaisseur du trait dans
    // la grille 24 (1.9 comme l'apercu).
    inline void drawGlyph (juce::Graphics& g, const IconSet& set, Icon icon, juce::Rectangle<float> area,
                           juce::Colour colour, float strokeUnits = 1.9f)
    {
        const auto& glyph = set[icon];
        const float s = juce::jmin (area.getWidth(), area.getHeight()) / 24.0f;
        const auto t = juce::AffineTransform::scale (s).translated (area.getCentreX() - 12.0f * s,
                                                                   area.getCentreY() - 12.0f * s);
        g.setColour (colour);
        if (! glyph.fill.isEmpty())
            g.fillPath (glyph.fill, t);
        g.strokePath (glyph.stroke, juce::PathStrokeType (strokeUnits * s, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded), t);
    }

    // Pastille arrondie de couleur + pictogramme blanc (Reglages macOS).
    inline void drawBadge (juce::Graphics& g, const IconSet& set, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (area, area.getWidth() * 0.25f);
        drawGlyph (g, set, icon, area.withSizeKeepingCentre (area.getWidth() * 0.62f, area.getHeight() * 0.62f),
                   juce::Colours::white);
    }

    // Fleche d'une bulle accrochee sous un bouton : pointe en ax, base sur le
    // bord haut `top` du panneau (recouvre ce bord, puis trace ses 2 cotes).
    inline void drawPopoverArrow (juce::Graphics& g, const Theme& th, float ax, float top)
    {
        juce::Path arrow;
        arrow.startNewSubPath (ax - 11.0f, top + 1.5f);
        arrow.lineTo (ax, top - 9.0f);
        arrow.lineTo (ax + 11.0f, top + 1.5f);
        arrow.closeSubPath();
        g.setColour (th.bg);
        g.fillPath (arrow);
        juce::Path sides;
        sides.startNewSubPath (ax - 10.5f, top + 0.5f);
        sides.lineTo (ax, top - 9.0f);
        sides.lineTo (ax + 10.5f, top + 0.5f);
        g.setColour (th.edge);
        g.strokePath (sides, juce::PathStrokeType (1.0f));
    }

    //==============================================================================
    // LookAndFeel des menus deroulants, curseurs et ascenseur de la page.
    // Les autres controles (segments, interrupteurs, boutons) se dessinent
    // eux-memes.
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel() { refresh(); }

        // Relit le theme -- appele a chaque ouverture / changement de theme.
        void refresh()
        {
            const auto th = Theme::current();
            // Fond de ComboBox TRANSPARENT : sinon JUCE rend le composant
            // opaque et les coins arrondis dessines ici laisseraient des
            // residus.
            setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::textColourId, th.fg);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::arrowColourId, th.muted);
            setColour (juce::PopupMenu::backgroundColourId, th.popupBg);
            setColour (juce::PopupMenu::textColourId, th.fg);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accent);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::Label::textColourId, th.fg);
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                           int, int, int, int, juce::ComboBox& box) override
        {
            const auto th = Theme::current();
            auto bg = th.field;
            if (isButtonDown)               bg = bg.contrasting (0.08f);
            else if (box.isMouseOver (true)) bg = bg.contrasting (0.04f);
            g.setColour (bg);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 7.0f);

            // Double chevron des menus pop-up macOS, cale a droite.
            const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
            juce::Path p;
            p.startNewSubPath (cx - 3.5f, cy - 2.0f); p.lineTo (cx, cy - 5.5f); p.lineTo (cx + 3.5f, cy - 2.0f);
            p.startNewSubPath (cx - 3.5f, cy + 2.0f); p.lineTo (cx, cy + 5.5f); p.lineTo (cx + 3.5f, cy + 2.0f);
            g.setColour (th.muted);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override   { return font (13.0f); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (4, 0, box.getWidth() - 30, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }

        juce::Font getPopupMenuFont() override   { return font (13.0f); }
        int getPopupMenuBorderSize() override    { return 5; }

        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            const auto th = Theme::current();
            g.fillAll (th.popupBg);
            g.setColour (th.edge);
            g.drawRect (0, 0, width, height, 1);
        }

        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                        int& idealWidth, int& idealHeight) override
        {
            if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
            idealHeight = 26;
            idealWidth = (int) juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text) + 52;
        }

        void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                                const juce::String& text, const juce::String&, const juce::Drawable*,
                                const juce::Colour*) override
        {
            const auto th = Theme::current();
            if (isSeparator)
            {
                g.setColour (th.sep);
                g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
                return;
            }

            const auto r = area.toFloat().reduced (5.0f, 1.0f);
            const bool hot = isHighlighted && isActive;
            if (hot)
            {
                g.setColour (th.accent);
                g.fillRoundedRectangle (r, 5.0f);
            }
            const auto ink = hot ? juce::Colours::white : th.fg.withAlpha (isActive ? 1.0f : 0.4f);
            if (isTicked)
                drawGlyph (g, *icons, Icon::check, r.withWidth (26.0f).withSizeKeepingCentre (13.0f, 13.0f), ink, 2.6f);
            g.setColour (ink);
            g.setFont (getPopupMenuFont());
            g.drawText (text, r.withTrimmedLeft (26.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                               float, float, juce::Slider::SliderStyle, juce::Slider&) override
        {
            const auto th = Theme::current();
            const float cy = (float) y + (float) height * 0.5f;
            const auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (th.field);
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (th.accent);
            g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

            juce::Path thumb;
            thumb.addEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ sliderPos, cy }));
            juce::DropShadow (juce::Colours::black.withAlpha (0.28f), 4, { 0, 1 }).drawForPath (g, thumb);
            g.setColour (juce::Colours::white);
            g.fillPath (thumb);
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.strokePath (thumb, juce::PathStrokeType (0.5f));
        }

        int getSliderThumbRadius (juce::Slider&) override   { return 10; }

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                            bool isVertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override
        {
            const auto th = Theme::current();
            auto thumb = isVertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) width, (float) thumbSize)
                                    : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) height);
            thumb = thumb.reduced (isVertical ? 1.5f : 2.0f, isVertical ? 2.0f : 1.5f);
            g.setColour (th.fg.withAlpha (isMouseOver || isMouseDown ? 0.35f : 0.2f));
            g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
        }

        int getDefaultScrollbarWidth() override   { return 8; }

    private:
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Selecteur segmente (NSSegmentedControl) : piste grise, pastille blanche
    // sous le choix courant (gris clair en sombre).
    class Segmented : public juce::Component
    {
    public:
        explicit Segmented (juce::StringArray labelsIn) : labels (std::move (labelsIn))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            for (auto& l : labels)
                idealWidths.push_back (juce::GlyphArrangement::getStringWidth (font (12.5f, 1), l) + 22.0f);
        }

        std::function<void (int)> onChange;

        // Teinte du segment choisi (transparente = pastille Apple). Sert a
        // garder le violet de l'expandeur.
        void setTint (juce::Colour c)   { tint = c; repaint(); }

        void setSelected (int index)
        {
            if (index != selected)
            {
                selected = index;
                repaint();
            }
        }

        int getIdealWidth() const
        {
            float w = 4.0f;
            for (auto iw : idealWidths)
                w += iw;
            return (int) std::ceil (w);
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            g.setColour (th.field);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

            const auto segs = segmentBounds();
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto& s = segs[(size_t) i];
                const bool on = (i == selected);
                if (on)
                {
                    juce::Path p;
                    p.addRoundedRectangle (s, 6.0f);
                    juce::DropShadow (juce::Colours::black.withAlpha (th.dark ? 0.4f : 0.18f), 3, { 0, 1 }).drawForPath (g, p);
                    g.setColour (tint.isTransparent() ? th.segOn : tint);
                    g.fillPath (p);
                }
                else
                {
                    if (i == hovered)
                    {
                        g.setColour (th.hover);
                        g.fillRoundedRectangle (s, 6.0f);
                    }
                    // Fin separateur entre deux segments non choisis.
                    if (i > 0 && i - 1 != selected && i != hovered && i - 1 != hovered)
                    {
                        g.setColour (th.fg.withAlpha (0.14f));
                        g.fillRect (juce::Rectangle<float> (s.getX() - 0.5f, s.getY() + s.getHeight() * 0.25f,
                                                            1.0f, s.getHeight() * 0.5f));
                    }
                }
                g.setColour (on ? (tint.isTransparent() ? th.segOnFg : juce::Colours::white) : th.fg);
                g.setFont (font (12.5f, on ? 1 : 0));
                g.drawText (labels[i], s, juce::Justification::centred, false);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override   { setHovered (indexAt (e.position.x)); }
        void mouseExit (const juce::MouseEvent&) override      { setHovered (-1); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.position.x);
            if (i >= 0 && i != selected)
            {
                selected = i;
                repaint();
                if (onChange)
                    onChange (i);
            }
        }

    private:
        std::vector<juce::Rectangle<float>> segmentBounds() const
        {
            const auto inner = getLocalBounds().toFloat().reduced (2.0f);
            float total = 0.0f;
            for (auto iw : idealWidths)
                total += iw;
            const float k = total > 0.0f ? inner.getWidth() / total : 1.0f;

            std::vector<juce::Rectangle<float>> out;
            float x = inner.getX();
            for (auto iw : idealWidths)
            {
                out.push_back ({ x, inner.getY(), iw * k, inner.getHeight() });
                x += iw * k;
            }
            return out;
        }

        int indexAt (float x) const
        {
            const auto segs = segmentBounds();
            for (int i = 0; i < (int) segs.size(); ++i)
                if (x >= segs[(size_t) i].getX() && x < segs[(size_t) i].getRight())
                    return i;
            return -1;
        }

        void setHovered (int i)
        {
            if (i != hovered)
            {
                hovered = i;
                repaint();
            }
        }

        juce::StringArray labels;
        std::vector<float> idealWidths;
        int selected = -1, hovered = -1;
        juce::Colour tint; // transparente par defaut

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
    };

    //==============================================================================
    // Interrupteur iOS 38x22, vert quand actif, pastille animee.
    class Switch : public juce::Component, private juce::Timer
    {
    public:
        Switch()   { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void (bool)> onChange;

        void setOn (bool shouldBeOn)
        {
            on = shouldBeOn;
            pos = on ? 1.0f : 0.0f;
            stopTimer();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            const auto track = getLocalBounds().toFloat().withSizeKeepingCentre (38.0f, 22.0f);
            g.setColour (th.field.interpolatedWith (juce::Colour (0xff34c759), pos));
            g.fillRoundedRectangle (track, 11.0f);

            const float k = 18.0f;
            const float cx = track.getX() + 2.0f + k * 0.5f + pos * (track.getWidth() - 4.0f - k);
            juce::Path knob;
            knob.addEllipse (juce::Rectangle<float> (k, k).withCentre ({ cx, track.getCentreY() }));
            juce::DropShadow (juce::Colours::black.withAlpha (0.3f), 3, { 0, 1 }).drawForPath (g, knob);
            g.setColour (juce::Colours::white);
            g.fillPath (knob);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition()))
                return;
            on = ! on;
            startTimerHz (60);
            if (onChange)
                onChange (on);
        }

    private:
        void timerCallback() override
        {
            const float target = on ? 1.0f : 0.0f;
            pos += (target - pos) * 0.35f;
            if (std::abs (target - pos) < 0.02f)
            {
                pos = target;
                stopTimer();
            }
            repaint();
        }

        bool on = false;
        float pos = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Switch)
    };

    //==============================================================================
    // Curseur 0..100 + valeur affichee a droite.
    class ValueSlider : public juce::Component
    {
    public:
        ValueSlider()
        {
            addAndMakeVisible (slider);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 100.0, 1.0);
            // La molette fait defiler la page, pas le curseur.
            slider.setScrollWheelEnabled (false);
            slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            slider.onValueChange = [this]
            {
                repaint();
                if (onValueChange)
                    onValueChange();
            };
        }

        std::function<void()> onValueChange;

        double getValue() const        { return slider.getValue(); }
        void setValue (double v)       { slider.setValue (v, juce::dontSendNotification); repaint(); }
        void setDefault (double v)     { slider.setDoubleClickReturnValue (true, v); }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromRight (40);
            slider.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::current().muted);
            g.setFont (font (13.0f));
            g.drawText (juce::String (juce::roundToInt (slider.getValue())), getLocalBounds().removeFromRight (32),
                        juce::Justification::centredRight, false);
        }

    private:
        juce::Slider slider;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueSlider)
    };

    //==============================================================================
    // Bouton gris a texte bleu ("Open").
    class PillButton : public juce::Button
    {
    public:
        explicit PillButton (const juce::String& text) : juce::Button (text)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = Theme::current();
            g.setColour (down ? th.field.contrasting (0.1f) : over ? th.field.contrasting (0.05f) : th.field);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
            g.setColour (th.accent);
            g.setFont (font (13.0f, 1));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        }
    };

    // Croix de fermeture ronde.
    class CloseButton : public juce::Button
    {
    public:
        CloseButton() : juce::Button ("Close")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = Theme::current();
            const auto r = getLocalBounds().toFloat();
            g.setColour (down ? th.field.contrasting (0.1f) : over ? th.field.contrasting (0.05f) : th.field);
            g.fillEllipse (r);
            drawGlyph (g, *icons, Icon::close, r.withSizeKeepingCentre (14.0f, 14.0f), th.fg, 2.4f);
        }

    private:
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Pastilles a choix multiple (bandes de "Hard clip on bands"), alignees
    // a droite, sur plusieurs lignes si besoin.
    class ChipSet : public juce::Component
    {
    public:
        explicit ChipSet (juce::StringArray labelsIn) : labels (std::move (labelsIn)), states ((size_t) labels.size(), false)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            for (auto& l : labels)
                widths.push_back (juce::GlyphArrangement::getStringWidth (font (11.5f, 1), l) + 18.0f);
        }

        std::function<void (int, bool)> onToggle;

        void setState (int index, bool on)
        {
            if (index >= 0 && index < (int) states.size() && states[(size_t) index] != on)
            {
                states[(size_t) index] = on;
                repaint();
            }
        }

        int getIdealWidth() const
        {
            float w = 0.0f;
            for (auto cw : widths)
                w += cw + kGap;
            return (int) std::ceil (w - kGap);
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            const auto chips = layoutChips();
            for (size_t i = 0; i < chips.size(); ++i)
            {
                const bool on = states[i];
                g.setColour (on ? th.accent : th.field);
                g.fillRoundedRectangle (chips[i], chips[i].getHeight() * 0.5f);
                g.setColour (on ? juce::Colours::white : th.fg);
                g.setFont (font (11.5f, 1));
                g.drawText (labels[(int) i], chips[i], juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto chips = layoutChips();
            for (size_t i = 0; i < chips.size(); ++i)
                if (chips[i].contains (e.position))
                {
                    states[i] = ! states[i];
                    repaint();
                    if (onToggle)
                        onToggle ((int) i, states[i]);
                    return;
                }
        }

    private:
        std::vector<juce::Rectangle<float>> layoutChips() const
        {
            std::vector<juce::Rectangle<float>> out (widths.size());
            constexpr float h = 24.0f;
            float y = 0.0f, rowStart = 0.0f;
            size_t first = 0;
            const float maxW = (float) getWidth();
            const auto flush = [&] (size_t last, float rowW)
            {
                float x = maxW - rowW; // aligne a droite
                for (size_t k = first; k < last; ++k)
                {
                    out[k] = { x, y, widths[k], h };
                    x += widths[k] + kGap;
                }
            };
            float rowW = 0.0f;
            for (size_t i = 0; i < widths.size(); ++i)
            {
                const float add = (i == first ? 0.0f : kGap) + widths[i];
                if (i > first && rowW + add > maxW)
                {
                    flush (i, rowW);
                    first = i;
                    y += h + kGap;
                    rowW = widths[i];
                    continue;
                }
                rowW += add;
            }
            flush (widths.size(), rowW);
            juce::ignoreUnused (rowStart);
            return out;
        }

        static constexpr float kGap = 5.0f;
        juce::StringArray labels;
        std::vector<bool> states;
        std::vector<float> widths;
    };

    // Ligne de la barre laterale : pastille + nom + nombre de reglages.
    class CategoryItem : public juce::Button
    {
    public:
        CategoryItem (const juce::String& name, Icon iconIn, juce::Colour colourIn)
            : juce::Button (name), icon (iconIn), colour (colourIn)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void setCount (int n)   { count = n; repaint(); }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto th = Theme::current();
            const auto r = getLocalBounds().toFloat();
            const bool sel = getToggleState();
            if (sel || over)
            {
                g.setColour (sel ? th.accent : th.hover);
                g.fillRoundedRectangle (r, 8.0f);
            }

            const auto badge = juce::Rectangle<float> (24.0f, 24.0f).withPosition (r.getX() + 8.0f, r.getCentreY() - 12.0f);
            drawBadge (g, *icons, icon, badge, colour);

            const auto textArea = r.withLeft (badge.getRight() + 10.0f).withTrimmedRight (10.0f);
            g.setColour (sel ? juce::Colours::white.withAlpha (0.8f) : th.muted);
            g.setFont (font (12.0f));
            g.drawText (juce::String (count), textArea, juce::Justification::centredRight, false);
            g.setColour (sel ? juce::Colours::white : th.fg);
            g.setFont (font (14.0f));
            g.drawText (getButtonText(), textArea.withTrimmedRight (24.0f), juce::Justification::centredLeft, true);
        }

    private:
        Icon icon;
        juce::Colour colour;
        int count = 0;
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Page d'une categorie : groupes (titre, carte de lignes, note de bas de
    // groupe). Calcule sa propre hauteur via layout(width), le Viewport
    // parent s'en sert pour savoir jusqu'ou defiler.
    class Page : public juce::Component
    {
    public:
        void addGroup (const juce::String& title, const juce::String& footer = {})
        {
            Group grp;
            grp.title = title;
            grp.footer = footer;
            groups.push_back (grp);
        }

        // control peut etre nul (ligne d'information seule).
        void addRow (Icon icon, juce::Colour colour, const juce::String& title, const juce::String& subtitle,
                     juce::Component* control = nullptr, int controlWidth = 0, int controlHeight = 28)
        {
            Row r;
            r.icon = icon;
            r.colour = colour;
            r.title = title;
            r.subtitle = subtitle;
            r.control = control;
            r.controlWidth = controlWidth;
            r.controlHeight = controlHeight;
            groups.back().rows.push_back (r);
            if (control != nullptr)
                addAndMakeVisible (*control);
        }

        void setRowVisible (juce::Component& control, bool shouldBeVisible)
        {
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    if (r.control == &control)
                        r.visible = shouldBeVisible;
            control.setVisible (shouldBeVisible);
        }

        void setFooter (size_t groupIndex, const juce::String& text)
        {
            if (groupIndex < groups.size())
                groups[groupIndex].footer = text;
        }

        // Marges gauche / droite des cartes et place minimale gardee pour le
        // texte des lignes (defauts = page des Options).
        void setMargins (float leftMargin, float rightMargin, float minTextW)
        {
            marginLeft = leftMargin;
            marginRight = rightMargin;
            minTextWidth = minTextW;
        }

        int getNumVisibleRows() const
        {
            int n = 0;
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    n += r.visible ? 1 : 0;
            return n;
        }

        void layout (int width)
        {
            if (width <= 0)
                return;

            const float left = marginLeft, right = marginRight;
            constexpr float padX = 14.0f;
            const float cardW = (float) width - left - right;
            float y = 2.0f;

            for (auto& grp : groups)
            {
                grp.card = {};
                grp.titleArea = {};
                grp.footerArea = {};
                if (std::none_of (grp.rows.begin(), grp.rows.end(), [] (const Row& r) { return r.visible; }))
                    continue;

                if (grp.title.isNotEmpty())
                {
                    grp.titleArea = { left + 12.0f, y + 12.0f, cardW - 12.0f, 18.0f };
                    y += 36.0f;
                }
                else
                {
                    y += 10.0f;
                }

                const float top = y;
                for (auto& r : grp.rows)
                {
                    if (! r.visible)
                        continue;
                    const float h = juce::jmax (r.subtitle.isNotEmpty() ? 54.0f : 44.0f, (float) r.controlHeight + 16.0f);
                    r.bounds = { left, y, cardW, h };
                    if (r.control != nullptr)
                    {
                        const float cw = juce::jmin ((float) r.controlWidth, cardW - minTextWidth);
                        r.control->setBounds (juce::Rectangle<float> (left + cardW - padX - cw,
                                                                      y + (h - (float) r.controlHeight) * 0.5f,
                                                                      cw, (float) r.controlHeight).toNearestInt());
                    }
                    y += h;
                }
                grp.card = { left, top, cardW, y - top };

                if (grp.footer.isNotEmpty())
                {
                    grp.footerArea = { left + 12.0f, y + 6.0f, cardW - 24.0f, 32.0f };
                    y += 40.0f;
                }
                y += 8.0f;
            }

            setSize (width, juce::roundToInt (y + 16.0f));
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            for (auto& grp : groups)
            {
                if (grp.card.isEmpty())
                    continue;

                if (! grp.titleArea.isEmpty())
                {
                    g.setColour (th.muted);
                    g.setFont (font (12.0f, 1));
                    g.drawText (grp.title, grp.titleArea, juce::Justification::centredLeft, true);
                }

                g.setColour (th.group);
                g.fillRoundedRectangle (grp.card, 10.0f);
                g.setColour (th.sep);
                g.drawRoundedRectangle (grp.card.reduced (0.25f), 10.0f, 0.5f);

                bool first = true;
                for (auto& r : grp.rows)
                {
                    if (! r.visible)
                        continue;

                    const auto badge = juce::Rectangle<float> (28.0f, 28.0f).withPosition (r.bounds.getX() + 14.0f,
                                                                                         r.bounds.getCentreY() - 14.0f);
                    const float textX = badge.getRight() + 12.0f;
                    if (! first)
                    {
                        // Separateur en retrait, aligne sur le texte (macOS).
                        g.setColour (th.sep);
                        g.fillRect (juce::Rectangle<float> (textX, r.bounds.getY(), r.bounds.getRight() - textX, 1.0f));
                    }
                    first = false;

                    drawBadge (g, *icons, r.icon, badge, r.colour);

                    const float textRight = r.control != nullptr ? (float) r.control->getX() - 12.0f
                                                                 : r.bounds.getRight() - 14.0f;
                    const float textW = juce::jmax (10.0f, textRight - textX);
                    g.setColour (th.fg);
                    g.setFont (font (14.0f));
                    if (r.subtitle.isEmpty())
                    {
                        g.drawText (r.title, juce::Rectangle<float> (textX, r.bounds.getY(), textW, r.bounds.getHeight()),
                                    juce::Justification::centredLeft, true);
                    }
                    else
                    {
                        g.drawText (r.title, juce::Rectangle<float> (textX, r.bounds.getY() + 8.0f, textW, 20.0f),
                                    juce::Justification::centredLeft, true);
                        g.setColour (th.muted);
                        g.setFont (font (12.0f));
                        g.drawText (r.subtitle, juce::Rectangle<float> (textX, r.bounds.getY() + 28.0f, textW, 17.0f),
                                    juce::Justification::centredLeft, true);
                    }
                }

                if (! grp.footerArea.isEmpty())
                {
                    g.setColour (th.muted);
                    g.setFont (font (12.0f));
                    g.drawFittedText (grp.footer, grp.footerArea.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
                }
            }
        }

    private:
        struct Row
        {
            Icon icon = Icon::gear;
            juce::Colour colour;
            juce::String title, subtitle;
            juce::Component* control = nullptr;
            int controlWidth = 0, controlHeight = 28;
            bool visible = true;
            juce::Rectangle<float> bounds;
        };

        struct Group
        {
            juce::String title, footer;
            std::vector<Row> rows;
            juce::Rectangle<float> titleArea, card, footerArea;
        };

        std::vector<Group> groups;
        float marginLeft = 28.0f, marginRight = 20.0f, minTextWidth = 260.0f;
        juce::SharedResourcePointer<IconSet> icons;
    };
}

//==============================================================================
class OptionsOverlay : public juce::Component
{
public:
    OptionsOverlay()
    {
        setLookAndFeel (&laf);

        using OptionsUi::Icon;
        struct CategoryDef { const char* name; Icon icon; juce::uint32 colour; };
        static const CategoryDef cats[kNumCategories] =
        {
            { "General",    Icon::gear,  0xff8e8e93 },
            { "Appearance", Icon::brush, 0xff0a84ff },
            { "Meters",     Icon::gauge, 0xffff9f0a },
            { "Outputs",    Icon::split, 0xff30b158 },
            { "THD stage",  Icon::clip,  0xffff453a },
            { "License",    Icon::key,   0xffffcc00 },
        };
        for (int i = 0; i < kNumCategories; ++i)
        {
            auto* item = categoryItems.add (new OptionsUi::CategoryItem (cats[i].name, cats[i].icon, juce::Colour (cats[i].colour)));
            addAndMakeVisible (item);
            item->onClick = [this, i] { showCategory (i); };
        }

        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };

        addAndMakeVisible (viewport);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);

        buildGeneralPage();
        buildAppearancePage();
        buildMetersPage();
        buildOutputsPage();
        buildThdPage();
        buildLicensePage();
        updateCategoryCounts();
        showCategory (0);
    }

    ~OptionsOverlay() override
    {
        setLookAndFeel (nullptr);
    }

    // Relit le theme (clair / sombre) -- appele a chaque ouverture et
    // changement de theme par l'editeur.
    void applyAlphaStyle()
    {
        laf.refresh();
        sendLookAndFeelChange();
        repaint();
    }

    // Ouvre une categorie (0 General, 1 Appearance, 2 Meters, 3 Outputs,
    // 4 THD stage, 5 License). Les reglages de LEARN sont dans
    // CalculatorOverlay (engrenage a cote de LEARN, voir plus bas).
    void showCategory (int index)
    {
        currentCategory = juce::jlimit (0, kNumCategories - 1, index);
        for (int i = 0; i < kNumCategories; ++i)
            categoryItems[i]->setToggleState (i == currentCategory, juce::dontSendNotification);
        viewport.setViewedComponent (&pages[currentCategory], false);
        // Une page detachee ne recoit pas les changements de LookAndFeel.
        pages[currentCategory].sendLookAndFeelChange();
        viewport.setViewPosition (0, 0);
        repaint (titleArea);
    }

    void setCurrentScaleIndex (int idx)      { scaleSeg.setSelected (idx); }
    void setCurrentLatencyIndex (int idx)    { latencySeg.setSelected (idx); }
    void setCurrentUiThemeIndex (int idx)    { themeSeg.setSelected (idx); }

    // Peuple les menus. standalone = true : `pairLabels` liste les paires de
    // sortie de la carte ("1-2", "3-4", ...). standalone = false : un seul
    // choix de routage ("Own bus") -- c'est l'hote qui route les bus nommes.
    void setOutputRoutingOptions (bool standalone, const juce::StringArray& pairLabels)
    {
        outIsStandalone = standalone;
        outPairCount = standalone ? pairLabels.size() : 1;

        mainOutCombo.clear (juce::dontSendNotification);
        if (standalone)
            for (int p = 0; p < pairLabels.size(); ++p)
                mainOutCombo.addItem ("Out " + pairLabels[p], p + 1);

        for (int i = 0; i < kNumRouteBands; ++i)
        {
            auto& c = bandOutCombos[i];
            c.clear (juce::dontSendNotification);
            c.addItem ("In the mix", 1); // id 1 => selectedId-1 == 0 => "dans le mix"
            if (standalone)
                for (int p = 0; p < pairLabels.size(); ++p)
                    c.addItem ("Out " + pairLabels[p], p + 2); // id p+2 => selectedId-1 == p+1
            else
                c.addItem ("Own bus", 2);
        }

        auto& page = pages[kOutputsPage];
        page.setRowVisible (mainOutCombo, standalone);
        page.setFooter (0, standalone ? "A band set to an output pair leaves the main mix and plays on that pair of the sound card."
                                      : "A band set to Own bus leaves the main mix; the host routes the bus.");
        page.layout (pageWidth());
        updateCategoryCounts();
    }

    void setMainOutPair (int pairIndex)
    {
        mainOutCombo.setSelectedId (juce::jlimit (0, juce::jmax (0, outPairCount - 1), pairIndex) + 1,
                                    juce::dontSendNotification);
    }

    // comboIndex : 0 = dans le mix principal ; >=1 = (paire de sortie + 1) en
    // standalone, ou 1 = "sur son bus" en VST3.
    void setBandOutRouting (int band, int comboIndex)
    {
        if (band < 0 || band >= kNumRouteBands)
            return;
        bandOutCombos[band].setSelectedId (juce::jmax (0, comboIndex) + 1, juce::dontSendNotification);
    }

    void setNeedleSettings (float bounce, float ret)
    {
        bounceSlider.setValue (bounce);
        returnSlider.setValue (ret);
    }
    void setBandColourMode (int mode)    { colourModeSeg.setSelected (mode); }
    void setNeedleGlass (int glass)      { glassSeg.setSelected (glass); }
    void setNeedleRedTip (bool red)      { tipSeg.setSelected (red ? 0 : 1); }
    void setGonioLines (bool lines)      { gonioSeg.setSelected (lines ? 1 : 0); }
    void setGonioRedPhase (bool red)     { phaseSwitch.setOn (red); }
    void setFinish (int finish)          { finishSeg.setSelected (finish); }
    void setFaderStyle (int styleIdx)    { faderSeg.setSelected (styleIdx); }
    void setWoodStyle (int style)        { woodSeg.setSelected (style); }
    void setAttRelIcons (int style)      { attRelSeg.setSelected (style); }
    void setThdOversampling (int idx)    { thdOsSeg.setSelected (idx); }
    void setThdOsFilter (int idx)        { thdOsFilterSeg.setSelected (idx); }
    void setThdClipShape (int idx)       { thdClipShapeSeg.setSelected (idx); }
    void setThdTruePeak (bool on)        { thdTruePeakSwitch.setOn (on); }
    void setBandHardClip (int band, bool on) { thdHardBandChips.setState (band, on); }
    void setSpectrumStyle (int style)    { spectrumStyleSeg.setSelected (style); }
    void setSpectrumSmooth (int smooth)  { spectrumSmoothSeg.setSelected (smooth); }
    void setSpectrumPeak (bool on)       { spectrumPeakSwitch.setOn (on); }

    void resized() override
    {
        const auto bounds = getLocalBounds().toFloat();
        sheetBounds = bounds.withSizeKeepingCentre (juce::jmin (1080.0f, bounds.getWidth() - 40.0f),
                                                    juce::jmin (660.0f, bounds.getHeight() - 60.0f));
        auto sheet = sheetBounds;
        sidebarBounds = sheet.removeFromLeft (kSidebarWidth);

        auto side = sidebarBounds.reduced (10.0f, 14.0f);
        brandBounds = side.removeFromTop (52.0f);
        side.removeFromTop (8.0f);
        for (auto* item : categoryItems)
        {
            item->setBounds (side.removeFromTop (36.0f).toNearestInt());
            side.removeFromTop (2.0f);
        }

        titleArea = sheet.removeFromTop (64.0f).toNearestInt();
        closeButton.setBounds (juce::Rectangle<int> (30, 30).withCentre ({ titleArea.getRight() - 32, titleArea.getCentreY() + 2 }));
        sheet.removeFromBottom (12.0f);
        viewport.setBounds (sheet.toNearestInt());
        for (auto& p : pages)
            p.layout (pageWidth());

        rebuildShadow();
    }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        AlphaDraw::overlayVeil (g, getLocalBounds().toFloat());
        g.drawImageAt (shadowImage, shadowOrigin.x, shadowOrigin.y);

        juce::Path sheet;
        sheet.addRoundedRectangle (sheetBounds, 14.0f);
        g.setColour (th.bg);
        g.fillPath (sheet);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (sheet);
            g.setColour (th.side);
            g.fillRect (sidebarBounds);
            g.setColour (th.sep);
            g.fillRect (sidebarBounds.withLeft (sidebarBounds.getRight() - 1.0f));
        }
        g.setColour (th.edge);
        g.strokePath (sheet, juce::PathStrokeType (1.0f));

        // Marque : pastille engrenage, "Options", version.
        auto brand = brandBounds.reduced (8.0f, 6.0f);
        const auto badge = brand.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 34.0f);
        OptionsUi::drawBadge (g, *icons, OptionsUi::Icon::gear, badge, juce::Colour (0xff3fa9f5));
        brand.removeFromLeft (10.0f);
        g.setColour (th.fg);
        g.setFont (OptionsUi::font (15.0f, 2));
        g.drawText ("Options", brand.removeFromTop (brand.getHeight() * 0.5f + 1.0f), juce::Justification::bottomLeft, true);
        g.setColour (th.muted);
        g.setFont (OptionsUi::font (11.5f));
        g.drawText (juce::String ("Sevenpack Alpha ") + JucePlugin_VersionString, brand, juce::Justification::topLeft, true);

        // Titre de la categorie courante.
        g.setColour (th.fg);
        g.setFont (OptionsUi::font (22.0f, 2));
        g.drawText (categoryItems[currentCategory]->getButtonText(), titleArea.withTrimmedLeft (28).withTrimmedRight (70),
                    juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! sheetBounds.contains (e.position) && onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;
    std::function<void()> onOpenLicense;
    // 0=Dark, 1=Light, 2=DOS retro.
    std::function<void(int)> onUiThemeSelected;
    std::function<void(float)> onScaleSelected;
    std::function<void(int)> onLatencySelected;
    // pairIndex : paire de sortie de la carte (0 = 1-2, 1 = 3-4, ...).
    std::function<void(int pairIndex)> onMainOutPairChanged;
    // band : 0=Low..6=Air. comboIndex : 0 = dans le mix ; >=1 = paire+1
    // (standalone) ou 1 = sur son bus (VST3).
    std::function<void(int band, int comboIndex)> onBandOutRoutingChanged;
    // bounce/ret : 0..100.
    std::function<void(float bounce, float ret)> onNeedleSettingsChanged;
    // 0 = bandeau, 1 = teinte, 2 = aucune.
    std::function<void(int)> onBandColourModeSelected;
    // Vitre des aiguilles : 0 = ARC, 1 = SMOKED.
    std::function<void(int)> onNeedleGlassSelected;
    // Bout des aiguilles : true = rouge, false = blanc.
    std::function<void(bool)> onNeedleTipSelected;
    // Goniometre : true = lignes ; hors phase : true = rouge.
    std::function<void(bool)> onGonioLinesSelected;
    std::function<void(bool)> onGonioRedPhaseSelected;
    // Finition : 0 MATTE, 1 SATIN, 2 GLOSS, 3 MOLDED.
    std::function<void(int)> onFinishSelected;
    // Fader : 0 BLACK, 1 VINTAGE, 2 ALU.
    std::function<void(int)> onFaderStyleSelected;
    // Bois : 0 = Classic (v2.17), 1 = New (nouveau bois de Sunny).
    std::function<void(int)> onWoodStyleSelected;
    // Pictogrammes ATT / REL : 0 OFF, 1 CURVE, 2 WAVE, 3 LEDS.
    std::function<void(int)> onAttRelIconsSelected;
    // Options > THD stage : surechantillonnage (0 Off, 1 x2, 2 x4, 3 x8),
    // filtre (0 lineaire, 1 minimal), forme du clip (0 Soft, 1 Hard),
    // plafond de securite final, ecretage dur par bande.
    std::function<void(int)> onThdOversamplingSelected;
    std::function<void(int)> onThdOsFilterSelected;
    std::function<void(int)> onThdClipShapeSelected;
    std::function<void(bool)> onThdTruePeakChanged;
    std::function<void(int band, bool on)> onBandHardClipChanged;
    // Spectre de l'ecran des crossovers : style 0 CLASSIC, 1 LINE, 2 GLOW,
    // 3 BANDS, 4 FILLED ; lissage 0 d'origine, 1 plus doux ; ligne des cretes.
    std::function<void(int)> onSpectrumStyleSelected;
    std::function<void(int)> onSpectrumSmoothSelected;
    std::function<void(bool)> onSpectrumPeakChanged;

private:
    static constexpr int kNumCategories = 6;
    enum { kGeneralPage, kAppearancePage, kMetersPage, kOutputsPage, kThdPage, kLicensePage };
    static constexpr float kSidebarWidth = 250.0f;
    static constexpr int kNumScales = 8;
    static constexpr int kNumRouteBands = 7;
    // 0 = AUTO (taille calee sur l'ecran, defaut) ; jusqu'a 200 % pour les
    // ecrans 4K (demande explicite : "sur un ecran en 4k il etait tout petit").
    static constexpr float kScaleValues[kNumScales] = { 0.0f, 0.75f, 0.90f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

public:
    static int scaleIndexFor (float scale)
    {
        if (scale <= 0.0f)
            return 0;
        int best = 1;
        for (int i = 1; i < kNumScales; ++i)
            if (std::abs (scale - kScaleValues[i]) < std::abs (scale - kScaleValues[best]))
                best = i;
        return best;
    }

private:

    int pageWidth() const   { return viewport.getWidth() - viewport.getScrollBarThickness(); }

    void updateCategoryCounts()
    {
        for (int i = 0; i < kNumCategories; ++i)
            categoryItems[i]->setCount (pages[i].getNumVisibleRows());
    }

    // Ombre portee du panneau, mise en cache (DropShadow est couteux et le
    // voile est repeint a chaque rafraichissement des vu-metres dessous).
    void rebuildShadow()
    {
        const auto area = sheetBounds.expanded (64.0f).getSmallestIntegerContainer();
        shadowOrigin = area.getPosition();
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (sheetBounds.translated ((float) -area.getX(), (float) -area.getY()), 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 48, { 0, 18 }).drawForPath (sg, p);
    }

    static void setupCombo (juce::ComboBox& c)
    {
        c.setJustificationType (juce::Justification::centredLeft);
        c.setRepaintsOnMouseActivity (true);
        c.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void buildGeneralPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kGeneralPage];
        p.addGroup ({});
        p.addRow (Icon::window, juce::Colour (0xff5e5ce6), "Window size", "Auto fits the screen (4K included)", &scaleSeg, scaleSeg.getIdealWidth());
        p.addRow (Icon::theme, juce::Colour (0xff48484a), "Theme", "Dark, light or retro DOS", &themeSeg, themeSeg.getIdealWidth());
        p.addGroup ("Performance");
        p.addRow (Icon::cpu, juce::Colour (0xff8e8e93), "Linear mode CPU budget", "Latency of the linear-phase crossover",
                  &latencySeg, latencySeg.getIdealWidth());

        scaleSeg.onChange   = [this] (int i) { if (onScaleSelected) onScaleSelected (kScaleValues[i]); };
        themeSeg.onChange   = [this] (int i) { if (onUiThemeSelected) onUiThemeSelected (i); };
        latencySeg.onChange = [this] (int i) { if (onLatencySelected) onLatencySelected (i); };
    }

    void buildAppearancePage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kAppearancePage];
        p.addGroup ("Look");
        p.addRow (Icon::finish, juce::Colour (0xff0a84ff), "Finish", "Plastic look of the light theme", &finishSeg, finishSeg.getIdealWidth());
        p.addRow (Icon::palette, juce::Colour (0xffff375f), "Band colours", "Pastel banner, tinted card or none",
                  &colourModeSeg, colourModeSeg.getIdealWidth());
        p.addRow (Icon::fader, juce::Colour (0xff636366), "Band faders", "Console fader cap", &faderSeg, faderSeg.getIdealWidth());
        p.addRow (Icon::timer, juce::Colour (0xffff9f0a), "Attack / release icons", "Next to ATT and REL, animated while turning",
                  &attRelSeg, attRelSeg.getIdealWidth());
        p.addRow (Icon::palette, juce::Colour (0xffa2845e), "Wood", "Frame around the plugin", &woodSeg, woodSeg.getIdealWidth());

        finishSeg.onChange     = [this] (int i) { if (onFinishSelected) onFinishSelected (i); };
        colourModeSeg.onChange = [this] (int i) { if (onBandColourModeSelected) onBandColourModeSelected (i); };
        faderSeg.onChange      = [this] (int i) { if (onFaderStyleSelected) onFaderStyleSelected (i); };
        attRelSeg.onChange     = [this] (int i) { if (onAttRelIconsSelected) onAttRelIconsSelected (i); };
        woodSeg.onChange       = [this] (int i) { if (onWoodStyleSelected) onWoodStyleSelected (i); };
    }

    void buildMetersPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kMetersPage];
        p.addGroup ("Needles");
        p.addRow (Icon::spring, juce::Colour (0xffff9f0a), "Bounce", "Needle overshoot", &bounceSlider, 250);
        p.addRow (Icon::back, juce::Colour (0xffff9f0a), "Return", "Needle return speed", &returnSlider, 250);
        p.addRow (Icon::glass, juce::Colour (0xff64d2ff), "Glass", "Reflection on the dials (light theme)", &glassSeg, glassSeg.getIdealWidth());
        p.addRow (Icon::needle, juce::Colour (0xffff453a), "Needle tip", "Tip colour", &tipSeg, tipSeg.getIdealWidth());
        p.addGroup ("Goniometer");
        p.addRow (Icon::gonio, juce::Colour (0xff30b158), "Display", "Dots or connected lines", &gonioSeg, gonioSeg.getIdealWidth());
        p.addRow (Icon::phase, juce::Colour (0xffff453a), "Out of phase in red", "Anti-phase content in red instead of white",
                  &phaseSwitch, 44, 26);
        p.addGroup ("Spectrum", "Music curve on the crossover screen. Classic = the original look.");
        p.addRow (Icon::spectrum, juce::Colour (0xff30b0c7), "Style", "Classic, white line, glow, band colours or filled",
                  &spectrumStyleSeg, spectrumStyleSeg.getIdealWidth());
        p.addRow (Icon::phase, juce::Colour (0xff64d2ff), "Smoothing", "Smoother curve, easier to follow",
                  &spectrumSmoothSeg, spectrumSmoothSeg.getIdealWidth());
        p.addRow (Icon::amount, juce::Colour (0xff5e5ce6), "Peak line", "Dotted line holding the peaks for a moment",
                  &spectrumPeakSwitch, 44, 26);

        const auto needleChanged = [this]
        {
            if (onNeedleSettingsChanged)
                onNeedleSettingsChanged ((float) bounceSlider.getValue(), (float) returnSlider.getValue());
        };
        bounceSlider.onValueChange = needleChanged;
        returnSlider.onValueChange = needleChanged;
        bounceSlider.setDefault (55.0);
        returnSlider.setDefault (50.0);

        glassSeg.onChange    = [this] (int i) { if (onNeedleGlassSelected) onNeedleGlassSelected (i); };
        tipSeg.onChange      = [this] (int i) { if (onNeedleTipSelected) onNeedleTipSelected (i == 0); };
        gonioSeg.onChange    = [this] (int i) { if (onGonioLinesSelected) onGonioLinesSelected (i == 1); };
        phaseSwitch.onChange = [this] (bool on) { if (onGonioRedPhaseSelected) onGonioRedPhaseSelected (on); };
        spectrumStyleSeg.onChange   = [this] (int i) { if (onSpectrumStyleSelected) onSpectrumStyleSelected (i); };
        spectrumSmoothSeg.onChange  = [this] (int i) { if (onSpectrumSmoothSelected) onSpectrumSmoothSelected (i); };
        spectrumPeakSwitch.onChange = [this] (bool on) { if (onSpectrumPeakChanged) onSpectrumPeakChanged (on); };
    }

    void buildOutputsPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kOutputsPage];
        p.addGroup ("Separate outputs");
        p.addRow (Icon::out, juce::Colour (0xff30b158), "Main mix", "Output pair of the main mix", &mainOutCombo, 170);
        setupCombo (mainOutCombo);
        mainOutCombo.onChange = [this]
        {
            if (onMainOutPairChanged && mainOutCombo.getSelectedId() > 0)
                onMainOutPairChanged (mainOutCombo.getSelectedId() - 1);
        };

        static const char* bandNames[kNumRouteBands] = { "Low", "Low-Mid", "Mid", "High-Mid", "Presence", "High", "Air" };
        for (int i = 0; i < kNumRouteBands; ++i)
        {
            p.addRow (Icon::split, juce::Colour (0xff34c759), bandNames[i], {}, &bandOutCombos[i], 170);
            setupCombo (bandOutCombos[i]);
            bandOutCombos[i].onChange = [this, i]
            {
                if (onBandOutRoutingChanged && bandOutCombos[i].getSelectedId() > 0)
                    onBandOutRoutingChanged (i, bandOutCombos[i].getSelectedId() - 1); // 0 = MIX, >=1 = paire+1
            };
        }
    }

    // Options > THD stage (demande explicite : surechantillonnage x4 minimum
    // sur le clip et le plafond, soft clip sur le tonal, hard clip sur les
    // transitoires de batterie -- maquette, option A).
    void buildThdPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kThdPage];
        p.addGroup ("Oversampling", "THD stage only, when THD is on. Adds latency, reported to the host.");
        p.addRow (Icon::amount, juce::Colour (0xff5e5ce6), "Oversampling", "Less aliasing, peaks controlled between samples",
                  &thdOsSeg, thdOsSeg.getIdealWidth());
        p.addRow (Icon::phase, juce::Colour (0xff64d2ff), "Filter", "Linear: a little more latency. Minimum: almost none",
                  &thdOsFilterSeg, thdOsFilterSeg.getIdealWidth());
        p.addGroup ("Clip", "Hard clip bands are clipped on their own first. The final ceiling (THD CEIL) always applies to the full mix.");
        p.addRow (Icon::clip, juce::Colour (0xffff453a), "Clip shape", "Soft: rounded from 3 dB below the ceiling. Hard: straight cut",
                  &thdClipShapeSeg, thdClipShapeSeg.getIdealWidth());
        p.addRow (Icon::split, juce::Colour (0xffff9f0a), "Hard clip on bands", "Keeps drum transients punchy",
                  &thdHardBandChips, thdHardBandChips.getIdealWidth(), 24);
        p.addRow (Icon::gauge, juce::Colour (0xff30b158), "True peak ceiling", "Smooth limiter: peaks between samples stay under the ceiling",
                  &thdTruePeakSwitch, 44, 26);

        thdOsSeg.onChange          = [this] (int i) { if (onThdOversamplingSelected) onThdOversamplingSelected (i); };
        thdOsFilterSeg.onChange    = [this] (int i) { if (onThdOsFilterSelected) onThdOsFilterSelected (i); };
        thdClipShapeSeg.onChange   = [this] (int i) { if (onThdClipShapeSelected) onThdClipShapeSelected (i); };
        thdTruePeakSwitch.onChange = [this] (bool on) { if (onThdTruePeakChanged) onThdTruePeakChanged (on); };
        thdHardBandChips.onToggle  = [this] (int band, bool on) { if (onBandHardClipChanged) onBandHardClipChanged (band, on); };
    }

    void buildLicensePage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kLicensePage];
        p.addGroup ({});
        p.addRow (Icon::info, juce::Colour (0xff8e8e93), "Sevenpack Alpha", juce::String ("Version ") + JucePlugin_VersionString);
        p.addRow (Icon::key, juce::Colour (0xffffcc00), "License manager", "Activate, deactivate or move a license", &licenseButton, 76);
        licenseButton.onClick = [this] { if (onOpenLicense) onOpenLicense(); };
    }

    // Declare en premier : detruit en dernier, apres tous les controles.
    OptionsUi::LookAndFeel laf;
    juce::SharedResourcePointer<OptionsUi::IconSet> icons;

    // General
    OptionsUi::Segmented scaleSeg   { { "Auto", "75%", "90%", "100%", "125%", "150%", "175%", "200%" } };
    OptionsUi::Segmented themeSeg   { { "Dark", "Light", "DOS" } };
    OptionsUi::Segmented latencySeg { { "Low", "Medium", "High" } };
    // Appearance
    OptionsUi::Segmented finishSeg     { { "Matte", "Satin", "Gloss", "Molded" } };
    OptionsUi::Segmented colourModeSeg { { "Banner", "Tint", "None" } };
    OptionsUi::Segmented faderSeg      { { "Black", "Vintage", "Alu" } };
    OptionsUi::Segmented woodSeg       { { "Classic", "New" } };
    OptionsUi::Segmented attRelSeg     { { "Off", "Curve", "Wave", "LEDs" } };
    // Meters
    OptionsUi::ValueSlider bounceSlider, returnSlider;
    OptionsUi::Segmented glassSeg { { "Arc", "Smoked" } };
    OptionsUi::Segmented tipSeg   { { "Red", "White" } };
    OptionsUi::Segmented gonioSeg { { "Dots", "Lines" } };
    OptionsUi::Switch phaseSwitch;
    OptionsUi::Segmented spectrumStyleSeg  { { "Classic", "Line", "Glow", "Bands", "Filled" } };
    OptionsUi::Segmented spectrumSmoothSeg { { "Classic", "Smoother" } };
    OptionsUi::Switch spectrumPeakSwitch;
    // Outputs
    juce::ComboBox mainOutCombo;
    juce::ComboBox bandOutCombos[kNumRouteBands];
    bool outIsStandalone = false;
    int  outPairCount = 1;
    // THD stage
    OptionsUi::Segmented thdOsSeg        { { "Off", juce::String::fromUTF8 ("\xc3\x97" "2"), juce::String::fromUTF8 ("\xc3\x97" "4"), juce::String::fromUTF8 ("\xc3\x97" "8") } };
    OptionsUi::Segmented thdOsFilterSeg  { { "Linear", "Minimum" } };
    OptionsUi::Segmented thdClipShapeSeg { { "Soft", "Hard" } };
    OptionsUi::ChipSet thdHardBandChips  { { "LOW", "LOW-MID", "MID", "HIGH-MID", "PRESENCE", "HIGH", "AIR" } };
    OptionsUi::Switch thdTruePeakSwitch;
    // License
    OptionsUi::PillButton licenseButton { "Open" };

    OptionsUi::Page pages[kNumCategories];
    juce::OwnedArray<OptionsUi::CategoryItem> categoryItems;
    OptionsUi::CloseButton closeButton;
    juce::Viewport viewport;
    int currentCategory = 0;

    juce::Rectangle<float> sheetBounds, sidebarBounds, brandBounds;
    juce::Rectangle<int> titleArea;
    juce::Image shadowImage;
    juce::Point<int> shadowOrigin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsOverlay)
};

//==============================================================================
/**
    CalculatorOverlay
    -----------------
    Reglages du bouton LEARN, sortis des Options (demande explicite : "le
    menu IA learn serait mieux a cote du bouton learn. Genre un petit
    engrenage avec juste l'option IA learn. d'ailleurs on va retirer IA et
    remplacer par calculator. On va le retirer des options a gauche").
    Apercu claude.ai/artifact/MCQB1GtJSKWSiSMjW49b9N, choix "panneau 2
    colonnes, placement A". Panneau accroche sous l'engrenage (fleche),
    sans voile ; se ferme au clic a cote, par Echap ou la croix. Memes 11
    reglages, memes parametres (idAi...) et memes callbacks qu'avant dans
    OptionsOverlay ; seuls les textes "AI" deviennent "LEARN" / "the
    calculator".
*/
class CalculatorOverlay : public juce::Component
{
public:
    CalculatorOverlay()
    {
        setLookAndFeel (&laf);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
        leftPage.setMargins (20.0f, 10.0f, 200.0f);
        rightPage.setMargins (10.0f, 20.0f, 200.0f);
        addAndMakeVisible (leftPage);
        addAndMakeVisible (rightPage);
        buildPages();
    }

    ~CalculatorOverlay() override
    {
        setLookAndFeel (nullptr);
    }

    // Relit le theme (clair / sombre) -- a chaque ouverture et changement de theme.
    void applyAlphaStyle()
    {
        laf.refresh();
        sendLookAndFeelChange();
        repaint();
    }

    void setConfirmLearn (bool on)               { confirmSwitch.setOn (on); }

    // Point (repere de ce composant) vise par la fleche : le bas de l'engrenage.
    void setAnchor (juce::Point<float> p)
    {
        anchor = p;
        resized();
        repaint();
    }

    void setCurrentAiTargetIndex (int idx)       { aiTargetCombo.setSelectedId (idx + 1, juce::dontSendNotification); }
    void setCurrentAiCompressionMode (int idx)   { compressionSeg.setSelected (idx); }
    void setCurrentAiCharacter (int idx)         { characterSeg.setSelected (idx); }
    void setCurrentAiGainReduction (int idx)     { gainReductionSeg.setSelected (idx); }
    void setAiScopeStates (bool crossoverOn, bool compressionOn)
    {
        scopeCrossoverSwitch.setOn (crossoverOn);
        scopeCompressionSwitch.setOn (compressionOn);
    }
    void setCurrentAiStyle (int idx)             { aiStyleCombo.setSelectedId (idx + 1, juce::dontSendNotification); }
    void setAiFidelityState (bool enabled)       { fidelitySwitch.setOn (enabled); }
    void setCurrentAiExpandMode (int idx)        { expandModeSeg.setSelected (idx); }
    void setCurrentAiExpandCharacter (int idx)   { expandCharacterSeg.setSelected (idx); }
    void setCurrentAiExpandAmount (int idx)      { expandAmountSeg.setSelected (idx); }

    void resized() override
    {
        constexpr float width = 1040.0f, headH = 66.0f;
        const int colW = juce::roundToInt (width * 0.5f);
        leftPage.layout (colW);
        rightPage.layout (colW);
        const float bodyH = (float) juce::jmax (leftPage.getHeight(), rightPage.getHeight());
        const float x = juce::jlimit (14.0f, juce::jmax (14.0f, (float) getWidth() - width - 14.0f), anchor.x - 90.0f);
        float y = anchor.y + 10.0f;
        if (y + headH + bodyH > (float) getHeight() - 8.0f)
            y = juce::jmax (8.0f, (float) getHeight() - 8.0f - headH - bodyH);
        sheetBounds = { x, y, width, headH + bodyH };
        headerArea = sheetBounds.withHeight (headH);
        closeButton.setBounds (juce::Rectangle<int> (30, 30).withCentre ({ juce::roundToInt (headerArea.getRight() - 30.0f),
                                                                           juce::roundToInt (headerArea.getCentreY() + 2.0f) }));
        leftPage.setTopLeftPosition (juce::roundToInt (x), juce::roundToInt (y + headH));
        rightPage.setTopLeftPosition (juce::roundToInt (x) + colW, juce::roundToInt (y + headH));
        rebuildShadow();
    }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        g.drawImageAt (shadowImage, shadowOrigin.x, shadowOrigin.y);

        g.setColour (th.bg);
        g.fillRoundedRectangle (sheetBounds, 14.0f);
        g.setColour (th.edge);
        g.drawRoundedRectangle (sheetBounds.reduced (0.5f), 14.0f, 1.0f);

        // Fleche vers l'engrenage (recouvre le bord du panneau a sa base).
        const float ax = juce::jlimit (sheetBounds.getX() + 26.0f, sheetBounds.getRight() - 26.0f, anchor.x);
        OptionsUi::drawPopoverArrow (g, th, ax, sheetBounds.getY());

        // En-tete : pastille calculatrice, titre, sous-titre.
        auto head = headerArea.reduced (20.0f, 0.0f);
        const auto badge = head.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 34.0f).translated (0.0f, 2.0f);
        OptionsUi::drawBadge (g, *icons, OptionsUi::Icon::calc, badge, juce::Colour (0xffbf5af2));
        head.removeFromLeft (12.0f);
        head = head.withTrimmedRight (60.0f).withSizeKeepingCentre (head.getWidth() - 60.0f, 40.0f).translated (0.0f, 2.0f);
        g.setColour (th.fg);
        g.setFont (OptionsUi::font (19.0f, 2));
        g.drawText ("Calculator", head.removeFromTop (22.0f), juce::Justification::centredLeft, true);
        g.setColour (th.muted);
        g.setFont (OptionsUi::font (12.0f));
        g.drawText ("What LEARN sets when it listens", head, juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! sheetBounds.expanded (0.0f, 10.0f).contains (e.position) && onDismiss)
            onDismiss();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onDismiss)
                onDismiss();
            return true;
        }
        return false;
    }

    std::function<void()> onDismiss;
    std::function<void(bool)> onConfirmLearnChanged;
    std::function<void(int)> onAiTargetSelected;
    std::function<void(int)> onAiCompressionModeSelected;
    std::function<void(int)> onAiCharacterSelected;
    std::function<void(int)> onAiGainReductionSelected;
    // scopeIndex : 0=Crossover, 1=Compression.
    std::function<void(int scopeIndex, bool enabled)> onAiScopeChanged;
    std::function<void(int)> onAiStyleSelected;
    std::function<void(bool)> onAiFidelityChanged;
    std::function<void(int)> onAiExpandModeSelected;
    std::function<void(int)> onAiExpandCharacterSelected;
    std::function<void(int)> onAiExpandAmountSelected;

private:
    static void setupCombo (juce::ComboBox& c)
    {
        c.setJustificationType (juce::Justification::centredLeft);
        c.setRepaintsOnMouseActivity (true);
        c.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void rebuildShadow()
    {
        const auto area = sheetBounds.expanded (56.0f).getSmallestIntegerContainer();
        shadowOrigin = area.getPosition();
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (sheetBounds.translated ((float) -area.getX(), (float) -area.getY()), 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 40, { 0, 14 }).drawForPath (sg, p);
    }

    // Colonne gauche : Target + Scope ; colonne droite : Compression + Expander.
    void buildPages()
    {
        using OptionsUi::Icon;
        auto& l = leftPage;
        l.addGroup ("Target");
        l.addRow (Icon::target, juce::Colour (0xffbf5af2), "Mastering target", "Loudness reference (LUFS)", &aiTargetCombo, 190);
        l.addRow (Icon::music, juce::Colour (0xffff2d55), "Style", "Output balance by music style", &aiStyleCombo, 190);
        l.addRow (Icon::fidelity, juce::Colour (0xff30b158), "Fidelity", "Restore the mix tonal balance after compression",
                  &fidelitySwitch, 44, 26);
        l.addGroup ("Scope", "What the calculator is allowed to change.");
        l.addRow (Icon::scope, juce::Colour (0xff0a84ff), "Crossovers", "LEARN may move the crossover points", &scopeCrossoverSwitch, 44, 26);
        l.addRow (Icon::scope, juce::Colour (0xff0a84ff), "Compression", "LEARN may set input, ratio, attack and release",
                  &scopeCompressionSwitch, 44, 26);
        // Confirmation avant LEARN (case "Don't ask again" de la bulle) :
        // la reactive ici.
        l.addGroup ("Safety");
        l.addRow (Icon::alert, juce::Colour (0xffff9f0a), "Confirm before LEARN", "Ask before overwriting the current setting",
                  &confirmSwitch, 44, 26);
        confirmSwitch.onChange = [this] (bool on) { if (onConfirmLearnChanged) onConfirmLearnChanged (on); };

        auto& r = rightPage;
        r.addGroup ("Compression", "Off on one setting leaves only that setting alone; the calculator still sets the others.");
        r.addRow (Icon::comp, juce::Colour (0xff5e5ce6), "Compression", "Overall intensity", &compressionSeg, compressionSeg.getIdealWidth());
        r.addRow (Icon::timer, juce::Colour (0xffff9f0a), "Attack / release", "Character", &characterSeg, characterSeg.getIdealWidth());
        r.addRow (Icon::stack, juce::Colour (0xff0a84ff), "Gain reduction", "How much in total", &gainReductionSeg,
                  gainReductionSeg.getIdealWidth());
        r.addGroup ("Expander");
        r.addRow (Icon::expand, juce::Colour (0xffbf5af2), "Expand mode", "Bands set to Expand or Both", &expandModeSeg,
                  expandModeSeg.getIdealWidth());
        r.addRow (Icon::timer, juce::Colour (0xffbf5af2), "Expand attack / release", "Character", &expandCharacterSeg,
                  expandCharacterSeg.getIdealWidth());
        r.addRow (Icon::amount, juce::Colour (0xffbf5af2), "Expand amount", "How much in total", &expandAmountSeg,
                  expandAmountSeg.getIdealWidth());

        static const char* targetLabels[] = { "Off", "Apple Music", "YouTube", "Spotify", "Deezer", "SoundCloud", "TV", "CD", "Side Key", "Match" };
        for (int i = 0; i < (int) std::size (targetLabels); ++i)
            aiTargetCombo.addItem (targetLabels[i], i + 1);
        static const char* styleLabels[] = { "Off", "Bass", "Loud", "Rock", "Pop", "Electronic" };
        for (int i = 0; i < (int) std::size (styleLabels); ++i)
            aiStyleCombo.addItem (styleLabels[i], i + 1);
        setupCombo (aiTargetCombo);
        setupCombo (aiStyleCombo);
        aiTargetCombo.onChange = [this]
        {
            if (onAiTargetSelected && aiTargetCombo.getSelectedId() > 0)
                onAiTargetSelected (aiTargetCombo.getSelectedId() - 1);
        };
        aiStyleCombo.onChange = [this]
        {
            if (onAiStyleSelected && aiStyleCombo.getSelectedId() > 0)
                onAiStyleSelected (aiStyleCombo.getSelectedId() - 1);
        };
        fidelitySwitch.onChange = [this] (bool on) { if (onAiFidelityChanged) onAiFidelityChanged (on); };

        compressionSeg.onChange   = [this] (int i) { if (onAiCompressionModeSelected) onAiCompressionModeSelected (i); };
        characterSeg.onChange     = [this] (int i) { if (onAiCharacterSelected) onAiCharacterSelected (i); };
        gainReductionSeg.onChange = [this] (int i) { if (onAiGainReductionSelected) onAiGainReductionSelected (i); };

        // Violet de la diode Expand (demande explicite : "les options de
        // l'expandeur en violet comme la diode expand").
        for (auto* sgm : { &expandModeSeg, &expandCharacterSeg, &expandAmountSeg })
            sgm->setTint (juce::Colour (0xffbf5af2));
        expandModeSeg.onChange      = [this] (int i) { if (onAiExpandModeSelected) onAiExpandModeSelected (i); };
        expandCharacterSeg.onChange = [this] (int i) { if (onAiExpandCharacterSelected) onAiExpandCharacterSelected (i); };
        expandAmountSeg.onChange    = [this] (int i) { if (onAiExpandAmountSelected) onAiExpandAmountSelected (i); };

        scopeCrossoverSwitch.onChange   = [this] (bool on) { if (onAiScopeChanged) onAiScopeChanged (0, on); };
        scopeCompressionSwitch.onChange = [this] (bool on) { if (onAiScopeChanged) onAiScopeChanged (1, on); };
    }

    // Declare en premier : detruit en dernier, apres tous les controles.
    OptionsUi::LookAndFeel laf;
    juce::SharedResourcePointer<OptionsUi::IconSet> icons;

    juce::ComboBox aiTargetCombo, aiStyleCombo;
    OptionsUi::Switch fidelitySwitch;
    OptionsUi::Segmented compressionSeg     { { "Off", "Soft", "Medium", "Hard" } };
    OptionsUi::Segmented characterSeg       { { "Off", "Smooth", "Medium", "Punchy" } };
    OptionsUi::Segmented gainReductionSeg   { { "Off", "Smooth", "Mid", "Hard" } };
    OptionsUi::Segmented expandModeSeg      { { "Off", "Soft", "Medium", "Hard" } };
    OptionsUi::Segmented expandCharacterSeg { { "Off", "Smooth", "Medium", "Punchy" } };
    OptionsUi::Segmented expandAmountSeg    { { "Off", "Smooth", "Mid", "Hard" } };
    OptionsUi::Switch scopeCrossoverSwitch, scopeCompressionSwitch;

    OptionsUi::Switch confirmSwitch;

    OptionsUi::Page leftPage, rightPage;
    OptionsUi::CloseButton closeButton;
    juce::Point<float> anchor { 270.0f, 100.0f };
    juce::Rectangle<float> sheetBounds, headerArea;
    juce::Image shadowImage;
    juce::Point<int> shadowOrigin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalculatorOverlay)
};

//==============================================================================
// Engrenage rond a droite de LEARN (placement A de l'apercu) : ouvre /
// ferme CalculatorOverlay. Allume (bleu) tant que le panneau est ouvert.
class CalculatorGearButton : public juce::Button
{
public:
    CalculatorGearButton() : juce::Button ("LEARN settings")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto pal = AlphaPalette::get();
        const bool on = getToggleState() || down;
        const auto b = getLocalBounds().toFloat().reduced (1.5f);
        juce::Colour top, bottom, border, ink;
        if (! pal.light)
        {
            top    = on ? juce::Colour (0xff4ab0f7) : juce::Colour (0xff2a3d52);
            bottom = on ? juce::Colour (0xff3a9ce8) : juce::Colour (0xff1f2f41);
            border = on ? juce::Colour (0xff8fd0ff) : juce::Colour (0xff3a5470);
            ink    = on ? juce::Colours::white : juce::Colour (0xffb9cde2);
        }
        else
        {
            top    = on ? juce::Colour (0xff2f8fdc) : juce::Colours::white;
            bottom = on ? juce::Colour (0xff2f8fdc) : juce::Colour (0xffe4e9ee);
            border = on ? juce::Colour (0xff2779bd) : pal.accent.withAlpha (0.7f);
            ink    = on ? juce::Colours::white : pal.accentDeep;
        }
        if (over && ! on)
        {
            top = top.brighter (0.12f);
            bottom = bottom.brighter (0.12f);
        }

        if (on && ! pal.light)
        {
            g.setColour (pal.accent.withAlpha (0.35f)); // halo
            g.fillEllipse (b.expanded (1.5f));
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (pal.light ? 0.18f : 0.40f)); // ombre courte
            g.fillEllipse (b.translated (0.0f, 1.5f));
        }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
        g.fillEllipse (b);
        if (on && pal.light)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.28f), 0.0f, b.getY(),
                                                     juce::Colours::transparentBlack, 0.0f, b.getY() + b.getHeight() * 0.4f, false));
            g.fillEllipse (b); // creux
        }
        g.setColour (border);
        g.drawEllipse (b, 1.0f);
        OptionsUi::drawGlyph (g, *icons, OptionsUi::Icon::gear, b.reduced (b.getWidth() * 0.19f), ink, 2.1f);
    }

private:
    juce::SharedResourcePointer<OptionsUi::IconSet> icons;
};

//==============================================================================
/**
    LearnConfirmOverlay
    -------------------
    Confirmation avant LEARN (demande explicite : "quand on clique sur learn
    actuellement on peut detruire le reglage precedent. on peut ajouter un
    Are you sure? avec ceci va ecraser votre reglage precedent. si vous ne
    souhaitez pas ecraser le reglage precedent passez sur le B"). Apercu
    claude.ai/artifact/ME2Jwf8u8VuFGs6qbtbf8x, choix "forme 1, don't ask
    avec" : bulle accrochee sous LEARN, sans voile. Cancel / "Switch to
    <autre slot> and learn" (bleu, le choix sur ; Entree) / Overwrite
    (rouge). Echap ou clic a cote = Cancel. Case "Don't ask again" (prise
    en compte seulement si on lance LEARN) : se reactive dans le panneau
    Calculator ("Confirm before LEARN").
*/
class LearnConfirmOverlay : public juce::Component
{
private:
    // Bouton de dialogue : plain (gris), primary (bleu plein), danger (texte rouge).
    class DialogButton : public juce::Button
    {
    public:
        enum class Kind { plain, primary, danger };
        DialogButton (const juce::String& text, Kind k) : juce::Button (text), kind (k)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        int getIdealWidth() const
        {
            return (int) std::ceil (juce::GlyphArrangement::getStringWidth (OptionsUi::font (13.0f, 1), getButtonText()) + 30.0f);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = OptionsUi::Theme::current();
            const auto b = getLocalBounds().toFloat();
            juce::Colour bg = kind == Kind::primary ? th.accent : th.field;
            if (down)      bg = bg.contrasting (0.12f);
            else if (over) bg = bg.contrasting (0.06f);
            g.setColour (bg);
            g.fillRoundedRectangle (b, 8.0f);
            if (hasKeyboardFocus (false) || kind == Kind::primary)
            {
                g.setColour (th.accent.withAlpha (kind == Kind::primary ? 0.0f : 0.8f));
                g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
            }
            g.setColour (kind == Kind::primary ? juce::Colours::white
                         : kind == Kind::danger ? (th.dark ? juce::Colour (0xffff453a) : juce::Colour (0xffff3b30))
                                                : th.fg);
            g.setFont (OptionsUi::font (13.0f, kind == Kind::plain ? 0 : 1));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        Kind kind;
    };

    // Case a cocher + libelle (clic sur l'ensemble).
    class CheckRow : public juce::Component
    {
    public:
        explicit CheckRow (const juce::String& text) : label (text)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void setChecked (bool c) { checked = c; repaint(); }
        bool isChecked() const   { return checked; }

        void paint (juce::Graphics& g) override
        {
            const auto th = OptionsUi::Theme::current();
            const auto box = juce::Rectangle<float> (15.0f, 15.0f).withCentre ({ 7.5f, (float) getHeight() * 0.5f });
            g.setColour (checked ? th.accent : th.field);
            g.fillRoundedRectangle (box, 4.0f);
            if (! checked)
            {
                g.setColour (th.muted.withAlpha (0.6f));
                g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
            }
            else
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY() + 0.5f);
                tick.lineTo (box.getX() + 6.5f, box.getBottom() - 4.0f);
                tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
                g.setColour (juce::Colours::white);
                g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (th.muted);
            g.setFont (OptionsUi::font (12.5f));
            g.drawText (label, getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft, true);
        }

        void mouseDown (const juce::MouseEvent&) override { setChecked (! checked); }

    private:
        juce::String label;
        bool checked = false;
    };

public:
    enum Choice { cancel = 0, switchAndLearn = 1, overwrite = 2 };

    LearnConfirmOverlay()
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (cancelButton);
        addAndMakeVisible (switchButton);
        addAndMakeVisible (overwriteButton);
        addAndMakeVisible (dontAskBox);
        cancelButton.onClick    = [this] { finish (cancel); };
        switchButton.onClick    = [this] { finish (switchAndLearn); };
        overwriteButton.onClick = [this] { finish (overwrite); };
    }

    // usingB : slot A/B actuel ; le bouton bleu propose l'autre.
    void prepare (bool usingB)
    {
        current = usingB ? "B" : "A";
        other   = usingB ? "A" : "B";
        switchButton.setButtonText ("Switch to " + other + " and learn");
        dontAskBox.setChecked (false);
        resized();
        repaint();
    }

    // Point (repere de ce composant) vise par la fleche : le bas de LEARN.
    void setAnchor (juce::Point<float> p)
    {
        anchor = p;
        resized();
        repaint();
    }

    // choice : cancel / switchAndLearn / overwrite ; dontAskAgain : case
    // cochee ET LEARN lance.
    std::function<void (int choice, bool dontAskAgain)> onChoice;

    void resized() override
    {
        constexpr float w = 452.0f, h = 192.0f;
        const float x = juce::jlimit (14.0f, juce::jmax (14.0f, (float) getWidth() - w - 14.0f), anchor.x - 100.0f);
        bubble = { x, anchor.y + 12.0f, w, h };
        auto in = bubble.reduced (20.0f, 18.0f);
        auto btns = in.removeFromBottom (30.0f);
        const auto place = [&btns] (juce::Button& b, float bw)
        {
            b.setBounds (btns.removeFromRight (bw).toNearestInt());
            btns.removeFromRight (8.0f);
        };
        place (overwriteButton, (float) juce::jmax (92, overwriteButton.getIdealWidth()));
        place (switchButton, (float) switchButton.getIdealWidth());
        place (cancelButton, (float) juce::jmax (76, cancelButton.getIdealWidth()));
        in.removeFromBottom (12.0f);
        dontAskBox.setBounds (in.removeFromBottom (20.0f).withTrimmedLeft (54.0f).toNearestInt());
        badgeArea = juce::Rectangle<float> (in.getX(), in.getY() + 2.0f, 40.0f, 40.0f);
        textArea = in.withTrimmedLeft (54.0f);
        rebuildShadow();
    }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        g.drawImageAt (shadowImage, shadowOrigin.x, shadowOrigin.y);
        g.setColour (th.bg);
        g.fillRoundedRectangle (bubble, 14.0f);
        g.setColour (th.edge);
        g.drawRoundedRectangle (bubble.reduced (0.5f), 14.0f, 1.0f);
        OptionsUi::drawPopoverArrow (g, th, juce::jlimit (bubble.getX() + 26.0f, bubble.getRight() - 26.0f, anchor.x), bubble.getY());

        // Pastille orange + triangle d'alerte.
        g.setColour (juce::Colour (0xffff9f0a));
        g.fillRoundedRectangle (badgeArea, 10.0f);
        OptionsUi::drawGlyph (g, *icons, OptionsUi::Icon::alert, badgeArea.reduced (8.0f), juce::Colours::white, 2.2f);

        auto t = textArea;
        g.setColour (th.fg);
        g.setFont (OptionsUi::font (17.0f, 2));
        g.drawText ("Are you sure?", t.removeFromTop (24.0f), juce::Justification::centredLeft, true);
        g.setFont (OptionsUi::font (13.0f, 1));
        g.drawText ("LEARN will overwrite your current setting.", t.removeFromTop (20.0f), juce::Justification::centredLeft, true);
        g.setColour (th.muted);
        g.setFont (OptionsUi::font (13.0f));
        g.drawFittedText ("If you don't want to overwrite it, switch to " + other + ": LEARN will work on " + other
                              + " and your setting stays on " + current + ".",
                          t.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! bubble.expanded (0.0f, 10.0f).contains (e.position))
            finish (cancel);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { finish (cancel); return true; }
        if (key == juce::KeyPress::returnKey) { finish (switchAndLearn); return true; }
        return false;
    }

private:
    void finish (int choice)
    {
        if (onChoice)
            onChoice (choice, choice != cancel && dontAskBox.isChecked());
    }

    void rebuildShadow()
    {
        const auto area = bubble.expanded (48.0f).getSmallestIntegerContainer();
        shadowOrigin = area.getPosition();
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (bubble.translated ((float) -area.getX(), (float) -area.getY()), 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 36, { 0, 12 }).drawForPath (sg, p);
    }

    juce::SharedResourcePointer<OptionsUi::IconSet> icons;
    DialogButton cancelButton    { "Cancel",                  DialogButton::Kind::plain };
    DialogButton switchButton    { "Switch to B and learn",   DialogButton::Kind::primary };
    DialogButton overwriteButton { "Overwrite",               DialogButton::Kind::danger };
    CheckRow dontAskBox { "Don't ask again (turn it back on in the Calculator)" };
    juce::String current { "A" }, other { "B" };
    juce::Point<float> anchor { 222.0f, 100.0f };
    juce::Rectangle<float> bubble, badgeArea, textArea;
    juce::Image shadowImage;
    juce::Point<int> shadowOrigin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnConfirmOverlay)
};
