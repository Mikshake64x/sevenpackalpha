#pragma once
#include <JuceHeader.h>

/**
    RackPushButton
    ---------------
    Bouton style "D" valide en preview (mcp__visualize) : releve/embosse
    au repos, s'enfonce dans la plaque (degrade radial sombre + liseret
    couleur en bas) quand actif -- utilise pour SOLO (toggle persistant,
    voir ButtonAttachment sur idSolo) et SEL (retour utilisateur explicite :
    "sel ne doit pas rester enfonce, c'est pour selectionner, pas besoin de
    le laisser enfonce" -- pas un toggle, juste un clic momentane :
    `isDown` ne dure que pendant l'appui, getToggleState() reste toujours
    false pour ce bouton donc rien ne reste enfonce apres le relachement).
*/
class RackPushButton : public juce::Button
{
public:
    RackPushButton (const juce::String& text, juce::Colour accentColour)
        : juce::Button (text), accent (accentColour)
    {
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        auto b = getLocalBounds().toFloat();
        const bool sunken = isDown || getToggleState();
        const float corner = juce::jmin (4.0f, b.getHeight() * 0.3f);

        juce::Path shape;
        shape.addRoundedRectangle (b, corner);

        juce::ColourGradient base (juce::Colour (0xff2e3136), b.getX(), b.getY(),
                                    juce::Colour (0xff17191c), b.getX(), b.getBottom(), false);
        g.setGradientFill (base);
        g.fillRoundedRectangle (b, corner);

        if (! sunken)
        {
            juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 2, 2 }).drawForPath (g, shape);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
        }
        else
        {
            auto inset = b.reduced (1.0f);
            juce::ColourGradient ig (juce::Colours::black.withAlpha (0.6f), inset.getX(), inset.getY(),
                                      juce::Colours::transparentBlack,
                                      inset.getX() + inset.getWidth() * 0.6f, inset.getY() + inset.getHeight() * 0.6f, true);
            g.setGradientFill (ig);
            g.fillRoundedRectangle (inset, corner);

            g.setColour (accent);
            g.fillRect (b.getX() + 2.0f, b.getBottom() - 2.0f, b.getWidth() - 4.0f, 2.0f);
        }

        auto textColour = juce::Colours::white.interpolatedWith (accent, sunken ? 1.0f : 0.0f)
                               .withAlpha (isEnabled() ? (sunken ? 1.0f : 0.55f) : 0.3f);
        g.setColour (textColour);
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (10.0f, b.getHeight() * 0.45f), juce::Font::bold)).withExtraKerningFactor (0.03f));
        g.drawFittedText (getButtonText().toUpperCase(), b.toNearestInt(), juce::Justification::centred, 1);
    }

private:
    juce::Colour accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackPushButton)
};
