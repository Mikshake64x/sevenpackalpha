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
        envelopeAboveDb = 0.0f;
        envelopeBelowDb = 0.0f;
        feedbackTapLevel = 0.0f;
        detectorSmoothedLevel = 0.0f;
        // ~3ms -- voir doc au point d'usage dans processSample().
        detectorSmoothCoeff = std::exp (-1.0f / (0.001f * 3.0f * (float) fs));
        colourStage.prepare (sampleRate);
        scEQ.prepare (sampleRate);

        *audioHpFilter.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 80.0f);
        audioHpFilter.reset();

        updateCoeffs();
        updateBelowCoeffs();
    }

    void reset()
    {
        envelopeDb = 0.0f;
        envelopeAboveDb = 0.0f;
        envelopeBelowDb = 0.0f;
        feedbackTapLevel = 0.0f;
        detectorSmoothedLevel = 0.0f;
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

    // --- Branche BELOW (expansion) : ratio/knee/attack/release
    // INDEPENDANTS de la branche Above (compression) -- retour utilisateur
    // ("j'entends que l'expand" en mode Expand) : avant, Above et Below
    // partageaient un seul jeu ratio/attack/release, donc passer sur Expand
    // faisait tourner la compression avec le ratio/l'attaque REGLES POUR
    // L'EXPANSION (typiquement tres doux), rendant la compression
    // quasi-inaudible. Desormais Above garde TOUJOURS les reglages
    // Compression (voir PluginProcessor::updateDspFromParams -- setRatio/
    // setAttackMs/setReleaseMs/setKneeDb ne recoivent plus QUE les valeurs
    // Compression), et ces setters *Below dedies pilotent uniquement la
    // partie expansion, sur son propre jeu de coefficients/enveloppe (voir
    // processSample() et envelopeBelowDb).
    void setBelowRatio (float r) noexcept { ratioBelow = juce::jmax (1.01f, r); }
    void setBelowKneeDb (float db) noexcept { kneeDbBelow = juce::jmax (0.0f, db); }

    void setBelowAttackMs (float ms) noexcept
    {
        if (! juce::approximatelyEqual (attackMsBelow, ms))
        {
            attackMsBelow = ms;
            updateBelowCoeffs();
        }
    }

    void setBelowReleaseMs (float ms) noexcept
    {
        if (! juce::approximatelyEqual (releaseMsBelow, ms))
        {
            releaseMsBelow = ms;
            updateBelowCoeffs();
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

    // Above/Below/Both (demande explicite, "dans le meme esprit que
    // Ableton" + "il faut que les deux puissent jouer en meme temps") --
    // Above (defaut) = compression classique : le signal AU-DESSUS du
    // seuil est REDUIT, proportionnellement a (ratio-1). Below = le signal
    // EN DESSOUS du seuil est traite a la place -- meme forme de courbe
    // (knee/ratio), mais le sens de l'enveloppe est INVERSE : au lieu de
    // reduire le gain, on l'AUGMENTE (envelopeDb devient negatif, voir
    // processSample()) pour faire remonter le signal calme vers le seuil --
    // de la compression ASCENDANTE. Comme l'effet est base sur la MEME
    // topologie feedback (le detecteur regarde le signal deja traite), le
    // boost est intrinsequement stable : plus le signal monte, plus il se
    // rapproche du seuil, plus l'ecart (et donc le boost) diminue -- meme
    // logique d'auto-stabilisation que la compression normale, juste en
    // sens inverse. Both = LES DEUX A LA FOIS -- le dessus est reduit ET le
    // dessous est remonte simultanement, resserrant tout vers le seuil des
    // deux cotes (compression + expansion ascendante combinees).
    enum class AboveBelowMode { Above = 0, Below = 1, Both = 2 };

    // Range de l'expansion (demande explicite : "l'expander doit pas
    // fonctionner de base, il fonctionne juste quand on lui augmente le
    // seuil" + "le knob input du mode comp passe en mode range" -- SEULEMENT
    // pour Expand pur, Comp/Both gardent le knob Input normal) -- plafond
    // EXPLICITE (en dB) du boost applicable au signal sous le seuil,
    // 0 = expansion DESACTIVEE (aucun boost, quel que soit le signal),
    // jusqu'a 24 = plage maximale. Remplace l'ancien plafond fixe en dur
    // (kExpandRangeCapDb) -- desormais reglable, et c'est LUI qui pilote
    // le comportement plutot que l'Input (qui ne pouvait de toute facon
    // jamais "eteindre" l'expansion -- un vrai silence reste sous
    // n'importe quel seuil, meme apres un pre-gain enorme).
    void setExpandRangeDb (float db) noexcept { expandRangeDb = juce::jlimit (0.0f, 24.0f, db); }

    void setAboveBelowMode (AboveBelowMode mode) noexcept { aboveBelowMode = mode; }

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

        // Lissage du detecteur, uniquement quand Below/Both sont actifs
        // (retour utilisateur : "l'expandeur ajoute beaucoup de saturation
        // ... il doit etre le plus neutre possible", mesure au Plugin
        // Doctor -- pas juste un artefact de meter, une vraie distorsion
        // harmonique mesurable). Cause : rawLevel est un redressement
        // PLEINE ONDE instantane (|echantillon|), qui repasse par zero a
        // CHAQUE passage par zero du signal source. En Expand, le
        // detecteur "voit" donc un quasi-silence a chaque zero-crossing et
        // declenche un pic de boost SYNCHRONISE avec le signal lui-meme --
        // une authentique intermodulation, mesurable en THD, pas juste
        // visuelle. La compression garde le detecteur brut (le "grain" fait
        // partie du caractere recherche, type Distressor) -- seul Below/
        // Both, plus sensibles a cet artefact car ils amplifient pres du
        // seuil, beneficient du lissage. Court (~3ms, voir prepare()) pour
        // eliminer l'ondulation au rythme du signal SANS ralentir la
        // reponse globale (toujours pilotee par Attack/Release ensuite).
        // Passe d'optimisation : plus mis a jour en Comp pur (inutilise
        // dans ce mode, ~3 flops/echantillon economises sur les bandes qui
        // n'en ont pas besoin) -- un changement de mode est une action
        // utilisateur ponctuelle (pas un evenement audio-rate), le court
        // rattrapage de ~3ms au moment de rebasculer sur Below/Both est
        // inaudible, d'autant que Ratio/Attack/Release/Range changent tous
        // en meme temps a cet instant de toute facon (voir
        // rebindBandKnobsForMode() cote editeur).
        if (aboveBelowMode != AboveBelowMode::Above)
        {
            detectorSmoothedLevel = rawLevel + (detectorSmoothedLevel - rawLevel) * detectorSmoothCoeff;
            rawLevel = detectorSmoothedLevel;
        }

        const float detectorLevel = rawLevel + 1.0e-8f;
        const float levelDb = juce::Decibels::gainToDecibels (detectorLevel);

        // British Mode : ratio pousse vers l'extreme (quasi-limitation,
        // min ~20:1 meme si un ratio doux est selectionne) et knee resserre
        // (courbe plus "commutee", moins arrondie). Ne s'applique qu'a la
        // branche Above (compression) -- British est un caractere de
        // compresseur (1176 all-buttons-in), pas d'expansion.
        const float effRatio  = britishMode ? juce::jmax (ratio, 20.0f) : ratio;
        const float effKneeDb = britishMode ? kneeDb * 0.4f : kneeDb;

        // Meme forme de courbe knee/ratio des deux cotes du seuil -- seul
        // le signe de "overDb" (quel cote on mesure) change ET le
        // ratio/knee employe differe desormais entre les deux appels (voir
        // plus bas, computeMagnitudeDb parametree). Factorisee en lambda
        // generique (prend ratio/knee en argument) pour servir aux DEUX
        // branches, chacune avec son propre jeu de reglages.
        auto computeMagnitudeDb = [] (float overDb, float r, float kneeDbArg) -> float
        {
            if (kneeDbArg > 0.0f && overDb > -kneeDbArg * 0.5f && overDb < kneeDbArg * 0.5f)
            {
                const float t = (overDb + kneeDbArg * 0.5f) / kneeDbArg;
                const float kneeOver = t * t * kneeDbArg * 0.5f;
                return kneeOver * (r - 1.0f);
            }
            if (overDb > 0.0f)
                return overDb * (r - 1.0f);
            return 0.0f;
        };

        // Above : magnitude POSITIVE = reduction de gain (comportement
        // d'origine, inchange). Below : meme magnitude mais NEGATIVE --
        // envelopeDb negatif se traduit plus bas en BOOST (gainLin =
        // 10^(-envelopeDb/20) > 1 quand envelopeDb < 0), donc le signal
        // sous le seuil remonte au lieu d'etre ecrase -- compression
        // ascendante/expansion. Both : les deux contributions s'additionnent
        // -- au-dessus du seuil ca reduit, en-dessous ca remonte, EN MEME
        // TEMPS (un seul des deux termes est non-nul a un instant donne
        // sauf dans la zone de knee, ou les deux se mixent en douceur).
        // IMPORTANT (retour utilisateur : "le expander est hyper extreme",
        // puis "super fort" verifie au Plugin Doctor) : contrairement au
        // cote Above, ou "overDb" est naturellement borne par la marge du
        // programme (le signal depasse rarement le seuil de plus de
        // 20-30dB), le cote Below n'a AUCUNE borne naturelle -- un
        // silence/bruit de fond peut se retrouver a 60-100dB sous le
        // seuil. Fix v1 (insuffisant, voir retour "super fort") : plafonner
        // seulement l'ECART pris en compte (underDb) a kExpandRangeCapDb --
        // mais la magnitude finale reste overDb*(ratio-1), donc un ratio
        // eleve (ex: 6:1) multiplie quand meme ce plafond par (ratio-1) et
        // redonne un boost demesure (24dB*5 = 120dB cible !). Fix v2 :
        // plafonner directement la MAGNITUDE resultante (pas seulement son
        // entree) a expandRangeDb -- exactement le role du parametre
        // "Range" d'un expander/gate classique : le ratio ne controle plus
        // que la VITESSE avec laquelle on approche ce plafond en descendant
        // sous le seuil, jamais son ampleur. Range REGLABLE (voir
        // setExpandRangeDb()) -- 0 = expansion completement desactivee
        // (demande explicite : "l'expander doit pas fonctionner de base").

        // Above et Below sont desormais deux enveloppes INDEPENDANTES,
        // chacune avec son propre ratio/knee/attack/release (voir
        // setBelowRatio/setBelowKneeDb/setBelowAttackMs/setBelowReleaseMs)
        // -- retour utilisateur ("j'entends que l'expand" en mode Expand) :
        // avec une seule enveloppe partagee, passer sur Expand faisait
        // tourner la COMPRESSION avec le ratio/l'attaque regles pour
        // l'expansion (typiquement tres doux), la rendant quasi-inaudible.
        float targetAboveDb = 0.0f;
        if (aboveBelowMode == AboveBelowMode::Above || aboveBelowMode == AboveBelowMode::Both)
            targetAboveDb = computeMagnitudeDb (levelDb - thresholdDb, effRatio, effKneeDb);

        const float aboveCoeff = (targetAboveDb > envelopeAboveDb) ? attackCoeff : releaseCoeff;
        envelopeAboveDb = targetAboveDb + (envelopeAboveDb - targetAboveDb) * aboveCoeff;

        float targetBelowDb = 0.0f;
        if (aboveBelowMode == AboveBelowMode::Below || aboveBelowMode == AboveBelowMode::Both)
            targetBelowDb = juce::jmin (computeMagnitudeDb (thresholdDb - levelDb, ratioBelow, kneeDbBelow), expandRangeDb);

        const float belowCoeff = (targetBelowDb > envelopeBelowDb) ? belowAttackCoeff : belowReleaseCoeff;
        envelopeBelowDb = targetBelowDb + (envelopeBelowDb - targetBelowDb) * belowCoeff;

        envelopeDb = envelopeAboveDb - envelopeBelowDb;

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
        envelopeAboveDb *= releaseCoeff;
        if (envelopeAboveDb < 0.001f)
            envelopeAboveDb = 0.0f;

        envelopeBelowDb *= belowReleaseCoeff;
        if (envelopeBelowDb < 0.001f)
            envelopeBelowDb = 0.0f;

        envelopeDb = envelopeAboveDb - envelopeBelowDb;
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

    // Branche Below : pas de stabilityFloor/Opto/British (ces caracteres
    // sont specifiques a la compression) -- juste attack/release bruts.
    void updateBelowCoeffs()
    {
        const float a = juce::jmax (0.01f, attackMsBelow);
        const float r = juce::jmax (1.0f, releaseMsBelow);
        belowAttackCoeff  = std::exp (-1.0f / (0.001f * a * (float) fs));
        belowReleaseCoeff = std::exp (-1.0f / (0.001f * r * (float) fs));
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

    // Branche Below (expansion) -- ratio/knee/attack/release independants,
    // voir setBelowRatio/setBelowKneeDb/setBelowAttackMs/setBelowReleaseMs.
    float ratioBelow  = 2.0f;
    float kneeDbBelow = 6.0f;
    float attackMsBelow  = 5.0f;
    float releaseMsBelow = 100.0f;
    float belowAttackCoeff  = 0.0f;
    float belowReleaseCoeff = 0.0f;

    bool optoCharacter = false;
    bool britishMode   = false;
    AboveBelowMode aboveBelowMode = AboveBelowMode::Above;
    float expandRangeDb = 0.0f; // 0 = expansion desactivee par defaut, voir setExpandRangeDb()
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

    float envelopeDb      = 0.0f; // combine (envelopeAboveDb - envelopeBelowDb), voir processSample()
    float envelopeAboveDb = 0.0f;
    float envelopeBelowDb = 0.0f;
    float feedbackTapLevel = 0.0f;
    float detectorSmoothedLevel = 0.0f;
    float detectorSmoothCoeff   = 0.0f;

    SidechainEQ scEQ;

    bool audioHpEngaged = false;
    juce::dsp::IIR::Filter<float> audioHpFilter;
};
