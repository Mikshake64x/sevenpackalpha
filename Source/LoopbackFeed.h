#pragma once
#include <JuceHeader.h>
#include <atomic>

/**
    LoopbackFeed
    ------------
    Pont entre la capture "Computer playback" (LoopbackCapture, horloge du
    peripherique Windows capture) et le traitement du Standalone (horloge de
    la carte son). Les deux horloges ne sont jamais exactement les memes :
    une simple file deborderait ou se viderait au bout de quelques minutes
    (clic). Ici :
      - file sans verrou (AbstractFifo, stereo, ~1,4 s) remplie par le
        thread de capture ;
      - lue par le thread audio a travers un interpolateur de Lagrange, au
        rapport frequence capture / frequence carte (48 kHz -> 44,1 kHz
        etc.), corrige en douceur (+-0,5 % max, en pratique quelques
        centiemes de %) pour garder le remplissage autour d'une cible :
        pas de clic, ecart de hauteur inaudible ;
      - amorcage : silence tant que la file n'a pas atteint la cible ; en
        cas de famine ou de debordement (peripherique coupe...), on
        reamorce proprement.
    Latence ajoutee : ~2048 echantillons de la capture + 2 blocs de la carte.
*/
class LoopbackFeed
{
public:
    LoopbackFeed()
    {
        ring.setSize (2, kCapacity);
        ring.clear();
        scratch.setSize (2, kScratch);
    }

    // --- Commande (thread de messages) ---
    void setActive (bool shouldBeActive)
    {
        if (shouldBeActive && ! active.load())
            resetRequested = true;
        active = shouldBeActive;
    }
    bool isActive() const noexcept                    { return active.load(); }
    // Anti-larsen : la sortie joue sur le peripherique capture.
    void setOutputMuted (bool shouldMute) noexcept    { outputMuted = shouldMute; }
    bool isOutputMuted() const noexcept               { return outputMuted.load(); }

    // --- Thread de capture ---
    void prepareCapture (double rate)
    {
        captureRate = rate;
        resetRequested = true;
    }

    void push (const float* const* stereo, int n)
    {
        if (! active.load() || n <= 0)
            return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2); // plein : le surplus est perdu (reamorcage cote lecture)
        for (int ch = 0; ch < 2; ++ch)
        {
            if (n1 > 0) ring.copyFrom (ch, s1, stereo[ch], n1);
            if (n2 > 0) ring.copyFrom (ch, s2, stereo[ch] + n1, n2);
        }
        fifo.finishedWrite (n1 + n2);
    }

    // --- Thread audio : remplit L/R (n echantillons a hostRate) ---
    void pull (float* L, float* R, int n, double hostRate) noexcept
    {
        const double cr = captureRate.load();
        if (resetRequested.exchange (false))
            primed = false;
        if (cr <= 0.0 || hostRate <= 0.0 || L == nullptr || R == nullptr)
        {
            zero (L, R, n);
            return;
        }

        const double nominal = cr / hostRate;
        const int target = juce::jlimit (1024, kCapacity / 2, 2048 + (int) std::ceil (2.0 * n * nominal));

        if (! primed)
        {
            const int avail = fifo.getNumReady();
            if (avail < target)
            {
                zero (L, R, n);
                return;
            }
            skip (avail - target);
            interpL.reset();
            interpR.reset();
            fillAvg = (double) target;
            primed = true;
        }

        int done = 0;
        while (done < n)
        {
            const int chunk = juce::jmin (n - done, kMaxChunk);
            const int avail = fifo.getNumReady();

            // Debordement (la carte s'est arretee un moment...) : retour a la cible.
            if (avail > kCapacity - kCapacity / 8)
                skip (avail - target);

            // Correction lente du rapport selon le remplissage moyen.
            fillAvg += 0.02 * ((double) fifo.getNumReady() - fillAvg);
            const double err = (fillAvg - (double) target) / (double) target;
            const double ratio = nominal * (1.0 + juce::jlimit (-0.005, 0.005, err * 0.004));

            const int need = (int) std::ceil (chunk * ratio) + 6;
            if (fifo.getNumReady() < need || need > kScratch)
            {
                // Famine : silence, puis reamorcage.
                zero (L + done, R + done, n - done);
                primed = false;
                return;
            }

            int s1, n1, s2, n2;
            fifo.prepareToRead (need, s1, n1, s2, n2);
            for (int ch = 0; ch < 2; ++ch)
            {
                if (n1 > 0) scratch.copyFrom (ch, 0, ring, ch, s1, n1);
                if (n2 > 0) scratch.copyFrom (ch, n1, ring, ch, s2, n2);
            }
            const int used = interpL.process (ratio, scratch.getReadPointer (0), L + done, chunk);
            interpR.process (ratio, scratch.getReadPointer (1), R + done, chunk);
            fifo.finishedRead (juce::jmin (used, n1 + n2));
            done += chunk;
        }
    }

private:
    static constexpr int kCapacity = 1 << 16, kScratch = 1 << 14, kMaxChunk = 2048;

    static void zero (float* L, float* R, int n) noexcept
    {
        if (L != nullptr) juce::FloatVectorOperations::clear (L, n);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, n);
    }

    void skip (int frames) noexcept
    {
        if (frames <= 0)
            return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (frames, s1, n1, s2, n2);
        fifo.finishedRead (n1 + n2);
    }

    juce::AbstractFifo fifo { kCapacity };
    juce::AudioBuffer<float> ring, scratch;
    juce::LagrangeInterpolator interpL, interpR;
    std::atomic<double> captureRate { 0.0 };
    std::atomic<bool> active { false }, outputMuted { false }, resetRequested { true };
    bool primed = false;
    double fillAvg = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackFeed)
};
