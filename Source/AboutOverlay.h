#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    AboutOverlay
    ------------
    Voile plein ecran affiche au clic sur le logo Tripressor.

    Mise en page EN LONGUEUR (3 colonnes cote a cote, separees par de
    fines lignes -- meme langage que les dividers du panneau principal).
    Chaque colonne est desormais un juce::Viewport scrollable (texte
    Overview fourni par l'utilisateur, assez long pour ne plus tenir dans
    la hauteur fixe du panneau) -- colonnes A+B pour l'Overview, colonne C
    pour Features + Philosophy (demande explicite de repartition).
    Se ferme au clic n'importe ou (y compris a l'interieur d'une colonne
    scrollable -- le scroll lui-meme se fait a la molette/scrollbar, donc
    aucun conflit avec un clic simple) ou a la touche Echap, sauf sur le
    lien sidekey.fr.
*/
class AboutOverlay : public juce::Component
{
public:
    // Un item de colonne : paragraphe justifie, sous-titre en gras, ou
    // puce (style liste Features). ParagraphColumn calcule sa PROPRE
    // hauteur de contenu (setSize) a partir de sa largeur -- c'est ce qui
    // permet au juce::Viewport parent de savoir jusqu'ou scroller.
    struct ColumnItem
    {
        enum class Kind { Paragraph, Header, Bullet };
        juce::String text;
        Kind kind = Kind::Paragraph;
    };

    class ParagraphColumn : public juce::Component
    {
    public:
        // Un clic simple (pas un drag de scroll -- le scroll se fait a la
        // molette, voir Viewport, donc aucun conflit) sur le texte doit
        // quand meme fermer le voile About, comme n'importe quel autre
        // clic dessus (demande explicite : "ou qu'on clique n'importe ou
        // dessus"). Sans cet override, un Component qui ne gere pas
        // mouseDown() avale silencieusement l'evenement (JUCE ne le
        // remonte jamais tout seul au parent).
        std::function<void()> onAnyClick;

        void mouseDown (const juce::MouseEvent&) override { if (onAnyClick) onAnyClick(); }

        void setItems (std::vector<ColumnItem> newItems) { items = std::move (newItems); }

        // Appele explicitement par le parent (PAS depuis resized(), pour
        // eviter tout risque de boucle setSize->resized->setSize) une fois
        // que la largeur de colonne est connue -- calcule la hauteur de
        // contenu necessaire et redimensionne le composant en consequence.
        void layout (int width)
        {
            float y = 0.0f;
            for (auto& it : items)
                y += itemHeight (it, (float) width) + itemGap (it);

            setSize (width, juce::roundToInt (y) + 4);
        }

        void paint (juce::Graphics& g) override
        {
            float y = 0.0f;
            const float w = (float) getWidth();

            for (auto& it : items)
            {
                switch (it.kind)
                {
                    case ColumnItem::Kind::Header:
                        g.setColour (SuppressorColours::ledBlue);
                        g.setFont (headFont);
                        g.drawFittedText (it.text, { 0, (int) y, (int) w, 18 }, juce::Justification::centredLeft, 1);
                        break;

                    case ColumnItem::Kind::Bullet:
                        g.setColour (SuppressorColours::ledAmber);
                        g.fillEllipse (0.0f, y + 6.0f, 5.0f, 5.0f);
                        // Texte indente de 12px, largeur RETIREE de ces 12px
                        // (pas de troncature a 2 lignes fixes -- hauteur
                        // reellement calculee, voir itemHeight, pour que la
                        // puce et le texte restent alignes meme si le libelle
                        // fait 3 lignes -- bug rapporte : "pas bien en face").
                        drawParagraph (g, it.text, { 12.0f, y, w - 12.0f, 0.0f }, bodyFont, SuppressorColours::textBright, 1.25f);
                        break;

                    case ColumnItem::Kind::Paragraph:
                    default:
                        drawParagraph (g, it.text, { 0.0f, y, w, 0.0f }, bodyFont, SuppressorColours::textBright, 1.3f);
                        break;
                }

                y += itemHeight (it, w) + itemGap (it);
            }
        }

    private:
        static float itemHeight (const ColumnItem& it, float width)
        {
            switch (it.kind)
            {
                case ColumnItem::Kind::Header: return 18.0f;
                case ColumnItem::Kind::Bullet:
                {
                    // Meme calcul que Paragraph, mais largeur reduite des
                    // 12px d'indentation (voir paint()) -- une puce dont le
                    // libelle fait 2-3 lignes doit reserver la vraie
                    // hauteur, sinon la puce/le texte suivants chevauchent.
                    juce::AttributedString as;
                    as.append (it.text, bodyFont, SuppressorColours::textBright);
                    as.setWordWrap (juce::AttributedString::byWord);
                    as.setLineSpacing (1.25f);
                    juce::TextLayout tl;
                    tl.createLayout (as, juce::jmax (10.0f, width - 12.0f));
                    return juce::jmax (16.0f, tl.getHeight());
                }
                case ColumnItem::Kind::Paragraph:
                default:
                {
                    juce::AttributedString as;
                    as.append (it.text, bodyFont, SuppressorColours::textBright);
                    as.setWordWrap (juce::AttributedString::byWord);
                    as.setLineSpacing (1.3f);
                    juce::TextLayout tl;
                    tl.createLayout (as, width);
                    return tl.getHeight();
                }
            }
        }

        static float itemGap (const ColumnItem& it)
        {
            switch (it.kind)
            {
                case ColumnItem::Kind::Header:    return 6.0f;
                case ColumnItem::Kind::Bullet:     return 4.0f;
                case ColumnItem::Kind::Paragraph:
                default:                            return 10.0f;
            }
        }

        static float drawParagraph (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                     juce::Font font, juce::Colour colour, float lineSpacing)
        {
            juce::AttributedString as;
            as.append (text, font, colour);
            as.setWordWrap (juce::AttributedString::byWord);
            as.setLineSpacing (lineSpacing);

            juce::TextLayout tl;
            tl.createLayout (as, area.getWidth());
            tl.draw (g, area.withHeight (tl.getHeight()));
            return area.getY() + tl.getHeight();
        }

        inline static const juce::Font bodyFont { juce::FontOptions (13.0f, juce::Font::plain) };
        inline static const juce::Font headFont { juce::FontOptions (15.0f, juce::Font::bold) };

        std::vector<ColumnItem> items;
    };

    AboutOverlay()
    {
        badgeImage = juce::ImageCache::getFromMemory (BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
        photoImage = juce::ImageCache::getFromMemory (BinaryData::mikshake_about_png, BinaryData::mikshake_about_pngSize);

        buildOverviewColumns();
        buildFeaturesAndPhilosophyColumn();

        for (auto* vp : { &viewportA, &viewportB, &viewportC })
        {
            vp->setScrollBarsShown (true, false);
            addAndMakeVisible (*vp);
        }
        viewportA.setViewedComponent (&columnA, false);
        viewportB.setViewedComponent (&columnB, false);
        viewportC.setViewedComponent (&columnC, false);

        for (auto* col : { &columnA, &columnB, &columnC })
            col->onAnyClick = [this] { if (onDismiss) onDismiss(); };

        // ESC ferme le voile (demande explicite) -- il faut le focus
        // clavier pour recevoir keyPressed(), voir visibilityChanged().
        setWantsKeyboardFocus (true);
    }

    void resized() override
    {
        auto layout = computeLayout();

        viewportA.setBounds (layout.colA.toNearestInt());
        viewportB.setBounds (layout.colB.toNearestInt());
        viewportC.setBounds (layout.colC.toNearestInt());

        // Largeur utile = largeur du viewport moins la place de la barre
        // de scroll verticale (evite le double-wrap texte/scrollbar).
        const int scrollBarW = viewportA.getScrollBarThickness();
        columnA.layout (juce::jmax (10, layout.colA.getWidth() - scrollBarW));
        columnB.layout (juce::jmax (10, layout.colB.getWidth() - scrollBarW));
        columnC.layout (juce::jmax (10, layout.colC.getWidth() - scrollBarW));

        chromeCache = juce::Image(); // force la reconstruction au prochain paint()
    }

    // Le voile n'est monte/affiche qu'occasionnellement (clic sur le
    // logo) -- des qu'il redevient visible, on force le focus clavier
    // (pour ESC) et on s'assure que le fond statique sera reconstruit a
    // la bonne taille au prochain paint (au cas ou setVisible(true) arrive
    // avant tout resize).
    void visibilityChanged() override
    {
        if (isVisible())
            grabKeyboardFocus();
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

    // OPTIMISATION CPU (demande explicite : "pour pas faire ramer le VST") :
    // tout ce qui est STATIQUE (voile noir, panneau verre + reflet + bordure,
    // en-tete, pied de page, tampon, separateurs) est desormais rasterise
    // UNE SEULE FOIS dans chromeCache, reconstruit seulement au resize (voir
    // resized()) -- paint() se contente ensuite de blitter cette image.
    // Auparavant, tout ce decor (deux gradients de panneau, un tampon avec
    // 46 ellipses, plusieurs TextLayout) etait entierement redessine a
    // CHAQUE frame de scroll des 3 colonnes (repaint() en cascade), meme si
    // rien de statique n'avait change -- c'etait la source du ralentissement.
    void paint (juce::Graphics& g) override
    {
        if (! chromeCache.isValid())
            rebuildChromeCache();

        g.drawImageAt (chromeCache, 0, 0);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (websiteBounds.contains (e.position))
        {
            juce::URL ("https://sidekey.fr").launchInDefaultBrowser();
            return;
        }

        if (manualBounds.contains (e.position))
        {
            juce::URL ("https://sidekey.fr/manuals/Tripressor_Manual_EN.pdf").launchInDefaultBrowser();
            return;
        }

        if (onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;

private:
    struct Layout
    {
        juce::Rectangle<float> headerRow, footerRow, headerDividerY, footerDividerY;
        juce::Rectangle<int> colA, colB, colC;
        float dividerX1 = 0.0f, dividerX2 = 0.0f, colTop = 0.0f, colBottom = 0.0f;
    };

    juce::Rectangle<float> panelBounds() const
    {
        auto bounds = getLocalBounds().toFloat();
        return bounds.reduced (juce::jmax (20.0f, bounds.getWidth() * 0.015f),
                                juce::jmax (12.0f, bounds.getHeight() * 0.04f));
    }

    void rebuildChromeCache()
    {
        const int w = getWidth(), h = getHeight();
        if (w < 4 || h < 4)
            return;

        chromeCache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (chromeCache);

        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRect (bounds);

        auto panel = panelBounds();
        SuppressorLookAndFeel::renderGlassPanel (g, panel, 12.0f);
        SuppressorLookAndFeel::renderGlossySheen (g, panel);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.5f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.0f);

        auto layout = computeLayout();

        // --- En-tete : monogramme + titre + tagline (gauche), version (droite) ---
        auto headerRow = layout.headerRow;

        if (! badgeImage.isNull())
        {
            auto badgeArea = headerRow.removeFromLeft (28.0f);
            const float bh = badgeArea.getHeight();
            const float bw = bh * ((float) badgeImage.getWidth() / (float) badgeImage.getHeight());
            auto badgeBounds = juce::Rectangle<float> (bw, bh).withCentre (badgeArea.getCentre());
            g.drawImage (badgeImage, (int) badgeBounds.getX(), (int) badgeBounds.getY(), (int) badgeBounds.getWidth(), (int) badgeBounds.getHeight(),
                         0, 0, badgeImage.getWidth(), badgeImage.getHeight());
            headerRow.removeFromLeft (10.0f);
        }

        auto versionArea = headerRow.removeFromRight (56.0f);
        g.setColour (SuppressorColours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("v1.7.11", versionArea.toNearestInt(), juce::Justification::centredRight, 1);

        auto titleArea = headerRow.removeFromLeft (juce::jmin (240.0f, headerRow.getWidth() * 0.48f));
        g.setColour (SuppressorColours::ledAmber);
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("ABOUT TRIPRESSOR", titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        headerRow.removeFromLeft (14.0f);
        g.setColour (SuppressorColours::ledBlue.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::italic)).withExtraKerningFactor (0.10f));
        g.drawFittedText ("Dynamics. Refined.", headerRow.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (layout.headerDividerY.getX(), layout.headerDividerY.getY(), layout.headerDividerY.getRight(), layout.headerDividerY.getY(), 1.0f);

        drawFooter (g, layout.footerRow); // met aussi a jour websiteBounds (coordonnees composant, pas image)

        g.drawLine (layout.footerDividerY.getX(), layout.footerDividerY.getY(), layout.footerDividerY.getRight(), layout.footerDividerY.getY(), 1.0f);

        // --- Separateurs verticaux entre les 3 colonnes ---
        g.setColour (SuppressorColours::gridLine);
        g.drawLine (layout.dividerX1, layout.colTop, layout.dividerX1, layout.colBottom, 1.0f);
        g.drawLine (layout.dividerX2, layout.colTop, layout.dividerX2, layout.colBottom, 1.0f);
    }

    // Meme calcul de mise en page pour paint() (lignes/texte statiques) ET
    // resized() (bounds des 3 Viewport) -- une seule source de verite pour
    // ne jamais desynchroniser les deux.
    Layout computeLayout() const
    {
        Layout L;
        auto content = panelBounds().reduced (20.0f, 12.0f);

        L.headerRow = content.removeFromTop (30.0f);
        content.removeFromTop (8.0f);
        L.headerDividerY = { content.getX(), content.getY(), content.getWidth(), 0.0f };
        content.removeFromTop (10.0f);

        L.footerRow = content.removeFromBottom (54.0f);
        content.removeFromBottom (10.0f);
        L.footerDividerY = { content.getX(), content.getBottom(), content.getWidth(), 0.0f };
        content.removeFromBottom (10.0f);

        const float gap = 18.0f;
        const float colW = (content.getWidth() - gap * 2.0f) / 3.0f;
        L.colTop = content.getY();
        L.colBottom = content.getBottom();

        auto colA = content.removeFromLeft (colW);
        content.removeFromLeft (gap);
        auto colB = content.removeFromLeft (colW);
        content.removeFromLeft (gap);
        auto colC = content;

        L.colA = colA.toNearestInt();
        L.colB = colB.toNearestInt();
        L.colC = colC.toNearestInt();
        L.dividerX1 = colA.getRight() + gap * 0.5f;
        L.dividerX2 = colB.getRight() + gap * 0.5f;

        return L;
    }

    // --- Contenu : Overview complet (fourni par l'utilisateur), reparti
    // sur les colonnes A et B. Caracteres speciaux/tirets de ponctuation
    // evites (demande explicite) -- seuls les mots composes necessaires
    // (feedforward, etc.) sont gardes en un seul mot.
    void buildOverviewColumns()
    {
        columnA.setItems ({
            { "Tripressor: Built for My Way of Mastering", ColumnItem::Kind::Header },
            { "I developed Tripressor with a very simple goal: to create the mastering compressor I have always wanted to use. "
              "A tool capable of controlling dynamics with surgical precision, while still respecting the musicality of a mix.", ColumnItem::Kind::Paragraph },
            { "My starting point was clear: preserve the transparency of a wideband compressor when working with very gentle ratios, "
              "while still being able to tame aggressive transients when a mix engineer has gone a bit heavy handed on transient shapers or envelopers.", ColumnItem::Kind::Paragraph },
            { "A feedforward architecture quickly became the obvious choice. It offers an extremely precise response, allowing Tripressor "
              "to stay clean, predictable, and flexible enough to shape dynamics exactly the way I want.", ColumnItem::Kind::Paragraph },
            { "Over the years, I have tried a huge number of mastering compressors. Some sounded great but were always missing one essential feature. "
              "Others were extremely effective, but their wideband design meant that a simple kick hit or an energetic sub would trigger compression across the entire spectrum. "
              "On the other hand, some were so transparent that the difference was only really audible on very high end monitoring systems, "
              "when 99.9% of listeners today hear their music on a phone, earbuds, or small speakers.", ColumnItem::Kind::Paragraph },
            { "Tripressor was born to close that gap.", ColumnItem::Kind::Paragraph },
            { "Inspired by the Distressor philosophy, but entirely rethought for mastering, I designed my own crossover filter from the ground up. "
              "This was probably one of the most complex parts of development, but the result was well worth it.", ColumnItem::Kind::Paragraph },
            { "Developing the two crossover filters was a major part of the project. Their goal was not just to split the bands, "
              "but to do so with the highest possible fidelity.", ColumnItem::Kind::Paragraph },
            { "The Analog crossover shows less than 0.01dB of deviation when all bands are at unity. Its gentle, slightly overlapping slopes preserve "
              "transients very naturally and give a feel close to analog processing. It is very musical, my personal favorite.", ColumnItem::Kind::Paragraph },
            { "The Linear crossover also achieves less than 0.01dB of deviation. So the difference is not in amplitude accuracy, but in behavior. "
              "Thanks to its linear phase response, it delivers much more surgical frequency separation, ideal for very modern productions or mixes where every detail needs to stay perfectly controlled.", ColumnItem::Kind::Paragraph },
            { "So the choice between the two is not about picking the more accurate mode, but the one that best matches your mastering philosophy.", ColumnItem::Kind::Paragraph },
        });

        columnB.setItems ({
            { "The compression engine was also extensively reworked. The knee curves, ratios, and detector behavior were redesigned "
              "to achieve smoother, more natural, more musical compression. Each band also gets a very subtle touch of harmonic coloration. "
              "Do not expect a metal zone effect or anything dramatic, that is not the goal of this project. This coloration is intentionally subtle "
              "and exists only to add a bit of density without compromising transparency. It is a modification of the 2nd, 3rd, 4th, and 5th order harmonics.", ColumnItem::Kind::Paragraph },
            { "A Different Approach to Multiband", ColumnItem::Kind::Header },
            { "Tripressor also features independent Dry/Wet controls per band, allowing you to blend compressed and original signal with great precision. "
              "This approach opens up a new dimension of control: you can tighten dynamic control in one frequency zone while still preserving some of the natural character of the source signal.", ColumnItem::Kind::Paragraph },
            { "Each band also has a Mono/Stereo Spread control, letting you shape stereo width in a targeted way. The goal is not to create excessive artificial "
              "width, but to add a touch of space and depth exactly where it is actually needed.", ColumnItem::Kind::Paragraph },
            { "Why Only Three Bands?", ColumnItem::Kind::Header },
            { "A question that comes up often: why not four bands?", ColumnItem::Kind::Paragraph },
            { "The choice of three bands is above all a choice for precision and musicality. Designing a high performing multiband crossover is a particularly "
              "complex exercise. Every additional band increases the difficulty of frequency separation, phase coherence, and transient preservation.", ColumnItem::Kind::Paragraph },
            { "But the main reason has to do with how multiband is actually used in mastering.", ColumnItem::Kind::Paragraph },
            { "In most situations, you rarely need to compress extreme high frequencies independently. Very high frequencies carry a lot of transient "
              "information and energy that can easily be degraded by overly aggressive processing.", ColumnItem::Kind::Paragraph },
            { "Rather than adding a fourth band that would rarely get used, I chose to build in an advanced sidechain detection system that lets you precisely "
              "define which zones trigger compression, using Bell, High Pass, and Low Pass filters.", ColumnItem::Kind::Paragraph },
            { "This approach gives you more control while better preserving transients. It lets you target a specific problem in the spectrum without creating "
              "unnecessary extra separation between bands.", ColumnItem::Kind::Paragraph },
            { "Tripressor prioritizes real world effectiveness over feature count. Every element was added with a specific purpose: to deliver surgical "
              "dynamic control while keeping the musical feel of a wideband treatment.", ColumnItem::Kind::Paragraph },
            { "I then added my Signature THD stage.", ColumnItem::Kind::Paragraph },
            { "The idea is simple: make it possible to complete a full mastering chain in a single processor, without needing to open a final limiter whose "
              "only real effect is sometimes to crush transients or degrade the signal.", ColumnItem::Kind::Paragraph },
            { "The THD module includes three independent VU meters, one per band, so you can precisely monitor saturation level. No more guessing whether "
              "you are pushing the signal too far or drifting into unwanted clipping territory.", ColumnItem::Kind::Paragraph },
            { "Tripressor was not designed to impress in a five second A/B comparison. It was designed to produce masters that translate correctly on every "
              "playback system, whether that is a pair of high end studio monitors, earbuds, a Bluetooth speaker, or a phone's built in speaker.", ColumnItem::Kind::Paragraph },
            { "At the end of the day, that is where your audience actually listens to your music.", ColumnItem::Kind::Paragraph },
        });
    }

    // --- Colonne C : Features (liste complete) + Philosophy (demande
    // explicite de regrouper les deux dans la 3eme colonne).
    void buildFeaturesAndPhilosophyColumn()
    {
        std::vector<ColumnItem> items;
        items.push_back ({ "Features", ColumnItem::Kind::Header });

        static const char* features[] =
        {
            "3 Band Feedforward Compression (Low / Mid / High)",
            "Wide (Analog) / Linear (FIR) Crossover, Switchable",
            "Crossover Amplitude Accuracy: 0.01dB, Wide and Linear",
            "7 Step Discrete LED Ratio Selection, 2:1 to 20:1",
            "Optical and Overkill Mode per Band",
            "Per Band Sidechain EQ: Low/High Cut plus Bell (dual Bell on Mid)",
            "Stereo Detector Link per Band",
            "Audio Tube and Fuzz Colouration per Band",
            "Per Band Solo",
            "THD Valve: Soft / Tape / Iron / Air Mastering Saturation, One Shared Stage",
            "True Peak Safety Ceiling on the THD Valve",
            "Dual Needle VU (Input / Output / THD) plus 3 Per Band THD Needles",
            "Global Power with Full Unit Dim",
            "Link In / Link Out for Ganged Input/Output Trim",
            "Factory and User Preset Browser, with a Genuine Default Preset",
            "Resizable UI (75% to 100%), Locked Against Accidental Host Fullscreen",
            "Calibrated I/O, 0.2dB Linearity, Hand Measured",
            "AI Learn: Listens to Your Program Material and Calculates Compression, Expansion, "
            "Sidechain Detection and Crossover Settings, with Loudness Targeting for Streaming Platform Norms",
            "Independent Upward Expansion per Band (Comp / Expand), with its Own Ratio, Attack, "
            "Release and Range, Running Alongside Compression at All Times"
        };
        for (auto* f : features)
            items.push_back ({ f, ColumnItem::Kind::Bullet });

        // Meme texte que Nuke Mext (demande explicite : "reprendre le meme
        // texte de philosophy que le mext"), sans le paragraphe sur la
        // calibration input/output deja couvert par la description et les
        // features (demande explicite de le retirer, redondant).
        items.push_back ({ "The Side Key System Philosophy", ColumnItem::Kind::Header });
        items.push_back ({ "At Side Key System, calibration is not a final adjustment. It is part of the engineering process itself.", ColumnItem::Kind::Paragraph });
        items.push_back ({ "Every tool is carefully designed, measured, tested and calibrated with meticulous attention to precision, stability and "
                            "measurement consistency. Every control is tuned to deliver predictable behavior, repeatable results and a workflow you can "
                            "trust, whether working on a single track or a full mastering session.", ColumnItem::Kind::Paragraph });
        items.push_back ({ "The objective is simple: create professional tools that behave like precision instruments, where every parameter has a "
                            "purpose and every decision serves the music.", ColumnItem::Kind::Paragraph });

        columnC.setItems (std::move (items));
    }

    // Pied de page : photo (gauche) + lien site + tampon "Calibrated"
    // (droite), sur une seule rangee -- garde tout "en longueur" au lieu
    // d'empiler ces elements verticalement.
    void drawFooter (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (! photoImage.isNull())
        {
            const float photoH = area.getHeight();
            const float photoW = photoH * ((float) photoImage.getWidth() / (float) photoImage.getHeight());
            auto photoBounds = juce::Rectangle<float> (photoW, photoH).withCentre (area.getCentre()).withX (area.getX());

            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (photoBounds, 6.0f);
            g.reduceClipRegion (clip);
            g.drawImage (photoImage, (int) photoBounds.getX(), (int) photoBounds.getY(), (int) photoBounds.getWidth(), (int) photoBounds.getHeight(),
                         0, 0, photoImage.getWidth(), photoImage.getHeight());
            g.restoreState();

            g.setColour (SuppressorColours::ledAmber.withAlpha (0.4f));
            g.drawRoundedRectangle (photoBounds, 6.0f, 1.0f);

            area.removeFromLeft (photoBounds.getWidth() + 16.0f);
        }

        auto stampArea = area.removeFromRight (58.0f);
        area.removeFromRight (14.0f);
        drawStamp (g, stampArea);

        auto copyrightRow = area.removeFromBottom (13.0f);
        drawWebsiteLink (g, area);

        g.setColour (SuppressorColours::textDim.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::plain)).withExtraKerningFactor (0.04f));
        g.drawFittedText ("(c) 2026 Side Key System. All rights reserved.   Made in France",
                           copyrightRow.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // Effet "tampon" (cachet) pour le badge de calibration : double anneau
    // legerement rotate, texte interieur, et un grain deterministe pour
    // simuler une encre de tampon use -- pas juste une icone + texte plat.
    void drawStamp (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto centre = area.getCentre();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const juce::Colour ink = SuppressorColours::ledRed.withAlpha (0.82f);

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-8.0f), centre.x, centre.y));

        g.setColour (ink);
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.8f);
        g.drawEllipse (centre.x - r * 0.82f, centre.y - r * 0.82f, r * 1.64f, r * 1.64f, 0.9f);

        g.setFont (juce::Font (juce::FontOptions (r * 0.30f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("CALIBRATED",
                           juce::Rectangle<float> (centre.x - r * 0.78f, centre.y - r * 0.52f, r * 1.56f, r * 0.4f).toNearestInt(),
                           juce::Justification::centred, 1);
        g.setFont (juce::Font (juce::FontOptions (r * 0.20f, juce::Font::bold)).withExtraKerningFactor (0.04f));
        g.drawFittedText ("SIDE KEY", juce::Rectangle<float> (centre.x - r * 0.7f, centre.y + r * 0.06f, r * 1.4f, r * 0.32f).toNearestInt(),
                           juce::Justification::centred, 1);
        g.drawFittedText ("SYSTEM", juce::Rectangle<float> (centre.x - r * 0.7f, centre.y + r * 0.36f, r * 1.4f, r * 0.3f).toNearestInt(),
                           juce::Justification::centred, 1);

        // Grain "encre use" deterministe (pas de Random, donc jamais de
        // scintillement si le panneau est repeint plusieurs fois)
        for (int i = 0; i < 46; ++i)
        {
            const float ang = (float) i * 0.151f;
            const float rr = r * (0.32f + 0.66f * std::abs (std::sin ((float) i * 2.3f)));
            const float px = centre.x + std::cos (ang) * rr;
            const float py = centre.y + std::sin (ang) * rr;
            g.setColour (ink.withAlpha (0.16f * std::abs (std::sin ((float) i * 1.7f))));
            g.fillEllipse (px - 0.6f, py - 0.6f, 1.2f, 1.2f);
        }

        g.restoreState();
    }

    // Deux liens cote a cote (sidekey.fr, puis Manual -- demande explicite :
    // "dans about en bas ou il y a le site tu ajoute le lien vers le
    // manual") -- chacun garde son propre rectangle cliquable (websiteBounds/
    // manualBounds), verifies separement dans mouseDown().
    void drawWebsiteLink (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto row = area.withSizeKeepingCentre (area.getWidth(), 16.0f);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.04f));

        juce::GlyphArrangement gaSite;
        gaSite.addLineOfText (g.getCurrentFont(), "sidekey.fr", 0.0f, 0.0f);
        const float siteW = gaSite.getBoundingBox (0, -1, true).getWidth();

        websiteBounds = row.withWidth (siteW);
        g.setColour (SuppressorColours::ledBlue);
        g.drawFittedText ("sidekey.fr", websiteBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        g.drawLine (websiteBounds.getX(), websiteBounds.getBottom() - 3.0f,
                    websiteBounds.getX() + siteW, websiteBounds.getBottom() - 3.0f, 1.0f);

        const float sepW = 18.0f;
        juce::GlyphArrangement gaManual;
        gaManual.addLineOfText (g.getCurrentFont(), "Manual", 0.0f, 0.0f);
        const float manualW = gaManual.getBoundingBox (0, -1, true).getWidth();

        manualBounds = row.withX (websiteBounds.getRight() + sepW).withWidth (manualW);
        g.setColour (SuppressorColours::textDim.withAlpha (0.5f));
        g.drawFittedText ("|", juce::Rectangle<float> (websiteBounds.getRight(), row.getY(), sepW, row.getHeight()).toNearestInt(),
                           juce::Justification::centred, 1);
        g.setColour (SuppressorColours::ledAmber);
        g.drawFittedText ("Manual", manualBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        g.drawLine (manualBounds.getX(), manualBounds.getBottom() - 3.0f,
                    manualBounds.getX() + manualW, manualBounds.getBottom() - 3.0f, 1.0f);
    }

    juce::Image badgeImage;
    juce::Image photoImage;
    juce::Image chromeCache;
    juce::Rectangle<float> websiteBounds;
    juce::Rectangle<float> manualBounds;

    juce::Viewport viewportA, viewportB, viewportC;
    ParagraphColumn columnA, columnB, columnC;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};
