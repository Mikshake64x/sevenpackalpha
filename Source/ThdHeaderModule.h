#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "ThdEngageSwitch.h"
#include "ToggleSwitch.h"

/**
    ThdHeaderModule
    ---------------
    Version COMPACTE du THD Valve, deplacee dans la barre de controle
    (retour utilisateur : "on va liberer le rack du bas ou il y a THD ...
    juste le on/off et les options du THD dans le header, a gauche de AI
    Learn et Analog/Linear") -- remplace l'ancien ThdValveModule (chassis
    pleine largeur + 3 aiguilles par bande, voir SixpackEditorContent::
    vuPlateBounds, desormais supprime). Garde uniquement l'actionneur ON/
    OFF (ThdEngageSwitch), le choix de mode SOFT/TAPE/IRON/AIR (4
    ToggleSwitch en radio exclusif, meme mecanique que l'ancien module) et
    UNE mesure discrete (barre LED, amplitude GLOBALE de saturation --
    SixpackAudioProcessor::clipMeterAmount -- pas 3 mesures par bande).
*/
class ThdHeaderModule : public juce::Component, private juce::Timer
{
public:
    explicit ThdHeaderModule (std::atomic<float>& softClipOnToWatch, std::atomic<float>& clipAmountToWatch)
        : softToggle ("SOFT", SixpackColours::ledGreen, &softClipOnToWatch),
          tapeToggle ("TAPE", SixpackColours::ledAmber, &softClipOnToWatch),
          ironToggle ("IRON", SixpackColours::ledRed,   &softClipOnToWatch),
          airToggle  ("AIR",  SixpackColours::ledCyan,  &softClipOnToWatch),
          clipAmount (clipAmountToWatch)
    {
        addAndMakeVisible (engageSwitch);

        // Meme mecanique que l'ancien ThdValveModule (voir sa doc) : un
        // vrai groupe radio JUCE, sinon cliquer le bouton deja actif le
        // fait basculer OFF cote JUCE avant que le callback externe (voir
        // PluginEditor::wireThdModeButton()) ne force le bon etat.
        constexpr int thdModeRadioGroupId = 9402;
        for (auto* t : { &softToggle, &tapeToggle, &ironToggle, &airToggle })
        {
            t->setRadioGroupId (thdModeRadioGroupId, juce::dontSendNotification);
            addAndMakeVisible (*t);
        }

        startTimerHz (20); // rafraichit la barre LED (amplitude de saturation)
    }

    ~ThdHeaderModule() override { stopTimer(); }

    // Actionneur physique -- attache a idSoftClip via ButtonAttachment
    // dans PluginEditor (meme pattern que l'ancien ThdValveModule).
    ThdEngageSwitch engageSwitch;
    ToggleSwitch softToggle, tapeToggle, ironToggle, airToggle;

    void resized() override
    {
        auto content = getLocalBounds().toFloat();

        auto engageCol = content.removeFromLeft (44.0f);
        engageSwitch.setBounds (juce::Rectangle<float> (38.0f, 38.0f)
                                     .withCentre ({ engageCol.getCentreX(), content.getY() + 20.0f }).toNearestInt());

        content.removeFromLeft (8.0f);

        // Grille 2x2 (et non 1x4) -- une seule rangee de 4 boutons dans
        // ~176px ne laisse qu'~20px de texte par bouton, "SOFT"/"TAPE"
        // se tronquent alors en "SO..."/"TA..." (verifie en capture). La
        // grille double la largeur utile de chaque bouton pour le meme
        // encombrement total.
        constexpr float gap = 4.0f;
        const float cellW = (content.getWidth() - gap) * 0.5f;
        auto row1 = content.removeFromTop (24.0f);
        content.removeFromTop (gap);
        auto row2 = content.removeFromTop (24.0f);
        softToggle.setBounds (row1.removeFromLeft (cellW).toNearestInt());
        row1.removeFromLeft (gap);
        tapeToggle.setBounds (row1.toNearestInt());
        ironToggle.setBounds (row2.removeFromLeft (cellW).toNearestInt());
        row2.removeFromLeft (gap);
        airToggle.setBounds (row2.toNearestInt());

        content.removeFromTop (6.0f);
        meterBounds = content.removeFromTop (7.0f);
    }

    void paint (juce::Graphics& g) override
    {
        // Barre LED discrete -- amplitude GLOBALE de saturation (voir doc
        // de classe), degrade vert->ambre->rouge (meme helper que les
        // autres mesures de GR du plugin, voir SixpackLookAndFeel::
        // ledGradientColour()).
        constexpr int numSegs = 14;
        const float amt = juce::jlimit (0.0f, 1.0f, clipAmount.load());
        const float segW = (meterBounds.getWidth() - (float) (numSegs - 1) * 2.0f) / (float) numSegs;
        for (int i = 0; i < numSegs; ++i)
        {
            const float t = (float) i / (float) (numSegs - 1);
            const bool lit = amt * (float) numSegs > (float) i;
            g.setColour (lit ? SixpackLookAndFeel::ledGradientColour (t) : juce::Colour (0xff1c2830));
            g.fillRoundedRectangle (meterBounds.getX() + (float) i * (segW + 2.0f), meterBounds.getY(), segW, meterBounds.getHeight(), 1.0f);
        }
    }

private:
    // Optimisation CPU : ne redessine que si l'amplitude affichee a
    // reellement bouge (meme principe que GRMeter::timerCallback()).
    void timerCallback() override
    {
        const float amt = juce::jlimit (0.0f, 1.0f, clipAmount.load());
        if (std::abs (amt - lastPaintedAmount) > 0.01f)
        {
            lastPaintedAmount = amt;
            repaint();
        }
    }

    float lastPaintedAmount = -1.0f;
    std::atomic<float>& clipAmount;
    juce::Rectangle<float> meterBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdHeaderModule)
};
