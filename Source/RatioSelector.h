#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

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
    deja convertis) : coque neumorphic (SevenpackLookAndFeel::
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
            const auto* laf = dynamic_cast<SevenpackLookAndFeel*> (&getLookAndFeel());
            const bool powered = (laf == nullptr) || laf->isPowered();
            // Bouton physique : l'aspect (enfonce/sureleve) suit l'etat
            // mecanique, pas le courant. Seule la diode s'eteint.
            const bool on = getToggleState();
            const bool ledLit = powered && on;

            SevenpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), on, isDown, ledColour, juce::String(),
                                                         1.0f, haloMargin);

            // Mode compact (tranches Sevenpack, voir RatioSelector::
            // setCompact()) : chiffre seul ("4" pour 4:1), pas de diode --
            // le chiffre du ratio actif prend la couleur d'accent (et
            // s'eteint hors tension, comme la diode).
            if (compact)
            {
                auto b = getLocalBounds().toFloat().reduced (haloMargin);
                const bool lightTheme = SevenpackLookAndFeel::isLightThemeStatic();
                const auto litColour = lightTheme ? ledColour.darker (0.45f) : ledColour;
                if (SevenpackLookAndFeel::isDosThemeStatic() && on)
                    g.setColour (juce::Colours::black); // DOS : bouton actif rempli de la couleur d'accent
                else
                    g.setColour (ledLit ? litColour
                                        : (lightTheme ? juce::Colour (0xff1a1a1a) : juce::Colours::white).withAlpha (on ? 0.5f : 0.8f));
                g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, b.getHeight() * 0.56f), juce::Font::bold)));
                g.drawFittedText (text.upToFirstOccurrenceOf (":", false, false), b.toNearestInt(), juce::Justification::centred, 1);
                return;
            }

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
            const bool lightTheme = SevenpackLookAndFeel::isLightThemeStatic();
            g.setColour (on ? ledColour
                            : (lightTheme ? juce::Colour (0xff1a1a1a) : juce::Colours::white).withAlpha (0.88f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, bounds.getWidth() * 0.34f), juce::Font::bold)));
            g.drawFittedText (text, bounds.withTrimmedTop (bounds.getHeight() * 0.5f).toNearestInt(),
                               juce::Justification::centred, 1);
        }

        juce::String text;
        juce::Colour ledColour;
        float haloMargin = 4.0f;
        bool compact = false;
    };

    RatioSelector()
    {
        static const char* names[numRatios] = { "2:1", "3:1", "4:1", "6:1", "8:1", "10:1", "20:1" };

        for (int i = 0; i < numRatios; ++i)
        {
            // Degrade cyan (2:1, le plus doux) -> rouge (20:1, le plus dur).
            const juce::Colour ledColour = SevenpackLookAndFeel::ratioLedColour ((float) i / (float) (numRatios - 1));
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
    // Marge de halo du mode compact -- meme contrat que buttonMargin
    // ci-dessus (l'appelant agrandit les bounds d'autant).
    static constexpr float compactButtonMargin = 1.5f;

    // Rangee etroite des tranches Sevenpack (~170px pour 7 boutons, voir
    // BandStrip.h) : chiffre seul, pas de diode, halo et gap reduits.
    void setCompact (bool shouldBeCompact)
    {
        compact = shouldBeCompact;
        for (auto& b : buttons)
        {
            b->compact = shouldBeCompact;
            b->setHaloMargin (shouldBeCompact ? compactButtonMargin : 4.0f);
        }
        resized();
    }

    void resized() override
    {
        const float margin = compact ? compactButtonMargin : buttonMargin;
        auto bounds = getLocalBounds().toFloat().reduced (margin);
        // Gap agrandi (4 -> 9, retour utilisateur : reference visuelle
        // "C -- extreme") : reserve un peu d'air entre les 7 boutons pour
        // que le halo (voir RatioButton::haloMargin) reste lisible comme
        // un vrai relief individuel plutot que de fusionner visuellement
        // avec le voisin. 3px seulement en mode compact (manque de place).
        const float gap = compact ? 3.0f : 9.0f;
        const float w = (bounds.getWidth() - gap * (float) (numRatios - 1)) / (float) numRatios;
        for (int i = 0; i < numRatios; ++i)
        {
            juce::Rectangle<float> nominal (bounds.getX() + (float) i * (w + gap), bounds.getY(), w, bounds.getHeight());
            buttons[i]->setBounds (nominal.expanded (margin).toNearestInt());
        }
    }

    void paint (juce::Graphics&) override {}

private:
    bool compact = false;
    int selected = 3; // 6:1 par defaut
    std::unique_ptr<RatioButton> buttons[numRatios];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RatioSelector)
};
