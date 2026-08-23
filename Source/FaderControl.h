#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    FaderVerticalLookAndFeel
    ------------------------
    Style "barre LED segmentee" encastree, valide par l'utilisateur en
    preview -- puits en creux (extrusion) contenant une colonne de segments
    arrondis, allumes en couleur d'accent entre la valeur NEUTRE (voir
    setNeutralValue()) et la valeur courante, avec une lueur douce derriere
    chaque segment allume. Une "vitre" (degrade diagonal + scanlines
    discretes) est dessinee par-dessus pour l'effet ecran. Pas de capuchon
    separe : les segments allumes SONT l'indicateur, comme un vrai meter
    bridge. Utilise pour les 2 faders par bande : Mix (neutre = bas, 0 =
    Dry) et Width (neutre = centre, 100% = neutre stereo).
*/
class FaderVerticalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                            float /*sliderPos*/, float /*minSliderPos*/, float /*maxSliderPos*/,
                            const juce::Slider::SliderStyle /*style*/, juce::Slider& slider) override
    {
        const auto outerBounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        constexpr float wellCorner = 6.0f;

        // --- Puits encastre (extrusion) : fond quasi-noir + double liseret
        // (sombre puis clair tres discret) pour simuler un renfoncement
        // dans le panneau, meme principe que renderKnobSocket. ---
        g.setColour (juce::Colour (0xff08090a));
        g.fillRoundedRectangle (outerBounds, wellCorner);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (outerBounds.reduced (0.5f), wellCorner, 1.4f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (outerBounds.reduced (1.6f), wellCorner - 1.0f, 1.0f);

        const auto bounds = outerBounds.reduced (7.0f, 8.0f);

        constexpr int numSegs = 14;
        constexpr float gapPx = 3.0f;
        const float segW = bounds.getWidth();
        const float segH = (bounds.getHeight() - gapPx * (float) (numSegs - 1)) / (float) numSegs;

        const double neutral = slider.getProperties().contains (neutralValuePropertyId)
                                    ? (double) slider.getProperties()[neutralValuePropertyId]
                                    : slider.getMinimum();
        const double range = slider.getMaximum() - slider.getMinimum();
        const float neutralFrac = range > 0.0 ? (float) ((neutral - slider.getMinimum()) / range) : 0.0f;
        const float valueFrac   = range > 0.0 ? (float) ((slider.getValue() - slider.getMinimum()) / range) : 0.0f;

        const float loFrac = juce::jmin (neutralFrac, valueFrac);
        const float hiFrac = juce::jmax (neutralFrac, valueFrac);

        for (int i = 0; i < numSegs; ++i)
        {
            const float segCentre = ((float) i + 0.5f) / (float) numSegs;
            const bool lit = segCentre >= loFrac && segCentre <= hiFrac;

            // Couleur a 2 tons de part et d'autre du neutre (demande
            // explicite : "le fader spread est completement orange, c'est
            // pas hyper visible si on est cote mono et cote stereo") --
            // segments SOUS le neutre (retrecissement, cote mono) dans
            // accentColourBelowNeutral, segments AU-DESSUS (elargissement)
            // dans accentColour. Par defaut les 2 sont identiques (Mix,
            // neutre en bas -- jamais de segment "sous" le neutre), donc
            // aucun changement de comportement pour les faders a 1 sens.
            const juce::Colour litColour = (segCentre < neutralFrac) ? accentColourBelowNeutral : accentColour;

            // i=0 = segment du bas (bounds.getBottom()).
            const float segY = bounds.getBottom() - (float) (i + 1) * segH - (float) i * gapPx;
            auto segRect = juce::Rectangle<float> (bounds.getCentreX() - segW * 0.5f, segY, segW, segH);

            if (lit)
            {
                g.setColour (litColour.withAlpha (0.28f));
                g.fillRoundedRectangle (segRect.expanded (1.5f), 3.0f);
            }

            g.setColour (lit ? litColour : SixpackColours::gridLine);
            g.fillRoundedRectangle (segRect, 2.0f);

            if (lit)
            {
                auto topSliver = segRect.removeFromTop (juce::jmax (1.0f, segH * 0.28f));
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRoundedRectangle (topSliver, 2.0f);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (segRect.reduced (0.25f), 2.0f, 0.5f);
            }
        }

        // --- Vitre en verre teinte (effet ecran) : reflet diagonal +
        // scanlines discretes, par-dessus toute la barre. ---
        juce::ColourGradient glassGrad (juce::Colours::white.withAlpha (0.10f), outerBounds.getX(), outerBounds.getY(),
                                         juce::Colours::black.withAlpha (0.22f), outerBounds.getRight(), outerBounds.getBottom(), false);
        glassGrad.addColour (0.4, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (glassGrad);
        g.fillRoundedRectangle (outerBounds, wellCorner);

        g.setColour (juce::Colours::black.withAlpha (0.10f));
        for (float sy = outerBounds.getY(); sy < outerBounds.getBottom(); sy += 3.0f)
            g.fillRect (juce::Rectangle<float> (outerBounds.getX(), sy, outerBounds.getWidth(), 1.0f));
    }

    void setAccentColour (juce::Colour c) { accentColour = c; if (! twoTone) accentColourBelowNeutral = c; }

    // Couleur DEDIEE aux segments SOUS le neutre (demande explicite, voir
    // doc au point d'usage dans drawLinearSlider) -- appeler APRES
    // setAccentColour() pour un fader a 2 tons (ex: Width : ambre au-dessus
    // du neutre = SPREAD, cyan en-dessous = MONO).
    void setAccentColourBelowNeutral (juce::Colour c) { accentColourBelowNeutral = c; twoTone = true; }

    // Fait demarrer l'allumage des segments depuis cette valeur au lieu du
    // bas (utilise par le fader Width : neutre = 100%, au centre de la
    // piste 0..200).
    static void setNeutralValue (juce::Slider& slider, double value)
    {
        slider.getProperties().set (neutralValuePropertyId, value);
    }

private:
    static constexpr const char* neutralValuePropertyId = "faderNeutralValue";
    juce::Colour accentColour = SixpackColours::ledCyan;
    juce::Colour accentColourBelowNeutral = SixpackColours::ledCyan;
    bool twoTone = false;
};

/**
    FaderControl
    ------------
    Conteneur fader vertical + libelles d'extremites, pour Mix et Width par
    bande (voir PluginProcessor.h : idBandMix / idBandWidth).
*/
struct FaderControl
{
    juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label  topLabel;
    juce::Label  bottomLabel;
    FaderVerticalLookAndFeel lookAndFeel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    FaderControl() { slider.setLookAndFeel (&lookAndFeel); }
    ~FaderControl() { slider.setLookAndFeel (nullptr); }
};
