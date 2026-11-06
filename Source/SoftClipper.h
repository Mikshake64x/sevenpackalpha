#pragma once
#include <JuceHeader.h>

/**
    SoftClipper
    -----------
    Etage de fin de chaine (THD), applique au mix quand le THD est engage.

    Deux etapes par echantillon :
      1. couleur (SatMode, voir idThdMode) : SOFT = aucune couleur, Tape /
         Iron / Air = variantes "mastering" (Clip retire, pas assez
         esthetique a l'usage). AUCUN knob d'intensite : l'ampleur de la
         saturation suit le niveau du signal ;
      2. ecretage au plafond THD CEIL (-0.5 a -3 dBFS, voir setCeilingDb() /
         idThdOutput), forme au choix (Shape, voir idThdClipShape) :
           - Soft : courbe arrondie -- lineaire jusqu'a 3 dB sous le
             plafond, puis tangente hyperbolique qui le rejoint en douceur
             (demande explicite : "preferer un soft clip, courbe arrondie") ;
           - Hard : coupe nette au plafond.

    Pense pour tourner au rythme SUR-ECHANTILLONNE (voir idThdOversampling
    et PluginProcessor::processBlock()) : setOversamplingFactor() recale les
    filtres internes (Iron, Air) et la retombee du temoin de clip pour que
    le son et l'affichage ne changent pas avec le facteur.

    (Avant : des que le THD etait engage, le mode SOFT etait en fait un
    ecretage dur, et la "protection true-peak" -- moyenne de deux
    echantillons deja sous le plafond -- ne pouvait jamais se declencher.
    Remplace par la forme Soft/Hard ci-dessus et le plafond applique au
    signal sur-echantillonne.)
*/
class SoftClipper
{
public:
    enum class SatMode { Soft = 0, Tape, Iron, Air };
    enum class Shape { Soft = 0, Hard };

    void setDriveDb (float db) noexcept
    {
        driveGain = juce::Decibels::decibelsToGain (db);
    }

    void setCeilingDb (float db) noexcept
    {
        ceilingLin = juce::Decibels::decibelsToGain (db);
    }

    void setMode (SatMode m) noexcept { mode = m; }
    void setShape (Shape s) noexcept  { shape = s; }

    // Facteur de sur-echantillonnage du flux traite (1 = rythme normal).
    void setOversamplingFactor (int factor) noexcept
    {
        factor = juce::jmax (1, factor);
        if (factor == osFactor)
            return;
        osFactor = factor;
        const float inv = 1.0f / (float) factor;
        // Meme frequence de coupure / meme retombee quel que soit le rythme :
        // un pole a (fs) vaut pole^(1/facteur) a (facteur * fs).
        ironDcR    = std::pow (0.996f, inv);
        airHpCoeff = std::pow (0.85f, inv);
        meterDecay = std::pow (0.995f, inv);
    }

    void reset() noexcept
    {
        clipAmount = 0.0f;
        ironDcPrev = ironDcState = 0.0f;
        airHpState = airXPrev = 0.0f;
    }

    inline float processSample (float input) noexcept
    {
        const float x = input * driveGain;
        const float coloured = mode == SatMode::Soft ? x : processMastering (x);
        const float z = (shape == Shape::Hard ? hardClip (coloured) : softClip (coloured)) / driveGain;

        // Intensite visuelle du clip (pour les diodes de l'UI)
        const float diff = std::abs (input) - std::abs (z);
        const float target = juce::jlimit (0.0f, 1.0f, diff * 2.5f);
        clipAmount = target > clipAmount ? target : clipAmount * meterDecay;

        return z;
    }

    float getClipAmount() const noexcept { return clipAmount; }

private:
    inline float hardClip (float x) const noexcept
    {
        return juce::jlimit (-ceilingLin, ceilingLin, x);
    }

    // Lineaire jusqu'au genou (3 dB sous le plafond), puis tanh qui
    // rejoint le plafond sans cassure (pente 1 au genou).
    inline float softClip (float x) const noexcept
    {
        const float knee = ceilingLin * kKneeRatio;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float room = ceilingLin - knee;
        const float y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }

    inline float processMastering (float x) noexcept
    {
        switch (mode)
        {
            case SatMode::Tape:
            {
                // Saturation asymetrique douce -- odd+even harmoniques,
                // chaleur "bande magnetique" classique en mastering.
                return x >= 0.0f ? std::tanh (x * 1.15f) / 1.15f
                                  : std::tanh (x * 0.85f) / 0.85f;
            }
            case SatMode::Iron:
            {
                // 2e harmonique dominante (asymetrique, cote "transfo"),
                // + DC blocker (le terme carre introduit un offset).
                constexpr float k = 0.16f;
                const float shaped = x + k * x * x * (x >= 0.0f ? 1.0f : -1.0f);
                const float y = shaped - ironDcPrev + ironDcR * ironDcState;
                ironDcPrev = shaped;
                ironDcState = y;
                return y;
            }
            case SatMode::Air:
            {
                // Exciter : derive un contenu HF (1-pole HP grossier) du
                // signal, le sature separement, et ne remelange QUE les
                // harmoniques generees (pas le HP lui-meme) par-dessus le
                // signal propre -- ajoute de l'"air" sans toucher le corps.
                airHpState = airHpCoeff * (airHpState + x - airXPrev);
                airXPrev = x;
                const float harmonics = std::tanh (airHpState * 3.5f) - airHpState;
                return x + harmonics * 0.4f;
            }
            case SatMode::Soft:
            default:
                return x;
        }
    }

    static constexpr float kKneeRatio = 0.70794578f; // -3 dB

    float driveGain  = 1.0f;
    float clipAmount = 0.0f;
    float ceilingLin = juce::Decibels::decibelsToGain (-0.5f); // defaut -0.5 dBFS, voir setCeilingDb() / idThdOutput
    SatMode mode = SatMode::Soft;
    Shape shape = Shape::Soft;

    int osFactor = 1;
    float ironDcR = 0.996f, airHpCoeff = 0.85f, meterDecay = 0.995f;
    float ironDcPrev = 0.0f, ironDcState = 0.0f;
    float airHpState = 0.0f, airXPrev = 0.0f;
};
