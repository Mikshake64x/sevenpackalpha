#pragma once
#include <JuceHeader.h>
#include "AlphaSkin.h"
#include "PluginProcessor.h"

/**
    AlphaThdNeedle
    --------------
    Cadran a aiguille d'UNE bande, en tete de sa tranche. Trois modes,
    choisis par les boutons THD / GR / GONIO a gauche des aiguilles
    (processor.meterMode -- demande explicite, valable pour les 7 bandes) :
      - THD (0) : part du residu de saturation de l'etage THD global qui
        tombe dans cette bande (processor.clipMeterAmountBand[band], voir
        PluginProcessor::processBlock()), graduee 0..100 %, zone rouge
        > 70 % -- au repos quand THD est coupe ou le plugin eteint ;
      - GR (1) : zero au CENTRE, compression vers la droite, expansion vers
        la gauche ("voir la reduction ou l'expansion sur le meme
        vu-metre"), +-12 dB lineaire ; memes valeurs que les diodes GR/EXP
        (grMeterDb / grMeterExpandDb) ;
      - GONIO (2) : goniometre en demi-lune de la bande (bandScopes, apres
        Mix et Spread ; L a gauche, R a droite, mono = vertical) et
        correlation en pied.

    Ballistique : masse-ressort-amortisseur dont la raideur (RETOUR) et
    l'amortissement (REBOND) viennent des options (processor.needleReturn /
    needleBounce). Cadran (arc, graduations, chiffres) en cache image, un
    par mode.
*/
class AlphaThdNeedle : public juce::Component, private juce::Timer
{
public:
    AlphaThdNeedle (SevenpackAudioProcessor& p, int bandIndex)
        : processor (p), band (bandIndex),
          powerParam (*p.apvts.getRawParameterValue (SevenpackAudioProcessor::idPower)),
          thdParam (*p.apvts.getRawParameterValue (SevenpackAudioProcessor::idSoftClip))
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (true); // fond peint (couleur de tranche) : un repaint de l'aiguille ne redessine pas toute la tranche
        mode = currentMode();
        startTimerHz (60);
    }

    ~AlphaThdNeedle() override { stopTimer(); }

    void refreshTheme() { cacheDirty = true; repaint(); }
    void resized() override { cacheDirty = true; }
    // Plaque de la tranche dans le repere du cadran (repeinte derriere lui :
    // le cadran est opaque, voir AlphaDraw::paintPlate()).
    void setPlateArea (juce::Rectangle<float> r) { if (r != plateArea) { plateArea = r; cacheDirty = true; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const bool enabled = isMeasuring();
        if (cacheDirty || enabled != cachedEnabled || mode != cachedMode
            || cache.getWidth() != getWidth() || cache.getHeight() != getHeight())
            rebuildCache (enabled);
        g.drawImageAt (cache, 0, 0);

        const auto pal = AlphaPalette::get();
        const auto foot = getLocalBounds().toFloat().removeFromBottom (14.0f).reduced (7.0f, 0.0f);
        g.setFont (AlphaDraw::lcdFont (AlphaType::value));

        if (mode == Mode::gonio)
        {
            paintGonioPoints (g, enabled);
            g.setColour (pal.faceArc.withAlpha (enabled ? 0.95f : 0.35f));
            g.drawText (readout, foot, juce::Justification::centredRight, false);
            paintGlass (g);
            return;
        }

        if (mode == Mode::gr)
        {
            g.setColour (pal.faceArc.withAlpha (enabled && readoutExp > 0.05f ? 0.95f : 0.5f));
            g.drawText ("EXP " + juce::String (readoutExp, 1), foot, juce::Justification::centredLeft, false);
            g.setColour (pal.faceArc.withAlpha (enabled && readoutGr > 0.05f ? 0.95f : 0.5f));
            g.drawText ("GR " + juce::String (readoutGr, 1), foot, juce::Justification::centredRight, false);
        }
        else
        {
            g.setColour (pal.faceArc.withAlpha (enabled ? 0.95f : 0.35f));
            g.drawText (readout, foot, juce::Justification::centredRight, false);
        }

        // Aiguille.
        const auto geo = geometry();
        const float deg = mode == Mode::gr ? 50.0f * juce::jlimit (-1.04f, 1.04f, pos)
                                           : -50.0f + 100.0f * juce::jlimit (-0.03f, 1.04f, pos);
        const float a = juce::degreesToRadians (deg);
        const juce::Point<float> p0 (geo.cx + std::sin (a) * geo.r * 0.28f, geo.cy - std::cos (a) * geo.r * 0.28f);
        const juce::Point<float> p1 (geo.cx + std::sin (a) * (geo.r + 3.0f), geo.cy - std::cos (a) * (geo.r + 3.0f));
        // Ombre portee sur le cadran (l'aiguille est sous la vitre, au-dessus
        // du fond) : un passage large et pale + un passage net.
        const juce::Point<float> sh (2.5f, 2.5f);
        g.setColour (juce::Colour ((juce::uint8) 0, (juce::uint8) 25, (juce::uint8) 50, 0.13f));
        g.drawLine ({ p0 + sh, p1 + sh }, 4.0f);
        g.setColour (juce::Colour ((juce::uint8) 0, (juce::uint8) 25, (juce::uint8) 50, 0.30f));
        g.drawLine ({ p0 + sh, p1 + sh }, 2.0f);
        // Aiguille blanche, bout ROUGE sur le dernier quart, la ou elle croise
        // les graduations (demande explicite).
        const juce::Point<float> pm (geo.cx + std::sin (a) * geo.r * 0.8f, geo.cy - std::cos (a) * geo.r * 0.8f);
        g.setColour (pal.needle.withAlpha (enabled ? 1.0f : 0.5f));
        g.drawLine (p0.x, p0.y, pm.x, pm.y, 1.6f);
        if (processor.needleRedTip.load()) // bout rouge ou blanc, au choix dans les Options
        {
            g.setColour (juce::Colour (0xffe8382b).withAlpha (enabled ? 1.0f : 0.5f));
            g.drawLine (pm.x, pm.y, p1.x, p1.y, 1.9f);
        }
        else
        {
            g.drawLine (pm.x, pm.y, p1.x, p1.y, 1.6f);
        }

        paintGlass (g);
    }

private:
    enum class Mode { thd = 0, gr = 1, gonio = 2 };
    struct Geo { float cx, cy, r; };
    struct GonioGeo { float cx, base, r; };

    // ---------- Vitre (demande explicite : "il manque une vitre") ----------
    // Deux variantes au choix dans les Options (processor.needleGlass) :
    // 0 = ARC (reflet en croissant qui suit l'echelle, cadre fin, ombre
    // interieure, point de brillance), 1 = SMOKED (verre teinte, cadre
    // biseaute, bande de brillance diagonale). Posee PAR-DESSUS l'aiguille
    // comme une vraie vitre ; image calculee une fois par taille / echelle
    // / variante.
    void paintGlass (juce::Graphics& g)
    {
        const int glass = juce::jlimit (0, 1, processor.needleGlass.load());
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (glass != cachedGlass || ! juce::approximatelyEqual (scale, glassScale)
            || glassCache.getWidth() != juce::roundToInt ((float) getWidth() * scale)
            || glassCache.getHeight() != juce::roundToInt ((float) getHeight() * scale))
        {
            glassCache = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                      juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
            juce::Graphics gg (glassCache);
            gg.addTransform (juce::AffineTransform::scale (scale));
            drawGlass (gg, glass);
            cachedGlass = glass;
            glassScale = scale;
        }
        g.drawImage (glassCache, getLocalBounds().toFloat());
    }

    void drawGlass (juce::Graphics& g, int glass) const
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        const auto shade = [] (float alpha) { return juce::Colour ((juce::uint8) 0, (juce::uint8) 20, (juce::uint8) 50, alpha); };
        const auto edge = [&g] (juce::Rectangle<float> r, juce::Point<float> from, juce::Point<float> to, juce::Colour c)
        {
            g.setGradientFill (juce::ColourGradient (c, from, c.withAlpha (0.0f), to, false));
            g.fillRect (r);
        };

        g.saveState();
        juce::Path frame; frame.addRoundedRectangle (0.0f, 0.0f, w, h, 6.0f);
        g.reduceClipRegion (frame);

        if (glass == 0)
        {
            // Ombre interieure tout autour.
            edge ({ 0.0f, 0.0f, w, 8.0f }, { 0.0f, 0.0f }, { 0.0f, 8.0f }, shade (0.34f));
            edge ({ 0.0f, h - 5.0f, w, 5.0f }, { 0.0f, h }, { 0.0f, h - 5.0f }, shade (0.22f));
            edge ({ 0.0f, 0.0f, 6.0f, h }, { 0.0f, 0.0f }, { 6.0f, 0.0f }, shade (0.22f));
            edge ({ w - 6.0f, 0.0f, 6.0f, h }, { w, 0.0f }, { w - 6.0f, 0.0f }, shade (0.22f));

            // Reflet en croissant qui suit l'arc de l'echelle.
            const auto geo = geometry();
            juce::Path crescent;
            constexpr int n = 24;
            for (int i = 0; i <= n; ++i)
            {
                const float t = juce::MathConstants<float>::pi * (1.08f + 0.84f * (float) i / (float) n);
                const juce::Point<float> p (geo.cx + std::cos (t) * (geo.r + 20.0f), geo.cy + 6.0f + std::sin (t) * (geo.r + 20.0f));
                if (i == 0) crescent.startNewSubPath (p); else crescent.lineTo (p);
            }
            for (int i = 0; i <= n; ++i)
            {
                const float t = juce::MathConstants<float>::pi * (1.9f - 0.8f * (float) i / (float) n);
                crescent.lineTo (geo.cx + std::cos (t) * (geo.r + 10.0f), geo.cy + 22.0f + std::sin (t) * (geo.r + 10.0f));
            }
            crescent.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.34f), 0.0f, 0.0f,
                                                     juce::Colours::white.withAlpha (0.02f), 0.0f, h * 0.5f, false));
            g.fillPath (crescent);

            // Point de brillance en haut a gauche.
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.38f), { w * 0.2f, h * 0.16f },
                                                     juce::Colours::white.withAlpha (0.0f), { w * 0.2f + 22.0f, h * 0.16f }, true));
            g.fillRect (0.0f, 0.0f, w, h);
        }
        else
        {
            g.setColour (juce::Colour ((juce::uint8) 0, (juce::uint8) 25, (juce::uint8) 55, 0.10f)); // teinte
            g.fillRect (0.0f, 0.0f, w, h);
            edge ({ 0.0f, 0.0f, w, 9.0f }, { 0.0f, 0.0f }, { 0.0f, 9.0f }, juce::Colour ((juce::uint8) 0, (juce::uint8) 15, (juce::uint8) 40, 0.4f));

            // Large bande de brillance en diagonale, d'un bord a l'autre.
            {
                g.saveState();
                const float cx = w * 0.5f, cy = h * 0.5f;
                g.addTransform (juce::AffineTransform::rotation (-0.42f, cx, cy));
                juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), cx, cy - 38.0f,
                                            juce::Colours::white.withAlpha (0.0f), cx, cy + 18.0f, false);
                sheen.addColour (0.45, juce::Colours::white.withAlpha (0.22f));
                sheen.addColour (0.60, juce::Colours::white.withAlpha (0.08f));
                g.setGradientFill (sheen);
                g.fillRect (cx - w, cy - 38.0f, w * 2.0f, 56.0f);
                g.restoreState();
            }
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillRect (8.0f, h - 1.2f, w - 16.0f, 1.0f);
        }
        g.restoreState();

        // Cadre.
        if (glass == 0)
        {
            g.setColour (juce::Colour ((juce::uint8) 8, (juce::uint8) 40, (juce::uint8) 80, 0.55f));
            g.drawRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 6.0f, 1.5f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour ((juce::uint8) 10, (juce::uint8) 40, (juce::uint8) 80, 0.75f), 0.0f, 0.0f,
                                                     juce::Colours::white.withAlpha (0.75f), 0.0f, h, false));
            g.drawRoundedRectangle (1.25f, 1.25f, w - 2.5f, h - 2.5f, 6.0f, 2.5f);
        }
    }

    juce::Rectangle<float> plateArea;
    juce::Image glassCache;
    int cachedGlass = -1;
    float glassScale = 0.0f;

    static constexpr float kGrRangeDb = 12.0f;  // +-12 dB lineaire (variante A validee)
    static constexpr int kScopePoints = 384;

    Mode currentMode() const noexcept { return (Mode) juce::jlimit (0, 2, processor.meterMode.load()); }

    Geo geometry() const noexcept
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        const float r = juce::jmin (h * 0.86f, w * 0.43f);
        return { w * 0.5f, h + h * 0.16f, r };
    }

    GonioGeo gonioGeometry() const noexcept
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        const float base = h - 17.0f;
        return { w * 0.5f, base, juce::jmax (4.0f, juce::jmin (base - 4.0f, w * 0.5f - 14.0f)) };
    }

    bool isMeasuring() const noexcept
    {
        const bool powered = powerParam.load() > 0.5f;
        return mode == Mode::thd ? powered && thdParam.load() > 0.5f : powered;
    }

    void rebuildCache (bool enabled)
    {
        const auto pal = AlphaPalette::get();
        cache = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics g (cache);
        auto b = getLocalBounds().toFloat();
        if (plateArea.isEmpty())
            g.fillAll (pal.strip); // opaque : meme bleu que la plaque de la tranche
        else
            AlphaDraw::paintPlate (g, plateArea, pal); // meme finition que la plaque
        juce::ColourGradient bg (juce::Colours::white.withAlpha (pal.light ? 0.14f : 0.04f), 0.0f, 0.0f,
                                 juce::Colours::black.withAlpha (pal.light ? 0.06f : 0.25f), 0.0f, b.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (pal.faceFoot);
        g.fillRect (b.removeFromBottom (14.0f));

        const float dim = enabled ? 1.0f : 0.4f;
        const auto footLabel = getLocalBounds().toFloat().removeFromBottom (14.0f).reduced (8.0f, 0.0f);

        if (mode == Mode::thd)
            paintThdFace (g, pal, dim, footLabel);
        else if (mode == Mode::gr)
            paintGrFace (g, pal, dim);
        else
            paintGonioFace (g, pal, dim, footLabel);

        cachedEnabled = enabled;
        cachedMode = mode;
        cacheDirty = false;
    }

    juce::Point<float> facePoint (const Geo& geo, float deg, float rad) const noexcept
    {
        const float a = juce::degreesToRadians (deg);
        return { geo.cx + std::sin (a) * rad, geo.cy - std::cos (a) * rad };
    }

    void faceArc (juce::Graphics& g, const Geo& geo, float rad, float deg0, float deg1, juce::Colour c, float width) const
    {
        juce::Path p;
        p.addCentredArc (geo.cx, geo.cy, rad, rad, 0.0f, juce::degreesToRadians (deg0), juce::degreesToRadians (deg1), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width));
    }

    void faceTick (juce::Graphics& g, const Geo& geo, float deg, float len, juce::Colour c, float width) const
    {
        const auto a0 = facePoint (geo, deg, geo.r), a1 = facePoint (geo, deg, geo.r - len);
        g.setColour (c);
        g.drawLine (a0.x, a0.y, a1.x, a1.y, width);
    }

    void faceLabel (juce::Graphics& g, const Geo& geo, float deg, const juce::String& text, juce::Colour c) const
    {
        g.setColour (c);
        g.drawText (text, juce::Rectangle<float> (26.0f, 11.0f).withCentre (facePoint (geo, deg, geo.r + 9.5f)),
                    juce::Justification::centred, false);
    }

    void paintThdFace (juce::Graphics& g, const AlphaPalette& pal, float dim, juce::Rectangle<float> footLabel)
    {
        const auto geo = geometry();
        faceArc (g, geo, geo.r - 2.5f, 20.0f, 50.0f, pal.faceRed.withAlpha (dim), 4.5f);  // zone rouge 70..100 %
        // (Plus d'arc blanc continu sous les graduations -- retour utilisateur : "l'arc de cercle blanc avec les marques c'est pas beau", seules les marques restent.)
        for (int p = 0; p <= 100; p += 5)
        {
            const bool major = (p % 25) == 0;
            faceTick (g, geo, -50.0f + (float) p, major ? 7.5f : 4.0f, (p > 70 ? pal.faceRed : pal.faceArc).withAlpha (dim), major ? 1.5f : 1.0f);
        }
        g.setFont (AlphaDraw::font (AlphaType::label));
        for (int p = 0; p <= 100; p += 25)
            faceLabel (g, geo, -50.0f + (float) p, juce::String (p), (p > 70 ? pal.faceRed : pal.faceLabel).withAlpha (dim));
        AlphaDraw::spacedText (g, "THD", juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ geo.cx, (float) getHeight() * 0.6f }),
                               pal.faceLabel.withAlpha (dim), AlphaType::label, AlphaType::kerning);
        AlphaDraw::spacedText (g, "%", footLabel, pal.faceArc.withAlpha (0.8f * dim), AlphaType::label, 0.1f, juce::Justification::centredLeft);
    }

    // GR : zero au centre, expansion a gauche (violet), compression a
    // droite (ambre), +-12 dB lineaire.
    void paintGrFace (juce::Graphics& g, const AlphaPalette& pal, float dim)
    {
        const auto geo = geometry();
        const auto expColour = pal.light ? juce::Colour (0xd94a208c) : juce::Colour (0xd9b48cff);
        const auto expInk    = pal.light ? juce::Colour (0xff3a1675) : juce::Colour (0xffd2bcff);
        faceArc (g, geo, geo.r - 2.5f, -50.0f, -1.5f, expColour.withMultipliedAlpha (dim), 3.0f);
        faceArc (g, geo, geo.r - 2.5f, 1.5f, 50.0f, juce::Colour (0xf2ffcd50).withMultipliedAlpha (dim), 3.0f);
        // (Plus d'arc blanc continu sous les graduations -- retour utilisateur : "l'arc de cercle blanc avec les marques c'est pas beau", seules les marques restent.)
        for (int db = -12; db <= 12; ++db)
        {
            const float len = db == 0 ? 9.0f : (db % 6 == 0 ? 7.5f : (db % 3 == 0 ? 5.5f : 3.5f));
            faceTick (g, geo, (float) db / kGrRangeDb * 50.0f, len, pal.faceArc.withAlpha (dim), db % 3 == 0 ? 1.5f : 1.0f);
        }
        g.setFont (AlphaDraw::font (AlphaType::label));
        for (int db : { -12, -6, 0, 6, 12 })
            faceLabel (g, geo, (float) db / kGrRangeDb * 50.0f, juce::String (std::abs (db)), pal.faceLabel.withAlpha (dim));
        const float y = (float) getHeight() * 0.66f;
        AlphaDraw::spacedText (g, "EXP", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ geo.cx - geo.r * 0.3f, y }),
                               expInk.withMultipliedAlpha (dim), AlphaType::label, AlphaType::kerning);
        AlphaDraw::spacedText (g, "GR", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ geo.cx + geo.r * 0.3f, y }),
                               pal.faceLabel.withAlpha (dim), AlphaType::label, AlphaType::kerning);
    }

    void paintGonioFace (juce::Graphics& g, const AlphaPalette& pal, float dim, juce::Rectangle<float> footLabel)
    {
        const auto gg = gonioGeometry();
        juce::Path half;
        half.addCentredArc (gg.cx, gg.base, gg.r, gg.r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (pal.faceArc.withAlpha (0.55f * dim));
        g.strokePath (half, juce::PathStrokeType (1.2f));

        g.setColour (pal.faceArc.withAlpha (0.22f * dim));
        g.drawLine (gg.cx - gg.r, gg.base, gg.cx + gg.r, gg.base, 1.0f);
        for (float deg : { -45.0f, 0.0f, 45.0f })
        {
            const float a = juce::degreesToRadians (deg);
            g.drawLine (gg.cx, gg.base, gg.cx + std::sin (a) * gg.r, gg.base - std::cos (a) * gg.r, 1.0f);
        }
        for (auto [deg, text] : { std::pair<float, const char*> { -45.0f, "L" }, { 45.0f, "R" } })
        {
            const float a = juce::degreesToRadians (deg);
            AlphaDraw::spacedText (g, text, juce::Rectangle<float> (14.0f, 12.0f).withCentre ({ gg.cx + std::sin (a) * (gg.r + 8.0f),
                                                                                                 gg.base - std::cos (a) * (gg.r + 8.0f) }),
                                   pal.faceLabel.withAlpha (dim), AlphaType::label, 0.0f);
        }
        AlphaDraw::spacedText (g, "CORR", footLabel, pal.faceArc.withAlpha (0.8f * dim), AlphaType::label, AlphaType::kerning,
                               juce::Justification::centredLeft);
    }

    // Points en blanc ; points HORS PHASE (L et R de signes opposes) en
    // rouge, un peu plus gros pour rester visibles pres du zero (demande
    // explicite : "on doit voir aussi les points quand c'est dans le zero,
    // en rouge, les points hors phase").
    void paintGonioPoints (juce::Graphics& g, bool enabled) const
    {
        static constexpr float ages[4] = { 0.25f, 0.45f, 0.65f, 0.9f }; // du plus ancien au plus recent
        const bool redPhase = processor.gonioRedPhase.load(); // Options > NEEDLES > PHASE
        const auto red = juce::Colour (0xffff4d3d);

        // Mode LIGNES (Options > NEEDLES > GONIO) : trace qui relie les points
        // successifs ; un saut plus long que la moitie du rayon (repliement
        // du demi-cercle) n'est pas trace.
        if (processor.gonioLines.load())
        {
            const float maxJump = gonioGeometry().r * 0.5f;
            for (int grp = 0; grp < 4; ++grp)
            {
                juce::Path inPhase, outOfPhase;
                const int from = juce::jmax (1, numPoints * grp / 4), to = numPoints * (grp + 1) / 4;
                for (int k = from; k < to; ++k)
                {
                    const auto& a = points[(size_t) (k - 1)];
                    const auto& b = points[(size_t) k];
                    if (a.getDistanceFrom (b) > maxJump)
                        continue;
                    auto& path = (redPhase && (pointOutOfPhase[(size_t) k] || pointOutOfPhase[(size_t) (k - 1)])) ? outOfPhase : inPhase;
                    path.startNewSubPath (a);
                    path.lineTo (b);
                }
                const float alpha = ages[grp] * (enabled ? 1.0f : 0.4f);
                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.strokePath (inPhase, juce::PathStrokeType (1.0f));
                g.setColour (red.withAlpha (juce::jmin (1.0f, alpha + 0.1f)));
                g.strokePath (outOfPhase, juce::PathStrokeType (1.2f));
            }
            return;
        }

        for (int grp = 0; grp < 4; ++grp)
        {
            juce::RectangleList<float> inPhase, outOfPhase;
            const int from = numPoints * grp / 4, to = numPoints * (grp + 1) / 4;
            for (int k = from; k < to; ++k)
            {
                const auto& p = points[(size_t) k];
                if (redPhase && pointOutOfPhase[(size_t) k])
                    outOfPhase.addWithoutMerging ({ p.x - 0.9f, p.y - 0.9f, 1.8f, 1.8f });
                else
                    inPhase.addWithoutMerging ({ p.x - 0.7f, p.y - 0.7f, 1.4f, 1.4f });
            }
            const float a = ages[grp] * (enabled ? 1.0f : 0.4f);
            g.setColour (juce::Colours::white.withAlpha (a));
            g.fillRectList (inPhase);
            g.setColour (red.withAlpha (juce::jmin (1.0f, a + 0.1f)));
            g.fillRectList (outOfPhase);
        }
    }

    void timerCallback() override
    {
        const auto newMode = currentMode();
        if (newMode != mode)
        {
            mode = newMode;
            cacheDirty = true;
            numPoints = 0;
            peakSinceReadout = peakGr = peakExp = 0.0f;
            readoutTick = 8; // relire tout de suite
            repaint();
        }

        const bool enabled = isMeasuring();
        if (mode == Mode::gonio)
        {
            updateGonio (enabled);
            return;
        }

        float target = 0.0f;
        float grDb = 0.0f, expDb = 0.0f;
        if (mode == Mode::thd)
        {
            target = enabled ? juce::jlimit (0.0f, 1.0f, processor.clipMeterAmountBand[(size_t) band].load()) : 0.0f;
        }
        else if (enabled)
        {
            grDb  = juce::jmax (0.0f, processor.grMeterDb[(size_t) band].load());
            expDb = juce::jmax (0.0f, processor.grMeterExpandDb[(size_t) band].load());
            target = juce::jlimit (-1.0f, 1.0f, (grDb >= expDb ? grDb : -expDb) / kGrRangeDb);
        }

        // RETOUR -> raideur, REBOND -> amortissement (1.05 = pas de
        // depassement, 0.18 = rebond marque). Sous-pas pour rester stable
        // quand le rebond est fort.
        const float bounce = juce::jlimit (0.0f, 100.0f, processor.needleBounce.load()) / 100.0f;
        const float ret    = juce::jlimit (0.0f, 100.0f, processor.needleReturn.load()) / 100.0f;
        const float k = 55.0f + 230.0f * ret;
        const float zeta = 1.05f - 0.87f * bounce;
        const float c = 2.0f * zeta * std::sqrt (k);
        const float lo = mode == Mode::thd ? -0.03f : -1.06f, hi = mode == Mode::thd ? 1.2f : 1.06f;
        constexpr int steps = 4;
        const float h = (1.0f / 60.0f) / (float) steps;
        for (int s = 0; s < steps; ++s)
        {
            vel += (k * (target - pos) - c * vel) * h;
            pos += vel * h;
            if (pos < lo) { pos = lo; vel = -vel * 0.3f * bounce; }
            if (pos > hi) { pos = hi; vel = -vel * 0.3f * bounce; }
        }

        // Chiffres a ~7.5 Hz (crete de la fenetre, lisible).
        peakSinceReadout = juce::jmax (peakSinceReadout, target);
        peakGr = juce::jmax (peakGr, grDb);
        peakExp = juce::jmax (peakExp, expDb);
        bool textChanged = false;
        if (++readoutTick >= 8)
        {
            readoutTick = 0;
            if (mode == Mode::thd)
            {
                const auto fresh = enabled ? juce::String (juce::roundToInt (peakSinceReadout * 100.0f)) : juce::String ("--");
                if (fresh != readout) { readout = fresh; textChanged = true; }
            }
            else if (std::abs (peakGr - readoutGr) > 0.04f || std::abs (peakExp - readoutExp) > 0.04f)
            {
                readoutGr = peakGr;
                readoutExp = peakExp;
                textChanged = true;
            }
            peakSinceReadout = peakGr = peakExp = 0.0f;
        }

        if (std::abs (pos - lastPaintedPos) > 0.0015f || textChanged || enabled != cachedEnabled)
        {
            lastPaintedPos = pos;
            repaint();
        }
    }

    // Goniometre : derniers points de la bande, gain automatique (le nuage
    // occupe ~1/3 du rayon quel que soit le niveau), correlation lissee.
    void updateGonio (bool enabled)
    {
        auto& scope = processor.bandScopes[(size_t) band];
        const auto count = scope.writeCount();
        const bool fresh = count != lastScopeCount; // rien de neuf : bande fusionnee, lecture arretee...
        lastScopeCount = count;

        int n = 0;
        if (enabled && fresh)
            n = scope.copyLatest (scopeL.data(), scopeR.data(), kScopePoints);

        double sll = 0.0, srr = 0.0, slr = 0.0;
        for (int k = 0; k < n; ++k)
        {
            sll += (double) scopeL[(size_t) k] * scopeL[(size_t) k];
            srr += (double) scopeR[(size_t) k] * scopeR[(size_t) k];
            slr += (double) scopeL[(size_t) k] * scopeR[(size_t) k];
        }
        const float rms = n > 0 ? (float) std::sqrt ((sll + srr) / (2.0 * n)) : 0.0f;
        const bool hadPoints = numPoints > 0;

        if (rms > 1.0e-5f)
        {
            displayRms = displayRms <= 0.0f ? rms : displayRms + (rms - displayRms) * 0.2f;
            const double den = std::sqrt (sll * srr);
            if (den > 1.0e-12)
                corr += ((float) (slr / den) - corr) * 0.15f;

            const auto gg = gonioGeometry();
            const float scale = gg.r * 0.3f / juce::jmax (1.0e-5f, displayRms);
            for (int k = 0; k < n; ++k)
            {
                float m = (scopeL[(size_t) k] + scopeR[(size_t) k]) * 0.70710678f;
                float s = (scopeR[(size_t) k] - scopeL[(size_t) k]) * 0.70710678f;
                pointOutOfPhase[(size_t) k] = scopeL[(size_t) k] * scopeR[(size_t) k] < 0.0f;
                if (m < 0.0f) { m = -m; s = -s; }
                // Contenu DANS l'arc (demande explicite) : rayon limite, la
                // direction (donc la phase) est conservee.
                float px = s * scale, py = m * scale;
                const float dist = std::sqrt (px * px + py * py), maxR = gg.r - 1.0f;
                if (dist > maxR) { px *= maxR / dist; py *= maxR / dist; }
                points[(size_t) k] = { gg.cx + px, gg.base - py };
            }
            numPoints = n;
            silentTicks = 0;
        }
        else if (++silentTicks > 6)
        {
            numPoints = 0;
            displayRms = 0.0f;
        }

        bool textChanged = false;
        if (++readoutTick >= 8)
        {
            readoutTick = 0;
            const auto fresh = (enabled && numPoints > 0) ? (corr >= 0.0f ? "+" : "") + juce::String (corr, 2) : juce::String ("--");
            if (fresh != readout) { readout = fresh; textChanged = true; }
        }

        if (numPoints > 0 || hadPoints || textChanged || enabled != cachedEnabled)
            repaint();
    }

    SevenpackAudioProcessor& processor;
    const int band;
    std::atomic<float>& powerParam;
    std::atomic<float>& thdParam;

    Mode mode = Mode::thd, cachedMode = Mode::thd;
    float pos = 0.0f, vel = 0.0f, lastPaintedPos = -1.0f;
    float peakSinceReadout = 0.0f, peakGr = 0.0f, peakExp = 0.0f, readoutGr = 0.0f, readoutExp = 0.0f;
    int readoutTick = 0;
    juce::String readout { "0" };

    std::array<float, (size_t) kScopePoints> scopeL {}, scopeR {};
    std::array<juce::Point<float>, (size_t) kScopePoints> points {};
    std::array<bool, (size_t) kScopePoints> pointOutOfPhase {};
    int numPoints = 0, silentTicks = 0;
    juce::uint32 lastScopeCount = 0;
    float displayRms = 0.0f, corr = 1.0f;

    juce::Image cache;
    bool cacheDirty = true, cachedEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaThdNeedle)
};

/**
    AlphaGrExpMeter
    ---------------
    Diodes GR (reduction de gain, vert -> rouge) et EXP (expansion, bleu ->
    violet) d'une bande, derriere le fader OUT -- memes diodes, degrades et
    calibrage que Sixpack. Composant OPAQUE avec son propre timer (audit :
    "les diodes GR/EXP redessinent toute la carte") : un rafraichissement
    des diodes ne repeint plus que ce rectangle, jamais toute la tranche.
    N'intercepte aucun clic (le fader est pose par-dessus).
*/
class AlphaGrExpMeter : public juce::Component, private juce::Timer
{
public:
    AlphaGrExpMeter (SevenpackAudioProcessor& p, int bandIndex)
        : processor (p), band (bandIndex),
          powerParam (*p.apvts.getRawParameterValue (SevenpackAudioProcessor::idPower))
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (true);
        startTimerHz (30);
    }

    ~AlphaGrExpMeter() override { stopTimer(); }

    // Carte de la tranche dans le repere des diodes (repeinte derriere
    // elles : composant opaque, voir AlphaDraw::paintCard()).
    void setCardArea (juce::Rectangle<float> r) { if (r != cardArea) { cardArea = r; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();
        const int colourMode = juce::jlimit (0, 2, processor.bandColourMode.load());
        if (cardArea.isEmpty())
            g.fillAll (pal.cardFor (band, colourMode));
        else
            AlphaDraw::paintCard (g, cardArea, pal.cardFor (band, colourMode), pal);

        const bool powered = powerParam.load() > 0.5f;
        constexpr int numLeds = 18;
        constexpr float kExpandLedDb = 0.25f;
        const float grDb = juce::jlimit (0.0f, 30.0f, processor.grMeterDb[(size_t) band].load());
        const float exDb = juce::jlimit (0.0f, kExpandLedDb * (float) numLeds, processor.grMeterExpandDb[(size_t) band].load());
        const int litGr = powered ? (int) std::round (grDbToFraction (grDb) * (float) numLeds) : 0;
        const int litEx = powered ? (int) std::round (exDb / kExpandLedDb) : 0;

        auto area = getLocalBounds().toFloat();
        auto labels = area.removeFromTop (12.0f);
        area.removeFromTop (2.0f);
        const float colW = 12.0f;
        const auto grCol  = juce::Rectangle<float> (area.getX() + 5.0f,  area.getY(), colW, area.getHeight());
        const auto expCol = juce::Rectangle<float> (area.getX() + 27.0f, area.getY(), colW, area.getHeight());

        auto drawColumn = [&] (juce::Rectangle<float> col, int lit, bool expand)
        {
            const float step = col.getHeight() / (float) numLeds;
            const float dot = juce::jmin (8.0f, step - 2.5f, col.getWidth());
            for (int i = 0; i < numLeds; ++i)
            {
                const float t = (float) i / (float) (numLeds - 1);
                auto r = juce::Rectangle<float> (dot, dot).withCentre ({ col.getCentreX(), col.getY() + step * ((float) i + 0.5f) });
                if (i < lit)
                {
                    const auto c = expand ? SevenpackLookAndFeel::ledGradientColourExpand (t) : SevenpackLookAndFeel::ledGradientColour (t);
                    g.setColour (c.withAlpha (0.35f));
                    g.fillEllipse (r.expanded (1.5f));
                    g.setColour (c);
                }
                else
                {
                    g.setColour (pal.light ? pal.track : SevenpackColours::ledOff);
                }
                g.fillEllipse (r);
            }
        };
        drawColumn (grCol, litGr, false);
        drawColumn (expCol, litEx, true);

        g.setFont (AlphaDraw::font (AlphaType::label - 1.0f));
        g.setColour (pal.cardInk.withAlpha (0.75f));
        g.drawText ("GR",  juce::Rectangle<float> (grCol.getCentreX() - 11.0f,  labels.getY(), 22.0f, labels.getHeight()), juce::Justification::centred, false);
        g.drawText ("EXP", juce::Rectangle<float> (expCol.getCentreX() - 11.0f, labels.getY(), 22.0f, labels.getHeight()), juce::Justification::centred, false);
    }

private:
    // Meme echelle non-lineaire que Sixpack/GRMeter (fine pres de 0 dB).
    static float grDbToFraction (float db) noexcept
    {
        static constexpr float marks[] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
        constexpr int numMarks = 10;
        db = juce::jlimit (0.0f, marks[numMarks - 1], db);
        for (int i = 0; i < numMarks - 1; ++i)
            if (db <= marks[i + 1])
            {
                const float segFrac = (marks[i + 1] > marks[i]) ? (db - marks[i]) / (marks[i + 1] - marks[i]) : 0.0f;
                return ((float) i + segFrac) / (float) (numMarks - 1);
            }
        return 1.0f;
    }

    void timerCallback() override
    {
        const float gr = processor.grMeterDb[(size_t) band].load();
        const float ex = processor.grMeterExpandDb[(size_t) band].load();
        const bool powered = powerParam.load() > 0.5f;
        const int colourMode = processor.bandColourMode.load();
        if (std::abs (gr - lastGr) > 0.05f || std::abs (ex - lastEx) > 0.02f || powered != lastPowered || colourMode != lastColourMode)
        {
            lastGr = gr; lastEx = ex; lastPowered = powered; lastColourMode = colourMode;
            repaint();
        }
    }

    SevenpackAudioProcessor& processor;
    const int band;
    std::atomic<float>& powerParam;
    juce::Rectangle<float> cardArea;
    float lastGr = -1.0f, lastEx = -1.0f;
    bool lastPowered = true;
    int lastColourMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaGrExpMeter)
};

/**
    AlphaLearnFx
    ------------
    Animation GLOBALE du plugin pendant LEARN (demande explicite : "une
    belle animation du VST quand on clique sur learn"). Calque transparent
    au-dessus du graphe et des tranches, purement visuel (n'intercepte aucun
    clic, ne touche a aucun parametre -- LEARN reste identique a Sixpack) :
      - pendant l'ecoute (8 s) puis l'analyse : un balayage lumineux
        traverse le plugin de gauche a droite, chaque tranche s'illumine a
        son passage, une barre de progression se remplit en bas a droite
        (barre du bas, demande explicite -- elle etait sous le logo) ;
      - a la fin : une vague parcourt les 7 tranches, de LOW a AIR.

    PERFORMANCE (audit "LEARN redessine tout le plugin 60 fois par
    seconde") : 40 images/s, et a chaque image seules les zones qui
    changent sont invalidees (bande de balayage, tranches illuminees, barre
    de progression) ; les halos sont de simples contours superposes, plus
    aucun flou recalcule.
*/
class AlphaLearnFx : public juce::Component, private juce::Timer
{
public:
    explicit AlphaLearnFx (SevenpackAudioProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }

    ~AlphaLearnFx() override { stopTimer(); }

    void setTargets (juce::Rectangle<float> panel, juce::Rectangle<float> graph,
                     const std::array<juce::Rectangle<float>, 7>& strips, juce::Rectangle<float> progressBar)
    {
        panelArea = panel;
        graphArea = graph;
        stripAreas = strips;
        progressArea = progressBar;
        effectBottom = graphArea.getBottom();
        for (const auto& s : stripAreas)
            effectBottom = juce::jmax (effectBottom, s.getBottom());
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = AlphaPalette::get();

        if (env > 0.005f)
        {
            const float top = graphArea.getY();
            const auto band = sweepRect (sweepX);
            {
                g.saveState();
                juce::Path clip; clip.addRoundedRectangle (panelArea, 12.0f);
                g.reduceClipRegion (clip);
                juce::ColourGradient grad (pal.accent.withAlpha (0.0f), band.getX(), 0.0f,
                                           pal.accent.withAlpha (0.0f), band.getRight(), 0.0f, false);
                grad.addColour (0.5, pal.accent.withAlpha (0.22f * env));
                g.setGradientFill (grad);
                g.fillRect (band);
                g.setColour (juce::Colours::white.withAlpha (0.6f * env));
                g.fillRect (sweepX - 0.75f, top, 1.5f, effectBottom - top);
                g.restoreState();
            }

            for (size_t i = 0; i < stripAreas.size(); ++i)
                if (stripGlow[i] > 0.01f)
                    glowStrip (g, stripAreas[i], pal.accent, stripGlow[i]);

            if (! progressArea.isEmpty())
            {
                // Libelle, piste, pourcentage (bas a droite de la barre du bas).
                auto area = progressArea;
                const auto labelArea = area.removeFromLeft (58.0f);
                const auto pctArea = area.removeFromRight (34.0f);
                area.removeFromLeft (4.0f);
                area.removeFromRight (8.0f);
                const auto bar = area.withSizeKeepingCentre (area.getWidth(), 6.0f);
                const float shown = busy ? progress : 1.0f;

                AlphaDraw::spacedText (g, busy ? "LEARN" : "DONE", labelArea, (pal.light ? pal.accentDeep : pal.accent).withAlpha (env),
                                       AlphaType::label, AlphaType::kerning, juce::Justification::centredLeft);
                g.setColour (pal.track.withAlpha (0.9f * env));
                g.fillRoundedRectangle (bar, 3.0f);
                g.setColour (pal.accent.withAlpha (env));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * shown), 3.0f);
                if (analysing)
                {
                    const float sx = bar.getX() + shimmer * bar.getWidth();
                    juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), sx - 30.0f, 0.0f,
                                             juce::Colours::white.withAlpha (0.0f), sx + 30.0f, 0.0f, false);
                    sh.addColour (0.5, juce::Colours::white.withAlpha (0.8f * env));
                    g.saveState();
                    juce::Path barClip; barClip.addRoundedRectangle (bar, 3.0f);
                    g.reduceClipRegion (barClip);
                    g.setGradientFill (sh);
                    g.fillRect (bar);
                    g.restoreState();
                }
                g.setColour (pal.ink.withAlpha (env));
                g.setFont (AlphaDraw::lcdFont (AlphaType::value));
                g.drawText (juce::String (juce::roundToInt (shown * 100.0f)) + "%", pctArea, juce::Justification::centredRight, false);
            }
        }

        for (size_t i = 0; i < stripAreas.size(); ++i)
            if (landingGlow[i] > 0.01f)
                glowStrip (g, stripAreas[i], juce::Colours::white, landingGlow[i]);
    }

private:
    static constexpr double kLandingSeconds = 0.55;
    static constexpr float kSweepW = 170.0f;

    juce::Rectangle<float> sweepRect (float x) const
    {
        return { x - kSweepW * 0.5f, graphArea.getY(), kSweepW, effectBottom - graphArea.getY() };
    }

    // Halo sans flou : quelques contours superposes de plus en plus larges.
    static void glowStrip (juce::Graphics& g, juce::Rectangle<float> s, juce::Colour colour, float k)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (colour.withAlpha (0.16f * k * (float) (4 - i)));
            g.drawRoundedRectangle (s.expanded ((float) i * 1.6f), 10.0f + (float) i, 2.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f * k));
        g.fillRoundedRectangle (s, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f * k));
        g.drawRoundedRectangle (s, 10.0f, 1.6f);
    }

    void timerCallback() override
    {
        const auto st = processor.aiLearnAnalyzer.getState();
        const bool nowBusy = st == AiLearnAnalyzer::State::Capturing || st == AiLearnAnalyzer::State::Analyzing;
        const double now = juce::Time::getMillisecondCounterHiRes();

        if (nowBusy && ! busy)
            learnStartMs = now;
        if (! nowBusy && busy)
            landingStartMs = now;
        busy = nowBusy;

        env += ((busy ? 1.0f : 0.0f) - env) * (busy ? 0.12f : 0.2f);
        if (! busy && env < 0.004f)
            env = 0.0f;

        // --- Etat de cette image ---
        const double t = (now - learnStartMs) / 1000.0;
        const float newSweepX = panelArea.getX() - kSweepW
                                + (float) std::fmod (t / 1.8, 1.0) * (panelArea.getWidth() + 2.0f * kSweepW);
        analysing = st == AiLearnAnalyzer::State::Analyzing;
        progress = analysing ? 1.0f : (float) juce::jlimit (0.0, 0.97, t / 8.0);
        shimmer = (float) std::fmod (t * 0.9, 1.0);

        std::array<float, 7> newGlow {}, newLanding {};
        for (size_t i = 0; i < stripAreas.size(); ++i)
        {
            newGlow[i] = env > 0.005f ? juce::jmax (0.0f, 1.0f - std::abs (stripAreas[i].getCentreX() - newSweepX) / 210.0f) * env : 0.0f;
            if (landingStartMs > 0.0)
            {
                const double local = (now - landingStartMs) / 1000.0 - 0.07 * (double) i;
                if (local > 0.0 && local < kLandingSeconds)
                    newLanding[i] = (float) std::sin (juce::MathConstants<double>::pi * local / kLandingSeconds);
            }
        }
        if (landingStartMs > 0.0 && (now - landingStartMs) / 1000.0 > kLandingSeconds + 0.07 * 7.0)
            landingStartMs = 0.0;

        // --- Invalidation PARTIELLE : seulement ce qui bouge ---
        const bool sweepVisible = env > 0.005f || lastEnv > 0.005f;
        if (sweepVisible)
        {
            repaint (sweepRect (sweepX).getUnion (sweepRect (newSweepX)).expanded (2.0f).getSmallestIntegerContainer());
            if (! progressArea.isEmpty())
                repaint (progressArea.expanded (2.0f).getSmallestIntegerContainer());
        }
        for (size_t i = 0; i < stripAreas.size(); ++i)
        {
            const bool changed = std::abs (newGlow[i] - stripGlow[i]) > 0.01f || std::abs (newLanding[i] - landingGlow[i]) > 0.01f;
            if (changed)
                repaint (stripAreas[i].expanded (7.0f).getSmallestIntegerContainer());
        }

        sweepX = newSweepX;
        stripGlow = newGlow;
        landingGlow = newLanding;
        lastEnv = env;

        const bool animating = env > 0.0f || landingStartMs > 0.0;
        const int wantedHz = animating ? 40 : 15;
        if (wantedHz != currentHz)
        {
            currentHz = wantedHz;
            startTimerHz (wantedHz);
        }
    }

    SevenpackAudioProcessor& processor;
    juce::Rectangle<float> panelArea, graphArea, progressArea;
    std::array<juce::Rectangle<float>, 7> stripAreas {};
    float effectBottom = 0.0f;

    bool busy = false, analysing = false;
    float env = 0.0f, lastEnv = 0.0f, sweepX = -1000.0f, progress = 0.0f, shimmer = 0.0f;
    std::array<float, 7> stripGlow {}, landingGlow {};
    double learnStartMs = 0.0, landingStartMs = 0.0;
    int currentHz = 15;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlphaLearnFx)
};
