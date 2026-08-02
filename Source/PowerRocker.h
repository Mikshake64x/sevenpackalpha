#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    PowerRocker
    -----------
    Interrupteur a bascule (rocker) pour le POWER de chaque tranche, a
    partir du sprite Power_switch_01.png (2 etats empiles verticalement :
    le haut de l'image = allume/rouge, le bas = eteint/noir).

    ETAPE PROJUCER REQUISE (comme pour sidekey_monogram.png, voir
    SidekeyMonogram.h) : ajouter Power_switch_01.png comme ressource
    binaire du projet pour que BinaryData::Power_switch_01_png existe.
*/
class PowerRocker : public juce::Button
{
public:
    PowerRocker() : juce::Button ("Power")
    {
        setClickingTogglesState (true);
        sprite = juce::ImageCache::getFromMemory (BinaryData::Power_switch_01_png, BinaryData::Power_switch_01_pngSize);
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (sprite.isNull())
        {
            // Filet de securite si la ressource binaire n'a pas ete ajoutee
            // dans Projucer : on dessine un placeholder plutot que de ne
            // rien afficher du tout.
            g.setColour (getToggleState() ? SuppressorColours::ledRed : juce::Colours::black);
            g.fillRoundedRectangle (bounds, 4.0f);
            return;
        }

        const int frameH = sprite.getHeight() / 2;
        const int srcY = getToggleState() ? 0 : frameH; // haut = ON, bas = OFF

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerRocker)
};
