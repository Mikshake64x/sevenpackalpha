#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

/**
    RackPushButton
    ---------------
    Style neumorphic "dark mono" (retour utilisateur : preview de 3
    familles -- extrusion/cadran, neumorphic, verre translucide -- "j'aime
    bien dark mono. on peut tester"). Le bouton est de la MEME teinte que
    la plaque qui l'entoure (pas de bordure/cadre visible) : au repos, un
    double halo doux (clair en haut-gauche, sombre en bas-droite) le fait
    paraitre souleve hors de la plaque ; allume (getToggleState() pour
    SOLO, setSelected() pour SEL), les halos s'inversent pour simuler un
    creux (comme presse dans la matiere) et le texte prend la couleur
    d'accent -- pas de fond colore plein, tout l'effet vient du relief.

    Utilise pour SOLO (toggle persistant, voir ButtonAttachment sur
    idSolo) et SEL (retour utilisateur explicite : "sel ne doit pas rester
    enfonce, c'est pour selectionner" -- pas un toggle, juste un clic
    momentane : `isDown` ne dure que pendant l'appui, voir setSelected()
    pour l'indicateur persistant separe).
*/
class RackPushButton : public juce::Button,
                        private juce::Timer
{
public:
    // externalEnabledSource : optionnel (meme idiome que ToggleSwitch) --
    // quand fourni, le bouton n'apparait "allume" (creux + texte colore)
    // QUE si *externalEnabledSource > 0.5f, meme si getToggleState()/
    // selected disent vrai. Utilise par ThdHeaderModule pour ses 4
    // boutons de mode (SOFT/TAPE/IRON/AIR reutilisent ce bouton, retour
    // utilisateur : "le A" -- variante neumorphic -- avec le radio group
    // JUCE natif de juce::Button, pas besoin d'une classe dediee) : le
    // mode selectionne ne doit pas paraitre allume si le THD lui-meme est
    // desengage.
    explicit RackPushButton (const juce::String& text, juce::Colour accentColour, bool pulseWhenLitIn = false,
                               std::atomic<float>* externalEnabledSource = nullptr)
        : juce::Button (text), accent (accentColour), pulseWhenLit (pulseWhenLitIn), externalEnabled (externalEnabledSource)
    {
        // Timer permanent mais bon marche (24Hz, repaint() saute des que
        // pas allume) -- seul SOLO (pulseWhenLit) en a besoin.
        if (pulseWhenLit)
            startTimerHz (24);
    }

    ~RackPushButton() override { stopTimer(); }

    // Espace REEL reserve autour du bouton visuel pour que le halo puisse
    // deborder SANS jamais sortir des bounds du composant (retour
    // utilisateur, reference visuelle "C -- extreme" : le halo doit
    // vraiment deborder hors du bouton) -- voir SevenpackLookAndFeel::
    // paintNeumorphicButton(). L'appelant (mise en page) DOIT agrandir
    // les bounds du composant de ce meme montant (voir BandMetersRack.h/
    // ThdHeaderModule.h/etc, meme idiome que ConsoleFader::
    // setEdgePadding()) -- sinon le bouton visuel serait juste plus petit
    // dans le meme espace, sans gagner de marge reelle.
    void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

    // Restreint la zone CLIQUABLE a la pilule visuelle (haloMargin exclu)
    // -- necessaire car les bounds du composant sont maintenant plus
    // grands que la pilule elle-meme (voir setHaloMargin()) : sans ca,
    // 2 boutons voisins dont les bounds elargis se chevauchent dans leur
    // marge pourraient se voler des clics l'un a l'autre.
    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
    }

    // Diode/allumage persistant distinct du "sunken" (retour utilisateur :
    // "tu peux mettre une diode dans le push sel pour savoir quelle bande
    // est selectionnee") -- SEL reste un bouton momentane (voir doc de
    // classe), donc cet etat "selectionne" est volontairement SEPARE de
    // isDown/getToggleState() : la bande active reste indiquee en
    // permanence par le creux + le texte colore, sans que le bouton ait
    // l'air presse en continu.
    void setSelected (bool shouldBeSelected) noexcept
    {
        if (selected != shouldBeSelected) { selected = shouldBeSelected; repaint(); }
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        const bool enabled = externalEnabled == nullptr || externalEnabled->load() > 0.5f;
        const bool lit = enabled && (getToggleState() || selected);

        // --- Pulse (SOLO uniquement, pendant qu'allume) : le texte/accent
        // respire doucement en continu, pour signaler "en cours" sans etre
        // agressif -- pas de changement de forme, juste l'alpha du texte. ---
        float textAlpha = 1.0f;
        if (lit && pulseWhenLit)
        {
            const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const float wave = (float) (0.5 + 0.5 * std::sin (t * juce::MathConstants<double>::twoPi * 0.7));
            textAlpha = 0.75f + wave * 0.25f;
        }

        SevenpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), lit, isDown,
                                                     accent, getButtonText().toUpperCase(), textAlpha, haloMargin);
    }

private:
    void timerCallback() override
    {
        const bool enabled = externalEnabled == nullptr || externalEnabled->load() > 0.5f;
        if (enabled && (getToggleState() || selected))
            repaint();
    }

    juce::Colour accent;
    bool selected = false;
    bool pulseWhenLit = false;
    std::atomic<float>* externalEnabled = nullptr;
    float haloMargin = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackPushButton)
};
