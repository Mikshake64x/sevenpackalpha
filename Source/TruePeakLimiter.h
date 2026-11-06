#pragma once
#include <JuceHeader.h>

/**
    TruePeakLimiter
    ---------------
    Limiteur de fin de chaine, derriere l'ecretage THD (Options > THD stage >
    True peak ceiling ; demande explicite : "activer le surechantillonnage
    (x4 minimum) sur le clipper et le limiteur").

    Remplace l'ancien plafond par coupe nette apres la redescente, qui
    recreait le repliement que le surechantillonnage venait d'eviter
    (mesure : repliement remonte de -85 a -37 dB en x4).

      - detection des pics VRAIS : le signal est sur-echantillonne x8
        (detection seulement, phase lineaire) et le plus grand des 8 points
        de chaque echantillon est retenu ;
      - gain calcule avec 1,5 ms d'anticipation : minimum glissant du gain
        requis sur la fenetre, puis moyenne glissante sur la meme fenetre
        (le gain atteint la valeur requise AU pic, sans cassure), puis
        retour a l'unite en douceur (50 ms) ;
      - le signal est retarde d'autant (anticipation + latence de la
        detection) : getLatency() a ajouter a la latence du plugin.
    Gain commun aux deux canaux (image stereo conservee).
*/
class TruePeakLimiter
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        channels = juce::jlimit (1, 2, numChannels);
        window = juce::jmax (2, juce::roundToInt (sampleRate * 0.0015)) + 1;
        // Detection en PHASE LINEAIRE (comme la mesure des pics vrais) : un
        // filtre a phase minimale deforme les cretes et fausse l'estimation.
        detector = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, (size_t) kDetectOrder,
                                                                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        detector->initProcessing ((size_t) juce::jmax (1, maxBlock));
        detLatency = measureUpLatency();
        detectBuffer.setSize (channels, juce::jmax (1, maxBlock));
        maxBlockSize = juce::jmax (1, maxBlock);
        delayLength = (window - 1) + detLatency;
        for (auto& d : signalDelay)
            d.assign ((size_t) juce::jmax (1, delayLength), 0.0f);
        required.assign ((size_t) window, 1.0f);
        heldMin.assign ((size_t) window, 1.0f);
        releaseCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.05));
        reset();
    }

    void reset()
    {
        if (detector != nullptr)
            detector->reset();
        for (auto& d : signalDelay)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (required.begin(), required.end(), 1.0f);
        std::fill (heldMin.begin(), heldMin.end(), 1.0f);
        heldSum = (double) window;
        delayPos = ringPos = 0;
        gain = 1.0f;
    }

    int getLatency() const noexcept { return delayLength; }

    // data : 1 ou 2 canaux, traites sur place. numSamples <= maxBlock.
    void process (float* const* data, int numSamples, float ceiling) noexcept
    {
        if (detector == nullptr || numSamples > maxBlockSize || data == nullptr || data[0] == nullptr)
            return;

        for (int ch = 0; ch < channels; ++ch)
            detectBuffer.copyFrom (ch, 0, data[ch] != nullptr ? data[ch] : data[0], numSamples);
        juce::dsp::AudioBlock<float> block (detectBuffer.getArrayOfWritePointers(), (size_t) channels, (size_t) numSamples);
        auto up = detector->processSamplesUp (block);

        const float target = ceiling * 0.99f; // petite marge (precision de la detection)
        for (int k = 0; k < numSamples; ++k)
        {
            float tp = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* u = up.getChannelPointer ((size_t) ch) + kDetectFactor * k;
                for (int j = 0; j < kDetectFactor; ++j)
                    tp = juce::jmax (tp, std::abs (u[j]));
            }
            const float need = tp > target ? target / tp : 1.0f;

            // Minimum glissant du gain requis, puis moyenne glissante.
            required[(size_t) ringPos] = need;
            float m = 1.0f;
            for (float r : required)
                m = juce::jmin (m, r);
            heldSum += (double) m - (double) heldMin[(size_t) ringPos];
            heldMin[(size_t) ringPos] = m;
            if (++ringPos >= window)
                ringPos = 0;
            const float smooth = (float) (heldSum / (double) window);

            gain = smooth < gain ? smooth : gain + (smooth - gain) * releaseCoeff;

            for (int ch = 0; ch < channels; ++ch)
            {
                float* x = data[ch];
                if (x == nullptr)
                    continue;
                auto& d = signalDelay[(size_t) ch];
                const float delayed = d[(size_t) delayPos];
                d[(size_t) delayPos] = x[k];
                x[k] = delayed * gain;
            }
            if (++delayPos >= delayLength)
                delayPos = 0;
        }
    }

private:
    // Retard de la seule MONTEE (getLatencyInSamples() compte montee +
    // descente) : position du pic de la reponse impulsionnelle, en
    // echantillons du rythme normal.
    static int measureUpLatency()
    {
        juce::dsp::Oversampling<float> probe (1, (size_t) kDetectOrder, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        constexpr int n = 512;
        probe.initProcessing ((size_t) n);
        juce::AudioBuffer<float> impulse (1, n);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        juce::dsp::AudioBlock<float> block (impulse);
        auto up = probe.processSamplesUp (block);
        int best = 0;
        for (int i = 1; i < (int) up.getNumSamples(); ++i)
            if (std::abs (up.getSample (0, i)) > std::abs (up.getSample (0, best)))
                best = i;
        return juce::roundToInt ((float) best / (float) kDetectFactor);
    }

    // Detection x8 : en x4, les cretes d'un signal tres ecrete (ringing
    // tout en haut du spectre) etaient sous-estimees jusqu'a ~0.9 dB.
    static constexpr int kDetectOrder = 3, kDetectFactor = 1 << kDetectOrder;

    std::unique_ptr<juce::dsp::Oversampling<float>> detector;
    juce::AudioBuffer<float> detectBuffer;
    std::array<std::vector<float>, 2> signalDelay;
    std::vector<float> required, heldMin;
    double heldSum = 0.0;
    int channels = 2, window = 2, detLatency = 0, delayLength = 1, maxBlockSize = 1;
    int delayPos = 0, ringPos = 0;
    float gain = 1.0f, releaseCoeff = 0.001f;
};
