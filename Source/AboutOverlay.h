#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "AlphaSkin.h"
#include "LicenseManager.h"
#include "BuildLabel.h"

/**
    AboutOverlay
    ------------
    Voile plein ecran affiche au clic sur le logo Sevenpack.

    Mise en page reprise du About de Merlin (demande explicite : "on va
    reprendre le about de merlin. Mikshake c'est Developpement et il manque
    la signature de sunny") :
      - en-tete : monogramme Side Key, ABOUT, logo 7PACK + pastille alpha,
        accroche en italique, version a droite ;
      - bandeau du duo : portraits noir et blanc (MIKSHAKE / DEVELOPMENT,
        SUNNY / DESIGN), legende a l'echelle de la carte, hauteur reduite
        sur une petite fenetre ; presentation + devise en italique a
        empattements ;
      - 3 colonnes de texte defilantes (Viewport), paragraphes justifies mot
        a mot ;
      - pied de page : les DEUX signatures (Mikshake, Sunny Jim) teintees au
        theme, liens site | Manual | licence (main au survol), copyright,
        tampon "Calibrated".
    Les anciens elements sont gardes derriere des drapeaux (kShowFooterPhoto,
    kShowSplashImage, kLegacySignature, kRedStamp).

    Tout le decor statique est rendu UNE fois dans chromeCache, a la
    resolution physique de l'ecran (4K, taille 200 %) ; paint() ne fait que
    le recopier (defilement et animation sans redessiner photos et ombres).
    Se ferme au clic n'importe ou ou a Echap, sauf sur les liens.
*/
class AboutOverlay : public juce::Component, private juce::Timer
{
public:
    // Un item de colonne : paragraphe justifie, sous-titre, puce, ou image
    // pleine largeur (hauteur deduite du ratio, voir kShowSplashImage).
    struct ColumnItem
    {
        enum class Kind { Paragraph, Header, Bullet, Image };
        juce::String text;
        Kind kind = Kind::Paragraph;
        juce::Image image;
    };

    // Police des Options (Segoe UI / sans-serif systeme), tailles en points
    // comme Merlin (13 = corps de texte). Creee a la demande, JAMAIS en
    // variable statique : une juce::Font statique est detruite apres l'arret
    // de JUCE, une cause connue de plantage a la fermeture sur Mac.
    static juce::Font aboutFont (float pt, int weight = 0, bool italic = false)
    {
       #if JUCE_WINDOWS
        const juce::String name ("Segoe UI");
       #else
        const juce::String name (juce::Font::getDefaultSansSerifFontName());
       #endif
        const char* style = italic ? (weight > 0 ? "Bold Italic" : "Italic")
                                   : (weight >= 2 ? "Bold" : weight == 1 ? "Semibold" : "Regular");
        return juce::Font (juce::FontOptions().withName (name).withStyle (style).withPointHeight (pt));
    }

    class ParagraphColumn : public juce::Component
    {
    public:
        // Un clic simple sur le texte ferme aussi le voile (le defilement se
        // fait a la molette / barre, aucun conflit).
        std::function<void()> onAnyClick;

        void mouseDown (const juce::MouseEvent&) override { if (onAnyClick) onAnyClick(); }

        void setItems (std::vector<ColumnItem> newItems) { items = std::move (newItems); }

        // Appele par le parent une fois la largeur connue (pas depuis
        // resized() : pas de boucle setSize -> resized) ; la hauteur sert au
        // Viewport pour savoir jusqu'ou defiler.
        void layout (int width)
        {
            float y = 0.0f;
            for (size_t i = 0; i < items.size(); ++i)
                y += drawItem (nullptr, items[i], 0.0f, (float) width) + itemGap (items[i], next (i));
            setSize (width, juce::roundToInt (y) + 4);
        }

        void paint (juce::Graphics& g) override
        {
            float y = 0.0f;
            const float w = (float) getWidth();
            for (size_t i = 0; i < items.size(); ++i)
            {
                y += drawItem (&g, items[i], y, w);
                y += itemGap (items[i], next (i));
            }
        }

    private:
        const ColumnItem* next (size_t i) const { return i + 1 < items.size() ? &items[i + 1] : nullptr; }

        // Ecart sous un element ; un titre qui suit une liste de puces s'en
        // detache davantage.
        static float itemGap (const ColumnItem& it, const ColumnItem* nextItem)
        {
            using K = ColumnItem::Kind;
            const float base = it.kind == K::Header ? 6.0f : it.kind == K::Bullet ? 4.0f : 10.0f;
            return base + ((it.kind == K::Bullet && nextItem != nullptr && nextItem->kind == K::Header) ? 10.0f : 0.0f);
        }

        // Mise en page mot a mot : paragraphes justifies (la derniere ligne
        // reste a gauche), titres et puces alignes a gauche. Renvoie la
        // hauteur ; g == nullptr : mesure seulement.
        static float drawItem (juce::Graphics* g, const ColumnItem& it, float y, float width)
        {
            using K = ColumnItem::Kind;
            const auto pal = AlphaPalette::get();

            if (it.kind == K::Image)
            {
                if (it.image.isNull())
                    return 0.0f;
                const float h = width * (float) it.image.getHeight() / (float) juce::jmax (1, it.image.getWidth());
                if (g != nullptr)
                {
                    g->setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g->drawImage (it.image, juce::Rectangle<float> (0.0f, y, width, h));
                }
                return h;
            }

            const bool head = it.kind == K::Header, bullet = it.kind == K::Bullet;
            const auto font = head ? aboutFont (15.0f, 2) : aboutFont (13.0f);
            const float x0 = bullet ? 12.0f : 0.0f, maxW = juce::jmax (10.0f, width - x0);
            const float lineH = font.getHeight();

            juce::StringArray words;
            words.addTokens (it.text, " ", "");
            words.removeEmptyStrings();
            std::vector<float> wordW;
            for (const auto& w : words)
                wordW.push_back (juce::GlyphArrangement::getStringWidth (font, w));
            const float space = juce::GlyphArrangement::getStringWidth (font, " ");

            std::vector<std::pair<int, int>> lines; // [debut, fin) dans words
            int start = 0;
            float cur = 0.0f;
            for (int i = 0; i < words.size(); ++i)
            {
                const float add = (i == start ? 0.0f : space) + wordW[(size_t) i];
                if (i > start && cur + add > maxW)
                {
                    lines.push_back ({ start, i });
                    start = i;
                    cur = wordW[(size_t) i];
                }
                else
                {
                    cur += add;
                }
            }
            if (start < words.size())
                lines.push_back ({ start, words.size() });

            if (g != nullptr)
            {
                if (bullet)
                {
                    g->setColour (pal.heading());
                    g->fillEllipse (0.0f, y + lineH * 0.5f - 2.5f, 5.0f, 5.0f);
                }
                juce::GlyphArrangement ga;
                float baseline = y + font.getAscent();
                for (size_t l = 0; l < lines.size(); ++l)
                {
                    const int a = lines[l].first, b = lines[l].second;
                    float total = 0.0f;
                    for (int i = a; i < b; ++i)
                        total += wordW[(size_t) i];
                    const bool justify = it.kind == K::Paragraph && l + 1 < lines.size() && b - a > 1;
                    const float gap = justify ? (maxW - total) / (float) (b - a - 1) : space;
                    float x = x0;
                    for (int i = a; i < b; ++i)
                    {
                        ga.addLineOfText (font, words[i], x, baseline);
                        x += wordW[(size_t) i] + gap;
                    }
                    baseline += lineH;
                }
                g->setColour (head ? pal.heading() : pal.sheetText());
                ga.draw (*g);
            }
            return (float) lines.size() * lineH;
        }

        std::vector<ColumnItem> items;
    };

    AboutOverlay()
    {
        badgeImage = juce::ImageCache::getFromMemory (BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
        logoImage  = juce::ImageCache::getFromMemory (BinaryData::LOGO4x_png, BinaryData::LOGO4x_pngSize);
        photoImage = juce::ImageCache::getFromMemory (BinaryData::mikshake_about_png, BinaryData::mikshake_about_pngSize);
        splashImage = juce::ImageCache::getFromMemory (BinaryData::Mikshakesplash_png, BinaryData::Mikshakesplash_pngSize);
        // Signatures de Merlin : encre seule (dans l'alpha), teintee au theme
        // a la construction du decor (voir drawFooter()).
        signature[0] = juce::ImageCache::getFromMemory (BinaryData::signature_mikshake_png, BinaryData::signature_mikshake_pngSize);
        signature[1] = juce::ImageCache::getFromMemory (BinaryData::signature_sunny_png, BinaryData::signature_sunny_pngSize);

        // Portraits du duo, en noir et blanc. Copies : desaturate() modifie
        // l'image et celles de l'ImageCache sont partagees.
        const auto loadPortrait = [] (const void* data, int size)
        {
            auto img = juce::ImageCache::getFromMemory (data, size).createCopy();
            img.desaturate();
            return img;
        };
        team[0] = { loadPortrait (BinaryData::mikshake_about_crop_jpg, BinaryData::mikshake_about_crop_jpgSize), "MIKSHAKE", "DEVELOPMENT" };
        team[1] = { loadPortrait (BinaryData::sunny_crop_jpg, BinaryData::sunny_crop_jpgSize), "SUNNY", "DESIGN" };

        buildOverviewColumns();
        buildFeaturesAndPhilosophyColumn();

        for (auto* vp : { &viewportA, &viewportB, &viewportC })
        {
            vp->setScrollBarsShown (true, false);
            vp->setScrollBarThickness (8);
            addAndMakeVisible (*vp);
        }
        viewportA.setViewedComponent (&columnA, false);
        viewportB.setViewedComponent (&columnB, false);
        viewportC.setViewedComponent (&columnC, false);

        for (auto* col : { &columnA, &columnB, &columnC })
            col->onAnyClick = [this] { if (onDismiss) onDismiss(); };

        // ESC ferme le voile -- il faut le focus clavier (visibilityChanged()).
        setWantsKeyboardFocus (true);
    }

    void resized() override
    {
        const auto layout = computeLayout();
        const juce::Rectangle<int> cols[3] = { layout.colA, layout.colB, layout.colC };
        juce::Viewport* vps[3] = { &viewportA, &viewportB, &viewportC };
        ParagraphColumn* columns[3] = { &columnA, &columnB, &columnC };
        for (int i = 0; i < 3; ++i)
        {
            vps[i]->setBounds (cols[i]);
            // Largeur utile = colonne moins la barre de defilement (pas de
            // double retour a la ligne).
            columns[i]->layout (juce::jmax (10, cols[i].getWidth() - vps[i]->getScrollBarThickness() - 6));
        }
        chromeCache = juce::Image(); // reconstruit au prochain paint()
    }

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

    void paint (juce::Graphics& g) override
    {
        const float px = juce::jlimit (1.0f, 6.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (! chromeCache.isValid() || ! juce::approximatelyEqual (px, chromeScale))
        {
            chromeScale = px;
            rebuildChromeCache();
        }

        // Voile (fondu) puis panneau (zoom + glissement), voir animation.
        AlphaDraw::overlayVeil (g, getLocalBounds().toFloat(), animOpacity);
        g.saveState();
        g.setOpacity (animOpacity);
        g.addTransform (panelTransform());
        g.drawImage (chromeCache, getLocalBounds().toFloat());
        g.restoreState();
    }

    // --- Apparition / disparition animees : le voile se fond, le panneau
    // grandit de 92 % a 100 % en montant de 16 px avec un leger rebond
    // (280 ms) ; a la fermeture il redescend et se fond (170 ms). ---
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

    // Main au survol des liens.
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool overLink = websiteBounds.contains (e.position) || manualBounds.contains (e.position)
                           || licenseBounds.contains (e.position);
        setMouseCursor (overLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    std::function<void()> onDismiss;
    std::function<void()> onOpenLicense;

    // Fixe par l'editeur : le pied de page montre la cle active ou les
    // jours de demo restants.
    void setLicenseManager (LicenseManager& lm) { license = &lm; }

    // Changement de theme : le decor (signatures teintees, logo, couleurs)
    // est reconstruit au prochain paint().
    void refreshTheme()
    {
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
        juce::Rectangle<float> header, team, footer;
        float headerLineY = 0.0f, teamLineY = 0.0f, footerLineY = 0.0f, lineX0 = 0.0f, lineX1 = 0.0f;
        juce::Rectangle<int> colA, colB, colC;
        float dividerX1 = 0.0f, dividerX2 = 0.0f, colTop = 0.0f, colBottom = 0.0f;
    };

    juce::Rectangle<float> panelBounds() const
    {
        auto bounds = getLocalBounds().toFloat();
        return bounds.reduced (juce::jmax (20.0f, bounds.getWidth() * 0.015f),
                                juce::jmax (12.0f, bounds.getHeight() * 0.04f));
    }

    // --- Outils d'image (comme Merlin) ---

    // Reduction par paliers x2 puis ajustement exact : une seule reduction a
    // fort rapport crenelle les traits fins (signatures, monogramme).
    static juce::Image smoothRescale (juce::Image img, int w, int h)
    {
        w = juce::jmax (1, w);
        h = juce::jmax (1, h);
        while (img.getWidth() > w * 2 && img.getHeight() > h * 2)
            img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);
        return img.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }

    // Signature (encre seule, dans l'alpha) a la couleur donnee.
    static juce::Image inked (const juce::Image& src, juce::Colour ink)
    {
        juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        const juce::Image::BitmapData s (src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData d (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
                d.setPixelColour (x, y, ink.withAlpha (s.getPixelColour (x, y).getFloatAlpha()));
        return out;
    }

    // Monogramme Side Key (argente) : assombri pour rester lisible sur le
    // panneau clair.
    static void darken (juce::Image& img, float k)
    {
        juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < d.height; ++y)
            for (int x = 0; x < d.width; ++x)
                d.setPixelColour (x, y, d.getPixelColour (x, y).withMultipliedBrightness (k));
    }

    // Police reduite au besoin pour tenir dans maxW.
    static juce::Font fitFont (juce::Font f, const juce::String& t, float maxW)
    {
        const float w = juce::GlyphArrangement::getStringWidth (f, t);
        return (w > maxW && w > 0.0f) ? f.withHeight (juce::jmax (6.0f, f.getHeight() * maxW / w)) : f;
    }

    void rebuildChromeCache()
    {
        const int w = getWidth(), h = getHeight();
        if (w < 4 || h < 4)
            return;

        chromeCache = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) w * chromeScale)),
                                   juce::jmax (1, juce::roundToInt ((float) h * chromeScale)), true);
        juce::Graphics g (chromeCache);
        g.addTransform (juce::AffineTransform::scale (chromeScale));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        // Panneau seul (le voile est peint a part dans paint(), pour pouvoir
        // le fondre independamment pendant l'animation).
        AlphaDraw::overlaySheet (g, panelBounds(), 12.0f);

        const auto L = computeLayout();
        drawHeader (g, L.header);
        drawTeam (g, L.team);
        drawFooter (g, L.footer); // met aussi a jour les zones des liens

        g.setColour (AlphaPalette::get().cardLine);
        for (const float y : { L.headerLineY, L.teamLineY, L.footerLineY })
            g.fillRect (L.lineX0, y, L.lineX1 - L.lineX0, 1.0f);
        g.fillRect (L.dividerX1 - 0.5f, L.colTop, 1.0f, L.colBottom - L.colTop);
        g.fillRect (L.dividerX2 - 0.5f, L.colTop, 1.0f, L.colBottom - L.colTop);
    }

    // Une seule mise en page pour le decor (cache) et les colonnes (Viewport).
    Layout computeLayout() const
    {
        Layout L;
        auto c = panelBounds().reduced (20.0f, 12.0f);
        L.lineX0 = c.getX();
        L.lineX1 = c.getRight();

        L.header = c.removeFromTop (30.0f);
        c.removeFromTop (8.0f);
        L.headerLineY = c.getY();

        L.footer = c.removeFromBottom (54.0f);
        c.removeFromBottom (10.0f);
        L.footerLineY = c.getBottom();
        c.removeFromBottom (10.0f);

        // Bandeau du duo : 250 px, reduit sur une petite fenetre pour
        // laisser lire les colonnes.
        c.removeFromTop (14.0f);
        L.team = c.removeFromTop (juce::jlimit (120.0f, kTeamRowHeight, c.getHeight() * 0.42f));
        c.removeFromTop (14.0f);
        L.teamLineY = c.getY();
        c.removeFromTop (14.0f);

        const float gap = 18.0f, colW = (c.getWidth() - 2.0f * gap) / 3.0f;
        L.colTop = c.getY();
        L.colBottom = c.getBottom();
        const auto a = c.removeFromLeft (colW);
        c.removeFromLeft (gap);
        const auto b = c.removeFromLeft (colW);
        c.removeFromLeft (gap);
        L.colA = a.toNearestInt();
        L.colB = b.toNearestInt();
        L.colC = c.toNearestInt();
        L.dividerX1 = a.getRight() + gap * 0.5f;
        L.dividerX2 = b.getRight() + gap * 0.5f;
        return L;
    }

    // En-tete : monogramme, ABOUT + logo 7PACK + pastille alpha, accroche,
    // version a droite.
    void drawHeader (juce::Graphics& g, juce::Rectangle<float> row) const
    {
        const auto pal = AlphaPalette::get();
        const auto ink = pal.heading();

        g.setColour (pal.sheetTextSoft());
        g.setFont (aboutFont (11.0f, 2).withExtraKerningFactor (0.08f));
        const auto versionBox = row.removeFromRight (70.0f); // mise en page inchangee ; le texte deborde a gauche ("v2.22.6 beta")
        g.drawText ("v" + sevenpackVersionLabel(), versionBox.withLeft (versionBox.getRight() - 130.0f), juce::Justification::centredRight, false);

        if (badgeImage.isValid())
        {
            const float mh = 28.0f, mw = mh * (float) badgeImage.getWidth() / (float) badgeImage.getHeight();
            auto img = smoothRescale (badgeImage, juce::roundToInt (mw * chromeScale), juce::roundToInt (mh * chromeScale));
            if (pal.light)
                darken (img, 0.3f);
            g.drawImage (img, juce::Rectangle<float> (row.getX(), row.getCentreY() - mh * 0.5f, mw, mh));
            row.removeFromLeft (mw + 10.0f);
        }

        const float cap = 16.0f, base = row.getCentreY() + cap * 0.5f; // ligne de base commune
        g.setColour (ink);
        juce::GlyphArrangement about;
        about.addLineOfText (aboutFont (22.0f, 2).withExtraKerningFactor (0.11f), "ABOUT", row.getX(), base);
        about.draw (g);
        float x = about.getBoundingBox (0, -1, false).getRight() + 12.0f;

        // Logo 7PACK (cadre ~1.6 x les capitales) puis la pastille alpha, aux
        // memes proportions que l'en-tete du plugin (AlphaLogo).
        if (logoImage.isValid())
        {
            const float lh = 30.0f, lw = lh * (float) logoImage.getWidth() / (float) logoImage.getHeight();
            const auto src = pal.sunny ? AlphaDraw::logoOnDark (logoImage, pal.logoBg) : logoImage;
            const juce::Rectangle<float> placed (x, row.getCentreY() - lh * (92.0f / 188.0f), lw, lh);
            g.drawImage (smoothRescale (src, juce::roundToInt (lw * chromeScale), juce::roundToInt (lh * chromeScale)), placed);
            x = placed.getX() + lw * (866.0f / 880.0f);
            if (AlphaLogo::badgeStyle > 0)
            {
                const float r = 0.37f * lh, gap = 0.17f * lh;
                AlphaDraw::alphaBadge (g, { x + gap + r, row.getCentreY() }, r, AlphaLogo::badgeStyle);
                x += gap + 2.0f * r;
            }
            x += 14.0f;
        }

        g.setColour (ink.withAlpha (0.85f));
        g.setFont (aboutFont (14.0f, 0, true).withExtraKerningFactor (0.10f));
        g.drawText ("Dynamics. Refined.", juce::Rectangle<float> (x, row.getY(), juce::jmax (0.0f, row.getRight() - x - 8.0f), row.getHeight()),
                    juce::Justification::centredLeft, true);
    }

    // --- Contenu : Overview (fourni par l'utilisateur), reparti sur les
    // colonnes A et B. Tirets de ponctuation evites (demande explicite).
    void buildOverviewColumns()
    {
        columnA.setItems ({
            { "SEVENPACK: Seven Bands, One Mastering Chain", ColumnItem::Kind::Header },
            // v2.23.5 : "we" et non "I", comme Merlin (demande du user : "dans about c'est on, pas je").
            { "We designed SEVENPACK with an experimental goal above all else. After many attempts at building multiband compressors, "
              "we started multiplying the bands right from the earliest stages of development, with a fairly simple idea: chase the "
              "best possible mastering, even if it meant making the CPU cry.", ColumnItem::Kind::Paragraph },
            { "The goal was to push the boundaries of multiband processing and explore just how far dynamic control could go without "
              "turning the mix into a spectral puzzle.", ColumnItem::Kind::Paragraph },
            { "We already have several ideas for what comes next, especially as processors keep bulking up on steroids. But for now, "
              "we want SEVENPACK to stay accessible and usable across a wide range of hardware configurations.", ColumnItem::Kind::Paragraph },
            { "The principle itself is fairly simple: 7 bands, in the philosophy of Tripressor, with a feedforward compressor built "
              "on Tripressor's current DSP, and a crossover designed to be as transparent as possible.", ColumnItem::Kind::Paragraph },
            { "With THD disabled, the measured deviation stays under 0.00001dB. In other words, the crossover tries very hard not to "
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
            { "We also built in a Learn module, designed to automatically provide a coherent starting point for compression and "
              "expansion.", ColumnItem::Kind::Paragraph },
            { "Because with seven bands, finding the right attack and release for every single one can quickly turn into a small "
              "exercise in mental torture. Eventually you end up practically negotiating with each transient.", ColumnItem::Kind::Paragraph },
            { "The Learn module analyzes the behavior of the signal to suggest a coherent starting point for your dynamics settings. "
              "The goal, obviously, is not to replace your ears, but to save you time and spare you from starting on a blank page.", ColumnItem::Kind::Paragraph },
            { "We naturally encourage you to experiment from there and find whatever sounds the most musical and professional for "
              "your mix and your own approach to mastering.", ColumnItem::Kind::Paragraph },
            { "With this many bands and this many things to tweak, the Learn module felt like an almost indispensable extension of "
              "SEVENPACK.", ColumnItem::Kind::Paragraph },
            { "Seven bands, two dynamics processors per band, and one button to try and put a little order back into all of it.", ColumnItem::Kind::Paragraph },
        };
        // Ancienne photo en fin de colonne B, masquee depuis le bandeau du
        // duo (gardee dans le code).
        if constexpr (kShowSplashImage)
            learnItems.push_back ({ "", ColumnItem::Kind::Image, splashImage });
        columnB.setItems (std::move (learnItems));
    }

    // Bandeau du duo : deux portraits (legende nom + role sur un degrade
    // sombre), puis la presentation, centree verticalement.
    void drawTeam (juce::Graphics& g, juce::Rectangle<float> row) const
    {
        const auto pal = AlphaPalette::get();
        const float cardW = row.getHeight() * kCardWidth / kTeamRowHeight;
        for (const auto& person : team)
        {
            const auto card = row.removeFromLeft (cardW);
            row.removeFromLeft (16.0f);
            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path clip;
                clip.addRoundedRectangle (card, 10.0f);
                g.reduceClipRegion (clip);
                g.setColour (juce::Colour (0xff111111));
                g.fillRect (card);

                if (const auto& img = person.photo; img.isValid())
                {
                    // Remplissage "cover", cadre un peu au-dessus du centre (visages).
                    const float s = juce::jmax (card.getWidth() / (float) img.getWidth(), card.getHeight() / (float) img.getHeight());
                    const float iw = (float) img.getWidth() * s, ih = (float) img.getHeight() * s;
                    g.drawImage (smoothRescale (img, juce::roundToInt (iw * chromeScale), juce::roundToInt (ih * chromeScale)),
                                 { card.getX() + (card.getWidth() - iw) * 0.5f, card.getY() + (card.getHeight() - ih) * 0.3f, iw, ih });
                }

                // Legende a l'echelle de la carte (plus petite sur une fenetre
                // basse), le nom retrecissant au besoin pour tenir en entier.
                const float k = juce::jlimit (0.6f, 1.0f, card.getHeight() / kTeamRowHeight);
                const auto capArea = card.withTrimmedTop (card.getHeight() - 72.0f * k);
                g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, capArea.getY(),
                                                         juce::Colours::black.withAlpha (0.72f), 0.0f, capArea.getBottom(), false));
                g.fillRect (capArea);
                auto text = card.reduced (14.0f * k, 12.0f * k);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.setFont (fitFont (aboutFont (12.0f * k).withExtraKerningFactor (0.12f), person.role, text.getWidth()));
                g.drawText (person.role, text.removeFromBottom (15.0f * k), juce::Justification::bottomLeft, false);
                g.setColour (juce::Colours::white);
                g.setFont (fitFont (aboutFont (19.0f * k, 2).withExtraKerningFactor (0.14f), person.name, text.getWidth()));
                g.drawText (person.name, text.removeFromBottom (24.0f * k), juce::Justification::bottomLeft, false);
            }
            g.setColour (pal.cardLine);
            g.drawRoundedRectangle (card, 10.0f, 1.0f);
        }

        // Presentation : titre, une phrase, puis la devise (empattements, italique).
        auto intro = row.withTrimmedLeft (10.0f).withTrimmedRight (10.0f);
        intro.setWidth (juce::jmin (intro.getWidth(), 620.0f));
        struct Part { juce::String text; juce::Font font; juce::Colour colour; float gapAfter; };
        const Part parts[] = {
            { "Two people, seven bands", aboutFont (15.0f, 2), pal.heading(), 6.0f },
            { "SEVENPACK ALPHA is made by two people: Mikshake writes the code, Sunny designs everything you see.",
              aboutFont (13.0f), pal.sheetText(), 10.0f },
            { "Seven bands, SEVENPACK: for once, the name tells the whole truth.",
              juce::Font (juce::FontOptions().withName ("Georgia").withStyle ("Italic").withPointHeight (16.0f)).withExtraKerningFactor (0.025f),
              pal.heading(), 0.0f },
        };
        juce::GlyphArrangement ga[3];
        float heights[3], total = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            ga[i].addJustifiedText (parts[i].font, parts[i].text, 0.0f, parts[i].font.getAscent(), intro.getWidth(), juce::Justification::left);
            heights[i] = ga[i].getNumGlyphs() > 0 ? ga[i].getBoundingBox (0, -1, true).getBottom() : 0.0f;
            total += heights[i] + parts[i].gapAfter;
        }
        float y = intro.getCentreY() - total * 0.5f;
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (parts[i].colour);
            ga[i].draw (g, juce::AffineTransform::translation (intro.getX(), y));
            y += heights[i] + parts[i].gapAfter;
        }
    }

    // --- Colonne C : Features (liste complete) + Philosophy.
    void buildFeaturesAndPhilosophyColumn()
    {
        std::vector<ColumnItem> items;
        items.push_back ({ "Features", ColumnItem::Kind::Header });

        static const char* features[] =
        {
            "7 Band Feedforward Compression (Low / LowMid / Mid / HighMid / Presence / High / Air)",
            "6 Point Crossover, Every Point Independently Foldable to Fuse Adjacent Bands",
            "Wide (Analog) / Linear (FIR) Crossover, Switchable",
            "Crossover Amplitude Accuracy: Under 0.00001dB, Wide and Linear",
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
            "Resizable UI (Auto up to 200%, 4K Ready, or 75% to 125%), Locked Against Accidental Host Fullscreen",
            "Calibrated I/O, Linearity Under 0.00001dB, Measured",
            "LEARN: Listens to Your Program Material and Calculates Compression, Expansion "
            "and Crossover Settings, with Loudness Targeting for Streaming Platform Norms",
            "Independent Upward Expansion per Band (Comp / Expand), with its Own Ratio, Attack, "
            "Release and Range, Running Alongside Compression at All Times"
        };
        for (auto* f : features)
            items.push_back ({ f, ColumnItem::Kind::Bullet });

        items.push_back ({ "The Side Key System Philosophy", ColumnItem::Kind::Header });
        items.push_back ({ "At Side Key System, calibration is not a final adjustment. It is part of the engineering process itself.", ColumnItem::Kind::Paragraph });
        items.push_back ({ "Every tool is carefully designed, measured, tested and calibrated with meticulous attention to precision, stability and "
                            "measurement consistency. Every control is tuned to deliver predictable behavior, repeatable results and a workflow you can "
                            "trust, whether working on a single track or a full mastering session.", ColumnItem::Kind::Paragraph });
        items.push_back ({ "The objective is simple: create professional tools that behave like precision instruments, where every parameter has a "
                            "purpose and every decision serves the music.", ColumnItem::Kind::Paragraph });

        columnC.setItems (std::move (items));
    }

    // Pied de page : les deux signatures, liens (site | Manual | licence) et
    // copyright, tampon a droite.
    void drawFooter (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto pal = AlphaPalette::get();

        if (kShowFooterPhoto && ! photoImage.isNull())
        {
            const float photoH = area.getHeight();
            const float photoW = photoH * ((float) photoImage.getWidth() / (float) photoImage.getHeight());
            auto photoBounds = juce::Rectangle<float> (photoW, photoH).withCentre (area.getCentre()).withX (area.getX());
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (photoBounds, 6.0f);
            g.reduceClipRegion (clip);
            g.drawImage (photoImage, photoBounds);
            area.removeFromLeft (photoBounds.getWidth() + 16.0f);
        }

        // Signatures : noire sur le panneau clair, blanche sur le sombre.
        const auto inkColour = pal.light ? juce::Colours::black : juce::Colours::white;
        if constexpr (kLegacySignature)
        {
            // Ancienne signature seule (Mikshake, pipeline de SevenpackLookAndFeel).
            const auto sig = SevenpackLookAndFeel::buildSignatureImage (inkColour, 55);
            if (sig.isValid())
            {
                g.drawImageAt (sig, (int) area.getX(), (int) (area.getCentreY() - (float) sig.getHeight() * 0.5f));
                area.removeFromLeft ((float) sig.getWidth() + 16.0f);
            }
        }
        else
        {
            const float sigH[2] = { 50.0f, 40.0f }; // Sunny Jim, plus large : meme poids visuel
            for (int i = 0; i < 2; ++i)
            {
                if (! signature[i].isValid())
                    continue;
                const float sh = sigH[i], sw = sh * (float) signature[i].getWidth() / (float) signature[i].getHeight();
                g.drawImage (inked (smoothRescale (signature[i], juce::roundToInt (sw * chromeScale), juce::roundToInt (sh * chromeScale)), inkColour),
                             { area.getX(), area.getCentreY() - sh * 0.5f, sw, sh });
                area.removeFromLeft (sw + (i == 0 ? 10.0f : 16.0f));
            }
        }

        drawStamp (g, area.removeFromRight (58.0f));
        area.removeFromRight (14.0f);

        // Liens, puis copyright, centres ensemble verticalement.
        const float top = area.getCentreY() - (16.0f + 4.0f + 13.0f) * 0.5f;
        drawLinks (g, juce::Rectangle<float> (area.getX(), top, area.getWidth(), 16.0f));

        g.setColour (pal.sheetTextSoft().withAlpha (0.8f));
        g.setFont (aboutFont (9.5f).withExtraKerningFactor (0.04f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xa9 2026 Side Key System. All rights reserved.   \xe2\x80\xa2   Made in France")),
                    juce::Rectangle<float> (area.getX(), top + 20.0f, area.getWidth(), 13.0f), juce::Justification::centredLeft, true);
    }

    // Trois liens cote a cote (site, Manual, licence) separes par "|", chacun
    // avec sa zone cliquable (verifiee dans mouseDown()).
    void drawLinks (juce::Graphics& g, juce::Rectangle<float> row)
    {
        const auto pal = AlphaPalette::get();
        const auto linkFont = aboutFont (13.0f, 2).withExtraKerningFactor (0.04f);
        constexpr float sepW = 18.0f;

        const auto drawLink = [&] (const juce::String& text, float x, bool underline)
        {
            const float tw = juce::GlyphArrangement::getStringWidth (linkFont, text);
            const auto bounds = row.withX (x).withWidth (tw);
            g.setColour (pal.heading());
            g.setFont (linkFont);
            g.drawText (text, bounds, juce::Justification::centredLeft, false);
            if (underline)
                g.fillRect (bounds.getX(), bounds.getBottom() - 2.0f, tw, 1.0f);
            return bounds;
        };
        const auto drawSep = [&] (float x)
        {
            g.setColour (pal.sheetTextSoft().withAlpha (0.6f));
            g.setFont (linkFont);
            g.drawText ("|", juce::Rectangle<float> (x, row.getY(), sepW, row.getHeight()), juce::Justification::centred, false);
        };

        websiteBounds = drawLink ("sidekeysystem.com", row.getX(), true);
        drawSep (websiteBounds.getRight());
        manualBounds = drawLink ("Manual", websiteBounds.getRight() + sepW, true);
        drawSep (manualBounds.getRight());
        // Etat de la licence (cle active ou jours de demo), sans soulignement
        // (une cle entiere soulignee se lit mal) : ouvre l'ecran de licence.
        licenseBounds = drawLink (licenseText(), manualBounds.getRight() + sepW, false);
    }

    juce::String licenseText() const
    {
        if (license == nullptr)
            return "License";
        // Cle beta (v2.22.6) : la date de fin plutot que la cle.
        if (license->getStatus() == LicenseManager::Status::betaEnded)
            return "Beta ended";
        if (license->getLicenseKey().isNotEmpty() && license->isBetaLicense())
            return "Beta - until " + license->getBetaEndDateText (true);
        if (const auto key = license->getLicenseKey(); key.isNotEmpty())
            return key;
        if (license->getStatus() == LicenseManager::Status::trial)
            return "Demo - " + juce::String (license->getTrialDaysLeft()) + " day(s) left";
        return "License";
    }

    // Tampon "Calibrated" : double anneau tourne, texte, grain d'encre
    // deterministe (pas de Random : jamais de scintillement).
    void drawStamp (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto centre = area.getCentre();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const juce::Colour ink = (kRedStamp ? SevenpackColours::ledRed : AlphaPalette::get().heading()).withAlpha (0.82f);

        juce::Graphics::ScopedSaveState state (g);
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

        for (int i = 0; i < 46; ++i)
        {
            const float ang = (float) i * 0.151f;
            const float rr = r * (0.32f + 0.66f * std::abs (std::sin ((float) i * 2.3f)));
            g.setColour (ink.withAlpha (0.16f * std::abs (std::sin ((float) i * 1.7f))));
            g.fillEllipse (centre.x + std::cos (ang) * rr - 0.6f, centre.y + std::sin (ang) * rr - 0.6f, 1.2f, 1.2f);
        }
    }

    juce::Image badgeImage, logoImage, photoImage, splashImage;
    juce::Image signature[2]; // Mikshake, Sunny : encre seule (alpha), teintee au theme

    // Duo (bandeau sous l'en-tete, voir drawTeam()).
    struct TeamMember { juce::Image photo; juce::String name, role; };
    std::array<TeamMember, 2> team;
    static constexpr float kTeamRowHeight = 250.0f, kCardWidth = 200.0f;
    // Anciens elements, gardes dans le code.
    static constexpr bool kShowFooterPhoto = false;  // photo de Mikshake dans le pied de page
    static constexpr bool kShowSplashImage = false;  // photo en fin de colonne B
    static constexpr bool kLegacySignature = false;  // signature de Mikshake seule
    static constexpr bool kRedStamp = false;         // tampon rouge (avant le About de Merlin)

    juce::Image chromeCache;
    float chromeScale = 1.0f; // resolution physique du cache (ecran 4K, taille 200 %)
    juce::Rectangle<float> websiteBounds, manualBounds, licenseBounds;
    LicenseManager* license = nullptr;

    juce::Viewport viewportA, viewportB, viewportC;
    ParagraphColumn columnA, columnB, columnC;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};
