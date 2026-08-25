#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    RackButton
    ----------
    Petit bouton-poussoir rectangulaire (COPY/PASTE/SAVE, </>, categories
    du navigateur de presets). Fonctionne aussi bien en MOMENTANE qu'en
    TOGGLE/groupe radio (voir setClickingTogglesState()).

    Style neumorphic "dark mono" unifie avec le reste du plugin (retour
    utilisateur, avec captures d'ecran : "les boutons ne sont pas tous
    identiques sur chaque bande et dans le header ... meme design
    d'enfoncement sur tous les boutons" -- l'ancien style "plastique mou
    mat" avec bordure TOUJOURS visible (+ bordure accent quand actif)
    tranchait avec SOLO/SEL/LEARN/etc, deja convertis).
*/
class RackButton : public juce::Button
{
public:
    explicit RackButton (const juce::String& labelText = {}, juce::Colour accentColour = SixpackColours::ledAmber)
        : juce::Button (labelText), accent (accentColour)
    {
        setButtonText (labelText);
    }

    // Voir RackPushButton::setHaloMargin() -- meme idiome.
    void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        const bool on = getToggleState();
        SixpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), on, isDown,
                                                     accent, getButtonText().toUpperCase(),
                                                     isEnabled() ? 1.0f : 0.4f, haloMargin);
    }

private:
    juce::Colour accent;
    float haloMargin = 5.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackButton)
};
