#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    RatioSelector
    -------------
    UN BOUTON PAR RATIO (2:1 / 3:1 / 4:1 / 6:1 / 8:1 / 10:1 / 20:1),
    directement cliquable -- pas de defilement. Un seul actif a la fois
    (radio group JUCE natif). (1:1 retire -- ratio neutre inutile.)

    Style bouton "plastique carre en relief" : au repos, le bouton est
    SURELEVE (bevel clair en haut, ombre en bas). Actif : le bouton
    s'ENFONCE (bevel inverse -- sombre en haut/gauche, clair en bas/droite)
    -- la couleur du bouton NE change PAS, seule la diode au-dessus
    s'allume. Le libelle du ratio est imprime au-dessus de la diode.

    Un seul actif a la fois (radio group JUCE natif). 10:1 est desormais
    un ratio comme les autres -- le comportement "Opto" (release plus
    doux/program-dependant) est pilote par le bouton OPTICAL global (voir
    PluginEditor), applicable a n'importe quel ratio.
*/
class RatioSelector : public juce::Component
{
public:
    static constexpr int numRatios = 7;

    struct RatioButton : public juce::Button, private PressAnimator
    {
        RatioButton (const juce::String& labelText, juce::Colour ledColourIn)
            : juce::Button (labelText), text (labelText), ledColour (ledColourIn)
        {
            setClickingTogglesState (true);
        }

        void buttonStateChanged() override
        {
            if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
        }

        void onPressAnimated() override { repaint(); }

        // Meme style EXACT que ToggleSwitch (Detector/Audio) : rectangle
        // mat, diode a gauche, bordure toujours neutre -- coherence
        // visuelle demandee entre toutes les rangees de boutons.
        void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/) override
        {
            auto bounds = getLocalBounds().toFloat();
            applyPressTransform (g, bounds);
            const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
            const bool powered = (laf == nullptr) || laf->isPowered();
            // Bouton physique : l'aspect (enfonce/sureleve, texte) suit
            // l'etat mecanique, pas le courant. Seule la diode s'eteint.
            const bool on = getToggleState();
            const bool ledLit = powered && on;
            const juce::Colour accent = ledColour;
            const float corner = juce::jmin (7.0f, bounds.getHeight() * 0.16f);

            juce::Path shape;
            shape.addRoundedRectangle (bounds, corner);
            juce::DropShadow shadow (juce::Colours::black.withAlpha (0.4f), 3, { 0, 2 });
            shadow.drawForPath (g, shape);

            juce::ColourGradient fillGrad (on ? juce::Colour (0xff2f333a) : juce::Colour (0xff2b2e33), bounds.getX(), bounds.getY(),
                                            on ? juce::Colour (0xff191c22) : juce::Colour (0xff17191c), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill (fillGrad);
            g.fillRoundedRectangle (bounds, corner);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

            const float ledDiameter = juce::jmin (7.0f, bounds.getWidth() * 0.22f);
            auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                                  .withCentre ({ bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.28f });
            g.setColour (ledLit ? accent : accent.interpolatedWith (juce::Colours::black, 0.85f));
            g.fillEllipse (ledBounds);

            g.setColour (on ? SuppressorColours::textBright : SuppressorColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, bounds.getWidth() * 0.34f), juce::Font::bold)));
            g.drawFittedText (text, bounds.withTrimmedTop (bounds.getHeight() * 0.5f).toNearestInt(),
                               juce::Justification::centred, 1);
        }

        juce::String text;
        juce::Colour ledColour;
    };

    RatioSelector()
    {
        static const char* names[numRatios] = { "2:1", "3:1", "4:1", "6:1", "8:1", "10:1", "20:1" };

        for (int i = 0; i < numRatios; ++i)
        {
            // Degrade cyan (2:1, le plus doux) -> rouge (20:1, le plus dur).
            const juce::Colour ledColour = SuppressorLookAndFeel::ratioLedColour ((float) i / (float) (numRatios - 1));
            buttons[i] = std::make_unique<RatioButton> (names[i], ledColour);
            buttons[i]->setRadioGroupId (1001, juce::dontSendNotification);
            addAndMakeVisible (*buttons[i]);

            buttons[i]->onClick = [this, i]
            {
                if (buttons[i]->getToggleState())
                {
                    selected = i;
                    if (onSelect)
                        onSelect (selected);
                    repaint();
                }
            };
        }

        buttons[selected]->setToggleState (true, juce::dontSendNotification);
    }

    std::function<void (int index)> onSelect;

    void setSelectedIndex (int index)
    {
        index = juce::jlimit (0, numRatios - 1, index);
        if (index != selected || ! buttons[index]->getToggleState())
        {
            selected = index;
            buttons[index]->setToggleState (true, juce::dontSendNotification);
            repaint();
        }
    }

    int getSelectedIndex() const noexcept { return selected; }

    void resized() override
    {
        auto bounds = getLocalBounds();
        const int gap = 4;
        const int w = (bounds.getWidth() - gap * (numRatios - 1)) / numRatios;
        for (int i = 0; i < numRatios; ++i)
            buttons[i]->setBounds (bounds.getX() + i * (w + gap), bounds.getY(), w, bounds.getHeight());
    }

    void paint (juce::Graphics&) override {}

private:
    int selected = 3; // 6:1 par defaut
    std::unique_ptr<RatioButton> buttons[numRatios];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RatioSelector)
};
