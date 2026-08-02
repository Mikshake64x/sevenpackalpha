#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    ToggleSwitch
    ------------
    Bouton rectangulaire "plastique mou mat" avec diode a gauche --
    utilise pour DETECTOR/AUDIO (HP, BELL, LINK, TUBE, FUZZ) et
    SOFTCLIP. Design valide en session : coins moyennement arrondis (PAS
    une pilule), degrade diffus mat (aucune bande brillante/glossy), ombre
    portee douce pour un leger relief 3D, BORDURE TOUJOURS NEUTRE (jamais
    teintee par la couleur d'accent). La DIODE est le seul indicateur
    d'etat -- pas de halo/glow autour du bouton ni du contour.

    Anime en plus un petit "squash" au clic (voir PressAnimator) -- ne
    reste JAMAIS visuellement enfonce, meme si l'etat logique (toggle)
    reste actif.
*/
class ToggleSwitch : public juce::Button, private PressAnimator
{
public:
    // externalEnabledSource : optionnel, meme traitement que le Power global
    // (diode eteinte) mais pilote par un atomic<float> LOCAL -- utilise par
    // ThdValveModule pour ses 4 boutons de mode (idSoftClip).
    explicit ToggleSwitch (const juce::String& labelText, juce::Colour accentColour = SuppressorColours::ledRed,
                            std::atomic<float>* externalEnabledSource = nullptr)
        : juce::Button (labelText), text (labelText), accent (accentColour), externalEnabled (externalEnabledSource)
    {
        setClickingTogglesState (true);
    }

    // Change le libelle affiche apres construction (ex: "HP" -> "LP" sur
    // la bande HIGH, voir SuppressorAudioProcessorEditor::buildBandPanel()).
    void setText (const juce::String& newText) { text = newText; repaint(); }

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();
        applyPressTransform (g, bounds);
        const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
        const bool powered = ((laf == nullptr) || laf->isPowered())
                              && (externalEnabled == nullptr || externalEnabled->load() > 0.5f);
        // Bouton physique : sa position/son aspect ne changent pas avec le
        // courant. SEULE la diode est electrique -- elle s'eteint hors
        // tension, meme si l'interrupteur est mecaniquement enclenche.
        const bool on = getToggleState();
        const bool ledLit = powered && on;
        const float corner = juce::jmin (7.0f, bounds.getHeight() * 0.2f);

        // --- Ombre portee douce (relief 3D leger, mat) ---
        juce::Path shape;
        shape.addRoundedRectangle (bounds, corner);
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.4f), 3, { 0, 2 });
        shadow.drawForPath (g, shape);

        // --- Fond mat : degrade diffus tres discret, quasi identique
        // off/on -- pas de bande glossy, pas de teinte de couleur qui
        // envahit le bouton. ---
        juce::ColourGradient fillGrad (on ? juce::Colour (0xff2f333a) : juce::Colour (0xff2b2e33), bounds.getX(), bounds.getY(),
                                        on ? juce::Colour (0xff191c22) : juce::Colour (0xff17191c), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (fillGrad);
        g.fillRoundedRectangle (bounds, corner);

        // --- Bordure TOUJOURS neutre (jamais teintee par l'accent) ---
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

        // --- Diode : seul indicateur d'etat, pas de halo autour ---
        const float ledDiameter = juce::jmin (8.0f, bounds.getHeight() * 0.34f);
        auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                              .withCentre ({ bounds.getX() + ledDiameter * 1.6f, bounds.getCentreY() });
        g.setColour (ledLit ? accent : accent.interpolatedWith (juce::Colours::black, 0.85f));
        g.fillEllipse (ledBounds);

        g.setColour (on ? SuppressorColours::textBright : SuppressorColours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText (text.toUpperCase(), bounds.withTrimmedLeft (ledDiameter * 2.8f).toNearestInt(),
                           juce::Justification::centredLeft, 1);
    }

private:
    juce::String text;
    juce::Colour accent;
    std::atomic<float>* externalEnabled = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleSwitch)
};
