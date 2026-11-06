#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

/**
    AlphaSkin
    ---------
    Habillage de l'interface v2 "Sevenpack Alpha" (maquette 7PACK validee
    par l'utilisateur : chassis argent + accents bleus, tranches bleues
    avec carte claire, cadre bois en arriere-plan). Deux palettes : CLAIRE
    (argent/bleu, celle de la maquette) et SOMBRE (graphite/bleu nuit) --
    le theme DOS retro de Sixpack tombe sur la palette sombre ici.

    Contient la palette, les petites primitives de dessin partagees et les
    widgets de l'interface Alpha (boutons bleus, interrupteurs a pilule,
    boutons icone, knob metal, fader), et le style des fenetres secondaires
    (Options, About, Presets, Licence, alertes) : voile + panneau clair.

    Echelle de texte FIXE (audit lisibilite, demande explicite) : voir
    AlphaType -- jamais en dessous de 9 px, espacement des lettres 0.15.
*/
namespace AlphaType
{
    constexpr float label   = 9.5f;  // etiquettes (ATT, SOLO, OPTICAL...)
    constexpr float value   = 11.0f; // valeurs chiffrees
    constexpr float title   = 12.0f; // titres (nom de bande...)
    constexpr float kerning = 0.15f;
}

struct AlphaPalette
{
    bool light = true;
    // Theme sombre = design de Sunny (maquette VST_V7.1_FULL.ai, demande
    // explicite : "tu peux coder cette version pour check le design") :
    // panneau bleu nuit, tranches bleues a liseret, carte #172130, knobs
    // noirs a arc bleu, ecran turquoise a vitre en biais... Les widgets
    // testent ce drapeau pour leur dessin "Sunny".
    bool sunny = false;
    bool highContrast = false;
    // Finition (Options > INTERFACE > FINISH, apercu "Theme clair glossy") :
    // 0 MATTE (d'origine), 1 SATIN, 2 GLOSS, 3 MOLDED. Voir
    // AlphaDraw::finishShine() / paintPlate() / paintCard().
    int finish = 0;
    // Fader OUT des bandes (Options > INTERFACE > FADER, apercu "Faders de
    // console") : 0 BLACK (console noire), 1 VINTAGE (creme), 2 ALU.
    int faderStyle = 0;
    // Fader BLACK en theme sombre (demande explicite : "fais correspondre en
    // mode sombre ... sauf que tu inverse la couleurs du fader") : 0 = comme
    // le clair (capuchon noir), 1 = capuchon BLANC a ligne noire, 2 = BLANC
    // a ligne bleue, 3 = BLANC + fente claire (tout inverse, CHOISI par
    // l utilisateur sur l apercu ; 1 et 2 gardes dans le code).
    int darkFader = 3;
    juce::Colour accent, accentDeep, accentGlow;
    juce::Colour ink, inkSoft;
    juce::Colour panelTop, panelUpper, panelMid, panelLower, panelBottom;
    juce::Colour sep, sepHi;
    juce::Colour graphBg, graphGrid, graphLine, graphInk;
    juce::Colour strip, card, cardInk, cardLine;
    juce::Colour pill, pillInk, lcd, lcdInk, track;
    juce::Colour logoBg;
    juce::Colour faceArc, faceLabel, faceRed, needle, faceFoot;

    static AlphaPalette get()
    {
        auto p = SevenpackLookAndFeel::isLightThemeStatic() ? makeLight() : makeDark();
        if (highContrastActive)
            p.applyHighContrast();
        p.finish = juce::jlimit (0, 3, finishActive);
        p.faderStyle = juce::jlimit (0, 2, faderStyleActive);
        p.darkFader = juce::jlimit (0, 3, darkFaderActive);
        return p;
    }

    // Contraste renforce (bouton sous A/B, demande explicite : "une fonction
    // contraste pour les ecrans pourris", puis "c'est plus les knobs qui sont
    // difficiles a voir, pas les ecritures") : n'agit QUE sur le dessin des
    // knobs (voir AlphaKnob::themeFor()), les couleurs de l'interface restent
    // les memes. Meme portee que le theme (statique, voir
    // SevenpackLookAndFeel::isLightThemeStatic()).
    static inline bool highContrastActive = false;
    // Finition active (meme portee statique que le theme).
    static inline int finishActive = 0;
    static inline int faderStyleActive = 0;
    static inline int darkFaderActive = 3;

    void applyHighContrast() { highContrast = true; }

    static AlphaPalette makeLight()
    {
        AlphaPalette p;
        p.light = true;
        p.accent = juce::Colour (0xff3fa9f5); p.accentDeep = juce::Colour (0xff1b7fcf); p.accentGlow = juce::Colour (0x8c3fa9f5);
        p.ink = juce::Colour (0xff2c3a47); p.inkSoft = juce::Colour (0xff6a7885);
        p.panelTop = juce::Colour (0xfff5f6f8); p.panelUpper = juce::Colour (0xffdfe1e4); p.panelMid = juce::Colour (0xffcdd0d3);
        p.panelLower = juce::Colour (0xffeceef0); p.panelBottom = juce::Colour (0xffc4c7ca);
        p.sep = juce::Colour (0x38283440); p.sepHi = juce::Colour (0xd9ffffff);
        p.graphBg = juce::Colours::white; p.graphGrid = juce::Colour (0x243fa9f5); p.graphLine = juce::Colour (0xff5ccbf5); p.graphInk = juce::Colour (0xff5b6e80);
        p.strip = juce::Colour (0xff3fa9f5); p.card = juce::Colour (0xffe6f1f3); /* teinte du panneau de l'image du knob (relief neumorphique) */ p.cardInk = juce::Colour (0xff2c3a47); p.cardLine = juce::Colour (0xffc9dcec);
        p.pill = juce::Colour (0xffdde3e8); p.pillInk = juce::Colour (0xff3a4652); p.lcd = juce::Colour (0xff565a5f); p.lcdInk = juce::Colour (0xfff3f6f8);
        p.track = juce::Colour (0xffc3d0da);
        p.logoBg = juce::Colours::white;
        p.faceArc = juce::Colours::white; p.faceLabel = juce::Colour (0xff0d2f4d); p.faceRed = juce::Colour (0xffd93a2b);
        p.needle = juce::Colours::white; p.faceFoot = juce::Colour (0x290a2846);
        return p;
    }

    static AlphaPalette makeDark()
    {
        // Couleurs relevees sur la maquette de Sunny (VST_V7.1_FULL.ai).
        AlphaPalette p;
        p.light = false;
        p.sunny = true;
        p.accent = juce::Colour (0xff3fa9f5); p.accentDeep = juce::Colour (0xff1b7fcf); p.accentGlow = juce::Colour (0x8c3fa9f5);
        p.ink = juce::Colour (0xffe6eef6); p.inkSoft = juce::Colour (0xff8fa3b8);
        p.panelTop = juce::Colour (0xff2a3341); p.panelUpper = juce::Colour (0xff283240); p.panelMid = juce::Colour (0xff242e3b);
        p.panelLower = juce::Colour (0xff212a36); p.panelBottom = juce::Colour (0xff1b232e);
        p.sep = juce::Colour (0xff161d25); p.sepHi = juce::Colour (0x0affffff);
        p.graphBg = juce::Colour (0xff0e1318); p.graphGrid = juce::Colour (0x1a3fa9f5); p.graphLine = juce::Colour (0xff4fd0f5); p.graphInk = juce::Colour (0xff8ea2b4);
        p.strip = juce::Colour (0xff224e8c); p.card = juce::Colour (0xff172130); p.cardInk = juce::Colour (0xffe6eef6); p.cardLine = juce::Colour (0xff2a3a4e);
        p.pill = juce::Colour (0xff253649); p.pillInk = juce::Colour (0xffa9c0d8); p.lcd = juce::Colour (0xff091016); p.lcdInk = juce::Colours::white;
        p.track = juce::Colour (0xff3a4656);
        p.logoBg = juce::Colour (0xff2a3341);
        p.faceArc = juce::Colour (0xffe6eef6); p.faceLabel = juce::Colours::white; p.faceRed = juce::Colour (0xffff4b3e);
        p.needle = juce::Colours::white; p.faceFoot = juce::Colour (0xff214065);
        return p;
    }

    // Ancien theme sombre "graphite" (avant le design de Sunny), garde dans
    // le code, inutilise.
    static AlphaPalette makeDarkClassic()
    {
        AlphaPalette p;
        p.light = false;
        p.accent = juce::Colour (0xff3fa9f5); p.accentDeep = juce::Colour (0xff1b7fcf); p.accentGlow = juce::Colour (0x8c3fa9f5);
        p.ink = juce::Colour (0xffcfe0ee); p.inkSoft = juce::Colour (0xff7d91a2);
        p.panelTop = juce::Colour (0xff30343a); p.panelUpper = juce::Colour (0xff25282d); p.panelMid = juce::Colour (0xff1c1e22);
        p.panelLower = juce::Colour (0xff23262a); p.panelBottom = juce::Colour (0xff26292d);
        p.sep = juce::Colour (0x8c000000); p.sepHi = juce::Colour (0x0fffffff);
        p.graphBg = juce::Colour (0xff0e1318); p.graphGrid = juce::Colour (0x1a3fa9f5); p.graphLine = juce::Colour (0xff4fc2f2); p.graphInk = juce::Colour (0xff8ea2b4);
        p.strip = juce::Colour (0xff174066); p.card = juce::Colour (0xff1a222b); p.cardInk = juce::Colour (0xffcfe0ee); p.cardLine = juce::Colour (0xff2b3946);
        p.pill = juce::Colour (0xff29333d); p.pillInk = juce::Colour (0xffa7bccd); p.lcd = juce::Colour (0xff0b1015); p.lcdInk = juce::Colour (0xff7fd3ff);
        p.track = juce::Colour (0xff33414e);
        p.logoBg = juce::Colour (0xff0e1318);
        p.faceArc = juce::Colour (0xffd4e8f8); p.faceLabel = juce::Colour (0xffbfe2ff); p.faceRed = juce::Colour (0xffff5a45);
        p.needle = juce::Colours::white; p.faceFoot = juce::Colour (0x4d000000);
        return p;
    }

    // Couleurs pastel des 7 bandes (bandeau / teinte de carte / zones du
    // graphe), dans l'ordre LOW -> AIR.
    static juce::Colour bandPastel (int band) noexcept
    {
        static const juce::uint32 c[7] = { 0xfff6b8c4, 0xfff8cea6, 0xfff2e29a, 0xffbde6c5, 0xffd4c6f2, 0xffb6d7f4, 0xffb2e8e1 };
        return juce::Colour (c[juce::jlimit (0, 6, band)]);
    }

    static const char* bandName (int band) noexcept
    {
        static const char* names[7] = { "LOW", "LOW-MID", "MID", "HIGH-MID", "PRESENCE", "HIGH", "AIR" };
        return names[juce::jlimit (0, 6, band)];
    }

    // Fond de la carte d'une tranche selon le mode de couleur des bandes
    // (0 bandeau, 1 teinte, 2 aucune) -- partage par la tranche et par les
    // composants opaques poses dessus (diodes GR/EXP).
    juce::Colour cardFor (int band, int colourMode) const
    {
        return colourMode == 1 ? card.interpolatedWith (bandPastel (band), light ? 0.34f : 0.18f) : card;
    }

    // Fenetres secondaires (Options, About, Presets, Licence).
    juce::Colour veil() const          { return light ? juce::Colour (0x731b2a38) : juce::Colour (0xa6000000); }
    juce::Colour sheet() const         { return light ? juce::Colour (0xfff7f9fb) : juce::Colour (0xff1d252e); }
    juce::Colour sheetText() const     { return light ? juce::Colour (0xff1f2a35) : juce::Colour (0xffe3e9ee); }
    juce::Colour sheetTextSoft() const { return light ? juce::Colour (0xff5d6b78) : juce::Colour (0xff9aa8b5); }
    juce::Colour heading() const       { return light ? accentDeep : accent; }
};

namespace AlphaDraw
{
    inline juce::Font font (float size, bool bold = true)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Texte large espace (les libelles de la maquette : "L E A R N").
    inline void spacedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour,
                            float size, float kerning = 0.15f, juce::Justification just = juce::Justification::centred)
    {
        g.setFont (font (size).withExtraKerningFactor (kerning));
        g.setColour (colour);
        g.drawText (text, area, just, false);
    }

    // Afficheur "LCD" a chiffres (police monospace systeme).
    inline juce::Font lcdFont (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }

    // --- Fenetres secondaires : voile + panneau clair arrondi (audit :
    // "repeindre ces fenetres avec la palette Alpha") ---
    inline void overlayVeil (juce::Graphics& g, juce::Rectangle<float> area, float opacity = 1.0f)
    {
        const auto pal = AlphaPalette::get();
        g.setColour (pal.veil().withMultipliedAlpha (opacity));
        g.fillRect (area);
    }

    inline void overlaySheet (juce::Graphics& g, juce::Rectangle<float> sheet, float corner = 12.0f)
    {
        const auto pal = AlphaPalette::get();
        juce::Path p; p.addRoundedRectangle (sheet, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (pal.light ? 0.28f : 0.55f), 26, { 0, 8 }).drawForPath (g, p);
        g.setColour (pal.sheet());
        g.fillPath (p);
        g.saveState();
        g.reduceClipRegion (p);
        g.setColour (pal.accent);
        g.fillRect (sheet.withHeight (4.0f));
        g.restoreState();
        g.setColour (pal.cardLine);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // Recolore recursivement les controles JUCE standard d'une fenetre
    // secondaire (TextButton, ComboBox, Slider, TextEditor, Label,
    // ScrollBar) avec la palette Alpha -- a rappeler a chaque changement de
    // theme / ouverture.
    inline void styleControls (juce::Component& root)
    {
        const auto pal = AlphaPalette::get();
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (&c))
            {
                b->setColour (juce::TextButton::buttonColourId, pal.pill);
                b->setColour (juce::TextButton::buttonOnColourId, pal.accent);
                b->setColour (juce::TextButton::textColourOffId, pal.pillInk);
                b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                b->setColour (juce::ComboBox::outlineColourId, pal.cardLine);
            }
            else if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
            {
                cb->setColour (juce::ComboBox::backgroundColourId, pal.light ? juce::Colours::white : pal.pill);
                cb->setColour (juce::ComboBox::textColourId, pal.sheetText());
                cb->setColour (juce::ComboBox::arrowColourId, pal.accent);
                cb->setColour (juce::ComboBox::outlineColourId, pal.cardLine);
            }
            else if (auto* s = dynamic_cast<juce::Slider*> (&c))
            {
                s->setColour (juce::Slider::trackColourId, pal.accent);
                s->setColour (juce::Slider::backgroundColourId, pal.track);
                s->setColour (juce::Slider::thumbColourId, pal.light ? juce::Colours::white : pal.ink);
                s->setColour (juce::Slider::textBoxTextColourId, pal.sheetText());
                s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
                s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            }
            else if (auto* te = dynamic_cast<juce::TextEditor*> (&c))
            {
                te->setColour (juce::TextEditor::backgroundColourId, pal.light ? juce::Colours::white : pal.pill);
                te->setColour (juce::TextEditor::textColourId, pal.sheetText());
                te->setColour (juce::TextEditor::outlineColourId, pal.cardLine);
                te->setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
                te->setColour (juce::TextEditor::highlightColourId, pal.accent.withAlpha (0.3f));
                te->applyColourToAllText (pal.sheetText());
            }
            else if (auto* sb = dynamic_cast<juce::ScrollBar*> (&c))
            {
                sb->setColour (juce::ScrollBar::thumbColourId, pal.accent.withAlpha (0.55f));
                sb->setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
            }
            for (auto* child : c.getChildren())
                visit (*child);
        };
        visit (root);
    }

    // Couleurs des boites de dialogue JUCE (avertissements CPU, sauvegarde
    // de preset...) posees sur un LookAndFeel.
    inline void styleAlertWindows (juce::LookAndFeel& lf)
    {
        const auto pal = AlphaPalette::get();
        lf.setColour (juce::AlertWindow::backgroundColourId, pal.sheet());
        lf.setColour (juce::AlertWindow::textColourId, pal.sheetText());
        lf.setColour (juce::AlertWindow::outlineColourId, pal.accent);
        lf.setColour (juce::TextButton::buttonColourId, pal.pill);
        lf.setColour (juce::TextButton::buttonOnColourId, pal.accent);
        lf.setColour (juce::TextButton::textColourOffId, pal.pillInk);
        lf.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        lf.setColour (juce::TextEditor::backgroundColourId, pal.light ? juce::Colours::white : pal.pill);
        lf.setColour (juce::TextEditor::textColourId, pal.sheetText());
        lf.setColour (juce::TextEditor::outlineColourId, pal.cardLine);
        lf.setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
    }

    // Bloc bleu de la maquette. Deux etats bien distincts (retour
    // utilisateur : "les boutons dans le header n'ont pas de statut active
    // ... peut-etre un etat de push enfonce") :
    //  - eteint = EN RELIEF : ombre portee, reflet diagonal, liseret clair
    //    en haut ;
    //  - actif  = ENFONCE : plus d'ombre portee, aplat plus sombre, ombre
    //    INTERIEURE en haut, liseret clair en BAS, reflet tres attenue.
    // ---------- Finitions plastique (MATTE / SATIN / GLOSS / MOLDED) ----------
    // Reflet pose PAR-DESSUS une piece deja peinte (bouton, touche,
    // bandeau, pastille...) ; 'strength' l'attenue selon la piece (touches
    // sombres, boutons enfonces). Rien en MATTE.
    inline void finishShine (juce::Graphics& g, juce::Rectangle<float> r, float corner, int finish, float strength = 1.0f)
    {
        if (finish <= 0 || r.isEmpty())
            return;
        const auto w = [strength] (float a) { return juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, a * strength)); };
        juce::Path shape; shape.addRoundedRectangle (r, corner);
        g.saveState();
        g.reduceClipRegion (shape);
        if (finish == 1) // SATIN : reflet doux sur la moitie haute
        {
            juce::ColourGradient gr (w (0.45f), 0.0f, r.getY(), w (0.0f), 0.0f, r.getY() + r.getHeight() * 0.62f, false);
            gr.addColour (0.88, w (0.08f));
            g.setGradientFill (gr);
            g.fillRect (r);
        }
        else if (finish == 2) // GLOSS : reflet net coupe a mi-hauteur + lueur basse
        {
            const float mid = r.getY() + r.getHeight() * 0.5f;
            g.setGradientFill (juce::ColourGradient (w (0.75f), 0.0f, r.getY(), w (0.3f), 0.0f, mid, false));
            g.fillRect (r.withBottom (mid));
            g.setGradientFill (juce::ColourGradient (w (0.0f), 0.0f, r.getY() + r.getHeight() * 0.6f, w (0.3f), 0.0f, r.getBottom(), false));
            g.fillRect (r.withTop (mid));
        }
        else // MOLDED : reflet arrondi en lentille + ombre basse interieure
        {
            const auto lens = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.08f, r.getY() - r.getHeight() * 0.35f,
                                                      r.getWidth() * 0.84f, r.getHeight() * 0.85f);
            g.setGradientFill (juce::ColourGradient (w (0.65f), 0.0f, r.getY(), w (0.0f), 0.0f, lens.getBottom(), false));
            g.fillEllipse (lens);
            const float sh = juce::jmin (6.0f, r.getHeight() * 0.35f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, r.getBottom() - sh,
                                                     juce::Colours::black.withAlpha (0.10f * strength), 0.0f, r.getBottom(), false));
            g.fillRect (r);
        }
        g.restoreState();
        // Lisere lumineux sur l'arete du haut.
        g.setColour (w (finish == 2 ? 0.85f : 0.6f));
        g.drawLine (r.getX() + corner, r.getY() + 0.75f, r.getRight() - corner, r.getY() + 0.75f, 1.0f);
    }

    // Ombre portee plus profonde sous chaque piece (MOLDED uniquement).
    inline void finishDropShadow (juce::Graphics& g, juce::Rectangle<float> r, float corner, int finish)
    {
        if (finish != 3)
            return;
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.5f), corner);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);
    }

    // Plaque bleue d'une tranche (aussi repeinte, decalee, derriere le
    // cadran a aiguille qui est opaque -- voir AlphaThdNeedle).
    inline void paintPlate (juce::Graphics& g, juce::Rectangle<float> plate, const AlphaPalette& pal)
    {
        constexpr float corner = 10.0f;
        if (pal.sunny)
        {
            // Design de Sunny : anneau bleu, fin filet sombre, plaque bleue.
            g.setColour (juce::Colour (0xff1d4f85));
            g.fillRoundedRectangle (plate, corner);
            g.setColour (juce::Colour (0xff0b1a2c));
            g.drawRoundedRectangle (plate.reduced (3.0f), corner - 2.5f, 1.0f);
            g.setColour (pal.strip);
            g.fillRoundedRectangle (plate.reduced (3.5f), corner - 3.0f);
            return;
        }
        if (pal.finish <= 0)
        {
            g.setColour (pal.strip);
            g.fillRoundedRectangle (plate, corner);
            return;
        }
        juce::ColourGradient base (pal.strip.brighter (0.12f), 0.0f, plate.getY(), pal.strip.darker (0.12f), 0.0f, plate.getBottom(), false);
        base.addColour (0.45, pal.strip);
        g.setGradientFill (base);
        g.fillRoundedRectangle (plate, corner);

        g.saveState();
        juce::Path p; p.addRoundedRectangle (plate, corner);
        g.reduceClipRegion (p);
        const float sheenH = plate.getHeight() * (pal.finish == 2 ? 0.09f : 0.14f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (pal.finish == 2 ? 0.5f : 0.3f), 0.0f, plate.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, plate.getY() + sheenH, false));
        g.fillRect (plate.withHeight (sheenH));
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, plate.getBottom() - 6.0f,
                                                 juce::Colour ((juce::uint8) 0, (juce::uint8) 30, (juce::uint8) 80, 0.25f), 0.0f, plate.getBottom(), false));
        g.fillRect (plate.withTop (plate.getBottom() - 6.0f));
        g.restoreState();

        g.setColour (juce::Colours::white.withAlpha (pal.finish == 2 ? 0.85f : 0.6f));
        g.drawLine (plate.getX() + corner, plate.getY() + 0.8f, plate.getRight() - corner, plate.getY() + 0.8f, 1.0f);
    }

    // Carte claire d'une tranche (aussi repeinte, decalee, derriere les
    // diodes GR/EXP qui sont opaques -- voir AlphaGrExpMeter).
    inline void paintCard (juce::Graphics& g, juce::Rectangle<float> card, juce::Colour base, const AlphaPalette& pal)
    {
        constexpr float corner = 8.0f;
        if (pal.sunny)
        {
            g.setColour (base);
            g.fillRoundedRectangle (card, corner);
            g.setColour (juce::Colour (0xff1b3a62));
            g.drawRoundedRectangle (card.reduced (0.5f), corner, 1.0f);
            return;
        }
        if (pal.finish <= 0)
        {
            g.setColour (base);
            g.fillRoundedRectangle (card, corner);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.10f), 0.0f, card.getY(),
                                                     juce::Colours::transparentBlack, 0.0f, card.getY() + 6.0f, false));
            g.fillRoundedRectangle (card, corner);
            return;
        }
        g.setGradientFill (juce::ColourGradient (base.brighter (0.04f), 0.0f, card.getY(), base.darker (0.05f), 0.0f, card.getBottom(), false));
        g.fillRoundedRectangle (card, corner);

        g.saveState();
        juce::Path p; p.addRoundedRectangle (card, corner);
        g.reduceClipRegion (p);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.07f), 0.0f, card.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, card.getY() + 6.0f, false));
        g.fillRect (card.withHeight (6.0f));
        if (pal.finish == 1)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f), 0.0f, card.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), 0.0f, card.getY() + card.getHeight() * 0.16f, false));
            g.fillRect (card.withHeight (card.getHeight() * 0.16f));
        }
        else if (pal.finish == 2)
        {
            const float cut = card.getY() + card.getHeight() * 0.095f;
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.7f), 0.0f, card.getY(),
                                                     juce::Colours::white.withAlpha (0.25f), 0.0f, cut, false));
            g.fillRect (card.withBottom (cut));
        }
        else
        {
            const auto lens = juce::Rectangle<float> (card.getX() + card.getWidth() * 0.05f, card.getY() - card.getHeight() * 0.08f,
                                                      card.getWidth() * 0.9f, card.getHeight() * 0.22f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.7f), 0.0f, card.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), 0.0f, lens.getBottom(), false));
            g.fillEllipse (lens);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, card.getBottom() - 8.0f,
                                                 juce::Colour ((juce::uint8) 0, (juce::uint8) 40, (juce::uint8) 70, pal.finish == 3 ? 0.12f : 0.07f),
                                                 0.0f, card.getBottom(), false));
        g.fillRect (card.withTop (card.getBottom() - 8.0f));
        g.restoreState();

        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.drawLine (card.getX() + corner, card.getY() + 0.75f, card.getRight() - corner, card.getY() + 0.75f, 1.0f);
    }

    inline void blueBlock (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool lit, bool down, juce::Colour base)
    {
        juce::Path shape; shape.addRoundedRectangle (r, corner);

        if (lit)
        {
            g.setColour (base.darker (0.28f));
            g.fillPath (shape);

            g.saveState();
            g.reduceClipRegion (shape);
            juce::ColourGradient top (juce::Colours::black.withAlpha (0.42f), 0.0f, r.getY(),
                                      juce::Colours::transparentBlack, 0.0f, r.getY() + juce::jmin (10.0f, r.getHeight() * 0.45f), false);
            g.setGradientFill (top);
            g.fillRect (r);
            juce::ColourGradient left (juce::Colours::black.withAlpha (0.22f), r.getX(), 0.0f,
                                       juce::Colours::transparentBlack, r.getX() + 6.0f, 0.0f, false);
            g.setGradientFill (left);
            g.fillRect (r);
            g.restoreState();

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.drawLine (r.getX() + corner, r.getBottom() - 0.6f, r.getRight() - corner, r.getBottom() - 0.6f, 1.0f);
            return;
        }

        g.setColour (juce::Colours::black.withAlpha (0.26f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);

        g.setColour (down ? base.darker (0.15f) : base);
        g.fillPath (shape);

        g.saveState();
        g.reduceClipRegion (shape);
        juce::Path sheen;
        sheen.addTriangle (r.getX(), r.getY(), r.getX() + r.getWidth() * 0.62f, r.getY(), r.getX(), r.getY() + r.getHeight() * 0.85f);
        g.setColour (juce::Colours::white.withAlpha (0.24f));
        g.fillPath (sheen);
        g.restoreState();

        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.drawLine (r.getX() + corner, r.getY() + 0.6f, r.getRight() - corner, r.getY() + 0.6f, 1.0f);
    }

    // ---------- Boutons du design de Sunny (theme sombre) ----------
    // Bright  : degrade bleu vif + liseret clair (LEARN, ANALOG actif, SEL...) ;
    // Pressed : bleu plus profond, a plat (THD engage) ;
    // Dark    : ardoise sombre a fin contour (boutons inactifs, SOLO) ;
    // Amber   : SOLO actif.
    enum class SunnyBlock { Bright, Pressed, Dark, Amber };

    inline void sunnyBlock (juce::Graphics& g, juce::Rectangle<float> r, float corner, SunnyBlock kind, bool over, bool down,
                            float borderWidth = 1.0f)
    {
        juce::Colour top, bottom, border;
        switch (kind)
        {
            case SunnyBlock::Bright:  top = juce::Colour (0xff4ab0f7); bottom = juce::Colour (0xff3a9ce8); border = juce::Colour (0xff8fd0ff); break;
            case SunnyBlock::Pressed: top = juce::Colour (0xff2f82cc); bottom = juce::Colour (0xff2a78bf); border = juce::Colour (0xff4f98d8); break;
            case SunnyBlock::Amber:   top = juce::Colour (0xfff7c75a); bottom = juce::Colour (0xffe9b240); border = juce::Colour (0xffffe199); break;
            case SunnyBlock::Dark:
            default:                  top = juce::Colour (0xff253649); bottom = juce::Colour (0xff223244); border = juce::Colour (0xff31465d); break;
        }
        if (over && ! down) { top = top.brighter (0.08f); bottom = bottom.brighter (0.08f); }
        if (down)           { top = top.darker (0.12f);   bottom = bottom.darker (0.12f); }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (border.withAlpha (kind == SunnyBlock::Dark ? 1.0f : 0.9f));
        g.drawRoundedRectangle (r.reduced (borderWidth * 0.5f), corner, borderWidth);
    }
}

/**
    AlphaButton -- bouton bleu de la maquette (LEARN, ANALOG, THD, SOFT...,
    SOLO/SEL des tranches). Toggle ou momentane selon setClickingTogglesState().
    `invSuffix` : petite etiquette inversee (blanc sur bleu) apres le texte,
    comme LINK [IN] / LINK [OUT] sur la maquette.

    Styles (audit "tous les boutons ont le meme poids visuel") :
      - Standard : bloc bleu en relief, ENFONCE quand actif ;
      - Primary  : idem + lisere lumineux permanent (LEARN, action
        principale), et pulsation (halo qui s'elargit + texte qui clignote,
        repris de la preview HTML Sevenpack) quand setPulsing(true) ;
      - Mode     : version DISCRETE quand inactif (fond clair, contour et
        texte bleus), bloc bleu enfonce quand actif (ANALOG/LINEAR,
        SOFT/TAPE/IRON/AIR) ;
      - Ghost    : bouton translucide pose sur une tranche bleue (SOLO/SEL),
        texte bleu nuit sur fond blanc pour rester lisible.
*/
class AlphaButton : public juce::Button, private juce::Timer
{
public:
    enum class Style { Standard, Primary, Mode, Ghost };

    explicit AlphaButton (const juce::String& text, const juce::String& invSuffix = {})
        : juce::Button (text), suffix (invSuffix) {}

    ~AlphaButton() override { stopTimer(); }

    void setStyle (Style s) { style = s; repaint(); }
    void setFontSize (float s) { fontSize = s; repaint(); }
    void setDimmed (bool d) { if (dimmed != d) { dimmed = d; repaint(); } }
    void setForceLit (bool l) { if (forceLit != l) { forceLit = l; repaint(); } }
    // Couleur de fond quand actif (SOLO = ambre) -- par defaut l'accent.
    void setOnColour (juce::Colour c) { onColour = c; hasOnColour = true; }
    // Retro-compatibilite : setGhost(true) == setStyle(Style::Ghost).
    void setGhost (bool g) { if (g) style = Style::Ghost; }
    // Marge reservee autour du bloc pour le halo (Primary) -- les bounds du
    // composant doivent deja l'inclure (meme principe que Sixpack).
    void setHaloMargin (float m) { haloMargin = m; repaint(); }

    void setPulsing (bool p)
    {
        if (p == pulsing)
            return;
        pulsing = p;
        pulseStartMs = juce::Time::getMillisecondCounterHiRes();
        if (pulsing) startTimerHz (40); else stopTimer();
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto pal = AlphaPalette::get();
        const bool lit = getToggleState() || forceLit;
        auto r = getLocalBounds().toFloat().reduced (2.0f + haloMargin, 2.5f + haloMargin);
        const float corner = juce::jmin (6.0f, r.getHeight() * 0.25f);

        if (dimmed)
            g.beginTransparencyLayer (0.45f);

        // --- Halos (Primary) : lisere permanent + pulsation pendant LEARN ---
        if (style == Style::Primary)
        {
            if (pulsing)
            {
                // 0 -> 7 px -> 0 en 0.7 s (box-shadow de la preview HTML).
                const double t = (juce::Time::getMillisecondCounterHiRes() - pulseStartMs) / 1000.0;
                const float k = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * t / 0.7);
                const float spread = 7.0f * k;
                g.setColour (juce::Colour (0xff5fd0ff).withAlpha (0.6f * k));
                g.fillRoundedRectangle (r.expanded (spread), corner + spread);
            }
            for (int i = 3; i >= 1 && ! pal.sunny; --i)
            {
                g.setColour (juce::Colour (0xff7cc6ff).withAlpha (0.10f * (float) (4 - i)));
                g.drawRoundedRectangle (r.expanded ((float) i * 1.4f), corner + (float) i, 1.4f);
            }
        }

        // --- Fond ---
        juce::Colour textCol;
        if (pal.sunny)
        {
            // Design de Sunny : bleu vif quand actif (et pour les boutons
            // d'action au repos), ardoise sombre quand inactif ; THD engage =
            // bleu plus profond ; SOLO actif = ambre.
            using SB = AlphaDraw::SunnyBlock;
            SB kind = SB::Dark;
            if (style == Style::Standard)      kind = lit ? (hasOnColour ? SB::Amber : SB::Pressed) : SB::Bright;
            else if (style == Style::Primary)  kind = SB::Bright;
            else                               kind = lit ? (hasOnColour ? SB::Amber : SB::Bright) : SB::Dark;
            AlphaDraw::sunnyBlock (g, r, juce::jmin (style == Style::Primary ? 8.0f : 6.0f, r.getHeight() * 0.25f), kind, over, down,
                                   style == Style::Primary ? 2.0f : 1.0f);
            textCol = kind == SB::Dark ? juce::Colour (0xffa9c0d8) : (kind == SB::Amber ? juce::Colour (0xff3b2a00) : juce::Colours::white);
        }
        else if (style == Style::Ghost)
        {
            juce::Colour base = pal.light ? juce::Colours::white.withAlpha (over ? 0.88f : 0.78f)
                                          : juce::Colours::white.withAlpha (over ? 0.16f : 0.10f);
            if (lit) base = hasOnColour ? onColour : juce::Colours::white;
            AlphaDraw::finishDropShadow (g, r, corner, pal.finish);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
            g.setColour (down ? base.darker (0.08f) : base);
            g.fillRoundedRectangle (r, corner);
            AlphaDraw::finishShine (g, r, corner, pal.finish);
            if (lit)
                textCol = hasOnColour ? juce::Colour (0xff3b2a00) : pal.accentDeep;
            else
                textCol = pal.light ? juce::Colour (0xff0d3a63) : juce::Colours::white.withAlpha (0.92f);
        }
        else if (style == Style::Mode && ! lit)
        {
            g.setColour (pal.light ? juce::Colours::white.withAlpha (over ? 0.75f : 0.55f)
                                   : juce::Colours::white.withAlpha (over ? 0.10f : 0.05f));
            g.fillRoundedRectangle (r, corner);
            AlphaDraw::finishShine (g, r, corner, pal.finish, 0.9f);
            g.setColour (pal.accent.withAlpha (0.65f));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
            textCol = pal.light ? pal.accentDeep : pal.accent;
        }
        else
        {
            juce::Colour base = pal.accent;
            if (lit && hasOnColour) base = onColour;
            if (over && ! lit) base = base.brighter (0.06f);
            if (! lit)
                AlphaDraw::finishDropShadow (g, r, corner, pal.finish);
            AlphaDraw::blueBlock (g, r, corner, lit, down, base);
            AlphaDraw::finishShine (g, r, corner, pal.finish, lit ? 0.35f : 0.8f);
            textCol = lit ? juce::Colours::white : juce::Colours::white.withAlpha (0.92f);
        }

        // Texte clignotant pendant la pulsation (0.35 s, comme la preview).
        if (pulsing)
        {
            const double t = (juce::Time::getMillisecondCounterHiRes() - pulseStartMs) / 1000.0;
            if (((int) (t / 0.35)) % 2 == 1)
                textCol = textCol.withMultipliedAlpha (0.25f);
        }

        // --- Texte (+ etiquette inversee) ---
        const bool pressedIn = lit && style != Style::Ghost && ! pal.sunny;
        auto textArea = pressedIn ? r.translated (0.0f, 1.0f) : r;
        auto f = AlphaDraw::font (fontSize).withExtraKerningFactor (AlphaType::kerning);
        float mainW = juce::GlyphArrangement::getStringWidth (f, getButtonText());
        if (suffix.isEmpty() && mainW > textArea.getWidth() - 6.0f)
        {
            // Libelle resserre s'il deborde (GONIO dans un bouton de 44 px).
            f = AlphaDraw::font (fontSize).withExtraKerningFactor (0.04f);
            mainW = juce::GlyphArrangement::getStringWidth (f, getButtonText());
        }
        g.setFont (f);
        float suffixW = 0.0f;
        if (suffix.isNotEmpty())
            suffixW = juce::GlyphArrangement::getStringWidth (f, suffix) + 6.0f;
        const float totalW = mainW + (suffix.isNotEmpty() ? suffixW + 3.0f : 0.0f);
        float x = textArea.getCentreX() - totalW * 0.5f;

        g.setColour (textCol);
        g.drawText (getButtonText(), juce::Rectangle<float> (x, textArea.getY(), mainW + 2.0f, textArea.getHeight()),
                    juce::Justification::centredLeft, false);

        if (suffix.isNotEmpty())
        {
            x += mainW + 3.0f;
            auto tag = juce::Rectangle<float> (x, textArea.getCentreY() - fontSize * 0.75f, suffixW, fontSize * 1.5f);
            g.setColour (juce::Colours::white.withAlpha (lit ? 1.0f : 0.85f));
            g.fillRoundedRectangle (tag, 1.5f);
            g.setColour (lit ? pal.accentDeep : pal.accent);
            g.drawText (suffix, tag.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
        }

        if (dimmed)
            g.endTransparencyLayer();
    }

private:
    void timerCallback() override { repaint(); }

    juce::String suffix;
    Style style = Style::Standard;
    float fontSize = AlphaType::label;
    float haloMargin = 0.0f;
    bool dimmed = false, forceLit = false, hasOnColour = false, pulsing = false;
    double pulseStartMs = 0.0;
    juce::Colour onColour;
};

/**
    AlphaPillToggle -- interrupteur a pilule ANIME (POWER, A/B, theme),
    repris de la preview "Apercu bouton VST anime" validee par l'utilisateur :
      - la pastille glisse avec un leger depassement (ease-out back, 380 ms) ;
      - le fond change de couleur en fondu (350 ms) ;
      - POWER : l'icone apparait en tournant (-90 deg) et en grossissant
        (x0.4 -> x1) quand on allume, disparait quand on eteint ;
      - THEME : le soleil (jour) sort vers la droite en tournant pendant que
        la lune (nuit) entre par la gauche, et inversement ;
      - A/B : la lettre change avec le meme mouvement que l'icone POWER ;
      - la pastille s'elargit tant que le clic est enfonce (style iOS).
    Toujours toggle (setClickingTogglesState(true)). Timer a 60 Hz actif
    uniquement pendant une animation.
*/
class AlphaPillToggle : public juce::Button, private juce::Timer
{
public:
    enum class Icon { Power, Letter, Moon, Contrast };

    explicit AlphaPillToggle (Icon i) : juce::Button ("pill"), icon (i)
    {
        setClickingTogglesState (true);
    }

    ~AlphaPillToggle() override { stopTimer(); }

    void setLetter (const juce::String& l)
    {
        if (l != letter) { letter = l; repaint(); }
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        syncAnimationTargets (down);

        const auto pal = AlphaPalette::get();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), pal.sunny ? 26.0f : 24.0f));
        const float rad = r.getHeight() * 0.5f;
        const float k = r.getHeight() / 88.0f; // echelle de la preview d'origine (88 px de haut)

        // Fond : off -> on en fondu. THEME : jour = accent, nuit = anthracite.
        juce::Colour offCol = pal.pill, onCol = pal.accent;
        if (icon == Icon::Moon) { offCol = pal.accent; onCol = pal.sunny ? juce::Colour (0xff3da0ec) : juce::Colour (0xff3a3a46); }
        if (pal.sunny && icon != Icon::Moon) { onCol = juce::Colour (0xff3da0ec); if (icon != Icon::Power) offCol = juce::Colour (0xff212c38); }
        if (icon == Icon::Power) offCol = pal.light ? juce::Colour (0xff8c8f94) : juce::Colour (0xff4a4f56);
        const auto bg = offCol.interpolatedWith (onCol, colourMix);

        if (colourMix > 0.01f && icon != Icon::Moon && ! pal.sunny)
        {
            juce::Path p; p.addRoundedRectangle (r, rad);
            juce::DropShadow (pal.accentGlow.withMultipliedAlpha (colourMix), 6, {}).drawForPath (g, p);
        }
        g.setColour (bg);
        g.fillRoundedRectangle (r, rad);
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.22f), r.getX(), r.getY(),
                                    juce::Colours::transparentBlack, r.getX(), r.getY() + r.getHeight() * 0.45f, false);
        g.setGradientFill (inner);
        g.fillRoundedRectangle (r, rad);
        AlphaDraw::finishShine (g, r, rad, pal.finish, 0.8f);

        // Icones (sous la pastille, dans la moitie libre).
        const float iconW = r.getHeight() * (112.0f / 88.0f);
        const auto leftZone  = juce::Rectangle<float> (r.getX(), r.getY(), iconW, r.getHeight());
        const auto rightZone = juce::Rectangle<float> (r.getRight() - iconW, r.getY(), iconW, r.getHeight());
        const float iconSize = r.getHeight() * 0.55f;

        if (icon == Icon::Moon)
        {
            // Nuit (on) : lune a gauche, entre depuis -30 px / -120 deg.
            drawAnimatedGlyph (g, leftZone.getCentre(), iconSize, iconProgress, -30.0f * k, -120.0f, 1.0f,
                               [this, bg] (juce::Graphics& gg, juce::Rectangle<float> a) { drawMoon (gg, a, bg); });
            // Jour (off) : soleil a droite, sort vers +30 px / +120 deg.
            drawAnimatedGlyph (g, rightZone.getCentre(), iconSize, 1.0f - iconProgress, 30.0f * k, 120.0f, 1.0f,
                               [] (juce::Graphics& gg, juce::Rectangle<float> a) { drawSun (gg, a); });
        }
        else if (icon == Icon::Power)
        {
            drawAnimatedGlyph (g, leftZone.getCentre(), iconSize, iconProgress, 0.0f, -90.0f, 0.4f,
                               [] (juce::Graphics& gg, juce::Rectangle<float> a) { drawPower (gg, a); });
        }
        else if (icon == Icon::Contrast)
        {
            // Contraste : demi-disque plein, du cote libre (comme la lettre A/B).
            const bool on = getToggleState();
            const float popT = juce::jlimit (0.0f, 1.0f, on ? iconProgress : 1.0f - iconProgress);
            drawAnimatedGlyph (g, (on ? leftZone : rightZone).getCentre(), iconSize, popT, 0.0f, on ? -90.0f : 90.0f, 0.4f,
                               [on, pal] (juce::Graphics& gg, juce::Rectangle<float> a)
                               {
                                   const auto c = on ? juce::Colours::white : pal.pillInk;
                                   auto disc = a.reduced (a.getWidth() * 0.06f);
                                   juce::Path half;
                                   half.addPieSegment (disc, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, 0.0f);
                                   gg.setColour (c);
                                   gg.fillPath (half);
                                   gg.drawEllipse (disc, juce::jmax (1.2f, a.getWidth() * 0.1f));
                               });
        }
        else
        {
            // A/B : la lettre se place du cote libre et "pop" a chaque bascule.
            const bool on = getToggleState();
            const float popT = juce::jlimit (0.0f, 1.0f, on ? iconProgress : 1.0f - iconProgress);
            drawAnimatedGlyph (g, (on ? leftZone : rightZone).getCentre(), iconSize, popT, 0.0f, on ? -90.0f : 90.0f, 0.4f,
                               [this, on, pal] (juce::Graphics& gg, juce::Rectangle<float> a)
                               {
                                   gg.setColour (on ? juce::Colours::white : pal.pillInk);
                                   gg.setFont (AlphaDraw::font (a.getHeight() * 0.95f));
                                   gg.drawText (letter, a.expanded (4.0f), juce::Justification::centred, false);
                               });
        }

        // Pastille (s'elargit pendant l'appui).
        const float d = r.getHeight() - 2.0f;
        const float knobW = d * (1.0f + 0.19f * pressT);
        const float travel = r.getWidth() - 2.0f - knobW;
        auto knob = juce::Rectangle<float> (r.getX() + 1.0f + knobPos * travel, r.getY() + 1.0f, knobW, d);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (knob.translated (0.0f, 1.2f), d * 0.5f);
        juce::ColourGradient kg (juce::Colours::white, knob.getX() + d * 0.35f, knob.getY() + d * 0.3f,
                                 pal.sunny ? juce::Colour (0xffe4e8ec) : juce::Colour (0xffb9c0c6), knob.getRight(), knob.getBottom(), true);
        g.setGradientFill (kg);
        g.fillRoundedRectangle (knob, d * 0.5f);
    }

private:
    // --- Animation ---------------------------------------------------------

    struct Tween
    {
        float from = 0.0f, to = 0.0f;
        double startMs = 0.0, durMs = 1.0;
        float valueAt (double now, bool backEase) const noexcept
        {
            const float t = (float) juce::jlimit (0.0, 1.0, (now - startMs) / durMs);
            float e;
            if (backEase)
            {
                // ~ cubic-bezier(.5, 1.6, .4, 1) de la preview : depasse ~10 % puis revient.
                constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
                e = 1.0f + c3 * std::pow (t - 1.0f, 3.0f) + c1 * std::pow (t - 1.0f, 2.0f);
            }
            else
            {
                e = t * t * (3.0f - 2.0f * t);
            }
            return from + (to - from) * e;
        }
        bool done (double now) const noexcept { return now - startMs >= durMs; }
    };

    void syncAnimationTargets (bool down)
    {
        const bool on = getToggleState();
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! initialised)
        {
            initialised = true;
            animatedOn = on;
            knobPos = colourMix = iconProgress = on ? 1.0f : 0.0f;
            knobTween = colourTween = iconTween = { knobPos, knobPos, now, 1.0 };
        }
        if (on != animatedOn)
        {
            animatedOn = on;
            const float target = on ? 1.0f : 0.0f;
            knobTween   = { knobPos,      target, now, 380.0 };
            colourTween = { colourMix,    target, now, 350.0 };
            iconTween   = { iconProgress, target, now, 400.0 };
            startTimerHz (60);
        }
        if (down != pressedTarget)
        {
            pressedTarget = down;
            startTimerHz (60);
        }
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        knobPos      = knobTween.valueAt (now, true);
        colourMix    = juce::jlimit (0.0f, 1.0f, colourTween.valueAt (now, false));
        iconProgress = iconTween.valueAt (now, true);
        pressT += ((pressedTarget ? 1.0f : 0.0f) - pressT) * 0.35f; // ~180 ms

        const bool pressSettled = std::abs (pressT - (pressedTarget ? 1.0f : 0.0f)) < 0.01f;
        if (knobTween.done (now) && colourTween.done (now) && iconTween.done (now) && pressSettled)
        {
            pressT = pressedTarget ? 1.0f : 0.0f;
            stopTimer();
        }
        repaint();
    }

    // Dessine un glyphe avec la transformation de la preview : opacite,
    // echelle (minScale -> 1), rotation (rotDeg -> 0) et decalage
    // (dx -> 0) suivant `progress` (0 = cache, 1 = en place ; depasse 1
    // legerement pendant le rebond).
    template <typename DrawFn>
    static void drawAnimatedGlyph (juce::Graphics& g, juce::Point<float> centre, float size, float progress,
                                   float dx, float rotDeg, float minScale, DrawFn&& draw)
    {
        const float opacity = juce::jlimit (0.0f, 1.0f, progress);
        if (opacity <= 0.01f)
            return;
        const float inv = 1.0f - progress;
        const float scale = minScale + (1.0f - minScale) * juce::jlimit (0.0f, 1.2f, progress);
        g.saveState();
        g.setOpacity (opacity);
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (rotDeg * inv), centre.x, centre.y)
                            .scaled (scale, scale, centre.x, centre.y)
                            .translated (dx * inv, 0.0f));
        draw (g, juce::Rectangle<float> (size, size).withCentre (centre));
        g.restoreState();
    }

    static void drawPower (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (juce::Colours::white);
        juce::Path arc;
        arc.addCentredArc (a.getCentreX(), a.getCentreY() + a.getHeight() * 0.04f, a.getWidth() * 0.4f, a.getWidth() * 0.4f, 0.0f,
                           juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi * 0.22f, true);
        const float sw = juce::jmax (1.5f, a.getWidth() * 0.14f);
        g.strokePath (arc, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (a.getCentreX(), a.getY() + a.getHeight() * 0.02f, a.getCentreX(), a.getCentreY(), sw);
    }

    static void drawSun (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (juce::Colours::white);
        const auto c = a.getCentre();
        const float r = a.getWidth() * 0.22f;
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        const float sw = juce::jmax (1.2f, a.getWidth() * 0.09f);
        for (int i = 0; i < 8; ++i)
        {
            const float ang = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const float r0 = r * 1.5f, r1 = a.getWidth() * 0.5f;
            g.drawLine (c.x + std::sin (ang) * r0, c.y - std::cos (ang) * r0, c.x + std::sin (ang) * r1, c.y - std::cos (ang) * r1, sw);
        }
    }

    // Croissant : disque blanc puis "effacement" avec la couleur de fond
    // courante (fond uni sous l'icone).
    static void drawMoon (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour bg)
    {
        auto disc = a.reduced (a.getWidth() * 0.08f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (disc);
        g.setColour (bg);
        g.fillEllipse (disc.translated (disc.getWidth() * 0.34f, -disc.getHeight() * 0.22f));
    }

    Icon icon;
    juce::String letter { "A" };

    bool initialised = false, animatedOn = false, pressedTarget = false;
    float knobPos = 0.0f, colourMix = 0.0f, iconProgress = 0.0f, pressT = 0.0f;
    Tween knobTween, colourTween, iconTween;
};

/** Trois points verticaux (menu d'options). */
class AlphaKebab : public juce::Button
{
public:
    AlphaKebab() : juce::Button ("Options") {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto pal = AlphaPalette::get();
        const auto c = getLocalBounds().toFloat().getCentre();
        for (int i = -1; i <= 1; ++i)
        {
            auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.x, c.y + (float) i * 10.0f });
            if (pal.sunny) // design de Sunny : pastilles sombres cerclees de bleu
            {
                g.setColour (juce::Colour (0xff102a3d));
                g.fillEllipse (dot);
                g.setColour ((over ? pal.accent : juce::Colour (0xff2f6f9f)));
                g.drawEllipse (dot, 1.3f);
                continue;
            }
            juce::Path p; p.addEllipse (dot);
            juce::DropShadow (pal.accentGlow.withAlpha (over ? 0.8f : 0.5f), 4, {}).drawForPath (g, p);
            g.setColour (pal.accent);
            g.fillEllipse (dot);
        }
    }
};

/** Boutons icone de la barre du bas (presets, copier/coller...). */
class AlphaIconButton : public juce::Button
{
public:
    enum class Icon { Prev, Next, Copy, Paste, Save, Trash, Up, Down, Undo, Redo };

    explicit AlphaIconButton (Icon i, const juce::String& tip = {}) : juce::Button (tip), icon (i)
    {
        setTooltip (tip);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto pal = AlphaPalette::get();
        if (pal.sunny && (icon == Icon::Copy || icon == Icon::Paste))
        {
            // Design de Sunny : COPY / PASTE en lettres espacees dans un cadre.
            const auto box = getLocalBounds().toFloat().reduced (1.0f, 5.0f);
            const auto ink = ((over || down) ? pal.accent : pal.ink).withAlpha (isEnabled() ? 1.0f : 0.3f);
            g.setColour (ink);
            g.drawRoundedRectangle (box, 3.0f, 1.3f);
            AlphaDraw::spacedText (g, icon == Icon::Copy ? "COPY" : "PASTE", box, ink, AlphaType::label, 0.3f);
            return;
        }
        auto a = getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f);
        juce::Colour c = (icon == Icon::Up || icon == Icon::Down) ? (pal.sunny ? juce::Colour (0xffc9d6e3) : pal.accent) : pal.ink;
        if (pal.sunny && (icon == Icon::Prev || icon == Icon::Next)) c = juce::Colour (0xffa9c0d8);
        if (over || down) c = pal.accent;
        if (! isEnabled()) c = c.withAlpha (0.3f);
        g.setColour (c);

        const juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path p;
        switch (icon)
        {
            case Icon::Prev: p.addTriangle (a.getRight() - 4.0f, a.getY() + 2.0f, a.getX() + 3.0f, a.getCentreY(), a.getRight() - 4.0f, a.getBottom() - 2.0f); g.fillPath (p); break;
            case Icon::Next: p.addTriangle (a.getX() + 4.0f, a.getY() + 2.0f, a.getRight() - 3.0f, a.getCentreY(), a.getX() + 4.0f, a.getBottom() - 2.0f); g.fillPath (p); break;
            case Icon::Up:   p.addTriangle (a.getCentreX(), a.getY() + 3.0f, a.getRight() - 1.0f, a.getBottom() - 3.0f, a.getX() + 1.0f, a.getBottom() - 3.0f); g.fillPath (p); break;
            case Icon::Down: p.addTriangle (a.getX() + 1.0f, a.getY() + 3.0f, a.getRight() - 1.0f, a.getY() + 3.0f, a.getCentreX(), a.getBottom() - 3.0f); g.fillPath (p); break;
            case Icon::Undo:
            case Icon::Redo:
            {
                // Fleche courbe (annuler = vers la gauche, retablir = miroir).
                const bool undo = icon == Icon::Undo;
                const float sx = undo ? 1.0f : -1.0f;
                const float cx = a.getCentreX();
                auto px = [cx, sx] (float x) { return cx + (x - cx) * sx; };
                p.startNewSubPath (px (a.getX() + 5.5f), a.getY() + 2.5f);
                p.lineTo (px (a.getX() + 2.0f), a.getY() + 6.0f);
                p.lineTo (px (a.getX() + 5.5f), a.getY() + 9.5f);
                p.startNewSubPath (px (a.getX() + 2.5f), a.getY() + 6.0f);
                p.lineTo (px (a.getX() + 10.0f), a.getY() + 6.0f);
                p.quadraticTo (px (a.getRight() - 1.0f), a.getY() + 6.0f, px (a.getRight() - 1.0f), a.getY() + 10.0f);
                p.quadraticTo (px (a.getRight() - 1.0f), a.getBottom() - 1.5f, px (a.getX() + 9.0f), a.getBottom() - 1.5f);
                p.lineTo (px (a.getX() + 7.0f), a.getBottom() - 1.5f);
                g.strokePath (p, stroke);
                break;
            }
            case Icon::Copy:
            case Icon::Paste:
                p.addRoundedRectangle (a.getX() + 1.0f, a.getY() + 4.5f, 9.0f, 10.0f, 1.0f);
                p.startNewSubPath (a.getX() + 5.0f, a.getY() + 4.5f);
                p.lineTo (a.getX() + 5.0f, a.getY() + 1.0f);
                p.lineTo (a.getRight() - 1.0f, a.getY() + 1.0f);
                p.lineTo (a.getRight() - 1.0f, a.getY() + 11.0f);
                p.lineTo (a.getX() + 10.0f, a.getY() + 11.0f);
                if (icon == Icon::Paste)
                {
                    p.startNewSubPath (a.getX() + 3.5f, a.getY() + 9.5f); p.lineTo (a.getX() + 7.5f, a.getY() + 9.5f);
                    p.startNewSubPath (a.getX() + 5.5f, a.getY() + 7.5f); p.lineTo (a.getX() + 5.5f, a.getY() + 11.5f);
                }
                g.strokePath (p, stroke);
                break;
            case Icon::Save:
                p.startNewSubPath (a.getX() + 1.0f, a.getY() + 1.0f);
                p.lineTo (a.getRight() - 4.0f, a.getY() + 1.0f);
                p.lineTo (a.getRight() - 1.0f, a.getY() + 4.0f);
                p.lineTo (a.getRight() - 1.0f, a.getBottom() - 1.0f);
                p.lineTo (a.getX() + 1.0f, a.getBottom() - 1.0f);
                p.closeSubPath();
                p.addRectangle (a.getX() + 4.0f, a.getY() + 1.0f, 6.0f, 4.0f);
                p.addRectangle (a.getX() + 4.0f, a.getBottom() - 6.0f, 8.0f, 5.0f);
                g.strokePath (p, stroke);
                break;
            case Icon::Trash:
                p.startNewSubPath (a.getX() + 1.0f, a.getY() + 3.5f); p.lineTo (a.getRight() - 1.0f, a.getY() + 3.5f);
                p.startNewSubPath (a.getX() + 6.0f, a.getY() + 3.5f); p.lineTo (a.getX() + 6.0f, a.getY() + 1.0f);
                p.lineTo (a.getRight() - 6.0f, a.getY() + 1.0f); p.lineTo (a.getRight() - 6.0f, a.getY() + 3.5f);
                p.startNewSubPath (a.getX() + 3.0f, a.getY() + 3.5f); p.lineTo (a.getX() + 4.0f, a.getBottom() - 1.0f);
                p.lineTo (a.getRight() - 4.0f, a.getBottom() - 1.0f); p.lineTo (a.getRight() - 3.0f, a.getY() + 3.5f);
                p.startNewSubPath (a.getX() + 6.5f, a.getY() + 6.5f); p.lineTo (a.getX() + 6.5f, a.getBottom() - 4.0f);
                p.startNewSubPath (a.getRight() - 6.5f, a.getY() + 6.5f); p.lineTo (a.getRight() - 6.5f, a.getBottom() - 4.0f);
                g.strokePath (p, stroke);
                break;
        }
    }

private:
    Icon icon;
};

/** Petit bouton "LCD" (ratio des tranches). */
class AlphaLcdButton : public juce::Button
{
public:
    explicit AlphaLcdButton (const juce::String& t) : juce::Button (t) {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto pal = AlphaPalette::get();
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (pal.sunny) // design de Sunny : cases noires, choix en bleu a plat
        {
            g.setColour (on ? juce::Colour (0xff3ea9f5) : (over ? juce::Colour (0xff142231) : pal.lcd));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.9f));
            g.setFont (AlphaDraw::lcdFont (12.0f));
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
            return;
        }
        if (on)
        {
            juce::Path p; p.addRoundedRectangle (r, 2.0f);
            juce::DropShadow (pal.accentGlow, 5, {}).drawForPath (g, p);
        }
        AlphaDraw::finishDropShadow (g, r, 2.0f, pal.finish);
        g.setColour (on ? pal.accent : (over ? pal.lcd.brighter (0.15f) : pal.lcd));
        g.fillRoundedRectangle (r, 2.0f);
        AlphaDraw::finishShine (g, r, 2.0f, pal.finish, on ? 0.6f : 0.42f);
        g.setColour (on ? juce::Colours::white : pal.lcdInk);
        g.setFont (AlphaDraw::lcdFont (12.0f));
        g.drawText (getButtonText(), r, juce::Justification::centred, false);
    }
};

/** Puce grise/bleue (OPTICAL / OVERKILL / SC, segments des options). */
class AlphaChipButton : public juce::Button
{
public:
    explicit AlphaChipButton (const juce::String& t) : juce::Button (t) {}
    void setFontSize (float s) { fontSize = s; }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto pal = AlphaPalette::get();
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (on ? pal.accent : (over ? pal.pill.brighter (0.05f) : pal.pill));
        g.fillRoundedRectangle (r, 3.0f);
        AlphaDraw::spacedText (g, getButtonText(), r, on ? juce::Colours::white : pal.pillInk, fontSize, 0.08f);
    }

private:
    float fontSize = AlphaType::label;
};

/**
    AlphaKnob -- knob neumorphique d'apres l'image de reference fournie
    (retour utilisateur : "il est parfait"). Modele cale pixel par pixel
    sur cette image :
      - face PLATE, de la couleur de la carte (rien n'est peint dessus) ;
      - tout le relief est sur le bord : chanfrein arrondi blanc en haut a
        gauche, sombre en bas a droite, colle au bord, puis ombre courte ;
      - halo clair en haut a gauche ;
      - couronne de 72 traits blancs posee SUR la carte, autour du knob ;
      - point bleu creuse (sombre en haut a gauche, cyan en bas a droite).
    Le corps est calcule par pixel (sur-echantillonne 3x3) dans une image
    ARGB transparente posee sur la carte : calques clair/sombre en alpha,
    donc valable sur toutes les teintes de carte. Calcule UNE fois par
    taille et par theme (cache partage), seul le point bouge ensuite ; la
    lumiere ne tourne pas avec le knob, le point est donc aussi un sprite
    fixe. Distances en fractions du rayon R du knob ; le dessin deborde
    jusqu'a kExtent * R (le composant doit reserver cette place).
*/
namespace AlphaKnob
{
    constexpr float kExtent = 1.30f;   // couronne de traits jusqu'a ~1.29 R
    constexpr float kDotRadius = 0.672f;
    constexpr float kDotSize = 0.0778f;

    inline float smooth (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }
    inline float bump (float in0, float peak, float out1, float x) noexcept
    {
        return x < peak ? smooth (in0, peak, x) : 1.0f - smooth (peak, out1, x);
    }

    struct Theme
    {
        float lightScale, darkScale, tickAlpha;
        juce::Colour lightColour, shadowColour;
        // Contraste renforce (variante C "silhouette" choisie sur l'apercu) :
        // filet au bord, face teintee. Le CORPS du knob seulement -- les
        // traits de la couronne restent ceux du mode normal (demande
        // explicite : "le mode contraste c'est juste les knobs, pas les
        // marqueurs autour des knobs").
        float edge = 0.0f, face = 0.0f;
        bool contrastOnShadow = false; // filet/face dans le calque sombre (theme clair) ou clair (theme sombre)
    };
    inline Theme themeFor (bool light, bool highContrast = false) noexcept
    {
        Theme t = light ? Theme { 1.0f, 1.0f, 0.509f, juce::Colours::white, juce::Colour (0xff738c9e) }
                        // Theme sombre : meme geometrie, lumiere tres retenue, ombre noire.
                        : Theme { 0.18f, 1.25f, 0.13f, juce::Colours::white, juce::Colours::black };
        if (highContrast)
        {
            if (light) { t.darkScale = 2.0f; t.edge = 0.7f; t.face = 0.2f; t.contrastOnShadow = true; }
            else       { t.lightScale = 0.3f; t.darkScale = 1.8f; t.edge = 0.4f; t.face = 0.1f; }
        }
        return t;
    }

    // Calques au point (dx, dy) du centre, R en pixels : kL clair, kD sombre.
    inline void layers (float dx, float dy, float R, const Theme& t, float& kL, float& kD) noexcept
    {
        using MC = juce::MathConstants<float>;
        const float r = std::sqrt (dx * dx + dy * dy), rho = r / R;
        const float th = std::atan2 (dy, dx);
        const float s = std::cos (th - MC::pi * 0.25f);       // +1 en bas a droite, -1 en haut a gauche
        const float sp = juce::jmax (0.0f, s), sn = juce::jmax (0.0f, -s);

        kD = (0.505f * std::pow (sp, 1.62f) + 0.021f) * bump (0.851f, 0.992f, 1.265f, rho);
        const float kR = 0.946f * std::pow (sn, 1.46f) * bump (0.868f, 0.989f, 1.239f, rho);
        const float kG = 0.225f * std::pow (sn, 0.789f) * bump (0.747f, 1.102f, 1.315f, rho);

        float kT = 0.0f;
        if (rho > 1.038f && rho < 1.343f)
        {
            const float step = MC::twoPi / 72.0f;                 // un trait a midi
            float a = std::fmod (th + MC::halfPi, step);
            if (a < 0.0f) a += step;
            if (a > step * 0.5f) a -= step;
            const float cov = juce::jlimit (0.0f, 1.0f, 0.5f + (juce::jmax (0.9f, 0.0497f * R) * 0.5f - std::abs (a) * r) * 1.5f);
            kT = t.tickAlpha * cov * smooth (1.068f, 1.108f, rho) * (1.0f - smooth (1.273f, 1.313f, rho));
        }

        // Contraste renforce : filet au bord (tout le tour) + face teintee.
        const float kE = t.edge * std::exp (-std::pow ((rho - 0.99f) / 0.035f, 2.0f));
        const float kF = t.face * (1.0f - smooth (0.95f, 1.0f, rho));
        const float extra = 1.0f - (1.0f - kE) * (1.0f - kF);
        const float tickL = juce::jmin (1.0f, kT); // traits : toujours dans le calque clair, comme en mode normal

        kL = 1.0f - (1.0f - kR * t.lightScale) * (1.0f - kG * t.lightScale) * (1.0f - tickL) * (1.0f - (t.contrastOnShadow ? 0.0f : extra));
        kD = 1.0f - (1.0f - juce::jmin (1.0f, kD * t.darkScale)) * (1.0f - (t.contrastOnShadow ? extra : 0.0f));
    }

    // Corps du knob (sans le point). R en pixels PHYSIQUES ; centre au
    // milieu de l'image.
    inline juce::Image renderBody (float R, bool lightTheme, bool highContrast = false)
    {
        const auto t = themeFor (lightTheme, highContrast);
        const int size = 2 * (int) std::ceil (R * kExtent) + 2;
        const float c = (float) size * 0.5f;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        const float lr = t.lightColour.getFloatRed(), lg = t.lightColour.getFloatGreen(), lb = t.lightColour.getFloatBlue();
        const float sr = t.shadowColour.getFloatRed(), sg = t.shadowColour.getFloatGreen(), sb = t.shadowColour.getFloatBlue();
        constexpr int N = 3;

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float A = 0.0f, pr = 0.0f, pg = 0.0f, pb = 0.0f;   // premultiplie
                for (int sy = 0; sy < N; ++sy)
                    for (int sx = 0; sx < N; ++sx)
                    {
                        float kL, kD;
                        layers ((float) x + ((float) sx + 0.5f) / N - c, (float) y + ((float) sy + 0.5f) / N - c, R, t, kL, kD);
                        // clair puis sombre par-dessus = un seul pixel premultiplie
                        const float wl = kL * (1.0f - kD);
                        A  += wl + kD;
                        pr += lr * wl + sr * kD;
                        pg += lg * wl + sg * kD;
                        pb += lb * wl + sb * kD;
                    }
                constexpr float inv = 255.0f / (float) (N * N);
                if (A * inv < 0.5f)
                    continue;
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))
                    ->setARGB ((juce::uint8) juce::roundToInt (A * inv), (juce::uint8) juce::roundToInt (pr * inv),
                               (juce::uint8) juce::roundToInt (pg * inv), (juce::uint8) juce::roundToInt (pb * inv));
            }
        return img;
    }

    // Point bleu creuse + lueur cyan, rayon ds en pixels PHYSIQUES ; centre
    // au milieu de l'image.
    inline juce::Image renderDot (float ds, bool powered)
    {
        const int e = (int) std::ceil (ds * 2.6f) + 1;
        const int size = 2 * e;
        const float c = (float) e;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        // sombre (haut gauche) / milieu / cyan (bas droite) ; gris si eteint
        const float c0[3] = { 74.0f, 93.0f, 152.0f }, c1[3] = { 70.0f, 140.0f, 197.0f }, c2[3] = { 115.0f, 219.0f, 254.0f };
        const float g0[3] = { 120.0f, 130.0f, 140.0f }, g1[3] = { 150.0f, 160.0f, 170.0f }, g2[3] = { 190.0f, 198.0f, 205.0f };
        const float glow[3] = { 127.0f, 211.0f, 245.0f };
        const float* k0 = powered ? c0 : g0;
        const float* k1 = powered ? c1 : g1;
        const float* k2 = powered ? c2 : g2;
        const float glA = powered ? 0.565f : 0.0f, glR = 1.04f * ds, gOff = 0.238f * ds * 0.7071f;
        constexpr int N = 4;

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float A = 0.0f, p[3] = { 0.0f, 0.0f, 0.0f };
                for (int sy = 0; sy < N; ++sy)
                    for (int sx = 0; sx < N; ++sx)
                    {
                        const float u = (float) x + ((float) sx + 0.5f) / N - c, v = (float) y + ((float) sy + 0.5f) / N - c;
                        const float kGl = glA * std::exp (-((u - gOff) * (u - gOff) + (v - gOff) * (v - gOff)) / (glR * glR));
                        const float cov = juce::jlimit (0.0f, 1.0f, (ds - std::sqrt (u * u + v * v)) * 3.0f + 0.5f);
                        const float w = juce::jlimit (-1.0f, 1.0f, (u + v) / (1.4142f * ds));
                        const float wg = kGl * (1.0f - cov);
                        A += wg + cov;
                        for (int k = 0; k < 3; ++k)
                        {
                            const float dc = w < 0.0f ? k1[k] + (k0[k] - k1[k]) * -w : k1[k] + (k2[k] - k1[k]) * w;
                            p[k] += glow[k] * wg + dc * cov;
                        }
                    }
                constexpr float inv = 1.0f / (float) (N * N);
                if (A * inv * 255.0f < 0.5f)
                    continue;
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))
                    ->setARGB ((juce::uint8) juce::roundToInt (A * inv * 255.0f), (juce::uint8) juce::roundToInt (p[0] * inv),
                               (juce::uint8) juce::roundToInt (p[1] * inv), (juce::uint8) juce::roundToInt (p[2] * inv));
            }
        return img;
    }

    // Cache partage par toutes les tranches (SharedResourcePointer : libere
    // avec le dernier editeur, pas d'image statique a la fermeture du
    // plugin). Cle = taille au quart de pixel physique + theme/etat.
    class Cache
    {
    public:
        const juce::Image& body (float R, bool lightTheme, bool highContrast)
        {
            const int q = juce::jmax (4, juce::roundToInt (R * 4.0f));
            return get (bodies, q * 4 + (lightTheme ? 1 : 0) + (highContrast ? 2 : 0),
                        [&] { return renderBody ((float) q / 4.0f, lightTheme, highContrast); });
        }
        const juce::Image& dot (float ds, bool powered)
        {
            const int q = juce::jmax (4, juce::roundToInt (ds * 4.0f));
            return get (dots, q * 2 + (powered ? 1 : 0), [&] { return renderDot ((float) q / 4.0f, powered); });
        }

    private:
        template <typename Fn>
        static const juce::Image& get (std::map<int, juce::Image>& m, int key, Fn&& make)
        {
            auto it = m.find (key);
            if (it == m.end())
            {
                if (m.size() > 24)
                    m.clear();
                it = m.emplace (key, make()).first;
            }
            return it->second;
        }

        std::map<int, juce::Image> bodies, dots;
    };
}

/**
    AlphaLookAndFeel -- sliders et knob de l'interface Alpha :
      - horizontal : piste fine, remplissage accent (depuis la valeur
        neutre si "alphaNeutral" est pose -- Spread), pouce blanc ;
      - vertical "alphaFader" : uniquement le capuchon de fader (les metres
        derriere sont peints par la tranche) ;
      - rotatif : knob neumorphique (AlphaKnob), point bleu creuse.
*/
class AlphaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static void setNeutral (juce::Slider& s, double v) { s.getProperties().set ("alphaNeutral", v); }
    static void markAsFader (juce::Slider& s) { s.getProperties().set ("alphaFader", true); }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const auto pal = AlphaPalette::get();
        const bool powered = (bool) slider.getProperties().getWithDefault ("alphaPowered", true);

        if (slider.getProperties().contains ("alphaFader") || style == juce::Slider::LinearVertical)
        {
            drawConsoleFader (g, (float) x, (float) y, (float) width, (float) height, sliderPos, slider, pal);
            return;
        }

        const float cy = (float) y + (float) height * 0.5f;
        auto track = juce::Rectangle<float> ((float) x, cy - 1.5f, (float) width, 3.0f);
        g.setColour (pal.track);
        g.fillRoundedRectangle (track, 1.5f);

        if (powered)
        {
            const double neutral = slider.getProperties().contains ("alphaNeutral")
                                       ? (double) slider.getProperties()["alphaNeutral"] : slider.getMinimum();
            const float neutralX = (float) x + (float) slider.valueToProportionOfLength (neutral) * (float) width;
            const float a = juce::jmin (neutralX, sliderPos), b = juce::jmax (neutralX, sliderPos);
            g.setColour (pal.accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (a, cy - 1.5f, juce::jmax (0.0f, b - a), 3.0f), 1.5f);
        }

        auto thumb = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ sliderPos, cy });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (thumb.translated (0.0f, 1.0f));
        juce::ColourGradient tg (juce::Colours::white, thumb.getX() + 3.0f, thumb.getY() + 3.0f, juce::Colour (0xffd0d6dc), thumb.getRight(), thumb.getBottom(), true);
        g.setGradientFill (tg);
        g.fillEllipse (thumb);
    }

    // Fader de console (demande explicite : "ils doivent faire fader de
    // console") : fente sombre graduee tous les 10 dB avec repere a 0 dB,
    // capuchon en relief qui projette son ombre. Les diodes GR/EXP restent
    // de chaque cote (composant AlphaGrExpMeter, derriere le fader). Trois
    // capuchons au choix (AlphaPalette::faderStyle) : BLACK, VINTAGE, ALU.
    static void drawConsoleFader (juce::Graphics& g, float x, float y, float width, float height, float sliderPos,
                                  juce::Slider& slider, const AlphaPalette& pal)
    {
        if (pal.sunny && kSunnyFader)
        {
            drawSunnyFader (g, x, width, sliderPos, slider, pal);
            return;
        }
        juce::ignoreUnused (y, height);
        const int styleIdx = pal.faderStyle;
        const int inverted = (! pal.light && styleIdx == 0) ? pal.darkFader : 0;
        const float cx = x + width * 0.5f;

        // --- Fente + graduations ---
        {
            const float w = styleIdx == 2 ? 3.0f : 5.0f, gx = cx - w * 0.5f;
            const float top = (float) slider.getPositionOfValue (slider.getMaximum()) - 6.0f;
            const float bottom = (float) slider.getPositionOfValue (slider.getMinimum()) + 6.0f;
            const double step = 10.0;
            for (double v = std::ceil (slider.getMinimum() / step) * step; v <= slider.getMaximum() + 0.001; v += step)
            {
                const float ty = std::round ((float) slider.getPositionOfValue (v)) + 0.5f;
                const bool zero = std::abs (v) < 0.001;
                const float len = zero ? 4.0f : 2.5f;
                g.setColour (zero ? pal.accentDeep : pal.cardInk.withAlpha (0.55f));
                g.drawLine (gx - len, ty, gx - 0.5f, ty, zero ? 1.5f : 1.0f);
                g.drawLine (gx + w + 0.5f, ty, gx + w + len, ty, zero ? 1.5f : 1.0f);
            }
            const auto slot = juce::Rectangle<float> (gx, top, w, bottom - top);
            if (inverted == 3)
            {
                juce::ColourGradient sg (juce::Colour (0xffaeb5bd), slot.getX(), 0.0f, juce::Colour (0xffc7cdd3), slot.getRight(), 0.0f, false);
                sg.addColour (0.5, juce::Colour (0xffe9edf0));
                g.setGradientFill (sg);
            }
            else if (styleIdx == 1)
                g.setColour (juce::Colour (0xff0c0c0c));
            else
            {
                juce::ColourGradient sg (juce::Colour (0xff05080b), slot.getX(), 0.0f, juce::Colour (0xff0b0f13), slot.getRight(), 0.0f, false);
                sg.addColour (0.5, juce::Colour (0xff262c33));
                g.setGradientFill (sg);
            }
            g.fillRoundedRectangle (slot, w * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawLine (slot.getRight() + 0.5f, top + 2.0f, slot.getRight() + 0.5f, bottom - 2.0f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawLine (slot.getX() - 0.5f, top + 2.0f, slot.getX() - 0.5f, bottom - 2.0f, 1.0f);
        }

        // --- Capuchon ---
        const float capW = styleIdx == 0 ? 32.0f : (styleIdx == 1 ? 30.0f : 34.0f);
        const float capH = styleIdx == 0 ? 24.0f : (styleIdx == 1 ? 26.0f : 22.0f);
        const float corner = styleIdx == 1 ? 4.0f : (styleIdx == 0 ? 3.0f : 2.0f);
        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });

        // ombre portee sur la carte (adoucie : 3 passages)
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (juce::Colour ((juce::uint8) 0, (juce::uint8) 20, (juce::uint8) 40, 0.13f));
            g.fillRoundedRectangle (cap.translated (1.0f, 4.0f).expanded (2.0f - (float) i), corner + 2.0f - (float) i);
        }

        if (inverted > 0) // BLACK inverse (theme sombre) : blanc a stries, ligne noire ou bleue
        {
            juce::ColourGradient body (juce::Colour (0xffffffff), 0.0f, cap.getY(), juce::Colour (0xffaab1b9), 0.0f, cap.getBottom(), false);
            body.addColour (0.12, juce::Colour (0xfff1f3f5));
            body.addColour (0.50, juce::Colour (0xffe0e4e8));
            body.addColour (0.88, juce::Colour (0xffc9ced4));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, corner);
            juce::ColourGradient side (juce::Colours::white.withAlpha (0.35f), cap.getX(), 0.0f, juce::Colours::black.withAlpha (0.16f), cap.getRight(), 0.0f, false);
            side.addColour (0.15, juce::Colours::transparentWhite);
            side.addColour (0.85, juce::Colours::transparentBlack);
            g.setGradientFill (side);
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colour ((juce::uint8) 20, (juce::uint8) 30, (juce::uint8) 40, 0.13f));
            for (int i = 0; i < 3; ++i)
            {
                g.drawLine (cap.getX() + 3.0f, cap.getY() + 3.5f + 2.5f * (float) i, cap.getRight() - 3.0f, cap.getY() + 3.5f + 2.5f * (float) i, 1.0f);
                g.drawLine (cap.getX() + 3.0f, cap.getBottom() - 3.5f - 2.5f * (float) i, cap.getRight() - 3.0f, cap.getBottom() - 3.5f - 2.5f * (float) i, 1.0f);
            }
            g.setColour (juce::Colours::white);
            g.fillRect (cap.getX() + 3.0f, cap.getY() + 0.8f, cap.getWidth() - 6.0f, 1.0f);
            g.setColour (juce::Colour ((juce::uint8) 20, (juce::uint8) 30, (juce::uint8) 40, 0.30f));
            g.fillRect (cap.getX() + 3.0f, cap.getBottom() - 1.6f, cap.getWidth() - 6.0f, 1.0f);
            if (inverted == 2)
            {
                g.setColour (pal.accent.withAlpha (0.35f)); // halo
                g.fillRect (cap.getX() + 1.0f, sliderPos - 2.5f, cap.getWidth() - 2.0f, 5.0f);
                g.setColour (pal.accent);
            }
            else
                g.setColour (juce::Colour (0xff15191d));
            g.fillRect (cap.getX() + 2.0f, sliderPos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        }
        else if (styleIdx == 0) // BLACK : anthracite a stries, ligne blanche
        {
            juce::ColourGradient body (juce::Colour (0xff5b6168), 0.0f, cap.getY(), juce::Colour (0xff101215), 0.0f, cap.getBottom(), false);
            body.addColour (0.12, juce::Colour (0xff3a3f45));
            body.addColour (0.50, juce::Colour (0xff2a2e33));
            body.addColour (0.88, juce::Colour (0xff1c1f23));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, corner);
            juce::ColourGradient side (juce::Colours::white.withAlpha (0.12f), cap.getX(), 0.0f, juce::Colours::black.withAlpha (0.3f), cap.getRight(), 0.0f, false);
            side.addColour (0.15, juce::Colours::transparentWhite);
            side.addColour (0.85, juce::Colours::transparentBlack);
            g.setGradientFill (side);
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            for (int i = 0; i < 3; ++i)
            {
                g.drawLine (cap.getX() + 3.0f, cap.getY() + 3.5f + 2.5f * (float) i, cap.getRight() - 3.0f, cap.getY() + 3.5f + 2.5f * (float) i, 1.0f);
                g.drawLine (cap.getX() + 3.0f, cap.getBottom() - 3.5f - 2.5f * (float) i, cap.getRight() - 3.0f, cap.getBottom() - 3.5f - 2.5f * (float) i, 1.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (cap.getX() + 3.0f, cap.getY() + 0.8f, cap.getWidth() - 6.0f, 1.0f);
            g.setColour (juce::Colour (0xfff2f4f6));
            g.fillRect (cap.getX() + 2.0f, sliderPos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        }
        else if (styleIdx == 1) // VINTAGE : creme creuse au centre, ligne noire
        {
            juce::ColourGradient body (juce::Colour (0xfffbf6ea), 0.0f, cap.getY(), juce::Colour (0xffb9af98), 0.0f, cap.getBottom(), false);
            body.addColour (0.3, juce::Colour (0xffe7dfcb));
            body.addColour (0.5, juce::Colour (0xffcfc5ad));
            body.addColour (0.7, juce::Colour (0xffe2d9c3));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colour ((juce::uint8) 90, (juce::uint8) 75, (juce::uint8) 50, 0.35f));
            for (int i = 0; i < 4; ++i)
            {
                g.drawLine (cap.getX() + 4.0f, cap.getY() + 4.0f + 2.2f * (float) i, cap.getRight() - 4.0f, cap.getY() + 4.0f + 2.2f * (float) i, 1.0f);
                g.drawLine (cap.getX() + 4.0f, cap.getBottom() - 4.0f - 2.2f * (float) i, cap.getRight() - 4.0f, cap.getBottom() - 4.0f - 2.2f * (float) i, 1.0f);
            }
            g.setColour (juce::Colour ((juce::uint8) 60, (juce::uint8) 50, (juce::uint8) 35, 0.45f));
            g.drawRoundedRectangle (cap.reduced (0.5f), corner, 1.0f);
            g.setColour (juce::Colour (0xff141414));
            g.fillRect (cap.getX() + 3.0f, sliderPos - 1.0f, cap.getWidth() - 6.0f, 2.0f);
        }
        else // ALU : aluminium brosse a chanfreins, ligne bleue lumineuse
        {
            juce::ColourGradient body (juce::Colour (0xfffbfcfd), 0.0f, cap.getY(), juce::Colour (0xff9aa3ac), 0.0f, cap.getBottom(), false);
            body.addColour (0.45, juce::Colour (0xffd3d9df));
            body.addColour (0.55, juce::Colour (0xffc3cad1));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, corner);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            for (int i = 0; i < 14; ++i)
            {
                const float bx = cap.getX() + 2.0f + (float) i * (cap.getWidth() - 4.0f) / 13.0f;
                g.drawLine (bx, cap.getY() + 2.0f, bx + 0.5f, cap.getBottom() - 2.0f, 0.6f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (cap.getX() + 2.0f, cap.getY() + 0.8f, cap.getWidth() - 4.0f, 1.0f);
            g.setColour (juce::Colour ((juce::uint8) 40, (juce::uint8) 50, (juce::uint8) 60, 0.45f));
            g.fillRect (cap.getX() + 2.0f, cap.getBottom() - 1.8f, cap.getWidth() - 4.0f, 1.0f);
            g.setColour (pal.accent.withAlpha (0.35f)); // halo
            g.fillRect (cap.getX() + 1.0f, sliderPos - 2.5f, cap.getWidth() - 2.0f, 5.0f);
            g.setColour (pal.accent);
            g.fillRect (cap.getX() + 2.0f, sliderPos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        }
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        if (s.getProperties().contains ("alphaFader") || s.getSliderStyle() == juce::Slider::LinearVertical)
            return 13; // capuchon de console (jusqu'a 26 px de haut) toujours entier en butee
        return 5;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        // Knob neumorphique (AlphaKnob) : les bounds reservent la couronne
        // de traits (kExtent * R) ; pas d'arc de valeur, ce sont les traits
        // de la couronne qui s'allument (voir plus bas).
        const auto pal = AlphaPalette::get();
        // "alphaGreyed" : knob grise par le mode des aiguilles (GONIO : tout
        // sauf SPREAD ; GR : SPREAD) -- toujours reglable, marqueurs et point
        // en gris.
        const bool lively = (bool) slider.getProperties().getWithDefault ("alphaPowered", true)
                            && ! (bool) slider.getProperties().getWithDefault ("alphaGreyed", false);
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        if (pal.sunny && kSunnyBlackKnobs)
        {
            drawSunnyKnob (g, area, pos, startAngle, endAngle, slider, pal, lively);
            return;
        }
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f / AlphaKnob::kExtent;
        const auto c = area.getCentre();
        const float scale = juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto& body = knobCache->body (R * scale, pal.light, pal.highContrast);
        const float half = (float) body.getWidth() * 0.5f / scale;
        g.drawImage (body, juce::Rectangle<float> (c.x - half, c.y - half, half * 2.0f, half * 2.0f));

        // Marqueurs lumineux (demande explicite : "les marqueurs autour des
        // rotatifs indiquent la position en s'eclairant") : les traits de
        // l'arc de reglage s'allument du depart -- ou de la valeur neutre
        // ("alphaNeutral" : IN 0 dB, SPREAD 100 %) -- jusqu'a la valeur ; le
        // trait le plus proche de la valeur est toujours allume. Memes
        // positions que la couronne d'AlphaKnob (72 traits, un a midi).
        {
            const float startDeg = juce::radiansToDegrees (startAngle) - 360.0f;   // -135 (rotary 1.25 pi .. 2.75 pi)
            const float spanDeg = juce::radiansToDegrees (endAngle - startAngle);
            const float valueDeg = startDeg + pos * spanDeg;
            float fromDeg = startDeg;
            if (slider.getProperties().contains ("alphaNeutral"))
                fromDeg = startDeg + (float) slider.valueToProportionOfLength ((double) slider.getProperties()["alphaNeutral"]) * spanDeg;
            const float lo = juce::jmin (fromDeg, valueDeg), hi = juce::jmax (fromDeg, valueDeg);
            const float nearest = std::round (valueDeg / 5.0f) * 5.0f;
            const float r0 = 1.08f * R, r1 = 1.30f * R;

            juce::Path ticks;
            for (float d = startDeg; d <= startDeg + spanDeg + 0.01f; d += 5.0f)
            {
                if (! ((d >= lo - 0.01f && d <= hi + 0.01f) || std::abs (d - nearest) < 0.01f))
                    continue;
                const float a = juce::degreesToRadians (d);
                ticks.startNewSubPath (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0);
                ticks.lineTo (c.x + std::sin (a) * r1, c.y - std::cos (a) * r1);
            }
            // Identiques en mode contraste (il n'agit que sur le corps du knob).
            const float w = juce::jmax (0.9f / scale, 0.0497f * R) + 0.6f / scale;
            const auto lit = ! lively ? pal.inkSoft.withAlpha (0.55f) : pal.accent;
            g.setColour (lit.withMultipliedAlpha (0.28f));   // halo sans flou
            g.strokePath (ticks, juce::PathStrokeType (w * 2.6f));
            g.setColour (lit);
            g.strokePath (ticks, juce::PathStrokeType (w));
        }

        const float ang = startAngle + pos * (endAngle - startAngle);
        const auto& dot = knobCache->dot (juce::jmax (2.0f, AlphaKnob::kDotSize * R) * scale * (pal.highContrast ? 1.45f : 1.0f), lively);
        const float dr = AlphaKnob::kDotRadius * R, dh = (float) dot.getWidth() * 0.5f / scale;
        const juce::Point<float> p (c.x + std::sin (ang) * dr, c.y - std::cos (ang) * dr);
        g.drawImage (dot, juce::Rectangle<float> (p.x - dh, p.y - dh, dh * 2.0f, dh * 2.0f));
    }

    // Knobs noirs du design de Sunny : retires (retour utilisateur : "j'aime
    // pas trop les knobs du mode sombre, je prefere les neumorphiques"),
    // le theme sombre garde le knob neumorphique. Code garde : passer a true.
    static constexpr bool kSunnyBlackKnobs = false;

    // Fader blanc a piste bleue du design de Sunny : retire (retour
    // utilisateur : "les faders et les diodes des outputs ne correspondent
    // pas en clair et en sombre, les mieux sont en mode clair") -- le theme
    // sombre reprend le fader de console du clair (option Band faders).
    // Code garde : passer a true.
    static constexpr bool kSunnyFader = false;

    // Knob du design de Sunny : sphere charbon avec reflet en croissant sur
    // le bord haut-gauche, arc bleu continu autour (depuis le depart ou la
    // valeur neutre jusqu'a la valeur), point bleu.
    static void drawSunnyKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float startAngle, float endAngle,
                               juce::Slider& slider, const AlphaPalette& pal, bool lively)
    {
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f / AlphaKnob::kExtent;
        const auto c = area.getCentre();
        const auto body = juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c);

        {
            const float arcR = R * 1.17f, arcW = juce::jmax (2.0f, R * 0.12f);
            float from = startAngle;
            if (slider.getProperties().contains ("alphaNeutral"))
                from = startAngle + (float) slider.valueToProportionOfLength ((double) slider.getProperties()["alphaNeutral"]) * (endAngle - startAngle);
            const float to = startAngle + pos * (endAngle - startAngle);
            if (std::abs (to - from) > 0.005f)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
                g.setColour (lively ? pal.accent : pal.inkSoft.withAlpha (0.5f));
                g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillEllipse (body.translated (0.0f, R * 0.08f).expanded (R * 0.03f));

        juce::ColourGradient bg (juce::Colour (0xff4b535d), c.x - R * 0.45f, c.y - R * 0.55f,
                                 juce::Colour (0xff0c0f13), c.x + R * 0.75f, c.y + R * 0.8f, true);
        bg.addColour (0.38, juce::Colour (0xff20262d));
        g.setGradientFill (bg);
        g.fillEllipse (body);

        {
            juce::Path crescent;
            crescent.setUsingNonZeroWinding (false);
            crescent.addEllipse (body);
            crescent.addEllipse (body.translated (R * 0.14f, R * 0.12f).reduced (R * 0.02f));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.75f), c.x - R * 0.75f, c.y - R * 0.55f,
                                                     juce::Colours::white.withAlpha (0.0f), c.x + R * 0.2f, c.y + R * 0.6f, false));
            g.fillPath (crescent);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (body, juce::jmax (0.8f, R * 0.03f));

        const float ang = startAngle + pos * (endAngle - startAngle);
        const float dr = R * 0.8f, ds = juce::jmax (1.7f, R * 0.08f);
        g.setColour (lively ? pal.accent : pal.inkSoft);
        g.fillEllipse (juce::Rectangle<float> (ds * 2.0f, ds * 2.0f).withCentre ({ c.x + std::sin (ang) * dr, c.y - std::cos (ang) * dr }));
    }

    // Fader du design de Sunny : piste fine grise au-dessus du capuchon,
    // bleue en dessous, grands traits blancs (haut, 0 dB, bas), capuchon
    // blanc a ligne bleue. Les diodes GR/EXP en tirets sont dessinees par
    // AlphaGrExpMeter, derriere.
    static void drawSunnyFader (juce::Graphics& g, float x, float width, float sliderPos, juce::Slider& slider, const AlphaPalette& pal)
    {
        const float cx = x + width * 0.5f;
        const float top = (float) slider.getPositionOfValue (slider.getMaximum());
        const float bottom = (float) slider.getPositionOfValue (slider.getMinimum());
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (double v : { slider.getMaximum(), 0.0, slider.getMinimum() })
        {
            const float ty = std::round ((float) slider.getPositionOfValue (v));
            g.fillRect (cx - 16.0f, ty - 0.75f, 9.0f, 1.5f);
            g.fillRect (cx + 7.0f,  ty - 0.75f, 9.0f, 1.5f);
        }
        constexpr float w = 3.0f;
        g.setColour (juce::Colour (0xff818c9a));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - w * 0.5f, top, w, juce::jmax (0.0f, sliderPos - top)), 1.5f);
        g.setColour (pal.accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - w * 0.5f, sliderPos, w, juce::jmax (0.0f, bottom - sliderPos)), 1.5f);

        const auto cap = juce::Rectangle<float> (30.0f, 14.0f).withCentre ({ cx, sliderPos });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f8fa), 0.0f, cap.getY(), juce::Colour (0xffc3cad2), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colour (0xff7d8896));
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (pal.accent);
        g.fillRect (cap.getX() + 4.0f, sliderPos - 0.6f, cap.getWidth() - 8.0f, 1.2f);
    }

private:
    juce::SharedResourcePointer<AlphaKnob::Cache> knobCache;
};

/**
    AlphaLogo -- "7 | PACK" (maquette, sans signature -- retour utilisateur), cliquable (About).
*/
class AlphaLogo : public juce::Button
{
public:
    AlphaLogo() : juce::Button ("7PACK")
    {
        logo = juce::ImageCache::getFromMemory (BinaryData::LOGO4x_png, BinaryData::LOGO4x_pngSize);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        // Nouveau logo fourni par l'utilisateur (Source/LOGO4x.png, 880 x 188
        // = 4x la taille affichee, net en HiDPI). Le dessin vectoriel
        // ci-dessous ne sert plus qu'en secours.
        if (logo.isValid())
        {
            // Design de Sunny : le blanc du logo ("7" et fond de PACK) passe
            // dans le bleu nuit du panneau.
            const auto palette = AlphaPalette::get();
            const auto& img = palette.sunny ? sunnyLogo (palette.logoBg) : logo;
            const auto area = getLocalBounds().toFloat();
            const auto placed = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                    .appliedTo (img.getBounds().toFloat(), area);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, placed);
            if (over)
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRect (placed.reduced (3.0f));
            }
            return;
        }

        const auto pal = AlphaPalette::get();
        auto r = getLocalBounds().toFloat();
        auto markArea = r.reduced (1.0f);
        auto mark = juce::Rectangle<float> (juce::jmin (markArea.getWidth(), 212.0f), markArea.getHeight()).withCentre (markArea.getCentre());

        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (mark.translated (0.0f, 1.5f));
        g.setColour (pal.logoBg);
        g.fillRect (mark);
        g.setColour (over ? pal.accent.brighter (0.15f) : pal.accent);
        g.drawRect (mark, 2.0f);

        auto seven = mark.removeFromLeft (mark.getHeight() * 0.9f);
        g.fillRect (seven);
        g.setColour (juce::Colours::white);
        g.setFont (AlphaDraw::font (mark.getHeight() * 0.82f));
        g.drawText ("7", seven, juce::Justification::centred, false);

        // "PACK" espace, taille ajustee pour toujours tenir dans le cadre.
        g.setColour (pal.accent);
        auto packArea = mark.withTrimmedLeft (14.0f).withTrimmedRight (6.0f);
        auto packFont = AlphaDraw::font (mark.getHeight() * 0.7f).withExtraKerningFactor (0.55f);
        const float packW = juce::GlyphArrangement::getStringWidth (packFont, "PACK");
        if (packW > packArea.getWidth())
            packFont = packFont.withHeight (packFont.getHeight() * packArea.getWidth() / packW);
        g.setFont (packFont);
        g.drawText ("PACK", packArea, juce::Justification::centredLeft, false);
    }

private:
    const juce::Image& sunnyLogo (juce::Colour dark)
    {
        if (! darkLogo.isValid() || dark != darkLogoColour)
        {
            darkLogo = logo.createCopy();
            darkLogoColour = dark;
            juce::Image::BitmapData px (darkLogo, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < px.height; ++y)
                for (int x = 0; x < px.width; ++x)
                {
                    const auto c = px.getPixelColour (x, y);
                    if (c.getAlpha() == 0)
                        continue;
                    const float minCh = (float) juce::jmin (c.getRed(), c.getGreen(), c.getBlue()) / 255.0f;
                    const float whiteness = juce::jlimit (0.0f, 1.0f, (minCh - 0.5f) / 0.5f);
                    px.setPixelColour (x, y, c.interpolatedWith (dark.withAlpha (c.getFloatAlpha()), whiteness));
                }
        }
        return darkLogo;
    }

    juce::Image logo, darkLogo;
    juce::Colour darkLogoColour;
};

/** Pilule de nom de preset (barre du bas) -- ouvre le navigateur. */
class AlphaPresetPill : public juce::Button
{
public:
    AlphaPresetPill() : juce::Button ("Presets") {}

    // Auteur du preset affiche a droite du nom (audit : "la pilule affiche
    // PRESETS sans nom d'auteur").
    void setAuthor (const juce::String& a) { if (a != author) { author = a; repaint(); } }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto pal = AlphaPalette::get();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        const bool lightPill = pal.light || pal.sunny; // design de Sunny : barre blanche
        juce::ColourGradient bg (lightPill ? juce::Colours::white : juce::Colour (0xff323840), 0.0f, r.getY(),
                                 lightPill ? juce::Colour (0xffd4d8dc) : juce::Colour (0xff22272d), 0.0f, r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, rad);
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.22f), 0.0f, r.getY(),
                                    juce::Colours::transparentBlack, 0.0f, r.getY() + 5.0f, false);
        g.setGradientFill (inner);
        g.fillRoundedRectangle (r, rad);
        AlphaDraw::finishShine (g, r, rad, pal.finish, 0.9f);
        if (over)
        {
            g.setColour (pal.accent.withAlpha (0.6f));
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
        const auto nameText = juce::String::fromUTF8 ("\xe2\x96\xbc  ") + getButtonText().toUpperCase();
        const auto nameInk = pal.sunny ? juce::Colour (0xff6a7885) : pal.ink;
        if (author.isEmpty())
        {
            AlphaDraw::spacedText (g, nameText, r, nameInk, AlphaType::label, pal.sunny ? 0.5f : 0.25f);
            return;
        }
        // Nom centre, auteur a droite en plus discret.
        auto textArea = r.reduced (rad, 0.0f);
        auto authorArea = textArea.removeFromRight (110.0f);
        AlphaDraw::spacedText (g, nameText, textArea, nameInk, AlphaType::label, pal.sunny ? 0.5f : 0.25f);
        g.setColour (pal.inkSoft);
        g.setFont (AlphaDraw::font (AlphaType::label, false).withExtraKerningFactor (0.05f));
        g.drawText (author, authorArea, juce::Justification::centredRight, false);
    }

private:
    juce::String author;
};

/**
    AlphaCeilControl -- plafond de sortie du THD (idThdOutput, -0.5 a -3 dB,
    pas de 0.5) : fleches haut/bas + afficheur bleu + "THD CEIL" (maquette).
    Molette = pas de 0.5 dB, double-clic sur l'afficheur = -0.5 dB.
*/
class AlphaCeilControl : public juce::Component
{
public:
    explicit AlphaCeilControl (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { value = v; repaint(); }, nullptr)
    {
        addAndMakeVisible (up);
        addAndMakeVisible (down);
        up.onClick   = [this] { step (0.5f); };
        down.onClick = [this] { step (-0.5f); };
        attachment.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto arrows = r.removeFromLeft (16);
        up.setBounds (arrows.removeFromTop (arrows.getHeight() / 2));
        down.setBounds (arrows);
        lcdArea = r.removeFromLeft (80).reduced (4, 5).toFloat();
        labelArea = r.toFloat().withTrimmedLeft (6.0f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();
        if (pal.sunny)
        {
            // Design de Sunny : afficheur turquoise, chiffres clairs espaces.
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15807d), 0.0f, lcdArea.getY(),
                                                     juce::Colour (0xff0a5150), 0.0f, lcdArea.getBottom(), false));
            g.fillRoundedRectangle (lcdArea, 3.0f);
            g.setColour (juce::Colour (0xff47c2bd).withAlpha (0.6f));
            g.drawRoundedRectangle (lcdArea.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (juce::Colour (0xffdffcf8));
            g.setFont (AlphaDraw::lcdFont (13.0f).boldened().withExtraKerningFactor (0.12f));
            g.drawText (juce::String (value, 1) + " dB", lcdArea, juce::Justification::centred, false);
            AlphaDraw::spacedText (g, "THD CEIL", labelArea, pal.ink, AlphaType::label, 0.55f, juce::Justification::centredLeft);
            return;
        }
        g.setColour (pal.accent);
        g.fillRoundedRectangle (lcdArea, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawRoundedRectangle (lcdArea, 2.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.setFont (AlphaDraw::lcdFont (13.0f));
        g.drawText (juce::String (value, 1) + " dB", lcdArea, juce::Justification::centred, false);
        AlphaDraw::spacedText (g, "THD CEIL", labelArea, pal.ink, AlphaType::label, 0.3f, juce::Justification::centredLeft);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (w.deltaY != 0.0f)
            step (w.deltaY > 0.0f ? 0.5f : -0.5f);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (lcdArea.contains (e.position))
            attachment.setValueAsCompleteGesture (-0.5f);
    }

private:
    void step (float delta)
    {
        const float nv = juce::jlimit (-3.0f, -0.5f, std::round ((value + delta) * 2.0f) / 2.0f);
        attachment.setValueAsCompleteGesture (nv);
    }

    AlphaIconButton up   { AlphaIconButton::Icon::Up,   "Raise THD ceiling" };
    AlphaIconButton down { AlphaIconButton::Icon::Down, "Lower THD ceiling" };
    float value = -0.5f;
    juce::Rectangle<float> lcdArea, labelArea;
    juce::ParameterAttachment attachment;
};

/**
    AlphaSegmentBar -- libelle + cases a choix unique liees a un parametre
    "choice" (barre du bas, option A de la maquette : OS = surechantillonnage
    de l'etage THD, CLIP = forme de l'ecretage). Grise quand le THD est
    coupe (setDimmed), comme les modes SOFT / TAPE / IRON / AIR.
*/
class AlphaSegmentBar : public juce::Component
{
public:
    AlphaSegmentBar (juce::RangedAudioParameter& p, const juce::String& labelText, const juce::StringArray& segmentTexts, float segmentWidth)
        : label (labelText), segments (segmentTexts), segW (segmentWidth),
          attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }

    int getIdealWidth() const
    {
        return (int) std::ceil (labelWidth() + kGap + (float) segments.size() * segW + (float) (segments.size() - 1) * kSegGap);
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();
        if (dimmed)
            g.beginTransparencyLayer (0.45f);
        auto r = getLocalBounds().toFloat();
        AlphaDraw::spacedText (g, label, r.removeFromLeft (labelWidth()), pal.ink, AlphaType::label, kLabelKerning, juce::Justification::centredLeft);
        r.removeFromLeft (kGap);
        for (int i = 0; i < segments.size(); ++i)
        {
            const auto seg = r.removeFromLeft (segW).withSizeKeepingCentre (segW, juce::jmin (24.0f, r.getHeight()));
            r.removeFromLeft (kSegGap);
            const bool on = i == selected, over = i == hovered;
            if (pal.sunny)
                AlphaDraw::sunnyBlock (g, seg, 5.0f, on ? AlphaDraw::SunnyBlock::Bright : AlphaDraw::SunnyBlock::Dark, over, false);
            else if (on)
                AlphaDraw::blueBlock (g, seg, 5.0f, true, false, pal.accent);
            else
            {
                g.setColour (juce::Colours::white.withAlpha (over ? 0.75f : 0.55f));
                g.fillRoundedRectangle (seg, 5.0f);
                g.setColour (pal.accent.withAlpha (0.65f));
                g.drawRoundedRectangle (seg.reduced (0.5f), 5.0f, 1.0f);
            }
            const auto ink = on ? juce::Colours::white : (pal.sunny ? juce::Colour (0xffa9c0d8) : pal.accentDeep);
            AlphaDraw::spacedText (g, segments[i], seg, ink, AlphaType::label, 0.04f);
        }
        if (dimmed)
            g.endTransparencyLayer();
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.position.x)); }
    void mouseExit (const juce::MouseEvent&) override   { setHovered (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position.x);
        if (i >= 0 && i != selected)
            attachment.setValueAsCompleteGesture ((float) i);
    }

private:
    float labelWidth() const
    {
        return juce::GlyphArrangement::getStringWidth (AlphaDraw::font (AlphaType::label).withExtraKerningFactor (kLabelKerning), label) + 2.0f;
    }

    int indexAt (float x) const
    {
        float sx = labelWidth() + kGap;
        for (int i = 0; i < segments.size(); ++i)
        {
            if (x >= sx && x < sx + segW)
                return i;
            sx += segW + kSegGap;
        }
        return -1;
    }

    void setHovered (int i) { if (i != hovered) { hovered = i; repaint(); } }

    static constexpr float kGap = 7.0f, kSegGap = 3.0f, kLabelKerning = 0.15f;
    juce::String label;
    juce::StringArray segments;
    float segW;
    int selected = 0, hovered = -1;
    bool dimmed = false;
    juce::ParameterAttachment attachment;
};
