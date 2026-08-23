#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    XoverModeButton
    ---------------
    Selecteur ANALOG / LINEAR -- 2 pilules EMPILEES VERTICALEMENT, style
    radio exclusif : cliquer sur l'une eteint l'autre. Chacune est un VRAI
    sous-composant independant (Segment, juce::Button), pas juste des
    zones peintes dans un seul Component -- permet a chacune d'avoir son
    PROPRE tooltip.

    Pilote idDigitalCrossover (bool) : true = LINEAR, false = ANALOG.

    (ANALOG 2 -- 3e segment ajoute puis retire, voir doc de classe
    Crossover.h -- retour utilisateur explicite : "on va supprimer analog
    2". Le composant redevient un simple toggle 2 voies.)

    Porte depuis Nuke Mext (meme fichier, couleurs/LookAndFeel adaptes a
    SixpackColours / SixpackLookAndFeel).

    Lit directement le parametre APVTS via un petit Timer interne, pour
    rester synchronise meme si la valeur change par un autre biais que le
    clic ici (automation host, etc). Le clic notifie l'editeur via
    onSelect pour qu'il pousse la nouvelle valeur au parametre.
*/
class XoverModeButton : public juce::Component, private juce::Timer
{
public:
    enum class Mode { Analog = 0, Linear = 1 };

    explicit XoverModeButton (std::atomic<float>& digitalCrossoverParamToWatch)
        : digitalCrossoverParam (digitalCrossoverParamToWatch)
    {
        analogSegment.setButtonText ("ANALOG");
        linearSegment.setButtonText ("LINEAR");
        analogSegment.setName ("Analog Crossover");
        linearSegment.setName ("Linear Crossover");
        analogSegment.accent = SixpackColours::ledBlue;
        linearSegment.accent = SixpackColours::ledRed;

        analogSegment.onClick = [this] { if (onSelect) onSelect (Mode::Analog); };
        linearSegment.onClick = [this] { if (onSelect) onSelect (Mode::Linear); };

        addAndMakeVisible (analogSegment);
        addAndMakeVisible (linearSegment);

        activeMode = readMode();
        updateSegmentStates();
        startTimerHz (15);
    }

    ~XoverModeButton() override { stopTimer(); }

    std::function<void (Mode)> onSelect;

    void setAnalogTooltip (const juce::String& text) { analogSegment.setTooltip (text); }
    void setLinearTooltip (const juce::String& text) { linearSegment.setTooltip (text); }

    void resized() override
    {
        auto bounds = getLocalBounds();
        constexpr int gapPx = 3;
        const int segH = (bounds.getHeight() - gapPx) / 2;

        analogSegment.setBounds (bounds.removeFromTop (segH));
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
                g.setColour (SixpackColours::bg.withAlpha (0.7f));
                g.fillRoundedRectangle (area, radius);
                g.setColour (SixpackColours::gridLine);
                g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
            }

            g.setColour (active ? SixpackColours::textBright : SixpackColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)).withExtraKerningFactor (0.04f));
            g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
        }

        juce::Colour accent;
    };

    Mode readMode() const noexcept
    {
        return digitalCrossoverParam.load() > 0.5f ? Mode::Linear : Mode::Analog;
    }

    void timerCallback() override
    {
        const auto live = readMode();
        if (live != activeMode)
        {
            activeMode = live;
            updateSegmentStates();
        }
    }

    void updateSegmentStates()
    {
        analogSegment.setToggleState (activeMode == Mode::Analog, juce::dontSendNotification);
        linearSegment.setToggleState (activeMode == Mode::Linear, juce::dontSendNotification);
        analogSegment.repaint();
        linearSegment.repaint();
    }

    std::atomic<float>& digitalCrossoverParam;
    Mode activeMode = Mode::Analog;

    Segment analogSegment, linearSegment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoverModeButton)
};
