#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    ThdEngageSwitch
    ---------------
    Interrupteur ON/OFF du THD Valve (SOFTCLIP) -- refonte neumorphic
    "dark mono" (retour utilisateur : preview A/B, "alors le A" -- rond,
    extrusion 3D marquee), remplace l'ancien sprite Button-08-S-Filmstrip.
    Meme recette que RackPushButton (SixpackLookAndFeel::
    paintNeumorphicButton) mais en CERCLE et sans texte : souleve (halo
    clair haut-gauche/sombre bas-droite) au repos, creuse quand engage. Un
    point central reste visible en PERMANENCE (retour utilisateur : sur 3
    variantes previewees -- "point terne toujours visible" / "anneau
    grave" / "rien au repos" -- "le b") : terne/creux au repos, cyan +
    glow une fois engage. La diode d'etat globale reste sur
    ThdHeaderModule (voir ThdHeaderModule.h) -- ce composant est juste
    l'actionneur physique.
*/
class ThdEngageSwitch : public juce::Button
{
public:
    ThdEngageSwitch() : juce::Button ("THD Engage")
    {
        setClickingTogglesState (true);
    }

    // Voir RackPushButton::setHaloMargin()/l'appelant (ThdHeaderModule.h)
    // doit agrandir les bounds de ce meme montant.
    void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
    }

    // Halo qui deborde REELLEMENT (retour utilisateur, reference visuelle
    // "C -- extreme") -- voir SixpackLookAndFeel::paintNeumorphicButton()
    // pour le meme raisonnement/technique, adapte ici en cercle.
    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        auto b = getLocalBounds().toFloat().reduced (haloMargin);
        const bool lit = getToggleState();
        const bool recessed = lit || isDown;
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto centre = b.getCentre();

        const bool lightTheme = SixpackLookAndFeel::isLightThemeStatic();
        const juce::Colour base = lightTheme ? SixpackColours::lightPanelGradTop : juce::Colour (0xff262422);

        juce::Path shape;
        shape.addEllipse (b);

        if (! recessed)
        {
            const float shadowRadius = juce::jmax (2.0f, haloMargin * 0.7f);
            const int offsetPx = juce::roundToInt (haloMargin * 0.45f);
            juce::DropShadow (juce::Colours::black.withAlpha (lightTheme ? 0.55f : 0.7f), (int) shadowRadius, { offsetPx, offsetPx }).drawForPath (g, shape);
            juce::DropShadow (juce::Colours::white.withAlpha (lightTheme ? 0.9f : 0.18f), (int) shadowRadius, { -offsetPx, -offsetPx }).drawForPath (g, shape);
            g.setColour (base);
            g.fillEllipse (b);
        }
        else
        {
            g.setColour (base.darker (lightTheme ? 0.10f : 0.16f));
            g.fillEllipse (b);

            g.saveState();
            g.reduceClipRegion (shape);
            juce::ColourGradient darkCorner (juce::Colours::black.withAlpha (lightTheme ? 0.35f : 0.6f), b.getX(), b.getY(),
                                              juce::Colours::transparentBlack, centre.x, centre.y, true);
            g.setGradientFill (darkCorner);
            g.fillEllipse (b);
            juce::ColourGradient lightCorner (juce::Colours::white.withAlpha (lightTheme ? 0.5f : 0.22f), b.getRight(), b.getBottom(),
                                               juce::Colours::transparentWhite, centre.x, centre.y, true);
            g.setGradientFill (lightCorner);
            g.fillEllipse (b);
            g.restoreState();
        }

        // Point central TOUJOURS visible (retour utilisateur, variante
        // "B" previewee : "j'aime bien quand il est active mais j'aime
        // pas quand il est off" -- l'ancienne version ne dessinait rien
        // au repos, juste un cercle nu) -- terne/creux quand eteint, cyan
        // + glow une fois engage.
        auto dot = juce::Rectangle<float> (r * 0.42f, r * 0.42f).withCentre (centre);
        if (lit)
        {
            g.setColour (SixpackColours::ledCyan.withAlpha (0.9f));
            g.fillEllipse (dot);
            g.setColour (SixpackColours::ledCyan.withAlpha (0.4f));
            g.drawEllipse (dot.expanded (2.0f), 2.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (dot);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (dot, 1.0f);
        }
    }

private:
    float haloMargin = 7.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdEngageSwitch)
};
