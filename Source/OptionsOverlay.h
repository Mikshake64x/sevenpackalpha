#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    OptionsOverlay
    --------------
    Voile plein ecran affiche au clic sur l'engrenage (latencyModeButton).
    Meme langage visuel que AboutOverlay (voile noir + panneau vitre
    encadre ambre), mais avec de VRAIS controles interactifs : taille de
    fenetre (75/85/100/125/150%, proportions conservees via un facteur d'echelle
    applique a l'editeur entier) et budget CPU du mode Linear (Low/Medium/
    High), qui remplace l'ancien PopupMenu de showLatencyModeMenu().
    Se ferme au clic hors du panneau, ou via le bouton CLOSE.
*/
class OptionsOverlay : public juce::Component
{
public:
    OptionsOverlay()
    {
        static const char* scaleLabels[kNumScales]     = { "75%", "80%", "85%", "90%", "95%", "100%" };
        static const char* latencyLabels[kNumLatency] = { "LOW CPU", "MEDIUM CPU", "HIGH CPU" };

        for (int i = 0; i < kNumScales; ++i)
        {
            addAndMakeVisible (scaleButtons[i]);
            scaleButtons[i].setButtonText (scaleLabels[i]);
            scaleButtons[i].setClickingTogglesState (true);
            scaleButtons[i].setRadioGroupId (9101, juce::dontSendNotification);
            scaleButtons[i].onClick = [this, i]
            {
                if (scaleButtons[i].getToggleState() && onScaleSelected)
                    onScaleSelected (kScaleValues[i]);
            };
            scaleButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
            scaleButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledAmber);
            scaleButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
            scaleButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }

        for (int i = 0; i < kNumLatency; ++i)
        {
            addAndMakeVisible (latencyButtons[i]);
            latencyButtons[i].setButtonText (latencyLabels[i]);
            latencyButtons[i].setClickingTogglesState (true);
            latencyButtons[i].setRadioGroupId (9102, juce::dontSendNotification);
            latencyButtons[i].onClick = [this, i]
            {
                if (latencyButtons[i].getToggleState() && onLatencySelected)
                    onLatencySelected (i);
            };
            latencyButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
            latencyButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledAmber);
            latencyButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
            latencyButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }

        addAndMakeVisible (closeButton);
        closeButton.setButtonText ("CLOSE");
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
        closeButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::textBright);
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };
    }

    void setCurrentScaleIndex (int idx)
    {
        for (int i = 0; i < kNumScales; ++i)
            scaleButtons[i].setToggleState (i == idx, juce::dontSendNotification);
    }

    void setCurrentLatencyIndex (int idx)
    {
        for (int i = 0; i < kNumLatency; ++i)
            latencyButtons[i].setToggleState (i == idx, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        panelBounds = bounds.withSizeKeepingCentre (580.0f, 340.0f);
        auto content = panelBounds.reduced (26.0f, 20.0f);

        content.removeFromTop (38.0f);          // titre
        content.removeFromTop (14.0f);           // ligne + marge
        scaleLabelBounds = content.removeFromTop (18.0f).toNearestInt();

        const float gap = 8.0f;
        auto scaleRow = content.removeFromTop (36.0f);
        const float bw = (scaleRow.getWidth() - gap * (kNumScales - 1)) / (float) kNumScales;
        for (int i = 0; i < kNumScales; ++i)
        {
            scaleButtons[i].setBounds (scaleRow.removeFromLeft ((int) bw).toNearestInt());
            scaleRow.removeFromLeft ((int) gap);
        }

        content.removeFromTop (22.0f);           // ligne + marge
        latencyLabelBounds = content.removeFromTop (18.0f).toNearestInt();

        auto latRow = content.removeFromTop (36.0f);
        const float bw2 = (latRow.getWidth() - gap * (kNumLatency - 1)) / (float) kNumLatency;
        for (int i = 0; i < kNumLatency; ++i)
        {
            latencyButtons[i].setBounds (latRow.removeFromLeft ((int) bw2).toNearestInt());
            latRow.removeFromLeft ((int) gap);
        }

        closeButton.setBounds (juce::Rectangle<int> (110, 30)
            .withCentre ({ (int) panelBounds.getCentreX(), (int) panelBounds.getBottom() - 28 }));
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRect (bounds);

        SuppressorLookAndFeel::renderGlassPanel (g, panelBounds, 12.0f);
        SuppressorLookAndFeel::renderGlossySheen (g, panelBounds);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.5f));
        g.drawRoundedRectangle (panelBounds.reduced (0.5f), 12.0f, 1.0f);

        auto content = panelBounds.reduced (26.0f, 20.0f);
        auto titleArea = content.removeFromTop (38.0f);
        g.setColour (SuppressorColours::ledAmber);
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("OPTIONS", titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), content.getY(), content.getRight(), content.getY(), 1.0f);

        g.setColour (SuppressorColours::ledBlue);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("WINDOW SIZE", scaleLabelBounds, juce::Justification::centredLeft, 1);
        g.drawFittedText ("LINEAR MODE CPU BUDGET", latencyLabelBounds, juce::Justification::centredLeft, 1);

        auto dividerY = latencyLabelBounds.getY() - 22.0f;
        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), dividerY, content.getRight(), dividerY, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds.contains (e.position) && onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;
    std::function<void(float)> onScaleSelected;
    std::function<void(int)> onLatencySelected;

private:
    static constexpr int kNumScales = 6;
    static constexpr int kNumLatency = 3;
    static constexpr float kScaleValues[kNumScales] = { 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.0f };

    juce::TextButton scaleButtons[kNumScales];
    juce::TextButton latencyButtons[kNumLatency];
    juce::TextButton closeButton;

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<int> scaleLabelBounds, latencyLabelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsOverlay)
};
