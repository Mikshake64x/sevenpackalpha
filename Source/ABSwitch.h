#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    ABSwitch
    --------
    Selecteur A/B en pilule encastree (extrusion) : le cote actif se colore
    (A = cyan, B = ambre), l'autre reste sombre/discret. Voir
    SuppressorAudioProcessor::toggleABCompare()/selectABSlot().
*/
class ABSwitch : public juce::Component, private PressAnimator
{
public:
    // Necessaire : JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (en bas de
    // la classe) supprime le constructeur de copie -- et en C++, declarer
    // (meme supprime) UN SEUL constructeur quel qu'il soit empeche le
    // compilateur de generer implicitement le constructeur par defaut.
    ABSwitch() = default;

    std::function<void (bool wantB)> onSelect;

    void setIsB (bool b) noexcept
    {
        if (b != isB) { isB = b; repaint(); }
    }

    bool getIsB() const noexcept { return isB; }

    void onPressAnimated() override { repaint(); }

    void mouseDown (const juce::MouseEvent&) override { notePressed(); }
    void mouseUp   (const juce::MouseEvent& e) override
    {
        noteReleased();
        const bool clickedB = e.position.x > (float) getWidth() * 0.5f;
        if (clickedB != isB && onSelect)
            onSelect (clickedB);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);
        const float corner = bounds.getHeight() * 0.5f;

        // Puits encastre (extrusion) -- meme principe que les faders.
        g.setColour (juce::Colour (0xff08090a));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.4f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (bounds.reduced (1.6f), corner - 1.0f, 1.0f);

        auto pillArea = bounds.reduced (4.0f);
        const float halfW = pillArea.getWidth() * 0.5f;
        auto aArea = pillArea.removeFromLeft (halfW);
        auto bArea = pillArea;

        drawHalf (g, aArea, "A", ! isB, SuppressorColours::ledCyan);
        drawHalf (g, bArea, "B", isB, SuppressorColours::ledAmber);
    }

private:
    static void drawHalf (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, bool on, juce::Colour accent)
    {
        const float corner = area.getHeight() * 0.5f;

        if (on)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (area, corner);
            g.setColour (accent.interpolatedWith (juce::Colours::black, 0.75f));
        }
        else
        {
            g.setColour (SuppressorColours::textDim.withAlpha (0.7f));
        }

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 1);
    }

    bool isB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ABSwitch)
};
