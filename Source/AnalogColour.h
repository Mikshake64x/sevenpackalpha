#pragma once
#include <JuceHeader.h>

/**
    AnalogColour
    ------------
    Generateur d'harmoniques douces (2nd ordre asymetrique + 3rd ordre
    symetrique), pour approcher le caractere d'un etage analogique
    (transfo/tube) sans pretendre reproduire exactement un circuit donne.

    - Terme x^2 (k2) : 2nd harmonique, asymetrique (cote "transfo", plus
      feutre/rond -- utilise pour le mode AUDIO "DIST 2")
    - Terme x^3 (k3) : 3rd harmonique, symetrique (cote "triode/saturation
      plus mordante -- utilise pour le mode AUDIO "DIST 3")
    - setHarmonicBias() permet de ponderer l'un vs l'autre sans changer la
      quantite totale d'harmoniques (amount) : bias = -1 -> tout k2,
      bias = +1 -> tout k3, 0 = equilibre (comportement d'origine).
    - DC blocker integre : le terme x^2 introduit un offset DC qu'il faut
      retirer, sinon ca pollue le detecteur feedback et le spectre basse
      frequence.

    Cout CPU minime : aucune fonction transcendante, juste des multiplications.
*/
class AnalogColour
{
public:
    void prepare (double sampleRate) noexcept
    {
        constexpr float dcBlockerHz = 20.0f;
        dcR = std::exp (-2.0f * juce::MathConstants<float>::pi * dcBlockerHz / (float) sampleRate);
        reset();
    }

    void reset() noexcept
    {
        xPrev = 0.0f;
        yPrev = 0.0f;
    }

    // amountPercent : 0..100 (0 = bypass total, transparent)
    void setAmount (float amountPercent) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, amountPercent * 0.01f);
    }

    // -1 (100% k2, "DIST 2") .. 0 (equilibre) .. +1 (100% k3, "DIST 3")
    void setHarmonicBias (float bias) noexcept
    {
        harmonicBias = juce::jlimit (-1.0f, 1.0f, bias);
    }

    inline float processSample (float x) noexcept
    {
        if (amount <= 0.0001f)
            return x;

        const float k2Weight = 1.0f - juce::jmax (0.0f, harmonicBias);
        const float k3Weight = 1.0f + juce::jmin (0.0f, harmonicBias);

        const float k2 = amount * 0.15f * k2Weight;
        const float k3 = amount * 0.08f * k3Weight;
        const float shaped = x + k2 * x * x - k3 * x * x * x;

        const float y = shaped - xPrev + dcR * yPrev;
        xPrev = shaped;
        yPrev = y;
        return y;
    }

private:
    float amount = 0.0f;
    float harmonicBias = 0.0f;
    float dcR     = 0.995f;
    float xPrev    = 0.0f;
    float yPrev    = 0.0f;
};
