#pragma once
#include <JuceHeader.h>
#include "SidechainEQ.h"
#include "AnalogColour.h"

/**
    FeedbackCompressor
    -------------------
    Compresseur a topologie FEEDBACK (comme le Distressor) : le detecteur
    regarde le niveau du signal DEJA traite (sortie), pas le signal d'entree
    brut.

    SIDECHAIN EXTERNE (bus "Sidechain" du plugin, voir PluginProcessor) :
    quand ce bus est actif (route depuis une autre piste dans le DAW --
    par ex. un kick pour faire du ducking), processSample() bascule en
    feedforward -- le detecteur ecoute directement CE signal externe au
    lieu du feedback interne. Voir le param sidechainSample.

    Valeurs alignees sur le manuel Empirical Labs EL8 Distressor (UAD) :
    - Detector HP : 100Hz, 6dB/oct
    - Detector Band Emphasis : boost ~6kHz (aide a dompter les frequences
      medium-aigues dures pendant une compression agressive)
    - Audio HP : 80Hz, approx. 18dB/oct (Bessel sur le hardware, approxime
      ici par un simple filtre IIR -- pas une reproduction de circuit)
    - 10:1 "Opto" : release nettement plus long/program-dependant (jusqu'a
      ~20s sur le hardware contre 3.5s nominal) -- setOptoCharacter()
      applique un facteur d'echelle approchant ce comportement.

    --- STEREO ---
    "Detector Link" (bit du bouton DETECTOR, LED rouge) : les deux canaux
    partagent le meme niveau au point "Link Sum" du block diagram --
    prelevement APRES le Distortion Generator, AVANT le 80Hz HPF de sortie
    (voir getFeedbackTapLevel()/setFeedbackTapLevel()). Approxime le "phase
    link" original. (Le "Stereo Image Link" du hardware EL8X, interrupteur
    dedie distinct, n'est pas expose dans Suppressor -- retire sur demande.)

    --- BRITISH MODE ---
    Approximation du "all-buttons-in" du 1176 (emule par le Distressor) --
    PAS juste un ratio plus eleve, ca modifie plusieurs etages a la fois :
    - Ratio pousse a l'extreme (min ~20:1), quel que soit le ratio choisi
      au bouton RATIO -- une limitation quasi-brickwall.
    - Knee resserre (x0.4) -- courbe moins arrondie, plus "commutee".
    - Attaque plus rapide (x0.6) et release plus mordant (x0.7) -- cote
      nerveux/imprevisible du all-buttons-in.
    - Detecteur legerement non-lineaire (pow 1.15 sur le niveau detecte) --
      exagere les transitoires, contribue au pompage caracteristique.
    - Saturation harmonique additionnelle (tanh, independante des boutons
      Dist2/Dist3) -- epaissit le son (2e/3e harmonique).
    Approximation musicale, pas une reproduction de circuit.
*/
class FeedbackCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envelopeDb = 0.0f;
        feedbackTapLevel = 0.0f;
        colourStage.prepare (sampleRate);
        scEQ.prepare (sampleRate);

        *audioHpFilter.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 80.0f);
        audioHpFilter.reset();

        updateCoeffs();
    }

    void reset()
    {
        envelopeDb = 0.0f;
        feedbackTapLevel = 0.0f;
        colourStage.reset();
        scEQ.reset();
        audioHpFilter.reset();
    }

    void setInputDriveDb (float db) noexcept
    {
        inputDriveDb = db;
        preGainLin = juce::Decibels::decibelsToGain (db);
    }

    void setRatio (float r) noexcept
    {
        const float clamped = juce::jmax (1.01f, r);
        if (! juce::approximatelyEqual (ratio, clamped))
        {
            ratio = clamped;
            updateCoeffs();
        }
    }

    void setOutputDb (float db) noexcept
    {
        outputDb = db;
        outGainLin = juce::Decibels::decibelsToGain (db);
    }

    void setColourAmount (float percent) noexcept { colourStage.setAmount (percent); }
    void setColourHarmonicBias (float bias) noexcept { colourStage.setHarmonicBias (bias); }

    // --- DETECTOR HP : frequence + pente (6-48dB/oct), pilotes par le
    // drag X/Y de la croix HP dans DetectorFilterGraph. On/off separe
    // (bouton HP) -- freq/pente restent en memoire meme quand coupe. ---
    void setDetectorHpEngaged (bool shouldEngage) noexcept
    {
        const bool wasOn = detectorHpOn;
        detectorHpOn = shouldEngage;
        scEQ.setCutoffHz (detectorHpOn ? detectorHpFreqHz : 20.0f); // 20Hz = bypass, voir SidechainEQ.h

        // Reset PRECISEMENT au moment de l'engagement (pas a chaque appel,
        // ni en continu) : demarre le filtre a plat plutot que de reutiliser
        // une memoire figee depuis le dernier passage en bypass -- evite un
        // crepitement audible au clic sur HP.
        if (detectorHpOn && ! wasOn)
            scEQ.resetHighPassState();
    }

    // Bascule le HP en LP (bande HIGH, deja isolee en frequence par le
    // crossover -- un passe-bas sur le detecteur y est plus utile qu'un
    // passe-haut). Meme croix XY, meme pente variable, voir SidechainEQ.h.
    void setDetectorHpIsLowPass (bool shouldBeLowPass) noexcept { scEQ.setIsLowPass (shouldBeLowPass); }

    void setDetectorHpFrequency (float hz) noexcept
    {
        detectorHpFreqHz = hz;
        if (detectorHpOn)
            scEQ.setCutoffHz (hz);
    }

    // steps : 1..8 (chaque cran = +6dB/octave, 1=6dB ... 8=48dB)
    void setDetectorHpSlopeSteps (int steps) noexcept
    {
        scEQ.setSlopeSteps (steps);
    }

    // --- DETECTOR Bell : frequence + gain continus, pilotes par le drag
    // X/Y de la croix BELL. On/off separe (bouton BELL). ---
    void setDetectorBandEmphasis (bool shouldEngage) noexcept
    {
        const bool wasOn = detectorBellOn;
        detectorBellOn = shouldEngage;
        scEQ.setBoostCutDb (detectorBellOn ? detectorBellGainDb : 0.0f);

        // Meme fix que setDetectorHpEngaged : reset uniquement au front
        // montant (off -> on), pour eviter le crepitement au clic sur BELL.
        if (detectorBellOn && ! wasOn)
            scEQ.resetPeakState();
    }

    void setDetectorBellFrequency (float hz) noexcept
    {
        detectorBellFreqHz = hz;
        scEQ.setFrequencyHz (hz);
        scEQ.setBandwidthOct (1.2f);
    }

    void setDetectorBellGainDb (float db) noexcept
    {
        detectorBellGainDb = db;
        if (detectorBellOn)
            scEQ.setBoostCutDb (db);
    }

    // --- DETECTOR Bell 2 : 2e point de forme, meme mecanique que le Bell
    // ci-dessus mais un etage independant (voir SidechainEQ::setBoostCutDb2/
    // setFrequencyHz2). N'a de bouton dans l'UI que sur la bande MID (voir
    // PluginEditor), remplace HP la -- techniquement dispo sur les 3 bandes. ---
    void setDetectorBandEmphasis2 (bool shouldEngage) noexcept
    {
        const bool wasOn = detectorBell2On;
        detectorBell2On = shouldEngage;
        scEQ.setBoostCutDb2 (detectorBell2On ? detectorBell2GainDb : 0.0f);

        if (detectorBell2On && ! wasOn)
            scEQ.resetPeak2State();
    }

    void setDetectorBell2Frequency (float hz) noexcept
    {
        detectorBell2FreqHz = hz;
        scEQ.setFrequencyHz2 (hz);
        scEQ.setBandwidthOct2 (1.2f);
    }

    void setDetectorBell2GainDb (float db) noexcept
    {
        detectorBell2GainDb = db;
        if (detectorBell2On)
            scEQ.setBoostCutDb2 (db);
    }

    // --- AUDIO : High Pass (80Hz, applique au signal audio, pas au detecteur) ---
    void setAudioHpEngaged (bool shouldEngage) noexcept { audioHpEngaged = shouldEngage; }

    // Genre "10:1 Opto" : release program-dependant, beaucoup plus long
    // (jusqu'a ~20s sur le hardware). Approximation : facteur d'echelle
    // applique au temps de release regle au knob.
    void setOptoCharacter (bool shouldUseOpto) noexcept
    {
        if (optoCharacter != shouldUseOpto)
        {
            optoCharacter = shouldUseOpto;
            updateCoeffs();
        }
    }

    void setBritishMode (bool shouldEnable) noexcept
    {
        if (britishMode != shouldEnable)
        {
            britishMode = shouldEnable;
            updateCoeffs();
        }
    }

    void setAttackMs (float ms) noexcept
    {
        if (! juce::approximatelyEqual (attackMs, ms))
        {
            attackMs = ms;
            updateCoeffs();
        }
    }

    void setReleaseMs (float ms) noexcept
    {
        if (! juce::approximatelyEqual (releaseMs, ms))
        {
            releaseMs = ms;
            updateCoeffs();
        }
    }

    void setKneeDb (float db) noexcept { kneeDb = juce::jmax (0.0f, db); }

    // sidechainSample : si non-nul, le detecteur ecoute CE signal externe
    // (bus Sidechain du plugin, ex. un kick route depuis une autre piste
    // dans Ableton) au lieu du feedback interne -- feedforward, pas
    // feedback, le temps que le bus est actif. nullptr = comportement
    // habituel (detecteur sur le signal deja traite, feedback).
    inline float processSample (float input, const float* sidechainSample = nullptr) noexcept
    {
        const float x = input * preGainLin;

        const float detectorSource = (sidechainSample != nullptr) ? *sidechainSample : feedbackTapLevel;
        const float scFiltered = scEQ.process (detectorSource);
        float rawLevel = std::abs (scFiltered);

        // British Mode : detecteur legerement non-lineaire -- exagere les
        // transitoires, contribue au pompage caracteristique du
        // "all-buttons-in" (voir doc de classe).
        if (britishMode)
            rawLevel = std::pow (rawLevel, 1.15f);

        const float detectorLevel = rawLevel + 1.0e-8f;
        const float levelDb = juce::Decibels::gainToDecibels (detectorLevel);

        float overDb = levelDb - thresholdDb;
        float targetGrDb = 0.0f;

        // British Mode : ratio pousse vers l'extreme (quasi-limitation,
        // min ~20:1 meme si un ratio doux est selectionne) et knee resserre
        // (courbe plus "commutee", moins arrondie).
        const float effRatio  = britishMode ? juce::jmax (ratio, 20.0f) : ratio;
        const float effKneeDb = britishMode ? kneeDb * 0.4f : kneeDb;

        if (effKneeDb > 0.0f && overDb > -effKneeDb * 0.5f && overDb < effKneeDb * 0.5f)
        {
            const float t = (overDb + effKneeDb * 0.5f) / effKneeDb;
            const float kneeOver = t * t * effKneeDb * 0.5f;
            targetGrDb = kneeOver * (effRatio - 1.0f);
        }
        else if (overDb > 0.0f)
        {
            targetGrDb = overDb * (effRatio - 1.0f);
        }

        const float coeff = (std::abs (targetGrDb) > std::abs (envelopeDb)) ? attackCoeff : releaseCoeff;
        envelopeDb = targetGrDb + (envelopeDb - targetGrDb) * coeff;

        const float gainLin = juce::Decibels::decibelsToGain (-envelopeDb);
        const float cleanY = x * gainLin;

        float coloured = colourStage.processSample (cleanY);

        // British Mode : saturation harmonique additionnelle (2e/3e
        // harmonique), independante des boutons Dist2/Dist3 -- "epaissit"
        // le son comme demande.
        if (britishMode)
        {
            constexpr float britishDrive = 1.6f;
            coloured = std::tanh (coloured * britishDrive) / britishDrive;
        }

        // Point de prelevement pour le feedback/Link, conforme au block
        // diagram : APRES le Distortion Generator, AVANT le 80Hz HPF.
        // La distorsion (Dist2/Dist3, British) fait donc partie de ce qui
        // declenche la compression au sample suivant, comme sur le hardware.
        feedbackTapLevel = coloured;

        const float afterAudioHp = audioHpEngaged ? audioHpFilter.processSample (coloured) : coloured;

        return afterAudioHp * outGainLin;
    }

    float getGainReductionDb() const noexcept { return envelopeDb; }

    // --- Detector Link (phase-link) : niveau de detection partage ---
    float getFeedbackTapLevel() const noexcept { return feedbackTapLevel; }
    void setFeedbackTapLevel (float v) noexcept { feedbackTapLevel = v; }

    // --- Stereo Image Link : reduction de gain partagee (preserve l'equilibre L/R) ---
    void setGainReductionDb (float v) noexcept { envelopeDb = v; }

    // Relaxe la reduction de gain vers 0 en suivant le coefficient de
    // RELEASE courant -- PAS un saut instantane. Utilise quand le bypass
    // global est actif : le signal ne traverse plus le compresseur (sortie
    // directement sur le sec), mais l'aiguille/LED de GR doit retomber
    // naturellement selon le temps de Release regle par l'utilisateur,
    // exactement comme la formule normale de l'enveloppe avec une cible a
    // 0 (envelopeDb = target + (envelopeDb-target)*coeff, target=0).
    void relaxGainReductionToZero() noexcept
    {
        envelopeDb *= releaseCoeff;
        if (std::abs (envelopeDb) < 0.001f)
            envelopeDb = 0.0f;
    }

private:
    void updateCoeffs()
    {
        const float stabilityFloorMs = juce::jmax (0.0f, (ratio - 1.0f) * 0.02f);
        float effectiveAttackMs = juce::jmax (attackMs, stabilityFloorMs);
        float effectiveReleaseMs = releaseMs;

        if (optoCharacter)
            effectiveReleaseMs *= 6.7f; // ~20s max au lieu de 3s nominal, comme le hardware

        if (britishMode)
        {
            effectiveReleaseMs *= 0.7f;   // release plus mordant
            effectiveAttackMs  *= 0.6f;   // attaque plus nerveuse ("explosive")
        }

        const float a = juce::jmax (0.01f, effectiveAttackMs);
        const float r = juce::jmax (1.0f, effectiveReleaseMs);
        attackCoeff  = std::exp (-1.0f / (0.001f * a * (float) fs));
        releaseCoeff = std::exp (-1.0f / (0.001f * r * (float) fs));
    }

    double fs = 44100.0;

    AnalogColour colourStage;

    float thresholdDb = -24.0f;
    float kneeDb       = 6.0f;
    float ratio         = 2.0f;

    float attackMs  = 5.0f;
    float releaseMs = 100.0f;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;

    bool optoCharacter = false;
    bool britishMode   = false;
    bool detectorHpOn  = false;
    float detectorHpFreqHz = 100.0f;
    bool detectorBellOn = false;
    float detectorBellFreqHz = 6000.0f;
    float detectorBellGainDb = 6.0f;
    bool detectorBell2On = false;
    float detectorBell2FreqHz = 3000.0f;
    float detectorBell2GainDb = 6.0f;

    float inputDriveDb = 0.0f;
    float outputDb     = 0.0f;
    float preGainLin    = 1.0f;
    float outGainLin    = 1.0f;

    float envelopeDb      = 0.0f;
    float feedbackTapLevel = 0.0f;

    SidechainEQ scEQ;

    bool audioHpEngaged = false;
    juce::dsp::IIR::Filter<float> audioHpFilter;
};
