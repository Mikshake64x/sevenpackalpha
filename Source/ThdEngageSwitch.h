#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    ThdEngageSwitch
    ---------------
    Interrupteur ON/OFF du THD Valve (SOFTCLIP), sprite
    Button-08-S-Filmstrip.png (filmstrip vertical 2 images -- haut =
    engage, bas = off), meme famille que PowerRocker.h. La diode d'etat
    reste sur ThdValveModule (voir ThdValveModule.h) -- ce composant est
    juste l'actionneur physique.
*/
class ThdEngageSwitch : public juce::Button
{
public:
    ThdEngageSwitch() : juce::Button ("THD Engage")
    {
        setClickingTogglesState (true);
        sprite = juce::ImageCache::getFromMemory (BinaryData::Button08SFilmstrip_png, BinaryData::Button08SFilmstrip_pngSize);
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (sprite.isNull())
        {
            g.setColour (getToggleState() ? SuppressorColours::ledAmber : juce::Colours::black);
            g.fillEllipse (bounds);
            return;
        }

        const int frameH = sprite.getHeight() / 2;
        const int srcY = getToggleState() ? 0 : frameH; // haut = engage, bas = off

        auto sub = sprite.getClippedImage ({ 0, srcY, sprite.getWidth(), frameH });

        const float scale = juce::jmin (bounds.getWidth() / (float) sub.getWidth(),
                                         bounds.getHeight() / (float) sub.getHeight());
        const float w = sub.getWidth() * scale;
        const float h = sub.getHeight() * scale;
        auto dest = juce::Rectangle<float> (w, h).withCentre (bounds.getCentre());

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sub, (int) dest.getX(), (int) dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                     0, 0, sub.getWidth(), sub.getHeight());
    }

private:
    juce::Image sprite;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdEngageSwitch)
};
