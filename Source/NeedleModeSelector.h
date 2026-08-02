#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    NeedleModeSelector
    ------------------
    3 boutons empiles verticalement (IN x / OUT x / THD, x = "L" ou "R"
    selon le canal du NeedleVU associe -- GR retire, pas utile pour ce
    cadran global), radio group JUCE natif -- place a GAUCHE de CHAQUE
    NeedleVU (un selecteur independant par cadran, voir PluginEditor) pour
    choisir directement le mode affiche, plutot que de faire defiler au
    clic sur le cadran.
*/
class NeedleModeSelector : public juce::Component
{
public:
    static constexpr int numModes = 3;

    struct ModeButton : public juce::Button, private PressAnimator
    {
        explicit ModeButton (const juce::String& labelText) : juce::Button (labelText), text (labelText)
        {
            setClickingTogglesState (true);
        }

        void buttonStateChanged() override
        {
            if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
        }

        void onPressAnimated() override { repaint(); }

        void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/) override
        {
            auto bounds = getLocalBounds().toFloat();
            applyPressTransform (g, bounds);
            const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
            const bool powered = (laf == nullptr) || laf->isPowered();
            const bool on = getToggleState();
            const float corner = juce::jmin (5.0f, bounds.getHeight() * 0.22f);

            juce::Path shape;
            shape.addRoundedRectangle (bounds, corner);
            juce::DropShadow shadow (juce::Colours::black.withAlpha (0.35f), 2, { 0, 1 });
            shadow.drawForPath (g, shape);

            juce::ColourGradient fillGrad (on ? juce::Colour (0xff2f333a) : juce::Colour (0xff2b2e33), bounds.getX(), bounds.getY(),
                                            on ? juce::Colour (0xff191c22) : juce::Colour (0xff17191c), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill (fillGrad);
            g.fillRoundedRectangle (bounds, corner);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

            g.setColour (on ? (powered ? SuppressorColours::ledAmber : SuppressorColours::textBright) : SuppressorColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)).withExtraKerningFactor (0.02f));
            g.drawFittedText (text, bounds.reduced (1.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);
        }

        juce::String text;
    };

    // channelSuffix : "L" ou "R" -- construit les libelles IN/OUT specifiques
    // au canal de ce cadran (THD reste commun).
    NeedleModeSelector (const juce::String& channelSuffix, int initialIndex = 0)
    {
        const juce::String names[numModes] = { "IN " + channelSuffix, "OUT " + channelSuffix, "THD" };

        for (int i = 0; i < numModes; ++i)
        {
            buttons[i] = std::make_unique<ModeButton> (names[i]);
            buttons[i]->setRadioGroupId (3001, juce::dontSendNotification);
            addAndMakeVisible (*buttons[i]);

            buttons[i]->onClick = [this, i]
            {
                if (buttons[i]->getToggleState())
                {
                    selected = i;
                    if (onSelect)
                        onSelect (i);
                }
            };
        }

        selected = juce::jlimit (0, numModes - 1, initialIndex);
        buttons[selected]->setToggleState (true, juce::dontSendNotification);
    }

    std::function<void (int index)> onSelect;

    void resized() override
    {
        auto bounds = getLocalBounds();
        const int gap = 3;
        const int h = (bounds.getHeight() - gap * (numModes - 1)) / numModes;
        for (int i = 0; i < numModes; ++i)
            buttons[i]->setBounds (bounds.getX(), bounds.getY() + i * (h + gap), bounds.getWidth(), h);
    }

    void paint (juce::Graphics&) override {}

private:
    int selected = 0;
    std::unique_ptr<ModeButton> buttons[numModes];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleModeSelector)
};
