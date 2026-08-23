#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "PluginProcessor.h"

/**
    NeedleVU
    --------
    VU-metre a aiguille (port du SabotageNeedleVU de M76 -- aiguille
    modelisee comme une masse ressort-amorti, mapping non-lineaire dB ->
    angle, aiguille facettee avec degrade 3D, monogramme Side Key integre
    au pivot), adapte a l'unite stereo unique de Suppressor (pas de bande).

    Chaque instance est dediee a un CANAL (Left ou Right), avec son propre
    NeedleModeSelector (GR / IN / OUT / THD, voir PluginEditor) qui
    choisit ce que l'aiguille affiche. Juste l'aiguille + le cadran --
    pas de pied/lecture numerique separee.

    OPTIMISATION CPU : le cadran (grain, graduations, arc rouge, vitre)
    est mis en cache dans une Image, reconstruit seulement au resize ou
    quand mode/power changent -- paint() se contente de blitter cette
    image puis dessiner l'aiguille (qui bouge).
*/
class NeedleVU : public juce::Component, private juce::Timer
{
public:
    enum class Mode { Input = 0, Output = 1, GainReduction = 2, ThdAmount = 3 };
    enum class Channel { Left, Right };

    // Correspondance avec l'ordre des boutons de NeedleModeSelector :
    // 0=IN 1=OUT 2=THD (GR retire, pas utile pour ce cadran global) --
    // source unique utilisee a la fois par PluginEditor (construction +
    // onSelect) pour rester synchronisee.
    static Mode modeForIndex (int idx) noexcept
    {
        switch (idx)
        {
            case 1:  return Mode::Output;
            case 2:  return Mode::ThdAmount;
            default: return Mode::Input;
        }
    }

    // externalAmountSource / accentOverride / needleColourOverride : voir
    // ThdValveModule -- permettent de reutiliser TEL QUEL ce composant (meme
    // cadran/aiguille que les VU de la barre de controle) pour les 3
    // aiguilles THD par bande (Low/Mid/High), en lisant directement un
    // atomic<float>& (deja 0..1) au lieu de processor.clipMeterAmountL/R, et
    // en teintant graduations/aiguille de la couleur d'accent DE LA BANDE au
    // lieu du cyan/ivoire fixe. Valeurs par defaut = comportement D'ORIGINE
    // inchange pour les VU de la barre de controle (aucun site d'appel
    // existant n'a besoin d'etre modifie).
    // externalEnabledSource : optionnel, meme traitement que le Power global
    // (aiguille retombe a zero, cadran/graduations/aiguille s'assombrissent,
    // ballistique cible = rangeLo(), AUCUNE lecture de clipAmount()/inDb()/etc
    // tant que desactive) mais pilote par un atomic<float> LOCAL au lieu du
    // Power -- utilise par ThdValveModule pour ses 3 aiguilles (idSoftClip).
    NeedleVU (SixpackAudioProcessor& p, Mode initialMode, Channel ch,
              std::atomic<float>* externalAmountSource = nullptr,
              juce::Colour accentOverride = SixpackColours::ledCyan,
              juce::Colour needleColourOverride = juce::Colour (0xFFF4E9C8),
              std::atomic<float>* externalEnabledSource = nullptr)
        : processor (p), mode (initialMode), channel (ch),
          externalAmount (externalAmountSource), tickAccent (accentOverride), needleColourBase (needleColourOverride),
          externalEnabled (externalEnabledSource)
    {
        skImage = juce::ImageCache::getFromMemory (BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
        needleDb = rangeLo();
        startTimerHz (60);
    }

    ~NeedleVU() override { stopTimer(); }

    // Marge du cadre metal "visse sur la plaque" (effet extrusion) --
    // l'ecran (gauge + pied) est encastre a l'interieur, pas colle aux
    // bords du composant.
    static constexpr int frameMargin = 9;

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        gaugeArea = { frameMargin, frameMargin, W - frameMargin * 2, H - frameMargin * 2 };
        staticCacheDirty = true;
    }

    // Change de mode -- appele par NeedleModeSelector (voir PluginEditor).
    void setMode (Mode m)
    {
        if (m == mode)
            return;
        mode = m;
        needleDb = rangeLo();
        needleVel = 0.0f;
        staticCacheDirty = true;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int W = getWidth(), H = getHeight();
        if (W < 10 || H < 10) return;

        const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = ((laf == nullptr) || laf->isPowered())
                              && (externalEnabled == nullptr || externalEnabled->load() > 0.5f);
        if (staticCacheDirty || powered != cachedPowered)
            rebuildStaticCache (powered);

        g.drawImageAt (staticCache, 0, 0);

        const auto ga = gaugeArea.toFloat().reduced (5.0f);
        const float cx = ga.getCentreX();
        const float cy = ga.getBottom() - ga.getHeight() * 0.12f;
        const float radius = juce::jmin (ga.getWidth() * 0.46f, ga.getHeight() * 0.72f);
        const float dim = powered ? 1.0f : 0.12f;

        auto pointOnArc = [&] (float angDeg, float r)
        {
            const float rad = juce::degreesToRadians (angDeg);
            return juce::Point<float> (cx + std::sin (rad) * r, cy - std::cos (rad) * r);
        };

        // --- Aiguille (masse ressort-amorti, facettee, degrade 3D) ---
        const float pivotR = juce::jlimit (9.0f, 13.0f, radius * 0.11f);
        {
            const float ang = angleFor (needleDb);
            const auto tip = pointOnArc (ang, radius * 0.92f);
            const juce::Colour needleCol = needleColourBase.withMultipliedBrightness (juce::jmax (0.25f, dim));
            const float baseW = 3.0f;
            const float dx = tip.x - cx, dy = tip.y - cy;
            const float len = std::sqrt (dx * dx + dy * dy);
            const float px = (len > 0.0001f) ? (-dy / len * baseW) : 0.0f;
            const float py = (len > 0.0001f) ? ( dx / len * baseW) : 0.0f;

            juce::Path shadow;
            shadow.startNewSubPath (cx - px, cy - py);
            shadow.lineTo (cx + px, cy + py);
            shadow.lineTo (tip.x, tip.y);
            shadow.closeSubPath();
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillPath (shadow, juce::AffineTransform::translation (0.7f, 0.7f));

            juce::Path faceL, faceR;
            faceL.startNewSubPath (cx - px, cy - py); faceL.lineTo (cx, cy); faceL.lineTo (tip.x, tip.y); faceL.closeSubPath();
            faceR.startNewSubPath (cx, cy); faceR.lineTo (cx + px, cy + py); faceR.lineTo (tip.x, tip.y); faceR.closeSubPath();

            juce::ColourGradient ngDark (needleCol.darker (0.55f).withAlpha (0.9f), cx, cy, needleCol.darker (0.15f), tip.x, tip.y, false);
            g.setGradientFill (ngDark); g.fillPath (faceL);

            juce::ColourGradient ngLight (needleCol.withAlpha (0.95f), cx, cy, needleCol.brighter (0.4f), tip.x, tip.y, false);
            g.setGradientFill (ngLight); g.fillPath (faceR);

            juce::Path outline;
            outline.startNewSubPath (cx - px, cy - py); outline.lineTo (tip.x, tip.y); outline.lineTo (cx + px, cy + py);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.strokePath (outline, juce::PathStrokeType (0.8f));

            g.setColour (juce::Colours::white.withAlpha (0.85f * dim));
            g.drawLine (cx, cy, tip.x, tip.y, 0.9f);

            g.setColour (juce::Colour (0xff17181a));
            g.fillEllipse (cx - pivotR, cy - pivotR, pivotR * 2.0f, pivotR * 2.0f);
            g.setColour (needleCol.withAlpha (0.6f));
            g.drawEllipse (cx - pivotR, cy - pivotR, pivotR * 2.0f, pivotR * 2.0f, 1.2f);

            // Monogramme -- tourne avec l'aiguille, dessine par-dessus le
            // pivot, doit rester dynamique (pas dans le cache statique).
            if (! skImage.isNull())
            {
                const float imgH = pivotR * 1.5f;
                const float imgW = imgH * ((float) skImage.getWidth() / (float) juce::jmax (1, skImage.getHeight()));
                auto imgBounds = juce::Rectangle<float> (imgW, imgH).withCentre ({ cx, cy });

                g.saveState();
                g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (ang), cx, cy));
                g.setOpacity (0.85f * dim);
                g.drawImage (skImage, juce::roundToInt (imgBounds.getX()), juce::roundToInt (imgBounds.getY()),
                             juce::roundToInt (imgBounds.getWidth()), juce::roundToInt (imgBounds.getHeight()),
                             0, 0, skImage.getWidth(), skImage.getHeight());
                g.setOpacity (1.0f);
                g.restoreState();
            }
        }
    }

private:
    // Lecture du canal (Left/Right) dedie a cette instance.
    float inDb() const   { return channel == Channel::Left ? processor.inputLevelDbL.load()  : processor.inputLevelDbR.load(); }
    float outDb() const  { return channel == Channel::Left ? processor.outputLevelDbL.load() : processor.outputLevelDbR.load(); }
    float grDb() const   { return channel == Channel::Left ? processor.grMeterDbL.load()      : processor.grMeterDbR.load(); }
    float clipAmount() const
    {
        if (externalAmount != nullptr)
            return juce::jlimit (0.0f, 1.0f, externalAmount->load());
        return channel == Channel::Left ? processor.clipMeterAmountL.load() : processor.clipMeterAmountR.load();
    }

    void rebuildStaticCache (bool powered)
    {
        const int W = getWidth(), H = getHeight();
        staticCache = juce::Image (juce::Image::ARGB, juce::jmax (1, W), juce::jmax (1, H), true);
        juce::Graphics g (staticCache);

        const float dim = powered ? 1.0f : 0.12f;

        auto full = getLocalBounds().toFloat();

        // --- Cadre metal "module visse sur la plaque" (effet extrusion
        // demande) : le meme langage que ThdValveModule -- l'ecran (gauge
        // + pied) apparait encastre dans le chassis plutot que de flotter
        // en boite plate. ---
        juce::ColourGradient frameGrad (juce::Colour (0xff3c3f44), full.getX(), full.getY(),
                                         juce::Colour (0xff1a1c1f), full.getX(), full.getBottom(), false);
        g.setGradientFill (frameGrad);
        g.fillRoundedRectangle (full, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (full.reduced (0.75f), 6.0f, 1.0f);
        SixpackLookAndFeel::renderCornerScrews (g, full, 8.0f, 2.4f);

        auto inset = full.reduced ((float) frameMargin);
        g.setColour (juce::Colour (0xff0d0f11));
        g.fillRoundedRectangle (inset, 4.0f);
        juce::ColourGradient innerShadow (juce::Colours::black.withAlpha (0.7f), inset.getCentreX(), inset.getY(),
                                           juce::Colours::transparentBlack, inset.getCentreX(), inset.getY() + inset.getHeight() * 0.25f, false);
        g.setGradientFill (innerShadow);
        g.fillRoundedRectangle (inset, 4.0f);

        // Grain fin deterministe -- calcule une seule fois ici.
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (inset, 4.0f);
            g.reduceClipRegion (clip);
            juce::Random rng (7);
            const int count = (int) (inset.getWidth() * inset.getHeight() * 0.010f);
            for (int i = 0; i < count; ++i)
            {
                const float gx = inset.getX() + rng.nextFloat() * inset.getWidth();
                const float gy = inset.getY() + rng.nextFloat() * inset.getHeight();
                g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.03f));
                g.fillRect (gx, gy, 1.0f, 1.0f);
            }
            g.restoreState();
        }

        const auto ga = gaugeArea.toFloat().reduced (5.0f);
        const float cx = ga.getCentreX();
        const float cy = ga.getBottom() - ga.getHeight() * 0.12f;
        const float radius = juce::jmin (ga.getWidth() * 0.46f, ga.getHeight() * 0.72f);

        if (powered)
        {
            juce::ColourGradient halo (tickAccent.withAlpha (0.07f), cx, cy - radius * 0.4f,
                                        juce::Colours::transparentBlack, cx, cy - radius * 1.1f, false);
            g.setGradientFill (halo);
            g.fillRect (ga);
        }

        auto pointOnArc = [&] (float angDeg, float r)
        {
            const float rad = juce::degreesToRadians (angDeg);
            return juce::Point<float> (cx + std::sin (rad) * r, cy - std::cos (rad) * r);
        };

        const float* dbs = currentDbs();
        const int n = currentN();

        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (8.0f, 11.0f, radius * 0.12f))));
        for (int i = 0; i < n; ++i)
        {
            const float db = dbs[i];
            const float ang = angleFor (db);
            const bool hot = isHot (db);
            const auto p0 = pointOnArc (ang, radius * 0.86f);
            const auto p1 = pointOnArc (ang, radius * 1.0f);
            g.setColour ((hot ? SixpackColours::ledRed : tickAccent).withAlpha (dim));
            g.drawLine (p0.x, p0.y, p1.x, p1.y, juce::approximatelyEqual (db, dbs[0]) ? 2.0f : 1.4f);
            const auto lp = pointOnArc (ang, radius * 1.16f);
            g.setColour (tickAccent.withAlpha (dim));
            g.drawText (tickLabel (db), (int) lp.x - 16, (int) lp.y - 6, 32, 12, juce::Justification::centred);
        }

        {
            juce::Path redArc;
            const int steps = 24;
            const float loHot = hotRangeLo();
            const float hiHot = dbs[n - 1];
            for (int i = 0; i <= steps; ++i)
            {
                const float db = loHot + (hiHot - loHot) * (float) i / (float) steps;
                const auto pt = pointOnArc (angleFor (db), radius * 1.0f);
                if (i == 0) redArc.startNewSubPath (pt); else redArc.lineTo (pt);
            }
            g.setColour (SixpackColours::ledRed.withAlpha (0.85f * dim));
            g.strokePath (redArc, juce::PathStrokeType (2.0f));
        }

        // --- Effet vitre : reflet diagonal + lisere ---
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (inset, 4.0f);
            g.reduceClipRegion (clip);

            juce::Path sheen;
            sheen.addQuadrilateral (inset.getX(), inset.getY(),
                                     inset.getX() + inset.getWidth() * 0.45f, inset.getY(),
                                     inset.getX() + inset.getWidth() * 0.20f, inset.getBottom(),
                                     inset.getX() - inset.getWidth() * 0.05f, inset.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillPath (sheen);

            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (inset.reduced (0.5f), 5.0f, 1.0f);
            g.restoreState();
        }

        cachedPowered = powered;
        staticCacheDirty = false;
    }

    void timerCallback() override
    {
        const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = ((laf == nullptr) || laf->isPowered())
                              && (externalEnabled == nullptr || externalEnabled->load() > 0.5f);

        float target;
        if (! powered)
        {
            target = rangeLo();
        }
        else
        {
            switch (mode)
            {
                case Mode::Input:         target = inDb(); break;
                case Mode::Output:        target = outDb(); break;
                case Mode::GainReduction: target = grDb(); break;
                case Mode::ThdAmount: default: target = clipAmount() * 100.0f; break;
            }
            target = juce::jlimit (rangeLo(), rangeHi(), target);
        }

        const float dt = 1.0f / 60.0f;
        constexpr float spring = 280.0f;
        constexpr float damping = 30.0f;
        const float accel = (target - needleDb) * spring - needleVel * damping;
        needleVel += accel * dt;
        needleDb  += needleVel * dt;

        if (std::abs (needleDb - target) < 0.02f && std::abs (needleVel) < 0.5f)
        {
            needleDb = target;
            needleVel = 0.0f;
        }

        repaint();
    }

    float rangeLo() const { return mode == Mode::GainReduction || mode == Mode::ThdAmount ? 0.0f : -24.0f; }
    float rangeHi() const { return mode == Mode::GainReduction ? 30.0f : (mode == Mode::ThdAmount ? 100.0f : 3.0f); }
    float hotRangeLo() const { return mode == Mode::GainReduction ? 24.0f : (mode == Mode::ThdAmount ? 70.0f : 1.0f); }

    bool isHot (float db) const
    {
        return mode == Mode::GainReduction ? db > hotRangeLo()
             : mode == Mode::ThdAmount     ? db > hotRangeLo()
                                            : db > hotRangeLo();
    }

    juce::String tickLabel (float db) const
    {
        if (mode == Mode::ThdAmount)
            return juce::String ((int) db) + "%";
        if (mode == Mode::GainReduction)
            return juce::String ((int) db);
        return (db > 0.0f ? "+" : "") + juce::String ((int) db);
    }

    static constexpr float levelDbs[]    = { -24.f, -12.f, -6.f, -3.f, 0.f, 1.f, 2.f, 3.f };
    static constexpr float levelAngles[] = { -50.f, -34.f, -21.f, -13.f, 0.f, 15.f, 30.f, 46.f };
    static constexpr float grDbs[]       = { 0.f, 5.f, 10.f, 15.f, 20.f, 25.f, 30.f };
    static constexpr float grAngles[]    = { -50.f, -30.f, -14.f, 0.f, 16.f, 32.f, 46.f };
    static constexpr float thdPct[]      = { 0.f, 25.f, 50.f, 75.f, 100.f };
    static constexpr float thdAngles[]   = { -50.f, -25.f, 0.f, 25.f, 50.f };

    const float* currentDbs() const
    {
        return mode == Mode::GainReduction ? grDbs : (mode == Mode::ThdAmount ? thdPct : levelDbs);
    }
    const float* currentAngles() const
    {
        return mode == Mode::GainReduction ? grAngles : (mode == Mode::ThdAmount ? thdAngles : levelAngles);
    }
    int currentN() const
    {
        return mode == Mode::GainReduction ? 7 : (mode == Mode::ThdAmount ? 5 : 8);
    }

    float angleFor (float db) const
    {
        const float* dbs = currentDbs();
        const float* angles = currentAngles();
        const int n = currentN();
        db = juce::jlimit (dbs[0], dbs[n - 1], db);
        for (int i = 0; i < n - 1; ++i)
            if (db <= dbs[i + 1])
            {
                const float t = (db - dbs[i]) / (dbs[i + 1] - dbs[i]);
                return angles[i] + t * (angles[i + 1] - angles[i]);
            }
        return angles[n - 1];
    }

    SixpackAudioProcessor& processor;
    Mode mode;
    Channel channel;

    // Voir constructeur -- nullptr = comportement d'origine (lit processor.
    // clipMeterAmountL/R selon channel), non-null = usage "aiguille de
    // bande" (ThdValveModule), lit directement cette source.
    std::atomic<float>* externalAmount = nullptr;
    juce::Colour tickAccent;
    juce::Colour needleColourBase;

    // nullptr = comportement d'origine (seul le Power global dim l'aiguille).
    std::atomic<float>* externalEnabled = nullptr;

    float needleDb = -24.0f;
    float needleVel = 0.0f;

    juce::Image skImage;
    juce::Rectangle<int> gaugeArea;

    juce::Image staticCache;
    bool staticCacheDirty = true;
    bool cachedPowered = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleVU)
};
