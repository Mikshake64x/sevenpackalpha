#pragma once
#include <JuceHeader.h>
#include "AlphaSkin.h"

/**
    Pictogrammes animes ATT / REL des bandes, a cote du libelle des knobs
    (Options > Appearance > Attack / release icons ; apercu
    claude.ai/artifact/RWC63P2BUzjP1Kjofc1zPD, demande explicite : "les 3
    dans le menu option avec la possibilite de desactiver pour revenir a la
    version actuelle") :
      1 CURVE : courbe de gain -- la reduction de gain plonge a l'attaque et
                remonte au release ; pente plus douce = temps plus long ;
                zone teintee = passage fort ;
      2 WAVE  : forme d'onde -- ATT : le debut du coup passe en entier puis
                il est ecrase ; REL : apres un passage fort, le son suivant
                ressort ecrase puis revient peu a peu (pompage) ;
      3 LEDS  : diodes de reduction au ralenti -- elles s'allument a la
                vitesse de l'attaque, s'eteignent a la vitesse du release ;
                figees : etat 0,25 s apres le debut de l'attaque / 0,5 s
                apres le debut du release.
    Figes au repos, animes pendant le reglage du knob (voir AlphaStrip).
*/
namespace AlphaAttRel
{
    enum Style { off = 0, curve, wave, leds };

    static constexpr float kWidth = 26.0f;          // pictogramme a cote du libelle (ligne de 12 px)
    static constexpr double kAnimMs = 1400.0;       // l'animation continue ~1,4 s apres le dernier reglage

    // 0 = temps le plus court de la plage du parametre, 1 = le plus long
    // (echelle log ; ATT 0,05..40 ms, REL 50..3000 ms).
    inline float slowness (bool attack, double ms)
    {
        const double lo = attack ? 0.05 : 50.0, hi = attack ? 40.0 : 3000.0;
        return (float) juce::jlimit (0.0, 1.0, std::log (juce::jmax (lo, ms) / lo) / std::log (hi / lo));
    }

    struct Ink { juce::Colour stroke, glow, faint, off; };

    inline Ink inkFor (const AlphaPalette& pal, float alpha)
    {
        Ink ink = pal.light ? Ink { pal.accentDeep, pal.accent, pal.accentDeep.withAlpha (0.16f), pal.cardInk.withAlpha (0.14f) }
                            : Ink { pal.accent, juce::Colour (0xff7cc6ff), pal.accent.withAlpha (0.20f), pal.cardInk.withAlpha (0.12f) };
        ink.stroke = ink.stroke.withMultipliedAlpha (alpha);
        ink.glow   = ink.glow.withMultipliedAlpha (alpha);
        ink.faint  = ink.faint.withMultipliedAlpha (alpha);
        ink.off    = ink.off.withMultipliedAlpha (alpha);
        return ink;
    }

    // Tete lumineuse qui parcourt le trace pendant l'animation.
    inline void head (juce::Graphics& g, float x, float y, float lw, const Ink& ink)
    {
        g.setColour (ink.glow.withMultipliedAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (lw * 4.4f, lw * 4.4f).withCentre ({ x, y }));
        g.setColour (ink.glow);
        g.fillEllipse (juce::Rectangle<float> (lw * 2.4f, lw * 2.4f).withCentre ({ x, y }));
    }

    template <typename Fn>
    juce::Path trace (Fn&& yOf, float x1, float x2, float step)
    {
        juce::Path p;
        p.startNewSubPath (x1, yOf (x1));
        for (float x = x1 + step; x < x2; x += step)
            p.lineTo (x, yOf (x));
        p.lineTo (x2, yOf (x2));
        return p;
    }

    // Avancee du trace (styles CURVE et WAVE) : balayage de 1,5 s, petite
    // pause trace complet en fin de cycle.
    inline float sweepX (float t, float W)
    {
        const float u = std::fmod (t / 1.5f, 1.0f);
        return juce::jlimit (0.0f, 1.0f, u * 1.12f) * (W - 1.0f) + 0.5f;
    }

    inline void paintCurve (juce::Graphics& g, float W, float H, bool attack, float l, float t, const Ink& ink)
    {
        const float top = H * 0.18f, bot = H * 0.82f, x0 = W * (attack ? 0.2f : 0.24f);
        const float tau = W * (attack ? juce::jmap (l, 0.025f, 0.42f) : juce::jmap (l, 0.04f, 0.55f));
        const auto yOf = [=] (float x)
        {
            if (attack) return x < x0 ? top : top + (bot - top) * (1.0f - std::exp (-(x - x0) / tau));
            return x < x0 ? bot : bot - (bot - top) * (1.0f - std::exp (-(x - x0) / tau));
        };

        g.setColour (ink.faint);
        if (attack) g.fillRect (juce::Rectangle<float> (x0, 0.0f, W - x0, H));
        else        g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, x0, H));

        const float lw = juce::jmax (1.1f, H * 0.1f);
        const juce::PathStrokeType st (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (t < 0.0f)
        {
            g.setColour (ink.stroke);
            g.strokePath (trace (yOf, 0.5f, W - 0.5f, 0.5f), st);
            return;
        }
        const float xh = sweepX (t, W);
        g.setColour (ink.stroke.withMultipliedAlpha (0.25f));
        g.strokePath (trace (yOf, 0.5f, W - 0.5f, 0.5f), st);
        if (xh > 1.0f)
        {
            g.setColour (ink.stroke);
            g.strokePath (trace (yOf, 0.5f, xh, 0.5f), st);
        }
        head (g, xh, yOf (xh), lw, ink);
    }

    inline void paintWave (juce::Graphics& g, float W, float H, bool attack, float l, float t, const Ink& ink)
    {
        const float mid = H * 0.5f, amp = H * 0.5f - juce::jmax (1.0f, H * 0.06f), per = juce::jmax (3.0f, W / 9.0f), gmin = 0.38f;
        const float x0 = W * (attack ? 0.14f : 0.3f);
        const float tau = W * (attack ? juce::jmap (l, 0.025f, 0.4f) : juce::jmap (l, 0.04f, 0.5f));
        // REL : passage fort deja compresse (0,62), puis son plus faible qui
        // sort d'abord ecrase (gain encore reduit) et remonte a 0,8.
        const auto env = [=] (float x)
        {
            if (attack) return x < x0 ? 0.05f : gmin + (1.0f - gmin) * std::exp (-(x - x0) / tau);
            return x < x0 ? 0.62f : 0.8f * (1.0f - 0.7f * std::exp (-(x - x0) / tau));
        };
        const auto yOf = [=] (float x)
        {
            return mid - amp * env (x) * std::sin (juce::MathConstants<float>::twoPi * (x - x0) / per);
        };
        const auto fillEnvelope = [&] (float x2)
        {
            juce::Path p;
            p.startNewSubPath (0.0f, mid - amp * env (0.0f));
            for (float x = 0.5f; x <= x2; x += 0.5f) p.lineTo (x, mid - amp * env (x));
            for (float x = x2; x >= 0.0f; x -= 0.5f) p.lineTo (x, mid + amp * env (x));
            p.closeSubPath();
            g.setColour (ink.faint);
            g.fillPath (p);
        };

        const float lw = juce::jmax (0.9f, H * 0.07f);
        const juce::PathStrokeType st (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (t < 0.0f)
        {
            fillEnvelope (W);
            g.setColour (ink.stroke);
            g.strokePath (trace (yOf, 0.5f, W - 0.5f, 0.35f), st);
            return;
        }
        const float xh = sweepX (t, W);
        fillEnvelope (xh);
        g.setColour (ink.stroke.withMultipliedAlpha (0.2f));
        g.strokePath (trace (yOf, 0.5f, W - 0.5f, 0.35f), st);
        if (xh > 1.0f)
        {
            g.setColour (ink.stroke);
            g.strokePath (trace (yOf, 0.5f, xh, 0.35f), st);
        }
        head (g, xh, yOf (xh), lw, ink);
    }

    // Duree "a l'ecran" de l'attaque / du release (secondes, temps ralenti).
    inline float visibleSeconds (bool attack, float l)
    {
        return attack ? juce::jmap (l, 0.06f, 0.9f) : juce::jmap (l, 0.25f, 2.6f);
    }

    inline void paintLeds (juce::Graphics& g, float W, float H, bool attack, float l, float t, const Ink& ink)
    {
        const float T = visibleSeconds (attack, l);
        float level;
        if (t < 0.0f)
            level = attack ? 1.0f - std::exp (-3.0f * 0.25f / T) : std::exp (-3.0f * 0.5f / T);
        else if (attack)
        {
            const float tc = std::fmod (t, T * 1.6f + 0.6f);
            level = tc < T * 1.6f + 0.35f ? 1.0f - std::exp (-3.0f * tc / T) : 0.0f;
        }
        else
        {
            const float tc = std::fmod (t, T * 1.6f + 0.7f);
            level = tc < 0.4f ? 1.0f : (tc < 0.4f + T * 1.6f ? std::exp (-3.0f * (tc - 0.4f) / T) : 0.0f);
        }

        const int n = W < 36.0f ? 6 : 9;
        const float gap = juce::jmax (1.0f, W * 0.025f);
        const float lw = (W - gap * (float) (n - 1)) / (float) n, lh = juce::jmax (3.0f, H * 0.5f), y0 = (H - lh) * 0.5f;
        const float r = juce::jmin (lw, lh) * 0.3f;
        for (int i = 0; i < n; ++i)
        {
            const auto led = juce::Rectangle<float> ((float) i * (lw + gap), y0, lw, lh);
            g.setColour (ink.off);
            g.fillRoundedRectangle (led, r);
            const float on = juce::jlimit (0.0f, 1.0f, level * (float) n - (float) i);
            if (on > 0.0f)
            {
                g.setColour (ink.glow.withMultipliedAlpha (on));
                g.fillRoundedRectangle (led, r);
                g.setColour (ink.glow.withMultipliedAlpha (on * 0.35f));
                g.fillRoundedRectangle (led.expanded (0.6f), r + 0.6f);
            }
        }
    }

    // area : zone du pictogramme (tout est dessine dedans) ; t : secondes
    // depuis le debut de l'animation, < 0 = fige ; alpha : grise / bande
    // eteinte.
    inline void paint (juce::Graphics& g, juce::Rectangle<float> area, int style, bool attack, float l, float t,
                       const AlphaPalette& pal, float alpha)
    {
        if (style == off || area.isEmpty())
            return;
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.toNearestInt());
        g.addTransform (juce::AffineTransform::translation (area.getX(), area.getY()));
        const auto ink = inkFor (pal, alpha);
        const float W = area.getWidth(), H = area.getHeight();
        switch (style)
        {
            case curve: paintCurve (g, W, H, attack, l, t, ink); break;
            case wave:  paintWave  (g, W, H, attack, l, t, ink); break;
            case leds:  paintLeds  (g, W, H, attack, l, t, ink); break;
            default:    break;
        }
    }
}
