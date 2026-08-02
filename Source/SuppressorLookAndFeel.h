#pragma once
#include <JuceHeader.h>

namespace SuppressorColours
{
    // Chassis rack anthracite/brosse, esthetique "unite hardware montee en
    // baie" -- remplace l'ancien theme "labo cyberpunk" de Nuke Mk1.
    const juce::Colour bg          { 0xff121417 };
    const juce::Colour panel       { 0xff1a1d21 };
    const juce::Colour panelLight  { 0xff21252b };
    const juce::Colour gridLine    { 0xff0a0b0d };

    const juce::Colour ledOff      { 0xff20232a };
    const juce::Colour ledBlue     { 0xff2fb6ff };
    const juce::Colour ledAmber    { 0xffffb000 };
    const juce::Colour ledRed      { 0xffff3b30 };

    // Degrade de la rangee de GR LEDs (vert -> jaune -> ambre -> rouge),
    // comme sur le hardware de reference.
    const juce::Colour ledCyan     { 0xff00e5ff };
    const juce::Colour ledGreen    { 0xff39ff6a };
    const juce::Colour ledYellow   { 0xfff5e642 };
    const juce::Colour ledOrange   { 0xffff8c1a };

    const juce::Colour lcdBg       { 0xff0a1620 };
    const juce::Colour lcdText     { 0xff3fd0ff };

    const juce::Colour textDim     { 0xffbcc3cc };
    const juce::Colour textBright  { 0xffffffff };

    // (Palette "knob cream" retiree -- le knob est desormais un anneau
    // metal brosse, voir drawRotarySlider() dans le .cpp)
}

/**
    SuppressorLookAndFeel
    ----------------------
    Habillage "unite hardware rack" pour Suppressor (clone d'apparence d'un
    compresseur feedback classique de type 1176/Distressor) :
    - knobs ivoire/creme avec echelle imprimee 0-10 et grip crenele (au lieu
      de l'ancien capot blanc + anneau de LEDs de Nuke Mk1) ;
    - reste de la boite a outils graphique (panneaux verre/brosses, vis de
      coin, degrade des LEDs, dot glossy) conserve tel quel pour les
      composants reutilises (GRMeter, ToggleSwitch, AboutOverlay).
*/
class SuppressorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SuppressorLookAndFeel();

    // Etat POWER centralise : chaque composant (knobs, ToggleSwitch,
    // RatioSelector, DetectorFilterGraph...) l'interroge dans son paint()
    // et se rend lui-meme "eteint" (comme debranche), au lieu d'un grand
    // rectangle noir plaque par-dessus tout. Mis a jour par l'editeur.
    void setPowered (bool p) noexcept { powered = p; }
    bool isPowered() const noexcept { return powered; }

    // Knob "anneau metal" : chiffres 0-10 imprimes sur l'anneau brosse
    // qui tourne AVEC le knob (espacement regulier + subdivisions par
    // demi-unite + repere plus grand au 5), double bezel noir en creux,
    // capuchon brosse central avec reflet glossy mobile, et un repere
    // fixe imprime sur le panneau juste au-dessus pour lire la valeur.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    // Teinte OPTIONNELLE et tres legere de la bague noire du knob (voir
    // drawRotarySlider() dans le .cpp) -- utilisee UNIQUEMENT sur les
    // knobs Xover Low/High pour marquer visuellement qu'ils sont "entre 2
    // bandes". PAS un degrade spatial fixe : la teinte resultante est un
    // MELANGE de colourAtMin/colourAtMax dont le dosage suit la POSITION
    // reelle du knob dans sa plage (sliderPos, deja fourni a chaque appel
    // de drawRotarySlider -- pas besoin d'ecouter les changements de
    // valeur) -- proche du minimum = colourAtMin domine, proche du maximum
    // = colourAtMax domine. Design/taille des knobs par bande inchanges
    // (propriete absente = comportement d'origine).
    static void setKnobAccentGradient (juce::Slider& slider, juce::Colour colourAtMin, juce::Colour colourAtMax)
    {
        slider.getProperties().set ("knobAccentAtMin", colourAtMin.toString());
        slider.getProperties().set ("knobAccentAtMax", colourAtMax.toString());
    }

    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    static void renderAnthraciteBackground (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Plaque metal brossee unifiee (remplace le fond anthracite "labo" --
    // un seul plan, pas de plaques separees par zone).
    static void renderMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Fine ligne verticale claire pour separer 2 zones fonctionnelles --
    // remplace les contours de groupe/racetrack : juste des traits, pas
    // de boites, comme sur l'image de reference.
    static void renderDividerLine (juce::Graphics& g, float x, float top, float bottom);

    static void renderGlassPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

    // Plaque plastique blanc brillant (effet 3D) -- utilisee pour la
    // plaque THD Valve, en rupture avec le reste du chassis metal sombre.
    static void renderGlossyWhitePanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

    // Fin contour arrondi pour delimiter un groupe de fonctions (GAIN
    // REDUCTION, RATIO, DETECTOR/AUDIO...), sans plaque/bezel -- juste un
    // trait clair, comme sur l'image de reference. labelText optionnel,
    // affiche centre sur le bord superieur (style "racetrack").
    static void renderGroupOutline (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                                     const juce::String& labelText = {});
    static void renderGlossySheen (juce::Graphics& g, juce::Rectangle<float> bounds);
    static void renderBrushedMetal (juce::Graphics& g, juce::Rectangle<float> bounds);

    static void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius);
    static void renderCornerScrews (juce::Graphics& g, juce::Rectangle<float> bounds, float inset, float screwRadius);

    // Anneau "socle encastre" derriere un knob -- effet extrusion (le knob
    // parait visse dans un decroche du chassis plutot que pose a plat).
    static void renderKnobSocket (juce::Graphics& g, juce::Point<float> centre, float radius);

    static juce::Colour ledGradientColour (float t);

    // Degrade des diodes du RatioSelector : cyan (ratio le plus doux,
    // 2:1) -> vert -> jaune -> orange -> rouge (20:1), t = 0..1.
    static juce::Colour ratioLedColour (float t);
    static float meterDisplayRatio (float db, float minDb, float maxDb);
    static void drawGlossyDot (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, bool lit);

private:
    bool powered = true;
};
