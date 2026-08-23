#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "PressAnimator.h"

/**
    PresetNameButton
    ----------------
    Petit "ecran LCD" cliquable affichant le nom du preset courant --
    MEME traitement que l'ecran central des knobs (fond SixpackColours::
    lcdBg, ombre interne, texte lumineux) au lieu d'un juce::TextButton par
    defaut. Le clic ouvre le navigateur de presets (voir
    SixpackEditorContent::openPresetBrowser()).
*/
class PresetNameButton : public juce::Button, private PressAnimator
{
public:
    PresetNameButton() : juce::Button ("Preset Name") {}

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);
        const float corner = 4.0f;

        juce::Path shape;
        shape.addRoundedRectangle (bounds, corner);
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.5f), 3, { 0, 1 });
        shadow.drawForPath (g, shape);

        g.setColour (SixpackColours::lcdBg);
        g.fillRoundedRectangle (bounds, corner);

        juce::ColourGradient innerShadow (juce::Colours::black.withAlpha (0.6f), bounds.getCentreX(), bounds.getY(),
                                           juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.5f, false);
        g.setGradientFill (innerShadow);
        g.fillRoundedRectangle (bounds, corner);

        if (isDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.fillRoundedRectangle (bounds, corner);
        }
        else if (isHighlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (bounds, corner);
        }

        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (bounds.reduced (1.2f), corner, 0.8f);

        g.setColour (SixpackColours::ledCyan);
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.5f), juce::Font::bold)));
        g.drawFittedText (getButtonText(), bounds.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        // Petit chevron a droite, indique que c'est cliquable (ouvre le navigateur).
        auto chevronArea = bounds.removeFromRight (16.0f);
        const auto c = chevronArea.getCentre();
        juce::Path chevron;
        chevron.addTriangle (c.x - 4.0f, c.y - 3.0f, c.x + 4.0f, c.y - 3.0f, c.x, c.y + 3.5f);
        g.setColour (SixpackColours::ledCyan.withAlpha (0.6f));
        g.fillPath (chevron);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetNameButton)
};
