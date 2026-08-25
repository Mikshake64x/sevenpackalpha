#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    RatioSelector
    -------------
    UN BOUTON PAR RATIO (2:1 / 3:1 / 4:1 / 6:1 / 8:1 / 10:1 / 20:1),
    directement cliquable -- pas de defilement. Un seul actif a la fois
    (radio group JUCE natif). (1:1 retire -- ratio neutre inutile.)

    Style neumorphic "dark mono" unifie avec le reste du plugin (retour
    utilisateur, avec capture d'ecran : "il y a des cadres qui apparaissent
    maintenant sur les bandes derriere les knob et bouton. je veux pas ca"
    -- l'ancien style "plastique carre en relief" (fond mat + bordure
    TOUJOURS visible, voir git blame) tranchait avec SOLO/SEL/LEARN/etc,
    deja convertis) : coque neumorphic (SixpackLookAndFeel::
    paintNeumorphicButton(), texte vide -- le libelle est dessine
    separement ci-dessous, sous la diode) + diode de couleur (degrade
    cyan->rouge, ratios doux->durs) au-dessus, INCHANGEE.

    Un seul actif a la fois (radio group JUCE natif). 10:1 est desormais
    un ratio comme les autres -- le comportement "Opto" (release plus
    doux/program-dependant) est pilote par le bouton OPTICAL global (voir
    PluginEditor), applicable a n'importe quel ratio.
*/
class RatioSelector : public juce::Component
{
public:
    static constexpr int numRatios = 7;

    struct RatioButton : public juce::Button
    {
        RatioButton (const juce::String& labelText, juce::Colour ledColourIn)
            : juce::Button (labelText), text (labelText), ledColour (ledColourIn)
        {
            setClickingTogglesState (true);
        }

        // Voir RackPushButton::setHaloMargin() -- meme idiome. Ces
        // boutons sont petits (7 dans une rangee etroite) donc une marge
        // plus modeste que les gros boutons (SOLO/SEL/LEARN).
        void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

        bool hitTest (int x, int y) override
        {
            return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
        }

        void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
        {
            const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
            const bool powered = (laf == nullptr) || laf->isPowered();
            // Bouton physique : l'aspect (enfonce/sureleve) suit l'etat
            // mecanique, pas le courant. Seule la diode s'eteint.
            const bool on = getToggleState();
            const bool ledLit = powered && on;

            SixpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), on, isDown, ledColour, juce::String(),
                                                         1.0f, haloMargin);

            auto bounds = getLocalBounds().toFloat().reduced (haloMargin);
            const float ledDiameter = juce::jmin (7.0f, bounds.getWidth() * 0.22f);
            auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                                  .withCentre ({ bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.28f });
            g.setColour (ledLit ? ledColour : ledColour.interpolatedWith (juce::Colours::black, 0.85f));
            g.fillEllipse (ledBounds);

            // Retour utilisateur : "quand on clique sur les bouton de
            // ratios, les 2/1, 4/1 et tout reste en noir ou blanc" -- le
            // chiffre du ratio SELECTIONNE doit prendre la couleur
            // d'accent (degrade cyan->rouge), pas juste la diode au-dessus.
            // ledColour.darker(0.2f) en theme clair rendait ca trop pres du
            // noir sur certains ratios pour se distinguer clairement.
            const bool lightTheme = SixpackLookAndFeel::isLightThemeStatic();
            g.setColour (on ? ledColour
                            : (lightTheme ? juce::Colour (0xff1a1a1a) : juce::Colours::white).withAlpha (0.88f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, bounds.getWidth() * 0.34f), juce::Font::bold)));
            g.drawFittedText (text, bounds.withTrimmedTop (bounds.getHeight() * 0.5f).toNearestInt(),
                               juce::Justification::centred, 1);
        }

        juce::String text;
        juce::Colour ledColour;
        float haloMargin = 4.0f;
    };

    RatioSelector()
    {
        static const char* names[numRatios] = { "2:1", "3:1", "4:1", "6:1", "8:1", "10:1", "20:1" };

        for (int i = 0; i < numRatios; ++i)
        {
            // Degrade cyan (2:1, le plus doux) -> rouge (20:1, le plus dur).
            const juce::Colour ledColour = SixpackLookAndFeel::ratioLedColour ((float) i / (float) (numRatios - 1));
            buttons[i] = std::make_unique<RatioButton> (names[i], ledColour);
            buttons[i]->setRadioGroupId (1001, juce::dontSendNotification);
            addAndMakeVisible (*buttons[i]);

            buttons[i]->onClick = [this, i]
            {
                if (buttons[i]->getToggleState())
                {
                    selected = i;
                    if (onSelect)
                        onSelect (selected);
                    repaint();
                }
            };
        }

        buttons[selected]->setToggleState (true, juce::dontSendNotification);
    }

    std::function<void (int index)> onSelect;

    void setSelectedIndex (int index)
    {
        index = juce::jlimit (0, numRatios - 1, index);
        if (index != selected || ! buttons[index]->getToggleState())
        {
            selected = index;
            buttons[index]->setToggleState (true, juce::dontSendNotification);
            repaint();
        }
    }

    int getSelectedIndex() const noexcept { return selected; }

    // Marge de halo partagee (meme idiome que XoverModeButton::
    // segmentMargin/ConsoleFader::edgePadding) -- l'appelant EXTERIEUR
    // (PluginEditor.cpp) doit agrandir les bounds de CE composant d'autant.
    static constexpr float buttonMargin = 4.0f;

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat().reduced (buttonMargin);
        // Gap agrandi (4 -> 9, retour utilisateur : reference visuelle
        // "C -- extreme") : reserve un peu d'air entre les 7 boutons pour
        // que le halo (voir RatioButton::haloMargin) reste lisible comme
        // un vrai relief individuel plutot que de fusionner visuellement
        // avec le voisin.
        constexpr float gap = 9.0f;
        const float w = (bounds.getWidth() - gap * (float) (numRatios - 1)) / (float) numRatios;
        for (int i = 0; i < numRatios; ++i)
        {
            juce::Rectangle<float> nominal (bounds.getX() + (float) i * (w + gap), bounds.getY(), w, bounds.getHeight());
            buttons[i]->setBounds (nominal.expanded (buttonMargin).toNearestInt());
        }
    }

    void paint (juce::Graphics&) override {}

private:
    int selected = 3; // 6:1 par defaut
    std::unique_ptr<RatioButton> buttons[numRatios];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RatioSelector)
};
