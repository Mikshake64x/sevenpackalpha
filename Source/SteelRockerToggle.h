#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    SteelRockerToggle
    -----------------
    Petit interrupteur a levier metal (asset switch_metal.png), utilise
    pour STEREO IMAGE LINK et BRITISH MODE. Filmstrip vertical a 2 images
    (64x64 chacune) : levier releve (haut) / leve abaisse (bas). Le levier
    releve est mappe sur "on" par defaut -- utiliser setTopFrameIsOn(false)
    si c'est l'inverse a l'usage.

    Libelle imprime en dessous du levier (comme sur le hardware de
    reference, ou le texte est serigraphie sur le panneau, pas sur le
    bouton lui-meme).

    ETAPE PROJUCER REQUISE : ajouter switch_metal.png comme ressource
    binaire (meme procedure que sidekey_monogram.png).
*/
class SteelRockerToggle : public juce::Button
{
public:
    explicit SteelRockerToggle (const juce::String& labelLine1, const juce::String& labelLine2 = {})
        : juce::Button ("SteelRockerToggle"), line1 (labelLine1), line2 (labelLine2)
    {
        setClickingTogglesState (true);
        sprite = juce::ImageCache::getFromMemory (BinaryData::switch_metal_png, BinaryData::switch_metal_pngSize);
    }

    // Si le rendu est inverse (ON montre le levier baisse), passer false ici.
    void setTopFrameIsOn (bool shouldTopBeOn) { topFrameIsOn = shouldTopBeOn; repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool /*isButtonDown*/) override
    {
        auto full = getLocalBounds().toFloat();
        auto switchArea = full;

        const bool hasLabel = line1.isNotEmpty();
        if (hasLabel)
            switchArea = full.withHeight (full.getWidth()); // zone carree pour le levier

        const bool on = getToggleState();

        if (sprite.isNull())
        {
            g.setColour (on ? SixpackColours::ledBlue.withAlpha (0.4f) : juce::Colours::black);
            g.fillRoundedRectangle (switchArea.reduced (2.0f), 4.0f);
        }
        else
        {
            const int frameH = sprite.getHeight() / 2;
            const bool showTop = (on == topFrameIsOn);
            const int srcY = showTop ? 0 : frameH;

            auto sub = sprite.getClippedImage ({ 0, srcY, sprite.getWidth(), frameH });

            const float scale = juce::jmin (switchArea.getWidth() / (float) sub.getWidth(),
                                             switchArea.getHeight() / (float) sub.getHeight());
            const float w = sub.getWidth() * scale;
            const float h = sub.getHeight() * scale;
            auto dest = juce::Rectangle<float> (w, h).withCentre (switchArea.getCentre());

            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (sub, (int) dest.getX(), (int) dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                         0, 0, sub.getWidth(), sub.getHeight());
        }

        juce::ignoreUnused (isHighlighted);

        if (hasLabel)
        {
            auto labelArea = full.withTop (switchArea.getBottom());
            g.setColour (on ? SixpackColours::ledBlue : SixpackColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (9.0f, labelArea.getHeight() * 0.42f), juce::Font::bold))
                           .withExtraKerningFactor (0.05f));

            if (line2.isNotEmpty())
            {
                auto l1Area = labelArea.removeFromTop (labelArea.getHeight() * 0.5f);
                g.drawFittedText (line1, l1Area.toNearestInt(), juce::Justification::centred, 1);
                g.drawFittedText (line2, labelArea.toNearestInt(), juce::Justification::centred, 1);
            }
            else
            {
                g.drawFittedText (line1, labelArea.toNearestInt(), juce::Justification::centred, 2);
            }
        }
    }

private:
    juce::Image sprite;
    juce::String line1, line2;
    bool topFrameIsOn = true; // levier releve = on, par convention visuelle du sprite

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteelRockerToggle)
};
