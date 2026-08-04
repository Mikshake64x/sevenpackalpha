#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"
#include "PluginProcessor.h"

/**
    AiLearnButton
    -------------
    Bouton "AI" (barre de controle, au-dessus de Analog/Linear) -- declenche
    AiLearnAnalyzer::beginCapture() au clic (voir PluginProcessor::
    startAiLearn()). PAS un toggle (setClickingTogglesState(false)) : c'est
    une action ponctuelle, pas un etat persistant -- le bouton lui-meme
    reste "enfonce" visuellement (via son propre Timer, pas un etat de
    Button) tant que l'analyse tourne, diode clignotante pendant Capturing/
    Analyzing (lit directement AiLearnAnalyzer::getState(), pas de
    parametre APVTS -- ce n'est pas automatisable/sauvegarde, comme
    Link In/Out).
*/
class AiLearnButton : public juce::Button, private PressAnimator
{
public:
    explicit AiLearnButton (SuppressorAudioProcessor& p)
        : juce::Button ("AI Learn"), processor (p)
    {
        setClickingTogglesState (false);

        // BlinkTimer separe (composition, pas heritage) : PressAnimator
        // herite deja PRIVEMENT de juce::Timer en interne pour son propre
        // usage -- heriter une 2e fois directement ici rendrait
        // startTimerHz()/stopTimer() ambigus (2 sous-objets Timer, erreur
        // MSVC C2385).
        blinkTimer.callback = [this]
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
        };
        blinkTimer.startTimerHz (6); // vitesse du clignotement pendant l'analyse
    }

    ~AiLearnButton() override { blinkTimer.stopTimer(); }

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);

        const auto state = processor.aiLearnAnalyzer.getState();
        const bool busy = state == AiLearnAnalyzer::State::Capturing || state == AiLearnAnalyzer::State::Analyzing;
        const bool ledLit = busy && blinkOn;

        const float corner = juce::jmin (7.0f, bounds.getHeight() * 0.3f);

        juce::Path shape;
        shape.addRoundedRectangle (bounds, corner);
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.4f), 3, { 0, 2 });
        shadow.drawForPath (g, shape);

        juce::ColourGradient fillGrad (busy ? juce::Colour (0xff2f333a) : juce::Colour (0xff2b2e33), bounds.getX(), bounds.getY(),
                                        busy ? juce::Colour (0xff191c22) : juce::Colour (0xff17191c), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (fillGrad);
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        // Diode a gauche -- clignote pendant l'analyse, eteinte au repos.
        const float ledDiameter = juce::jmin (7.0f, bounds.getHeight() * 0.36f);
        auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                              .withCentre ({ bounds.getX() + ledDiameter * 1.5f, bounds.getCentreY() });
        g.setColour (ledLit ? SuppressorColours::ledCyan : SuppressorColours::ledCyan.interpolatedWith (juce::Colours::black, 0.85f));
        g.fillEllipse (ledBounds);

        g.setColour (busy ? SuppressorColours::ledCyan : SuppressorColours::textBright);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.12f));
        g.drawFittedText ("AI", bounds.withTrimmedLeft (ledDiameter * 2.4f).toNearestInt(),
                           juce::Justification::centred, 1);
    }

private:
    struct BlinkTimer : public juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { if (callback) callback(); }
    };

    SuppressorAudioProcessor& processor;
    BlinkTimer blinkTimer;
    bool blinkOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiLearnButton)
};
