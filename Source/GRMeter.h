#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    GRMeter
    -------
    Rangee horizontale de petites LEDs degradees (cyan -> vert -> jaune -> orange -> rouge)
    indiquant la reduction de gain, avec une echelle en dB affichee en dessous.
    Attaque instantanee, retombee douce (ballistique type VU).

    Rangee principale (compression) : sens FIXE, identique quel que soit
    Comp/Expand (retour utilisateur : "quand on clique sur expand le GR se
    retourne dans l'autre sens ... il doit pas bouger le gain reduction de
    compression" -- un ancien mirroir en Expand pur a ete retire : Above
    (compression) tourne desormais EN PERMANENCE quel que soit le mode,
    voir FeedbackCompressor::AboveBelowMode, cette rangee n'a donc plus de
    raison de changer de sens).

    Rangee EXPAND dediee (demande explicite : "au lieu de mettre les
    chiffres, je prefererais une autre bande de diode qui affiche
    l'expander en temps reel, une diode = +0.2dB d'expansion") -- fine
    bande secondaire au-dessus des diodes principales, toujours visible,
    alimentee par un compteur SEPARE (grMeterExpandDb, voir
    PluginProcessor.h) qui ne recoit QUE la partie boost de l'enveloppe
    (jamais la reduction) -- independante du mode d'affichage de la
    rangee principale ci-dessus, donc lisible meme en BOTH (ou la rangee
    principale reste orientee "compression").
*/
class GRMeter : public juce::Component, private juce::Timer
{
public:
    GRMeter (std::atomic<float>& grValueDbToWatch, std::atomic<float>& expandGrValueDbToWatch,
              std::atomic<float>& powerParamToWatch, std::atomic<float>& aboveBelowParamToWatch);
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
    std::atomic<float>& expandGrValueDb;
    std::atomic<float>& powerParam;
    std::atomic<float>& aboveBelowParam;
    bool powered = true;
    bool isExpandMode = false; // true seulement en Expand PUR (1) -- Comp(0)/Both(2) gardent l'affichage normal
    float displayedGrDb = 0.0f;
    float displayedExpandGrDb = 0.0f;
    float maxRangeDb = 30.0f;

    // Rangee EXPAND dediee -- une diode = 0.1dB (demande explicite,
    // resserre depuis 0.2dB), MEME TAILLE/nombre de diodes que la rangee
    // compression ci-dessous (demande explicite : "les diodes de
    // l'expandeur doivent etre de la meme taille que celle du
    // compresseur") -- 36 diodes comme la rangee principale, voir paint().
    // kExpandLedDb * kNumExpandLeds = plage totale couverte avant
    // saturation (toutes les diodes allumees) -- au-dela, la rangee reste
    // pleine, elle ne "deborde" pas.
    static constexpr float kExpandLedDb = 0.1f;
    static constexpr int kNumExpandLeds = 36; // 36 * 0.1 = 3.6dB de plage

    // Optimisation CPU : on ne redessine que si l'affichage a reellement
    // change (valeur OU etat power/mode), au lieu de repaint() inconditionnel
    // a chaque tick -- evite de faire tourner le rendu (degrades, chemins)
    // pour rien quand le signal est silencieux/stable.
    float lastPaintedGrDb = -1.0f;
    float lastPaintedExpandGrDb = -1.0f;
    bool lastPaintedPowered = true;
    bool lastPaintedIsExpand = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRMeter)
};
