#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    PushButton
    ----------
    Bouton-poussoir MOMENTANE (pas un toggle) : ne reste jamais enfonce,
    revient a sa position initiale des le relachement -- comme les
    vrais boutons RATIO / DETECTOR / AUDIO du hardware de reference, qui
    font juste avancer un compteur d'etat a chaque pression.

    Sprite xp_button.png : filmstrip vertical 2 images (etat relache en
    haut, etat enfonce en bas), choisi via isButtonDown (etat transitoire
    du clic), PAS via getToggleState().

    ETAPE PROJUCER REQUISE : ajouter xp_button.png comme ressource binaire
    (meme procedure que les autres images).
*/
class PushButton : public juce::Button, private PressAnimator
{
public:
    PushButton() : juce::Button ("Push")
    {
        // Bouton momentane : pas de setClickingTogglesState -- le
        // comportement par defaut de juce::Button (clic = onClick au
        // relachement, sans etat persistant) est exactement ce qu'il faut.
        sprite = juce::ImageCache::getFromMemory (BinaryData::xp_button_png, BinaryData::xp_button_pngSize);
    }

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);

        if (sprite.isNull())
        {
            g.setColour (isButtonDown ? SuppressorColours::bg.darker (0.3f) : SuppressorColours::bg);
            g.fillEllipse (bounds);
            return;
        }

        const int frameH = sprite.getHeight() / 2;
        const int srcY = isButtonDown ? frameH : 0; // haut = relache, bas = enfonce

        auto sub = sprite.getClippedImage ({ 0, srcY, sprite.getWidth(), frameH });

        const float scale = juce::jmin (bounds.getWidth() / (float) sub.getWidth(),
                                         bounds.getHeight() / (float) sub.getHeight());
        const float w = sub.getWidth() * scale;
        const float h = sub.getHeight() * scale;
        auto dest = juce::Rectangle<float> (w, h).withCentre (bounds.getCentre());

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sub, (int) dest.getX(), (int) dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                     0, 0, sub.getWidth(), sub.getHeight());

        if (isHighlighted && ! isButtonDown)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillEllipse (bounds);
        }
    }

private:
    juce::Image sprite;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PushButton)
};
