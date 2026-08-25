#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "PluginProcessor.h"

/**
    AiLearnButton
    -------------
    Bouton "LEARN" (barre de controle) -- declenche AiLearnAnalyzer::
    beginCapture() au clic (voir PluginProcessor::startAiLearn()). PAS un
    toggle (setClickingTogglesState(false)) : c'est une action ponctuelle,
    pas un etat persistant.

    Meme style neumorphic "dark mono" que SOLO/SEL (retour utilisateur :
    "il faut le meme type de bouton que solo et sel" -- voir
    SixpackLookAndFeel::paintNeumorphicButton()), avec un "lit" qui
    CLIGNOTE (BlinkTimer) tant que l'analyse tourne (Capturing/Analyzing,
    lu directement sur AiLearnAnalyzer::getState(), pas de parametre APVTS
    -- ce n'est pas automatisable/sauvegarde, comme Link In/Out) au lieu
    d'etre pilote par un toggle ou une selection persistante.
*/
class AiLearnButton : public juce::Button,
                       private juce::Timer
{
public:
    explicit AiLearnButton (SixpackAudioProcessor& p)
        : juce::Button ("AI Learn"), processor (p)
    {
        setClickingTogglesState (false);
        startTimerHz (6); // vitesse du clignotement pendant l'analyse
    }

    ~AiLearnButton() override { stopTimer(); }

    // Voir RackPushButton::setHaloMargin() -- meme idiome.
    void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        const auto state = processor.aiLearnAnalyzer.getState();
        const bool busy = state == AiLearnAnalyzer::State::Capturing || state == AiLearnAnalyzer::State::Analyzing;
        const bool lit = busy && blinkOn;

        SixpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), lit, isDown,
                                                     SixpackColours::ledCyan, "LEARN", 1.0f, haloMargin);
    }

private:
    float haloMargin = 8.0f;
    void timerCallback() override
    {
        const auto state = processor.aiLearnAnalyzer.getState();
        const bool busy = state == AiLearnAnalyzer::State::Capturing || state == AiLearnAnalyzer::State::Analyzing;

        if (busy)
        {
            blinkOn = ! blinkOn;
            repaint();
        }
        else if (blinkOn)
        {
            blinkOn = false;
            repaint();
        }
    }

    SixpackAudioProcessor& processor;
    bool blinkOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiLearnButton)
};
