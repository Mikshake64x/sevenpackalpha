#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    GRMeter
    -------
    Rangee horizontale de petites LEDs degradees (cyan -> vert -> jaune -> orange -> rouge)
    indiquant la reduction de gain, avec une echelle en dB affichee en dessous.
    Attaque instantanee, retombee douce (ballistique type VU).
*/
class GRMeter : public juce::Component, private juce::Timer
{
public:
    explicit GRMeter (std::atomic<float>& grValueDbToWatch, std::atomic<float>& powerParamToWatch);
    ~GRMeter() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Echelle non-lineaire : ces reperes dB sont espaces EGALEMENT en
    // pixels (pas leur valeur reelle) -- resolution fine pres de 0dB,
    // plus large au-dela de 10dB, comme un vrai VU/GR meter hardware.
    // dbToFraction() convertit une valeur reelle de reduction (continue)
    // en fraction 0-1 le long de cette meme echelle, pour que la diode
    // allumee corresponde toujours exactement au repere imprime.
    static constexpr float scaleMarksDb[] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
    static constexpr int numScaleMarks = 10;
    static float dbToFraction (float db) noexcept;

    std::atomic<float>& grValueDb;
    std::atomic<float>& powerParam;
    bool powered = true;
    float displayedGrDb = 0.0f;
    float maxRangeDb = 30.0f;

    // Optimisation CPU : on ne redessine que si l'affichage a reellement
    // change (valeur OU etat power), au lieu de repaint() inconditionnel
    // a chaque tick -- evite de faire tourner le rendu (degrades, chemins)
    // pour rien quand le signal est silencieux/stable.
    float lastPaintedGrDb = -1.0f;
    bool lastPaintedPowered = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRMeter)
};
