#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    RackButton
    ----------
    Petit bouton-poussoir "plastique mou mat" rectangulaire, MEME langage
    visuel que ToggleSwitch (degrade diffus, coins moyennement arrondis,
    ombre portee douce, bordure toujours neutre) mais SANS diode -- juste
    un libelle centre. Fonctionne aussi bien en MOMENTANE (COPY/PASTE/SAVE,
    </>) qu'en TOGGLE/groupe radio (categories du navigateur de presets,
    voir setClickingTogglesState()).

    Remplace les juce::TextButton par defaut (aspect "web plat") utilises
    initialement pour la barre de presets -- coherence visuelle avec le
    reste du plugin (knobs/ToggleSwitch/RatioSelector).
*/
class RackButton : public juce::Button, private PressAnimator
{
public:
    explicit RackButton (const juce::String& labelText = {}, juce::Colour accentColour = SuppressorColours::ledAmber)
        : juce::Button (labelText), accent (accentColour)
    {
        setButtonText (labelText);
    }

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);
        const bool on = getToggleState();
        const bool active = on || isDown;
        const float corner = juce::jmin (6.0f, bounds.getHeight() * 0.22f);

        juce::Path shape;
        shape.addRoundedRectangle (bounds, corner);
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.4f), 3, { 0, 2 });
        shadow.drawForPath (g, shape);

        juce::ColourGradient fillGrad (active ? juce::Colour (0xff2f333a) : juce::Colour (0xff2b2e33), bounds.getX(), bounds.getY(),
                                        active ? juce::Colour (0xff191c22) : juce::Colour (0xff17191c), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (fillGrad);
        g.fillRoundedRectangle (bounds, corner);

        if (isHighlighted && ! isDown)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (bounds, corner);
        }

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        if (on)
        {
            g.setColour (accent.withAlpha (0.7f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.2f);
        }

        const bool enabledLook = isEnabled();
        g.setColour ((on ? accent : (enabledLook ? SuppressorColours::textBright : SuppressorColours::textDim))
                          .withAlpha (enabledLook ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (11.5f, bounds.getHeight() * 0.5f), juce::Font::bold)).withExtraKerningFactor (0.06f));
        g.drawFittedText (getButtonText().toUpperCase(), bounds.toNearestInt(), juce::Justification::centred, 1);
    }

private:
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackButton)
};
