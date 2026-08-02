#pragma once
#include <JuceHeader.h>
#include "SteelRockerToggle.h"
#include "SuppressorLookAndFeel.h"

/**
    SoloSwitch
    ----------
    Bouton SOLO par bande : levier metal (switch_metal.png, voir
    SteelRockerToggle) surmonte d'une diode qui s'allume quand le solo de
    CETTE bande est actif. Place a gauche de chaque rangee de bande (LOW/
    MID/HIGH), assez grand pour rester lisible.
*/
class SoloSwitch : public juce::Component, private juce::Timer
{
public:
    explicit SoloSwitch (juce::Colour ledColourIn) : ledColour (ledColourIn)
    {
        // Pas de cadre de focus clavier au clic/survol -- juste le levier.
        lever.setWantsKeyboardFocus (false);
        lever.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (lever);
        startTimerHz (15);
    }

    ~SoloSwitch() override { stopTimer(); }

    // Levier physique -- attache au parametre APVTS (idSoloLow/Mid/High)
    // via un ButtonAttachment cree par l'editeur, meme pattern que les
    // autres ToggleSwitch/SteelRockerToggle du plugin.
    SteelRockerToggle lever { juce::String() };

    void resized() override
    {
        // Diode a DROITE du libelle "SOLO" (plus a droite de la vis en
        // haut) -- levier sur toute la largeur au-dessus, rangee du bas
        // partagee entre le libelle (gauche) et la diode, plus petite
        // qu'avant (9px au lieu de 12).
        auto b = getLocalBounds();
        auto bottomRow = b.removeFromBottom (16);
        b.removeFromBottom (2);
        lever.setBounds (b);

        ledBounds = bottomRow.removeFromRight (16).withSizeKeepingCentre (9, 9).toFloat();
        labelBounds = bottomRow;
    }

    void paint (juce::Graphics& g) override
    {
        const bool on = lever.getToggleState();
        SuppressorLookAndFeel::drawGlossyDot (g, ledBounds,
            on ? ledColour : ledColour.interpolatedWith (juce::Colours::black, 0.85f), on);

        // Libelle TOUJOURS blanc (demande explicite) -- seule la diode
        // indique l'etat solo, le texte ne change plus de couleur.
        g.setColour (SuppressorColours::textBright);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("SOLO", labelBounds, juce::Justification::centred, 1);
    }

private:
    void timerCallback() override
    {
        const bool on = lever.getToggleState();
        if (on != lastOn)
        {
            lastOn = on;
            repaint();
        }
    }

    juce::Colour ledColour;
    juce::Rectangle<float> ledBounds;
    juce::Rectangle<int> labelBounds;
    bool lastOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloSwitch)
};
