#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

/**
    LatencyModeButton
    ------------------
    Petite icone engrenage, collee au bouton XoverModeButton (WIDE/LINEAR) :
    ouvre au clic un menu (Low ~7ms / Medium ~25ms / High ~80ms) pour choisir
    le budget de latence du crossover en mode LINEAR (voir Crossover.h et
    SevenpackAudioProcessor::paramLinearLatencyMode). Sans effet en mode
    WIDE (toujours 0 latence).

    Porte depuis Nuke Mext (couleurs adaptees a SevenpackColours). Ce
    composant ne dessine que l'icone et gere le clic (ouverture du menu) --
    le PopupMenu lui-meme et le cablage au parametre APVTS sont geres par
    l'editeur (voir SevenpackAudioProcessorEditor::showLatencyModeMenu()).
*/
class LatencyModeButton : public juce::Button
{
public:
    LatencyModeButton() : juce::Button ("LatencyMode")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        const auto centre = bounds.getCentre();
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        const juce::Colour colour = SevenpackColours::textDim.withAlpha (isDown ? 0.55f : (isHighlighted ? 1.0f : 0.8f));
        g.setColour (colour);

        constexpr int numTeeth = 8;
        const float outerR = r * 0.95f;
        const float innerR = r * 0.68f;

        juce::Path gear;
        for (int i = 0; i < numTeeth; ++i)
        {
            const float toothWidth = juce::MathConstants<float>::twoPi / (float) numTeeth * 0.55f;
            const float a0 = (float) i / (float) numTeeth * juce::MathConstants<float>::twoPi - toothWidth * 0.5f;
            const float a1 = a0 + toothWidth;
            gear.addPieSegment (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f,
                                 a0, a1, innerR / outerR);
        }
        g.fillPath (gear);

        g.fillEllipse (juce::Rectangle<float> (innerR * 1.65f, innerR * 1.65f).withCentre (centre));

        g.setColour (SevenpackColours::bg);
        g.fillEllipse (juce::Rectangle<float> (innerR * 0.85f, innerR * 0.85f).withCentre (centre));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (innerR * 0.85f, innerR * 0.85f).withCentre (centre), 1.1f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatencyModeButton)
};
