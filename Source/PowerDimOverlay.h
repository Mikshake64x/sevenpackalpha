#pragma once
#include <JuceHeader.h>

/**
    PowerDimOverlay
    ----------------
    Assombrit toute la zone des controles (diodes GR, ecrans LCD des
    knobs, boutons Detector/Audio, cadre X/Y) quand POWER est off --
    place en enfant TOPMOST par-dessus ces composants, ne capte aucun
    clic (setInterceptsMouseClicks(false,false)), donc les composants
    dessous restent inertes visuellement mais ne bloquent rien.
*/
class PowerDimOverlay : public juce::Component, private juce::Timer
{
public:
    explicit PowerDimOverlay (std::atomic<float>& powerParamToWatch)
        : powerParam (powerParamToWatch)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (10);
    }

    ~PowerDimOverlay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        if (! powered)
        {
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillAll();
        }
    }

private:
    void timerCallback() override
    {
        const bool p = powerParam.load() > 0.5f;
        if (p != powered)
        {
            powered = p;
            repaint();
        }
    }

    std::atomic<float>& powerParam;
    bool powered = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerDimOverlay)
};
