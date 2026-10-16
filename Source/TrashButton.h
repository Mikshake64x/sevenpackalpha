#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "PressAnimator.h"

/**
    TrashButton
    -----------
    Icone corbeille minimaliste (vecteur, pas d'image), pour supprimer un
    preset utilisateur. Se grise automatiquement quand desactive (preset
    Side Key/usine selectionne -- voir SevenpackEditorContent::refreshPresetDisplay()),
    pour qu'on comprenne d'un coup d'oeil qu'on ne peut pas l'utiliser.
*/
class TrashButton : public juce::Button, private PressAnimator
{
public:
    TrashButton() : juce::Button ("Delete Preset") {}

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (4.0f);
        applyPressTransform (g, bounds);
        const bool active = isEnabled();
        const juce::Colour colour = active ? SevenpackColours::ledRed : SevenpackColours::textDim.withAlpha (0.4f);
        const float alpha = active ? (isDown ? 0.65f : (isHighlighted ? 1.0f : 0.85f)) : 0.4f;

        g.setColour (colour.withAlpha (alpha));

        // Couvercle
        const float lidY = bounds.getY() + bounds.getHeight() * 0.16f;
        g.drawLine (bounds.getX(), lidY, bounds.getRight(), lidY, 1.6f);
        const float handleW = bounds.getWidth() * 0.34f;
        g.drawLine (bounds.getCentreX() - handleW * 0.5f, lidY, bounds.getCentreX() - handleW * 0.5f, lidY - 3.0f, 1.6f);
        g.drawLine (bounds.getCentreX() + handleW * 0.5f, lidY, bounds.getCentreX() + handleW * 0.5f, lidY - 3.0f, 1.6f);
        g.drawLine (bounds.getCentreX() - handleW * 0.5f, lidY - 3.0f, bounds.getCentreX() + handleW * 0.5f, lidY - 3.0f, 1.6f);

        // Corps (trapeze legerement retreci vers le bas)
        juce::Path body;
        const float bodyTop = lidY + 2.0f;
        const float bodyBottom = bounds.getBottom();
        const float topInset = bounds.getWidth() * 0.08f;
        const float botInset = bounds.getWidth() * 0.16f;
        body.startNewSubPath (bounds.getX() + topInset, bodyTop);
        body.lineTo (bounds.getRight() - topInset, bodyTop);
        body.lineTo (bounds.getRight() - botInset, bodyBottom);
        body.lineTo (bounds.getX() + botInset, bodyBottom);
        body.closeSubPath();
        g.strokePath (body, juce::PathStrokeType (1.6f));

        // 3 lignes verticales internes
        for (int i = -1; i <= 1; ++i)
        {
            const float x = bounds.getCentreX() + (float) i * bounds.getWidth() * 0.16f;
            g.drawLine (x, bodyTop + 3.0f, x, bodyBottom - 3.0f, 1.2f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrashButton)
};
