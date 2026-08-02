#pragma once
#include <JuceHeader.h>

/**
    SidechainEQ
    -----------
    Filtre applique UNIQUEMENT au signal qui alimente le detecteur du
    compresseur (feedback) -- jamais au signal audio principal.

    HP a PENTE VARIABLE (6 a 48 dB/octave, par pas de 6dB -- 8 crans) :
    cable sur le drag vertical de la croix HP dans DetectorFilterGraph.
    Implemente en cascade : jusqu'a 4 etages biquad (12dB chacun) + un
    etage 1-pole optionnel (6dB) pour les crans impairs -- ex. 18dB/oct =
    1 biquad (12dB) + 1 pole (6dB).

    Peak filter (Bell) : frequence + gain continus, cables sur le drag de
    la croix BELL (X = frequence, Y = gain jusqu'a +18dB).
*/
class SidechainEQ
{
public:
    static constexpr int maxSlopeSteps = 8; // 8 x 6dB = 48dB/oct max
    static constexpr int maxBiquads = 4;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
        dirty = true;
        updateCoefficients();
    }

    void reset()
    {
        for (auto& f : hpBiquads) f.reset();
        hpOnePole.reset();
        peakFilter.reset();
        peakFilter2.reset();
    }

    // Remet a zero la memoire interne des etages HP/Bell -- a appeler
    // PRECISEMENT au moment ou le bouton HP/BELL passe de off a on (voir
    // FeedbackCompressor::setDetectorHpEngaged/setDetectorBandEmphasis).
    // Sans ca, les biquads gardent l'etat fige d'AVANT le bypass (ils ne
    // sont jamais traites tant que le filtre est en position "off") --
    // au reengagement, ce vieil etat se combine brutalement aux nouveaux
    // coefficients et produit un crepitement audible.
    void resetHighPassState() noexcept
    {
        for (auto& f : hpBiquads) f.reset();
        hpOnePole.reset();
    }

    void resetPeakState() noexcept { peakFilter.reset(); }
    void resetPeak2State() noexcept { peakFilter2.reset(); }

    void setCutoffHz (float hz) noexcept
    {
        if (! juce::approximatelyEqual (cutoffHz, hz)) { cutoffHz = hz; dirty = true; }
    }

    // Bascule l'etage HP/LP entre passe-HAUT (comportement d'origine) et
    // passe-BAS -- meme cascade, meme pente variable 6-48dB/oct, juste le
    // type de filtre change. Utilise par la bande HIGH (deja isolee en
    // frequence par le crossover, un passe-bas sur le detecteur y est plus
    // utile qu'un passe-haut) -- voir FeedbackCompressor::setDetectorHpIsLowPass().
    void setIsLowPass (bool shouldBeLowPass) noexcept
    {
        if (isLowPass != shouldBeLowPass) { isLowPass = shouldBeLowPass; dirty = true; }
    }

    // steps : 1..8, chaque cran = +6dB/octave (1=6dB, 8=48dB)
    void setSlopeSteps (int steps) noexcept
    {
        steps = juce::jlimit (1, maxSlopeSteps, steps);
        if (steps != slopeSteps) { slopeSteps = steps; dirty = true; }
    }

    void setBoostCutDb (float db) noexcept
    {
        if (! juce::approximatelyEqual (boostCutDb, db)) { boostCutDb = db; dirty = true; }
    }

    void setFrequencyHz (float hz) noexcept
    {
        if (! juce::approximatelyEqual (freqHz, hz)) { freqHz = hz; dirty = true; }
    }

    void setBandwidthOct (float oct) noexcept
    {
        if (! juce::approximatelyEqual (bwOct, oct)) { bwOct = oct; dirty = true; }
    }

    // --- 2e etage Bell, independant du premier (voir FeedbackCompressor::
    // setDetectorBandEmphasis2()) -- meme mecanique, cascade juste apres le
    // 1er Bell dans process(). ---
    void setBoostCutDb2 (float db) noexcept
    {
        if (! juce::approximatelyEqual (boostCutDb2, db)) { boostCutDb2 = db; dirty = true; }
    }

    void setFrequencyHz2 (float hz) noexcept
    {
        if (! juce::approximatelyEqual (freqHz2, hz)) { freqHz2 = hz; dirty = true; }
    }

    void setBandwidthOct2 (float oct) noexcept
    {
        if (! juce::approximatelyEqual (bwOct2, oct)) { bwOct2 = oct; dirty = true; }
    }

    inline float process (float input) noexcept
    {
        if (dirty)
            updateCoefficients();

        // Ordre conforme au block diagram Distressor : Band Emphasis
        // (peak filter) AVANT le Sidechain HP Filter dans la chaine du
        // detecteur. Le 2e Bell (s'il existe) est cascade juste apres le 1er.
        float y = isPeakAtUnity ? input : peakFilter.processSample (input);
        if (! isPeak2AtUnity)
            y = peakFilter2.processSample (y);

        if (! isCutoffAtMinimum)
        {
            const int numBiquads = slopeSteps / 2;
            for (int i = 0; i < numBiquads; ++i)
                y = hpBiquads[(size_t) i].processSample (y);
            if (slopeSteps % 2 == 1)
                y = hpOnePole.processSample (y);
        }

        return y;
    }

private:
    void updateCoefficients()
    {
        const float nyquistMargin = (float) (fs * 0.45);

        isCutoffAtMinimum = (cutoffHz <= 20.01f);
        isPeakAtUnity = (std::abs (boostCutDb) < 0.01f);

        const float safeCutoff = juce::jlimit (20.0f, nyquistMargin, cutoffHz);
        auto hpCoeffs = isLowPass
                            ? juce::dsp::IIR::Coefficients<float>::makeLowPass (fs, safeCutoff)
                            : juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, safeCutoff);
        for (auto& f : hpBiquads)
            *f.coefficients = *hpCoeffs;
        *hpOnePole.coefficients = isLowPass
                            ? *juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (fs, safeCutoff)
                            : *juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (fs, safeCutoff);

        const float q = bandwidthToQ (bwOct);
        const float safeFreq = juce::jlimit (20.0f, nyquistMargin, freqHz);
        const float gainLin = juce::Decibels::decibelsToGain (boostCutDb);
        *peakFilter.coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (fs, safeFreq, q, gainLin);

        isPeak2AtUnity = (std::abs (boostCutDb2) < 0.01f);
        const float q2 = bandwidthToQ (bwOct2);
        const float safeFreq2 = juce::jlimit (20.0f, nyquistMargin, freqHz2);
        const float gainLin2 = juce::Decibels::decibelsToGain (boostCutDb2);
        *peakFilter2.coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (fs, safeFreq2, q2, gainLin2);

        dirty = false;
    }

    static float bandwidthToQ (float oct) noexcept
    {
        oct = juce::jmax (0.05f, oct);
        const float p = std::pow (2.0f, oct);
        return std::sqrt (p) / (p - 1.0f);
    }

    double fs = 44100.0;

    float cutoffHz   = 20.0f;   // 20Hz = position minimale = "Off" (vrai bypass)
    int   slopeSteps  = 2;       // 2 x 6dB = 12dB/oct par defaut (comportement d'origine)
    float boostCutDb = 0.0f;
    float freqHz     = 650.0f;
    float bwOct       = 1.8f;
    bool  dirty        = true;
    bool  isCutoffAtMinimum = true;
    bool  isPeakAtUnity = true;
    bool  isLowPass = false;

    // 2e etage Bell (voir setBoostCutDb2/setFrequencyHz2/setBandwidthOct2).
    float boostCutDb2 = 0.0f;
    float freqHz2     = 3000.0f;
    float bwOct2       = 1.8f;
    bool  isPeak2AtUnity = true;

    juce::dsp::IIR::Filter<float> hpBiquads[maxBiquads];
    juce::dsp::IIR::Filter<float> hpOnePole;
    juce::dsp::IIR::Filter<float> peakFilter;
    juce::dsp::IIR::Filter<float> peakFilter2;
};
