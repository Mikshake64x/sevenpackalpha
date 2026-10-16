#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

/**
    ToggleSwitch
    ------------
    Bouton avec diode a gauche -- utilise pour OPTICAL/OVERKILL/SC (les
    seuls encore visibles, voir SevenpackEditorContent::buildBandPanel() --
    DETECTOR/AUDIO HP/BELL/TUBE/FUZZ restent caches, voir addChildComponent
    plutot qu'addAndMakeVisible la-bas).

    Style neumorphic "dark mono" unifie avec le reste du plugin (retour
    utilisateur, avec capture d'ecran : "il y a des cadres qui apparaissent
    maintenant sur les bandes derriere les knob et bouton. je veux pas ca"
    -- l'ancien style "plastique mou mat" avec bordure TOUJOURS visible
    tranchait avec SOLO/SEL/LEARN/etc, deja convertis) : coque neumorphic
    (SevenpackLookAndFeel::paintNeumorphicButton(), texte vide -- le libelle
    est dessine separement ci-dessous, a droite de la diode) + diode
    INCHANGEE (seul indicateur d'etat electrique -- s'eteint hors tension
    meme si l'interrupteur est mecaniquement enclenche).
*/
class ToggleSwitch : public juce::Button
{
public:
    // externalEnabledSource : optionnel, meme traitement que le Power global
    // (diode eteinte) mais pilote par un atomic<float> LOCAL -- utilise par
    // ThdValveModule pour ses 4 boutons de mode (idSoftClip).
    explicit ToggleSwitch (const juce::String& labelText, juce::Colour accentColour = SevenpackColours::ledRed,
                            std::atomic<float>* externalEnabledSource = nullptr)
        : juce::Button (labelText), text (labelText), accent (accentColour), externalEnabled (externalEnabledSource)
    {
        setClickingTogglesState (true);
    }

    // Change le libelle affiche apres construction (ex: "HP" -> "LP" sur
    // la bande HIGH, voir SevenpackAudioProcessorEditor::buildBandPanel()).
    void setText (const juce::String& newText) { text = newText; repaint(); }

    // Change la couleur de la diode apres construction (demande explicite :
    // "il faut une diode de chaque couleur pour comp, expand et both") --
    // utilise par le bouton Above/Below/Both pour recolorer sa diode selon
    // le mode courant, voir SevenpackEditorContent::buildBandPanel().
    void setAccentColour (juce::Colour newAccent) { accent = newAccent; repaint(); }

    // Voir RackPushButton::setHaloMargin() -- meme idiome.
    void setHaloMargin (float margin) noexcept { haloMargin = margin; repaint(); }

    // Boutons etroits des tranches Sevenpack (voir BandStrip.h) : diode
    // plus petite et plus pres du bord, texte moins espace -- sinon
    // "OVERKILL" ne tient pas dans ~60px.
    void setCompact (bool shouldBeCompact) noexcept { compact = shouldBeCompact; repaint(); }

    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().reduced (haloMargin).contains ((float) x, (float) y);
    }

    void paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool isDown) override
    {
        const auto* laf = dynamic_cast<SevenpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = ((laf == nullptr) || laf->isPowered())
                              && (externalEnabled == nullptr || externalEnabled->load() > 0.5f);
        // Bouton physique : sa position/son aspect ne changent pas avec le
        // courant. SEULE la diode est electrique -- elle s'eteint hors
        // tension, meme si l'interrupteur est mecaniquement enclenche.
        const bool on = getToggleState();
        const bool ledLit = powered && on;

        SevenpackLookAndFeel::paintNeumorphicButton (g, getLocalBounds().toFloat(), on, isDown, accent, juce::String(),
                                                     1.0f, haloMargin);

        // LED/texte positionnes DANS la pilule visuelle (haloMargin
        // exclu), pas dans les bounds elargis du composant.
        auto bounds = getLocalBounds().toFloat().reduced (haloMargin);
        // --- Diode : seul indicateur d'etat, pas de halo autour ---
        const float ledDiameter = juce::jmin (compact ? 6.0f : 8.0f, bounds.getHeight() * 0.34f);
        auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                              .withCentre ({ bounds.getX() + ledDiameter * (compact ? 1.4f : 1.6f), bounds.getCentreY() });
        g.setColour (ledLit ? accent : accent.interpolatedWith (juce::Colours::black, 0.85f));
        g.fillEllipse (ledBounds);

        const bool lightTheme = SevenpackLookAndFeel::isLightThemeStatic();
        g.setColour (on ? (lightTheme ? accent.darker (0.2f) : accent)
                        : (lightTheme ? juce::Colour (0xff1a1a1a) : juce::Colours::white).withAlpha (0.88f));
        // DOS : un bouton enclenche est rempli de sa couleur d'accent (voir
        // paintNeumorphicButton()) -- texte en noir, sinon invisible.
        if (SevenpackLookAndFeel::isDosThemeStatic() && on)
            g.setColour (juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (compact ? 0.02f : 0.1f));
        if (compact)
            g.drawFittedText (text.toUpperCase(), bounds.withTrimmedLeft (ledDiameter * 2.4f).withTrimmedRight (2.0f).toNearestInt(),
                               juce::Justification::centred, 1);
        else
            g.drawFittedText (text.toUpperCase(), bounds.withTrimmedLeft (ledDiameter * 2.8f).toNearestInt(),
                               juce::Justification::centredLeft, 1);
    }

private:
    juce::String text;
    juce::Colour accent;
    std::atomic<float>* externalEnabled = nullptr;
    float haloMargin = 5.0f;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleSwitch)
};
