#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PushButton.h"

/**
    ModeCycleSelector
    -----------------
    Reproduit les boutons DETECTOR et AUDIO du hardware de reference :
    - une colonne VERTICALE de LEDs, chacune avec son libelle de fonction
      A COTE (HP / BELL / LINK ou HP / DIST 2 / DIST 3), comme sur le
      panneau de reference -- pas une rangee horizontale ;
    - un vrai bouton-poussoir MOMENTANE (PushButton, sprite xp_button) en
      dessous, qui fait avancer un compteur d'etat ; Maj-clic pour reculer.

    Le masque de bits associe a l'etat courant vient directement de
    l'index (mask == index) : le parametre sous-jacent cote processeur est
    un unique AudioParameterChoice, donc l'etat est TOUJOURS atomique --
    aucune fenetre ou 2 fonctions pourraient etre temporairement
    incoherentes.
*/
class ModeCycleSelector : public juce::Component
{
public:
    struct LedDef { juce::String label; juce::Colour colour; };

    // captionText : libelle du bouton ("DETECTOR" / "AUDIO")
    // leds        : une entree par LED affichee (2 ou 3 selon la section)
    // stateMasks  : masque de bits pour chaque valeur d'index (mask == index ici)
    ModeCycleSelector (juce::String captionText, std::vector<LedDef> leds, std::vector<int> stateMasks)
        : caption (std::move (captionText)), ledDefs (std::move (leds)), masks (std::move (stateMasks))
    {
        addAndMakeVisible (pushButton);
        pushButton.onClick = [this]
        {
            const bool reverse = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
            setSelectedIndex (selected + (reverse ? -1 : 1));
            if (onSelect)
                onSelect (selected);
        };
    }

    std::function<void (int index)> onSelect;

    void setSelectedIndex (int index)
    {
        const int n = (int) masks.size();
        index = ((index % n) + n) % n;
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    int getSelectedIndex() const noexcept { return selected; }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        buttonArea = bounds.removeFromBottom (juce::jmax (26.0f, bounds.getHeight() * 0.36f));

        // Meme logique de taille que RatioSelector -- le bouton doit
        // paraitre de la meme taille sur les 3 sections.
        const float btnSize = juce::jmin (buttonArea.getHeight() - 2.0f, buttonArea.getWidth() * 0.42f);
        pushButton.setBounds (juce::Rectangle<float> (btnSize, btnSize)
                                   .withCentre ({ buttonArea.getX() + btnSize * 0.62f, buttonArea.getCentreY() })
                                   .toNearestInt());
        captionArea = buttonArea.withLeft (pushButton.getRight() + 2.0f);

        ledRowBounds.clear();
        const int n = (int) ledDefs.size();
        const float rowH = bounds.getHeight() / (float) juce::jmax (1, n);
        for (int i = 0; i < n; ++i)
            ledRowBounds.add (bounds.withY (bounds.getY() + i * rowH).withHeight (rowH).reduced (0.0f, rowH * 0.06f));
    }

    void paint (juce::Graphics& g) override
    {
        const int mask = selected < (int) masks.size() ? masks[(size_t) selected] : 0;

        for (int i = 0; i < (int) ledDefs.size() && i < ledRowBounds.size(); ++i)
        {
            auto row = ledRowBounds.getReference (i);
            const bool lit = (mask & (1 << i)) != 0;

            const float ledD = juce::jmin (11.0f, row.getHeight() * 0.85f);
            auto ledR = juce::Rectangle<float> (ledD, ledD).withCentre ({ row.getX() + ledD * 0.6f, row.getCentreY() });
            SuppressorLookAndFeel::drawGlossyDot (g, ledR, ledDefs[(size_t) i].colour, lit);

            auto textArea = row.withLeft (ledR.getRight() + 4.0f);
            g.setColour (lit ? SuppressorColours::textBright : SuppressorColours::textDim);
            g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)).withExtraKerningFactor (0.03f));
            g.drawFittedText (ledDefs[(size_t) i].label, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        g.setColour (SuppressorColours::textBright);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)).withExtraKerningFactor (0.06f));
        g.drawFittedText (caption, captionArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

private:
    juce::String caption;
    std::vector<LedDef> ledDefs;
    std::vector<int> masks;

    int selected = 0;
    juce::Rectangle<float> buttonArea, captionArea;
    juce::Array<juce::Rectangle<float>> ledRowBounds;
    PushButton pushButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeCycleSelector)
};
