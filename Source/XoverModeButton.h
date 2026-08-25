#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    XoverModeButton
    ---------------
    Selecteur ANALOG / LINEAR -- 2 pilules EMPILEES VERTICALEMENT, style
    radio exclusif : cliquer sur l'une eteint l'autre. Chacune est un VRAI
    sous-composant independant (Segment, juce::Button), pas juste des
    zones peintes dans un seul Component -- permet a chacune d'avoir son
    PROPRE tooltip.

    Pilote idDigitalCrossover (bool) : true = LINEAR, false = ANALOG.

    (ANALOG 2 -- 3e segment ajoute puis retire, voir doc de classe
    Crossover.h -- retour utilisateur explicite : "on va supprimer analog
    2". Le composant redevient un simple toggle 2 voies.)

    Porte depuis Nuke Mext (meme fichier, couleurs/LookAndFeel adaptes a
    SixpackColours / SixpackLookAndFeel).

    Lit directement le parametre APVTS via un petit Timer interne, pour
    rester synchronise meme si la valeur change par un autre biais que le
    clic ici (automation host, etc). Le clic notifie l'editeur via
    onSelect pour qu'il pousse la nouvelle valeur au parametre.
*/
class XoverModeButton : public juce::Component, private juce::Timer
{
public:
    enum class Mode { Analog = 0, Linear = 1 };

    explicit XoverModeButton (std::atomic<float>& digitalCrossoverParamToWatch)
        : digitalCrossoverParam (digitalCrossoverParamToWatch)
    {
        analogSegment.setButtonText ("ANALOG");
        linearSegment.setButtonText ("LINEAR");
        analogSegment.setName ("Analog Crossover");
        linearSegment.setName ("Linear Crossover");
        analogSegment.accent = SixpackColours::ledBlue;
        linearSegment.accent = SixpackColours::ledRed;

        analogSegment.onClick = [this] { if (onSelect) onSelect (Mode::Analog); };
        linearSegment.onClick = [this] { if (onSelect) onSelect (Mode::Linear); };

        addAndMakeVisible (analogSegment);
        addAndMakeVisible (linearSegment);

        activeMode = readMode();
        updateSegmentStates();
        startTimerHz (15);
    }

    ~XoverModeButton() override { stopTimer(); }

    std::function<void (Mode)> onSelect;

    void setAnalogTooltip (const juce::String& text) { analogSegment.setTooltip (text); }
    void setLinearTooltip (const juce::String& text) { linearSegment.setTooltip (text); }

    // Marge de halo partagee (retour utilisateur : reference visuelle
    // "C -- extreme") -- meme idiome que ConsoleFader::edgePadding :
    // l'appelant EXTERIEUR (PluginEditor.cpp) doit agrandir les bounds
    // de CE composant (XoverModeButton) de ce meme montant, sinon les
    // segments internes n'ont nulle part ou deborder sans se faire
    // clipper par CE conteneur -- resized() ci-dessous retranche cette
    // meme marge avant de calculer sa mise en page interne, pour que la
    // position/taille NOMINALE des 2 pilules reste inchangee.
    static constexpr float segmentMargin = 5.0f;

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat().reduced (segmentMargin);
        constexpr float gapPx = 8.0f;
        const float segH = (bounds.getHeight() - gapPx) / 2.0f;

        analogSegment.setBounds (bounds.removeFromTop (segH).expanded (segmentMargin).toNearestInt());
        bounds.removeFromTop (gapPx);
        linearSegment.setBounds (bounds.expanded (segmentMargin).toNearestInt());
    }

private:
    // Style neumorphic "dark mono" unifie avec le reste du header
    // (retour utilisateur : "harmonisation des ecritures, taille" --
    // l'ancienne pilule pleine/bordee, en 8.5pt, tranchait nettement avec
    // le style/police (14-15pt bold, meme formule que paintNeumorphicButton)
    // deja partage par LEARN/LINK IN-OUT/SOFT-TAPE-IRON-AIR/SEL-SOLO)
    // -- meme rendu que RackPushButton, reutilise ici directement plutot
    // que de dupliquer la logique.
    struct Segment : public juce::Button
    {
        Segment() : juce::Button ("XoverSegment") {}

        bool hitTest (int x, int y) override
        {
            return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
        }

        void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
        {
            SixpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), getToggleState(), isDown,
                                                         accent, getButtonText().toUpperCase(), 1.0f, haloMargin);
        }

        juce::Colour accent;
        float haloMargin = segmentMargin;
    };

    Mode readMode() const noexcept
    {
        return digitalCrossoverParam.load() > 0.5f ? Mode::Linear : Mode::Analog;
    }

    void timerCallback() override
    {
        const auto live = readMode();
        if (live != activeMode)
        {
            activeMode = live;
            updateSegmentStates();
        }
    }

    void updateSegmentStates()
    {
        analogSegment.setToggleState (activeMode == Mode::Analog, juce::dontSendNotification);
        linearSegment.setToggleState (activeMode == Mode::Linear, juce::dontSendNotification);
        analogSegment.repaint();
        linearSegment.repaint();
    }

    std::atomic<float>& digitalCrossoverParam;
    Mode activeMode = Mode::Analog;

    Segment analogSegment, linearSegment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoverModeButton)
};
