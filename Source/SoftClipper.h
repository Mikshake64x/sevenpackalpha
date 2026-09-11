#pragma once
#include <JuceHeader.h>

/**
    SoftClipper
    -----------
    Etage de fin de chaine.

    Bouton LIMIT desactive (par defaut) : saturation douce (tanh) pure,
    AUCUN plafond artificiel -- le signal peut depasser 0dBFS librement,
    comme le softclip de l'Arousor/Distressor qui n'a pas de brickwall
    numerique strict.

    Bouton LIMIT active : brickwall a -0.5dBFS par defaut (abaissable
    jusqu'a -3dBFS, voir setCeilingDb() / idThdOutput -- barre du bas),
    avec une protection
    "true clip" inspiree du Glue Compressor d'Ableton : on estime le pic
    INTER-echantillon par interpolation lineaire et on limite aussi ce pic
    estime, pas seulement l'echantillon brut, pour eviter les depassements
    caches qui n'apparaissent qu'apres reconstruction D/A.

    SatMode (voir idThdMode/ThdValveModule) : SOFT (= comportement d'origine
    ci-dessus, bouton explicite desormais) + 3 variantes "mastering" --
    Tape/Iron/Air (Clip retire, pas assez esthetique a l'usage). AUCUN knob
    d'intensite separe : l'ampleur de la saturation suit NATURELLEMENT le
    niveau du signal (plus le signal est chaud, plus ca sature), pas un
    montant fixe -- volontairement pense pour du mastering (pas de
    distorsion agressive style guitare metal). Actif uniquement quand
    mode != Soft, remplace alors la branche LIMIT/tanh ci-dessus.
*/
class SoftClipper
{
public:
    enum class SatMode { Soft = 0, Tape, Iron, Air };

    void setDriveDb (float db) noexcept
    {
        driveGain = juce::Decibels::decibelsToGain (db);
    }

    void setHardLimitEnabled (bool shouldLimit) noexcept { hardLimit = shouldLimit; }

    // Plafond de l'etage (brickwall + protection true-peak). -0.5 dBFS par
    // defaut, reglable jusqu'a -3 dBFS depuis la barre du bas (voir
    // idThdOutput). N'a d'effet que sur les branches LIMIT et Tape/Iron/Air.
    void setCeilingDb (float db) noexcept
    {
        ceilingLin = juce::Decibels::decibelsToGain (db);
    }

    void setMode (SatMode m) noexcept { mode = m; }

    void reset() noexcept
    {
        clipAmount = 0.0f;
        prevY = 0.0f;
        ironDcPrev = ironDcState = 0.0f;
        airHpState = airXPrev = 0.0f;
    }

    inline float processSample (float input) noexcept
    {
        const float x = input * driveGain;
        float z;

        if (mode != SatMode::Soft)
        {
            // Tape/Iron/Air/Clip : IRON et AIR sont ADDITIFS (ils ajoutent de
            // l'energie harmonique par-dessus le signal, contrairement au
            // tanh du Classic qui reste toujours sous l'unite) -- sans
            // plafond, ca peut depasser 0dBFS et clipper en aval. On fait
            // donc TOUJOURS passer le resultat par la meme protection
            // true-peak que le mode LIMIT ci-dessous, quel que soit l'etat
            // du bouton LIMIT (ces modes sont penses pour du mastering, un
            // etage de sortie sans garde-fou n'a pas de sens ici).
            z = applyTruePeakCeiling (processMastering (x)) / driveGain;
        }
        else if (hardLimit)
        {
            // --- Mode LIMIT : brickwall a ceilingLin (-0.5..-3 dBFS) + protection true-peak ---
            z = applyTruePeakCeiling (x) / driveGain;
        }
        else
        {
            // --- Mode SOFT CLIP : tanh pur, AUCUN plafond artificiel ---
            // Le signal peut depasser 0dBFS librement si on le pousse fort,
            // comme sur l'Arousor/Distressor de reference (verifie via
            // PluginDoctor : son softclip n'a pas de brickwall numerique).
            const float y = std::tanh (x);
            prevY = y; // continuite si on bascule sur LIMIT au sample suivant
            z = y / driveGain;
        }

        // Intensite visuelle du clip (pour les diodes de l'UI)
        const float diff = std::abs (input) - std::abs (z);
        const float target = juce::jlimit (0.0f, 1.0f, diff * 2.5f);
        clipAmount = target > clipAmount ? target : clipAmount * 0.995f;

        return z;
    }

    float getClipAmount() const noexcept { return clipAmount; }

private:
    // Brickwall a ceilingLin (-0.5dBFS par defaut) + protection "true clip" (pic inter-echantillon
    // estime par interpolation lineaire, meme principe que le Glue
    // Compressor d'Ableton) -- partagee par le mode LIMIT et par les 4
    // modes mastering (voir processSample()).
    inline float applyTruePeakCeiling (float x) noexcept
    {
        const float clippedX = juce::jlimit (-ceilingLin, ceilingLin, x);
        const float interSamplePeak = juce::jmax (std::abs (clippedX), std::abs ((clippedX + prevY) * 0.5f));

        float limited = clippedX;
        if (interSamplePeak > ceilingLin)
            limited = clippedX * (ceilingLin / interSamplePeak);

        prevY = limited;
        return limited;
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
                constexpr float dcR = 0.996f;
                const float y = shaped - ironDcPrev + dcR * ironDcState;
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
                constexpr float hpCoeff = 0.85f;
                airHpState = hpCoeff * (airHpState + x - airXPrev);
                airXPrev = x;
                const float harmonics = std::tanh (airHpState * 3.5f) - airHpState;
                return x + harmonics * 0.4f;
            }
            case SatMode::Soft:
            default:
                return x; // non atteint (Soft passe par l'autre branche de processSample())
        }
    }

    float driveGain  = 1.0f;
    float clipAmount = 0.0f;
    bool  hardLimit    = false;
    float prevY         = 0.0f;
    float ceilingLin     = juce::Decibels::decibelsToGain (-0.5f); // defaut -0.5 dBFS, voir setCeilingDb() / idThdOutput
    SatMode mode = SatMode::Soft;

    float ironDcPrev = 0.0f, ironDcState = 0.0f;
    float airHpState = 0.0f, airXPrev = 0.0f;
};
