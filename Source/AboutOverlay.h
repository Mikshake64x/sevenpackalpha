#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "AlphaSkin.h"
#include "LicenseManager.h"

/**
    AboutOverlay
    ------------
    Voile plein ecran affiche au clic sur le logo Sevenpack.

    Bandeau "Duo en tete" (variante A de l'apercu
    claude.ai/artifact/47SvX1upNG8o8KkigfRitX, demande explicite : "il y a
    moi Mikshake le coder et Sunny le designer") : sous l'en-tete, les deux
    portraits (noir et blanc, nom + role en legende) et une courte
    presentation, puis les 3 colonnes de texte. L'ancienne photo du pied de
    page et l'image de la colonne du milieu sont masquees (kShowFooterPhoto,
    kShowSplashImage).

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
class AboutOverlay : public juce::Component, private juce::Timer
{
public:
    // Un item de colonne : paragraphe justifie, sous-titre en gras, ou
    // puce (style liste Features). ParagraphColumn calcule sa PROPRE
    // hauteur de contenu (setSize) a partir de sa largeur -- c'est ce qui
    // permet au juce::Viewport parent de savoir jusqu'ou scroller.
    struct ColumnItem
    {
        // Image : photo pleine largeur de colonne, hauteur deduite de son
        // ratio d'aspect (voir itemHeight()) -- text/image mutuellement
        // exclusifs selon kind, pas besoin d'un union, un seul item de plus
        // par colonne au total (voir buildOverviewColumns()).
        enum class Kind { Paragraph, Header, Bullet, Image };
        juce::String text;
        Kind kind = Kind::Paragraph;
        juce::Image image;
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
                        g.setColour (AlphaPalette::get().heading());
                        g.setFont (headFont);
                        g.drawFittedText (it.text, { 0, (int) y, (int) w, 18 }, juce::Justification::centredLeft, 1);
                        break;

                    case ColumnItem::Kind::Bullet:
                        g.setColour (AlphaPalette::get().heading());
                        g.fillEllipse (0.0f, y + 6.0f, 5.0f, 5.0f);
                        // Texte indente de 12px, largeur RETIREE de ces 12px
                        // (pas de troncature a 2 lignes fixes -- hauteur
                        // reellement calculee, voir itemHeight, pour que la
                        // puce et le texte restent alignes meme si le libelle
                        // fait 3 lignes -- bug rapporte : "pas bien en face").
                        drawParagraph (g, it.text, { 12.0f, y, w - 12.0f, 0.0f }, bodyFont, AlphaPalette::get().sheetText(), 1.25f);
                        break;

                    case ColumnItem::Kind::Paragraph:
                    default:
                        drawParagraph (g, it.text, { 0.0f, y, w, 0.0f }, bodyFont, AlphaPalette::get().sheetText(), 1.3f);
                        break;

                    case ColumnItem::Kind::Image:
                        if (! it.image.isNull())
                        {
                            // Pleine largeur de colonne, AUCUNE marge/bordure
                            // decorative (retour utilisateur : "assez gros,
                            // avec le minimum de zone morte autour") -- juste
                            // l'image, hauteur deduite de son ratio d'aspect
                            // (voir itemHeight()).
                            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                            const float h = itemHeight (it, w);
                            g.drawImage (it.image, 0.0f, y, w, h, 0, 0, it.image.getWidth(), it.image.getHeight());
                        }
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
                case ColumnItem::Kind::Image:
                    return it.image.isNull() ? 0.0f
                        : width * (float) it.image.getHeight() / (float) juce::jmax (1, it.image.getWidth());
                case ColumnItem::Kind::Bullet:
                {
                    // Meme calcul que Paragraph, mais largeur reduite des
                    // 12px d'indentation (voir paint()) -- une puce dont le
                    // libelle fait 2-3 lignes doit reserver la vraie
                    // hauteur, sinon la puce/le texte suivants chevauchent.
                    juce::AttributedString as;
                    as.append (it.text, bodyFont, AlphaPalette::get().sheetText());
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
                    as.append (it.text, bodyFont, AlphaPalette::get().sheetText());
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
        // Variante BLANCHE (pas la noire utilisee sous le logo SEVENPACK sur
        // la barre de controle claire) -- ce voile About est sombre (voile
        // noir + panneau verre, voir rebuildChromeCache()), une signature
        // noire y serait quasi invisible. Construite depuis le PNG maitre
        // via le meme pipeline de qualite que PluginEditor.cpp (retour
        // utilisateur : "ca fait une soupe de pixels" -- voir
        // SevenpackLookAndFeel::buildSignatureImage()).
        signatureImage = SevenpackLookAndFeel::buildSignatureImage (AlphaPalette::get().light ? juce::Colours::black : juce::Colours::white, 55);
        splashImage = juce::ImageCache::getFromMemory (BinaryData::Mikshakesplash_png, BinaryData::Mikshakesplash_pngSize);

        // Portraits du duo, en noir et blanc (choix par defaut de l'apercu :
        // harmonise les deux photos). Copies : desaturate() modifie l'image
        // et celles de l'ImageCache sont partagees.
        const auto loadPortrait = [] (const void* data, int size)
        {
            auto img = juce::ImageCache::getFromMemory (data, size).createCopy();
            img.desaturate();
            return img;
        };
        team[0] = { loadPortrait (BinaryData::mikshake_about_crop_jpg, BinaryData::mikshake_about_crop_jpgSize), "MIKSHAKE", "CODE" };
        team[1] = { loadPortrait (BinaryData::sunny_crop_jpg, BinaryData::sunny_crop_jpgSize), "SUNNY", "DESIGN" };

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

        // Voile (fondu) puis panneau (zoom + glissement), voir animation.
        AlphaDraw::overlayVeil (g, getLocalBounds().toFloat(), animOpacity);
        g.saveState();
        g.setOpacity (animOpacity);
        g.addTransform (panelTransform());
        g.drawImageAt (chromeCache, 0, 0);
        g.restoreState();
    }

    // --- Apparition / disparition animees (demande explicite : "une
    // petite animation sympathique pour faire apparaitre about et le faire
    // disparaitre. Il arrive trop comme une fenetre") : le voile se fond,
    // le panneau grandit de 92 % a 100 % en montant de 16 px avec un leger
    // rebond (280 ms) ; a la fermeture il redescend et se fond (170 ms). ---
    void showAnimated()
    {
        refreshTheme();
        setVisible (true);
        toFront (true);
        startAnimation (true);
    }

    void hideAnimated()
    {
        if (isVisible() && ! (animating && ! animatingIn))
            startAnimation (false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (animating && ! animatingIn)
            return; // deja en train de se fermer
        if (websiteBounds.contains (e.position))
        {
            juce::URL ("https://sidekeysystem.com").launchInDefaultBrowser();
            return;
        }

        if (manualBounds.contains (e.position))
        {
            juce::URL ("https://sidekey.fr/manuals/Sevenpack_Manual_EN.pdf").launchInDefaultBrowser();
            return;
        }

        if (licenseBounds.contains (e.position))
        {
            if (onOpenLicense)
                onOpenLicense();
            return;
        }

        if (onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;
    std::function<void()> onOpenLicense;

    // Set once by the editor so the footer can show either the active
    // license key or the remaining demo days instead of a static label.
    void setLicenseManager (LicenseManager& lm) { license = &lm; }

    // Called by the editor's theme toggle -- chromeCache is only rebuilt
    // from resized() otherwise, so a theme switch while this overlay is
    // hidden (or already shown) left it stuck on the stale bitmap until
    // the whole plugin was reopened.
    void refreshTheme()
    {
        // Signature noire sur le panneau clair, blanche sur le sombre.
        signatureImage = SevenpackLookAndFeel::buildSignatureImage (AlphaPalette::get().light ? juce::Colours::black
                                                                                             : juce::Colours::white, 55);
        AlphaDraw::styleControls (*this);
        chromeCache = juce::Image();
        repaint();
    }

private:
    juce::AffineTransform panelTransform() const
    {
        const auto c = panelBounds().getCentre();
        const float s = 0.92f + 0.08f * animScale;
        return juce::AffineTransform::scale (s, s, c.x, c.y).translated (0.0f, (1.0f - animScale) * 16.0f);
    }

    void startAnimation (bool in)
    {
        animatingIn = in;
        animating = true;
        animStartMs = juce::Time::getMillisecondCounterHiRes();
        animFrom = animOpacity;
        startTimerHz (60);
        timerCallback();
    }

    void timerCallback() override
    {
        const double dur = animatingIn ? 280.0 : 170.0;
        const float t = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - animStartMs) / dur);
        if (animatingIn)
        {
            constexpr float c1 = 1.2f, c3 = c1 + 1.0f; // rebond discret
            const float back = 1.0f + c3 * std::pow (t - 1.0f, 3.0f) + c1 * std::pow (t - 1.0f, 2.0f);
            animOpacity = animFrom + (1.0f - animFrom) * (1.0f - (1.0f - t) * (1.0f - t));
            animScale = back;
        }
        else
        {
            const float e = t * t; // acceleration vers la sortie
            animOpacity = animFrom * (1.0f - e);
            animScale = 1.0f - e;
        }

        const auto tr = panelTransform();
        for (auto* vp : { &viewportA, &viewportB, &viewportC })
        {
            vp->setTransform (tr);
            vp->setAlpha (animOpacity);
        }
        repaint();

        if (t >= 1.0f)
        {
            stopTimer();
            animating = false;
            if (! animatingIn)
            {
                setVisible (false);
                animOpacity = 0.0f;
            }
            else
            {
                animOpacity = animScale = 1.0f;
                for (auto* vp : { &viewportA, &viewportB, &viewportC })
                {
                    vp->setTransform ({});
                    vp->setAlpha (1.0f);
                }
            }
        }
    }

    bool animating = false, animatingIn = false;
    double animStartMs = 0.0;
    float animOpacity = 1.0f, animScale = 1.0f, animFrom = 0.0f;

    struct Layout
    {
        juce::Rectangle<float> headerRow, footerRow, headerDividerY, footerDividerY, teamRow, teamDividerY;
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

        // Panneau clair Alpha SEUL (le voile est peint a part dans paint(),
        // pour pouvoir le fondre independamment pendant l'animation).
        auto panel = panelBounds();
        AlphaDraw::overlaySheet (g, panel, 12.0f);

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
        g.setColour (AlphaPalette::get().sheetTextSoft());
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("v2.14.0", versionArea.toNearestInt(), juce::Justification::centredRight, 1);

        g.setColour (AlphaPalette::get().heading());
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        // "ABOUT TRIPRESSOR" -> "ABOUT SEVENPACK" (retour utilisateur : "il
        // faut reecrire la about en fonction de ce qu'est Sevenpack") puis
        // "ABOUT SEVENPACK ALPHA" (nouvelle page About, apercu du duo).
        const juce::String title ("ABOUT SEVENPACK ALPHA");
        const float titleW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 4.0f;
        auto titleArea = headerRow.removeFromLeft (juce::jmin (titleW, headerRow.getWidth() * 0.62f));
        g.drawFittedText (title, titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        headerRow.removeFromLeft (14.0f);
        g.setColour (AlphaPalette::get().heading().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::italic)).withExtraKerningFactor (0.10f));
        g.drawFittedText ("Dynamics. Refined.", headerRow.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (AlphaPalette::get().cardLine);
        g.drawLine (layout.headerDividerY.getX(), layout.headerDividerY.getY(), layout.headerDividerY.getRight(), layout.headerDividerY.getY(), 1.0f);

        drawTeam (g, layout.teamRow);
        g.setColour (AlphaPalette::get().cardLine);
        g.drawLine (layout.teamDividerY.getX(), layout.teamDividerY.getY(), layout.teamDividerY.getRight(), layout.teamDividerY.getY(), 1.0f);

        drawFooter (g, layout.footerRow); // met aussi a jour websiteBounds (coordonnees composant, pas image)

        g.drawLine (layout.footerDividerY.getX(), layout.footerDividerY.getY(), layout.footerDividerY.getRight(), layout.footerDividerY.getY(), 1.0f);

        // --- Separateurs verticaux entre les 3 colonnes ---
        g.setColour (AlphaPalette::get().cardLine);
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

        // Bandeau du duo (portraits + presentation), puis un filet.
        L.teamRow = content.removeFromTop (kTeamRowHeight);
        content.removeFromTop (14.0f);
        L.teamDividerY = { content.getX(), content.getY(), content.getWidth(), 0.0f };
        content.removeFromTop (14.0f);

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
            { "SEVENPACK: Seven Bands, One Mastering Chain", ColumnItem::Kind::Header },
            { "I designed SEVENPACK with an experimental goal above all else. After many attempts at building multiband compressors, "
              "I started multiplying the bands right from the earliest stages of development, with a fairly simple idea: chase the "
              "best possible mastering, even if it meant making the CPU cry.", ColumnItem::Kind::Paragraph },
            { "The goal was to push the boundaries of multiband processing and explore just how far dynamic control could go without "
              "turning the mix into a spectral puzzle.", ColumnItem::Kind::Paragraph },
            { "I already have several ideas for what comes next, especially as processors keep bulking up on steroids. But for now, "
              "I want SEVENPACK to stay accessible and usable across a wide range of hardware configurations.", ColumnItem::Kind::Paragraph },
            { "The principle itself is fairly simple: 7 bands, in the philosophy of Tripressor, with a feedforward compressor built "
              "on Tripressor's current DSP, and a crossover designed to be as transparent as possible.", ColumnItem::Kind::Paragraph },
            { "With THD disabled, the measured deviation reaches about 0.001dB. In other words, the crossover tries very hard not to "
              "be noticed. It does its job and goes back to its office.", ColumnItem::Kind::Paragraph },
            { "The other distinctive trait of SEVENPACK is the way the compressor and the expander work in tandem. The goal is to "
              "optimize mastering dynamics without needlessly losing energy after every release phase.", ColumnItem::Kind::Paragraph },
            { "The result is compression that can be extremely natural, or, at the other end of the scale, much more aggressive, all "
              "while keeping maximum energy for LUFS.", ColumnItem::Kind::Paragraph },
            { "In short, SEVENPACK lets you compress hard, very hard, or not at all, while trying to preserve what actually matters: "
              "the energy of the mix.", ColumnItem::Kind::Paragraph },
            { "And yes: seven bands, SEVENPACK. For once, the name tells the whole truth.", ColumnItem::Kind::Paragraph },
        });

        std::vector<ColumnItem> learnItems {
            { "Learn: A Starting Point, Not an Autopilot", ColumnItem::Kind::Header },
            { "I also built in a Learn module, designed to automatically provide a coherent starting point for compression and "
              "expansion.", ColumnItem::Kind::Paragraph },
            { "Because with seven bands, finding the right attack and release for every single one can quickly turn into a small "
              "exercise in mental torture. Eventually you end up practically negotiating with each transient.", ColumnItem::Kind::Paragraph },
            { "The Learn module analyzes the behavior of the signal to suggest a coherent starting point for your dynamics settings. "
              "The goal, obviously, is not to replace your ears, but to save you time and spare you from starting on a blank page.", ColumnItem::Kind::Paragraph },
            { "I naturally encourage you to experiment from there and find whatever sounds the most musical and professional for "
              "your mix and your own approach to mastering.", ColumnItem::Kind::Paragraph },
            { "With this many bands and this many things to tweak, the Learn module felt like an almost indispensable extension of "
              "SEVENPACK.", ColumnItem::Kind::Paragraph },
            { "Seven bands, two dynamics processors per band, and one button to try and put a little order back into all of it.", ColumnItem::Kind::Paragraph },
        };
        // Photo (retour utilisateur : "voici la nouvelle photo a mettre
        // dans about ... colonne du centre ... assez gros, avec le
        // minimum de zone morte autour", puis "il faut que la photo ...
        // soit sous le texte pas au dessus") -- pleine largeur de
        // colonne, en fin de colonne, apres le texte Learn. Masquee depuis
        // le bandeau du duo (gardee dans le code).
        if constexpr (kShowSplashImage)
            learnItems.push_back ({ "", ColumnItem::Kind::Image, splashImage });
        columnB.setItems (std::move (learnItems));
    }

    // Bandeau du duo : deux portraits (legende nom + role sur un degrade
    // sombre), puis la presentation a droite, centree verticalement.
    void drawTeam (juce::Graphics& g, juce::Rectangle<float> row) const
    {
        constexpr float cardW = 200.0f, radius = 10.0f;
        for (const auto& person : team)
        {
            const auto card = row.removeFromLeft (cardW);
            row.removeFromLeft (16.0f);

            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (card, radius);
            g.reduceClipRegion (clip);
            g.setColour (juce::Colour (0xff111111));
            g.fillRect (card);

            if (! person.photo.isNull())
            {
                // Remplissage "cover", cadrage un peu au-dessus du centre
                // (visages).
                const auto& img = person.photo;
                const float scale = juce::jmax (card.getWidth() / (float) img.getWidth(), card.getHeight() / (float) img.getHeight());
                const float w = (float) img.getWidth() * scale, h = (float) img.getHeight() * scale;
                const auto dest = juce::Rectangle<float> (card.getX() + (card.getWidth() - w) * 0.5f,
                                                          card.getY() + (card.getHeight() - h) * 0.3f, w, h);
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (img, dest);
            }

            const auto capArea = card.withTrimmedTop (card.getHeight() - 72.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, capArea.getY(),
                                                     juce::Colours::black.withAlpha (0.72f), 0.0f, capArea.getBottom(), false));
            g.fillRect (capArea);
            auto text = card.reduced (14.0f, 12.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::plain)).withExtraKerningFactor (0.12f));
            g.drawFittedText (person.role, text.removeFromBottom (15.0f).toNearestInt(), juce::Justification::bottomLeft, 1);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)).withExtraKerningFactor (0.14f));
            g.drawFittedText (person.name, text.removeFromBottom (24.0f).toNearestInt(), juce::Justification::bottomLeft, 1);
        }

        // Presentation.
        const auto pal = AlphaPalette::get();
        auto intro = row.withTrimmedLeft (10.0f).withTrimmedRight (10.0f);
        juce::AttributedString as;
        as.append ("Two people, seven bands\n", juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), pal.heading());
        as.append ("SEVENPACK ALPHA is made by two people: Mikshake writes the code, Sunny designs everything you see.\n",
                   juce::Font (juce::FontOptions (13.0f, juce::Font::plain)), pal.sheetText());
        as.append ("Dynamics. Refined.", juce::Font (juce::FontOptions (13.0f, juce::Font::plain)), pal.sheetTextSoft());
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (6.0f);
        juce::TextLayout tl;
        tl.createLayout (as, juce::jmin (intro.getWidth(), 620.0f));
        tl.draw (g, intro.withSizeKeepingCentre (intro.getWidth(), tl.getHeight()).withWidth (tl.getWidth()));
    }

    // --- Colonne C : Features (liste complete) + Philosophy (demande
    // explicite de regrouper les deux dans la 3eme colonne).
    void buildFeaturesAndPhilosophyColumn()
    {
        std::vector<ColumnItem> items;
        items.push_back ({ "Features", ColumnItem::Kind::Header });

        // Liste reecrite pour l'architecture 6 bandes actuelle (retour
        // utilisateur explicite : "les features correspondantes, la
        // deviation a 0.001db") -- l'ancienne liste datait du portage 3
        // bandes Tripressor. Verifiee contre les vrais identifiants de
        // parametres (PluginProcessor.h) : NumBands=6/NumCrossovers=5,
        // fusion de bandes via idCrossoverEnabled (feature reelle, absente
        // de l'ancienne liste), pas de filtre passe-bas cote detecteur SAUF
        // sur la derniere bande (comp.setDetectorHpIsLowPass(band==High)) --
        // l'ancien "Low/High Cut plus Bell" et "Bell, High Pass, and Low
        // Pass filters" etaient inexacts, corriges ici.
        static const char* features[] =
        {
            "7 Band Feedforward Compression (Low / LowMid / Mid / HighMid / Presence / High / Air)",
            "6 Point Crossover, Every Point Independently Foldable to Fuse Adjacent Bands",
            "Wide (Analog) / Linear (FIR) Crossover, Switchable",
            "Crossover Amplitude Accuracy: 0.001dB, Wide and Linear",
            "7 Step Discrete LED Ratio Selection, 2:1 to 20:1",
            "Optical and Overkill Mode per Band",
            "Stereo Detector Link per Band",
            "Per Band Solo",
            "Separate Outputs: Route Any Band to its Own Stereo Pair, Split from the Main Mix "
            "(Named Buses in a Host, Sound Card Output Pairs in the Standalone)",
            "THD Valve: Soft / Tape / Iron / Air Mastering Saturation, One Shared Stage",
            "Adjustable True Peak Output Ceiling on the THD Valve, -0.5 to -3 dB",
            "Dual Needle VU (Input / Output / THD) plus One THD Needle per Band",
            "Link In / Link Out for Ganged Input/Output Trim",
            "Factory and User Preset Browser, with a Genuine Default Preset",
            "Resizable UI (75% to 100%), Locked Against Accidental Host Fullscreen",
            "Calibrated I/O, 0.001dB Linearity, Hand Measured",
            "AI Learn: Listens to Your Program Material and Calculates Compression, Expansion "
            "and Crossover Settings, with Loudness Targeting for Streaming Platform Norms",
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
        if (kShowFooterPhoto && ! photoImage.isNull())
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

            g.setColour (AlphaPalette::get().heading().withAlpha (0.4f));
            g.drawRoundedRectangle (photoBounds, 6.0f, 1.0f);

            area.removeFromLeft (photoBounds.getWidth() + 16.0f);
        }

        // Signature, fond transparent, juste apres la photo (retour
        // utilisateur explicite : "dans la page about dans le cadre du bas
        // ou il y a la ma photo et les licences") -- construite depuis le
        // PNG maitre via SevenpackLookAndFeel::buildSignatureImage() (voir
        // constructeur), dessinee ici en 1:1 EXACT (drawImageAt, aucun
        // resample supplementaire). Variante BLANCHE (voile sombre ici).
        if (! signatureImage.isNull())
        {
            auto sigBounds = signatureImage.getBounds().toFloat().withCentre (area.getCentre()).withX (area.getX());
            g.drawImageAt (signatureImage, (int) sigBounds.getX(), (int) sigBounds.getY());

            area.removeFromLeft (sigBounds.getWidth() + 16.0f);
        }

        auto stampArea = area.removeFromRight (58.0f);
        area.removeFromRight (14.0f);
        drawStamp (g, stampArea);

        auto copyrightRow = area.removeFromBottom (13.0f);
        drawWebsiteLink (g, area);

        g.setColour (AlphaPalette::get().sheetTextSoft().withAlpha (0.75f));
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
        const juce::Colour ink = SevenpackColours::ledRed.withAlpha (0.82f);

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
        gaSite.addLineOfText (g.getCurrentFont(), "sidekeysystem.com", 0.0f, 0.0f);
        const float siteW = gaSite.getBoundingBox (0, -1, true).getWidth();

        websiteBounds = row.withWidth (siteW);
        g.setColour (AlphaPalette::get().heading());
        g.drawFittedText ("sidekeysystem.com", websiteBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        g.drawLine (websiteBounds.getX(), websiteBounds.getBottom() - 3.0f,
                    websiteBounds.getX() + siteW, websiteBounds.getBottom() - 3.0f, 1.0f);

        const float sepW = 18.0f;
        juce::GlyphArrangement gaManual;
        gaManual.addLineOfText (g.getCurrentFont(), "Manual", 0.0f, 0.0f);
        const float manualW = gaManual.getBoundingBox (0, -1, true).getWidth();

        manualBounds = row.withX (websiteBounds.getRight() + sepW).withWidth (manualW);
        g.setColour (AlphaPalette::get().sheetTextSoft().withAlpha (0.5f));
        g.drawFittedText ("|", juce::Rectangle<float> (websiteBounds.getRight(), row.getY(), sepW, row.getHeight()).toNearestInt(),
                           juce::Justification::centred, 1);
        g.setColour (AlphaPalette::get().heading());
        g.drawFittedText ("Manual", manualBounds.toNearestInt(), juce::Justification::centredLeft, 1);
        g.drawLine (manualBounds.getX(), manualBounds.getBottom() - 3.0f,
                    manualBounds.getX() + manualW, manualBounds.getBottom() - 3.0f, 1.0f);

        // License status link, same "|" separator style as Manual — shows
        // the active key or the remaining demo days, opens the license
        // manager (LicenseOverlay) even if already licensed/demoing. No
        // underline (can show a full key, underline reads as cluttered).
        juce::String licenseText = "License";
        if (license != nullptr)
        {
            auto key = license->getLicenseKey();
            if (key.isNotEmpty())
                licenseText = key;
            else if (license->getStatus() == LicenseManager::Status::trial)
                licenseText = "Demo - " + juce::String (license->getTrialDaysLeft()) + " day(s) left";
        }

        juce::GlyphArrangement gaLicense;
        gaLicense.addLineOfText (g.getCurrentFont(), licenseText, 0.0f, 0.0f);
        const float licenseW = gaLicense.getBoundingBox (0, -1, true).getWidth();

        licenseBounds = row.withX (manualBounds.getRight() + sepW).withWidth (licenseW);
        g.setColour (AlphaPalette::get().sheetTextSoft().withAlpha (0.5f));
        g.drawFittedText ("|", juce::Rectangle<float> (manualBounds.getRight(), row.getY(), sepW, row.getHeight()).toNearestInt(),
                           juce::Justification::centred, 1);
        g.setColour (AlphaPalette::get().heading());
        g.drawFittedText (licenseText, licenseBounds.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    juce::Image badgeImage;
    juce::Image photoImage;
    juce::Image signatureImage;
    juce::Image splashImage;

    // Duo (bandeau sous l'en-tete, voir drawTeam()).
    struct TeamMember { juce::Image photo; juce::String name, role; };
    std::array<TeamMember, 2> team;
    static constexpr float kTeamRowHeight = 250.0f;
    // Anciennes images remplacees par le bandeau du duo, gardees dans le code.
    static constexpr bool kShowFooterPhoto = false;
    static constexpr bool kShowSplashImage = false;
    juce::Image chromeCache;
    juce::Rectangle<float> websiteBounds;
    juce::Rectangle<float> manualBounds;
    juce::Rectangle<float> licenseBounds;
    LicenseManager* license = nullptr;

    juce::Viewport viewportA, viewportB, viewportC;
    ParagraphColumn columnA, columnB, columnC;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};
