#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "RackPushButton.h"

/**
    NeedleModeSelector
    ------------------
    3 boutons empiles verticalement (IN x / OUT x / THD, x = "L" ou "R"
    selon le canal du NeedleVU associe -- GR retire, pas utile pour ce
    cadran global), radio group JUCE natif -- place a GAUCHE de CHAQUE
    NeedleVU (un selecteur independant par cadran, voir PluginEditor) pour
    choisir directement le mode affiche, plutot que de faire defiler au
    clic sur le cadran.

    Style neumorphic "dark mono" unifie avec SOLO/SEL/LEARN/THD/LINK
    (retour utilisateur : "on va mettre les memes boutons ... aussi sur les
    selecteurs des vu metres") -- reutilise directement RackPushButton
    plutot qu'une classe de bouton dediee (l'ancien ModeButton + gradient
    plat propre a ce composant).
*/
class NeedleModeSelector : public juce::Component
{
public:
    static constexpr int numModes = 3;

    // Marge de halo partagee (meme idiome que XoverModeButton::
    // segmentMargin/ConsoleFader::edgePadding) -- l'appelant EXTERIEUR
    // (PluginEditor.cpp) doit agrandir les bounds de CE composant d'autant.
    static constexpr float buttonMargin = 5.0f;

    // channelSuffix : "L" ou "R" -- construit les libelles IN/OUT specifiques
    // au canal de ce cadran (THD reste commun).
    NeedleModeSelector (const juce::String& channelSuffix, int initialIndex = 0)
    {
        const juce::String names[numModes] = { "IN " + channelSuffix, "OUT " + channelSuffix, "THD" };

        for (int i = 0; i < numModes; ++i)
        {
            buttons[i] = std::make_unique<RackPushButton> (names[i], SixpackColours::ledAmber);
            // RackPushButton ne configure pas ceci par defaut (SEL en
            // depend pour rester momentane, voir RackPushButton.h) -- ici
            // un vrai radio group exclusif a besoin du toggle au clic,
            // meme raison que SOLO/LINK IN/LINK OUT.
            buttons[i]->setClickingTogglesState (true);
            buttons[i]->setRadioGroupId (3001, juce::dontSendNotification);
            buttons[i]->setHaloMargin (buttonMargin);
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
        auto bounds = getLocalBounds().toFloat().reduced (buttonMargin);
        constexpr float gap = 3.0f;
        const float h = (bounds.getHeight() - gap * (numModes - 1)) / (float) numModes;
        for (int i = 0; i < numModes; ++i)
        {
            juce::Rectangle<float> nominal (bounds.getX(), bounds.getY() + (float) i * (h + gap), bounds.getWidth(), h);
            buttons[i]->setBounds (nominal.expanded (buttonMargin).toNearestInt());
        }
    }

    void paint (juce::Graphics&) override {}

private:
    int selected = 0;
    std::unique_ptr<RackPushButton> buttons[numModes];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeedleModeSelector)
};
