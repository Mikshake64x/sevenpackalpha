#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    XoverModeButton
    ---------------
    Selecteur WIDE / LINEAR -- 2 pilules EMPILEES VERTICALEMENT (WIDE en
    haut, LINEAR en dessous), style radio : cliquer sur l'une eteint
    l'autre. Chacune est un VRAI sous-composant independant (Segment,
    juce::Button), pas juste 2 zones peintes dans un seul Component -- ca
    permet a chacune d'avoir son PROPRE tooltip.

    Porte depuis Nuke Mext (meme fichier, couleurs/LookAndFeel adaptes a
    SuppressorColours / SuppressorLookAndFeel).

    Lit directement le parametre APVTS via un petit Timer interne, pour
    rester synchronise meme si la valeur change par un autre biais que le
    clic ici (automation host, etc). Le clic notifie l'editeur via
    onSelect pour qu'il pousse la nouvelle valeur au parametre.
*/
class XoverModeButton : public juce::Component, private juce::Timer
{
public:
    explicit XoverModeButton (std::atomic<float>& digitalCrossoverParamToWatch)
        : digitalCrossoverParam (digitalCrossoverParamToWatch)
    {
        wideSegment.setButtonText ("ANALOG");
        linearSegment.setButtonText ("LINEAR");
        wideSegment.setName ("Wide Crossover");
        linearSegment.setName ("Linear Crossover");
        wideSegment.accent = SuppressorColours::ledBlue;
        linearSegment.accent = SuppressorColours::ledRed;

        wideSegment.onClick   = [this] { if (onSelect) onSelect (false); };
        linearSegment.onClick = [this] { if (onSelect) onSelect (true); };

        addAndMakeVisible (wideSegment);
        addAndMakeVisible (linearSegment);

        activeLinear = digitalCrossoverParam.load() > 0.5f;
        updateSegmentStates();
        startTimerHz (15);
    }

    ~XoverModeButton() override { stopTimer(); }

    std::function<void (bool wantLinear)> onSelect;

    void setWideTooltip (const juce::String& text) { wideSegment.setTooltip (text); }
    void setLinearTooltip (const juce::String& text) { linearSegment.setTooltip (text); }

    void resized() override
    {
        auto bounds = getLocalBounds();
        constexpr int gapPx = 4;
        const int segH = (bounds.getHeight() - gapPx) / 2;

        wideSegment.setBounds (bounds.removeFromTop (segH));
        bounds.removeFromTop (gapPx);
        linearSegment.setBounds (bounds);
    }

private:
    struct Segment : public juce::Button
    {
        Segment() : juce::Button ("XoverSegment") {}

        void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/) override
        {
            auto area = getLocalBounds().toFloat();
            const float radius = area.getHeight() * 0.32f;
            const bool active = getToggleState();

            if (active)
            {
                g.setColour (accent.withAlpha (0.24f));
                g.fillRoundedRectangle (area, radius);
                g.setColour (accent.withAlpha (0.75f));
                g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.2f);
            }
            else
            {
                g.setColour (SuppressorColours::bg.withAlpha (0.7f));
                g.fillRoundedRectangle (area, radius);
                g.setColour (SuppressorColours::gridLine);
                g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
            }

            g.setColour (active ? SuppressorColours::textBright : SuppressorColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.06f));
            g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
        }

        juce::Colour accent;
    };

    void timerCallback() override
    {
        const bool live = digitalCrossoverParam.load() > 0.5f;
        if (live != activeLinear)
        {
            activeLinear = live;
            updateSegmentStates();
        }
    }

    void updateSegmentStates()
    {
        wideSegment.setToggleState (! activeLinear, juce::dontSendNotification);
        linearSegment.setToggleState (activeLinear, juce::dontSendNotification);
        wideSegment.repaint();
        linearSegment.repaint();
    }

    std::atomic<float>& digitalCrossoverParam;
    bool activeLinear = false;

    Segment wideSegment, linearSegment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoverModeButton)
};
