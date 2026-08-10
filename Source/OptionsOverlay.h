#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    OptionsOverlay
    --------------
    Voile plein ecran affiche au clic sur l'engrenage (latencyModeButton).
    Meme langage visuel que AboutOverlay (voile noir + panneau vitre
    encadre ambre). Deux parties :
      - Haut FIXE (non-scrollable) : taille de fenetre, budget CPU du mode
        Linear -- les 2 reglages "coeur", toujours courts.
      - Section "AI" SCROLLABLE (juce::Viewport, meme pattern que
        AboutOverlay) : tout ce qui regle le comportement du bouton AI
        (barre de controle). Cette liste grandit au fil des demandes --
        passer par un Viewport des maintenant evite de redevoir rejouer un
        Tetris de hauteurs a chaque nouvelle option.
          - AI MASTERING TARGET : cible de loudness (LUFS) par plateforme,
            radio ("OFF" = comportement d'origine) -- "SIDE KEY" (dernier
            choix, -6 LUFS) est le repere le plus agressif de la liste.
          - AI COMPRESSION MODE : intensite generale (Off/Soft/Medium/Hard).
          - AI ATTACK/RELEASE CHARACTER : forme (Off/Smooth/Medium/Punchy).
          - AI GAIN REDUCTION : combien de compression au total
            (Off/Smooth/Mid/Hard) -- axe supplementaire pour calmer/pousser
            l'ensemble Ratio+Input d'un cran, independamment des 2 axes
            ci-dessus (demande explicite suite a "ca compresse beaucoup").
            Chacun de ces 3 axes a son propre "Off" (demande explicite :
            "on doit pouvoir agir que sur les points AI qui ne vont pas") --
            Off n'ecrit QUE sa propre cible (Input/Ratio/Attack+Release),
            le reste continue d'etre pousse normalement.
          - AI SCOPE : 3 interrupteurs INDEPENDANTS (Crossover/Detecteurs/
            Compression).
    Se ferme au clic hors du panneau, ou via le bouton CLOSE.
*/
class OptionsOverlay : public juce::Component
{
public:
    struct SectionItem
    {
        enum class Kind { Header, RadioRow, ToggleRow };
        juce::String label;
        Kind kind = Kind::Header;
        int numButtons = 0;
        int firstButtonIndex = 0; // index dans le tableau plat de boutons de la section
    };

    OptionsOverlay()
    {
        static const char* scaleLabels[kNumScales]     = { "75%", "80%", "85%", "90%", "95%", "100%" };
        static const char* latencyLabels[kNumLatency]  = { "LOW CPU", "MEDIUM CPU", "HIGH CPU" };

        for (int i = 0; i < kNumScales; ++i)
        {
            addAndMakeVisible (scaleButtons[i]);
            scaleButtons[i].setButtonText (scaleLabels[i]);
            scaleButtons[i].setClickingTogglesState (true);
            scaleButtons[i].setRadioGroupId (9101, juce::dontSendNotification);
            scaleButtons[i].onClick = [this, i]
            {
                if (scaleButtons[i].getToggleState() && onScaleSelected)
                    onScaleSelected (kScaleValues[i]);
            };
            scaleButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
            scaleButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledAmber);
            scaleButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
            scaleButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }

        for (int i = 0; i < kNumLatency; ++i)
        {
            addAndMakeVisible (latencyButtons[i]);
            latencyButtons[i].setButtonText (latencyLabels[i]);
            latencyButtons[i].setClickingTogglesState (true);
            latencyButtons[i].setRadioGroupId (9102, juce::dontSendNotification);
            latencyButtons[i].onClick = [this, i]
            {
                if (latencyButtons[i].getToggleState() && onLatencySelected)
                    onLatencySelected (i);
            };
            latencyButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
            latencyButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledAmber);
            latencyButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
            latencyButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }

        addAndMakeVisible (licenseButton);
        licenseButton.setButtonText ("LICENSE MANAGER");
        licenseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
        licenseButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::ledAmber);
        licenseButton.onClick = [this] { if (onOpenLicense) onOpenLicense(); };

        addAndMakeVisible (aiViewport);
        aiViewport.setScrollBarsShown (true, false);
        aiViewport.setViewedComponent (&aiPanel, false);
        aiPanel.build();

        addAndMakeVisible (closeButton);
        closeButton.setButtonText ("CLOSE");
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
        closeButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::textBright);
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };

        // Croix de fermeture en haut a droite du panneau (demande explicite,
        // en plus du bouton CLOSE en bas) -- meme action que CLOSE.
        addAndMakeVisible (closeCrossButton);
        closeCrossButton.setButtonText ("X");
        closeCrossButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeCrossButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
        closeCrossButton.onClick = [this] { if (onDismiss) onDismiss(); };
    }

    void setCurrentScaleIndex (int idx)
    {
        for (int i = 0; i < kNumScales; ++i)
            scaleButtons[i].setToggleState (i == idx, juce::dontSendNotification);
    }

    void setCurrentLatencyIndex (int idx)
    {
        for (int i = 0; i < kNumLatency; ++i)
            latencyButtons[i].setToggleState (i == idx, juce::dontSendNotification);
    }

    void setCurrentAiTargetIndex (int idx)          { aiPanel.setCurrentAiTargetIndex (idx); }
    void setCurrentAiCompressionMode (int idx)       { aiPanel.setCurrentAiCompressionMode (idx); }
    void setCurrentAiCharacter (int idx)             { aiPanel.setCurrentAiCharacter (idx); }
    void setCurrentAiGainReduction (int idx)         { aiPanel.setCurrentAiGainReduction (idx); }
    void setAiScopeStates (bool crossoverOn, bool detectorsOn, bool compressionOn)
    {
        aiPanel.setAiScopeStates (crossoverOn, detectorsOn, compressionOn);
    }
    void setCurrentAiStyle (int idx)      { aiPanel.setCurrentAiStyle (idx); }
    void setAiFidelityState (bool enabled) { aiPanel.setAiFidelityState (enabled); }
    void setCurrentAiExpandMode (int idx)      { aiPanel.setCurrentAiExpandMode (idx); }
    void setCurrentAiExpandCharacter (int idx) { aiPanel.setCurrentAiExpandCharacter (idx); }
    void setCurrentAiExpandAmount (int idx)    { aiPanel.setCurrentAiExpandAmount (idx); }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        panelBounds = bounds.withSizeKeepingCentre (620.0f, juce::jmin (720.0f, bounds.getHeight() - 40.0f));
        auto content = panelBounds.reduced (26.0f, 20.0f);

        content.removeFromTop (38.0f);          // titre
        content.removeFromTop (14.0f);           // ligne + marge

        const float gap = 8.0f;

        // En-tete "GENERAL" -- separe visuellement les reglages de base
        // (taille de fenetre, budget CPU) de la section AI plus bas,
        // demande explicite : "il faut bien separer les reglages dans les
        // options de base et les reglages de l'ia".
        generalHeaderBounds = content.removeFromTop (22.0f).toNearestInt();
        content.removeFromTop (6.0f);

        scaleLabelBounds = content.removeFromTop (18.0f).toNearestInt();
        auto scaleRow = content.removeFromTop (36.0f);
        const float bw = (scaleRow.getWidth() - gap * (kNumScales - 1)) / (float) kNumScales;
        for (int i = 0; i < kNumScales; ++i)
        {
            scaleButtons[i].setBounds (scaleRow.removeFromLeft ((int) bw).toNearestInt());
            scaleRow.removeFromLeft ((int) gap);
        }

        content.removeFromTop (18.0f);           // ligne + marge
        latencyLabelBounds = content.removeFromTop (18.0f).toNearestInt();
        auto latRow = content.removeFromTop (36.0f);
        const float bw2 = (latRow.getWidth() - gap * (kNumLatency - 1)) / (float) kNumLatency;
        for (int i = 0; i < kNumLatency; ++i)
        {
            latencyButtons[i].setBounds (latRow.removeFromLeft ((int) bw2).toNearestInt());
            latRow.removeFromLeft ((int) gap);
        }

        content.removeFromTop (14.0f);           // ligne + marge
        licenseButton.setBounds (content.removeFromTop (34.0f).toNearestInt());

        aiDividerY = content.getY() + 14.0f;
        content.removeFromTop (24.0f);           // ligne + marge

        // En-tete "AI" -- meme logique que GENERAL ci-dessus, mais pour
        // toute la section scrollable (portee, cibles, style, fidelity...).
        aiHeaderBounds = content.removeFromTop (22.0f).toNearestInt();
        content.removeFromTop (4.0f);

        closeButton.setBounds (juce::Rectangle<int> (110, 30)
            .withCentre ({ (int) panelBounds.getCentreX(), (int) panelBounds.getBottom() - 28 }));
        content.removeFromBottom (10.0f); // marge avant le bouton CLOSE

        aiViewport.setBounds (content.toNearestInt());
        const int scrollBarW = aiViewport.getScrollBarThickness();
        aiPanel.layout (juce::jmax (10, (int) content.getWidth() - scrollBarW));

        closeCrossButton.setBounds (juce::Rectangle<int> (28, 28)
            .withCentre ({ (int) panelBounds.getRight() - 22, (int) panelBounds.getY() + 22 }));
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRect (bounds);

        SuppressorLookAndFeel::renderGlassPanel (g, panelBounds, 12.0f);
        SuppressorLookAndFeel::renderGlossySheen (g, panelBounds);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.5f));
        g.drawRoundedRectangle (panelBounds.reduced (0.5f), 12.0f, 1.0f);

        auto content = panelBounds.reduced (26.0f, 20.0f);
        auto titleArea = content.removeFromTop (38.0f);
        g.setColour (SuppressorColours::ledAmber);
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("OPTIONS", titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), content.getY(), content.getRight(), content.getY(), 1.0f);

        auto drawSectionLabel = [&] (const juce::Rectangle<int>& area, const juce::String& text)
        {
            g.setColour (SuppressorColours::ledBlue);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
            g.drawFittedText (text, area, juce::Justification::centredLeft, 1);
        };

        // En-tetes de GROUPE (GENERAL / AI) -- plus grands et en ambre, pour
        // se distinguer clairement des labels de sous-section (bleus,
        // drawSectionLabel ci-dessus) : demande explicite de bien separer
        // les reglages de base des reglages IA.
        auto drawGroupHeader = [&] (const juce::Rectangle<int>& area, const juce::String& text)
        {
            g.setColour (SuppressorColours::ledAmber);
            g.setFont (juce::Font (juce::FontOptions (14.5f, juce::Font::bold)).withExtraKerningFactor (0.16f));
            g.drawFittedText (text, area, juce::Justification::centredLeft, 1);
        };

        drawGroupHeader (generalHeaderBounds, "GENERAL");
        drawSectionLabel (scaleLabelBounds, "WINDOW SIZE");
        drawSectionLabel (latencyLabelBounds, "LINEAR MODE CPU BUDGET");

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), latencyLabelBounds.getY() - 18.0f, content.getRight(), latencyLabelBounds.getY() - 18.0f, 1.0f);
        g.drawLine (content.getX(), aiDividerY, content.getRight(), aiDividerY, 1.0f);
        drawGroupHeader (aiHeaderBounds, "AI");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds.contains (e.position) && onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;
    std::function<void()> onOpenLicense;
    std::function<void(float)> onScaleSelected;
    std::function<void(int)> onLatencySelected;
    std::function<void(int)> onAiTargetSelected;
    std::function<void(int)> onAiCompressionModeSelected;
    std::function<void(int)> onAiCharacterSelected;
    std::function<void(int)> onAiGainReductionSelected;
    // scopeIndex : 0=Crossover, 1=Detectors, 2=Compression.
    std::function<void(int scopeIndex, bool enabled)> onAiScopeChanged;
    std::function<void(int)> onAiStyleSelected;
    std::function<void(bool)> onAiFidelityChanged;
    std::function<void(int)> onAiExpandModeSelected;
    std::function<void(int)> onAiExpandCharacterSelected;
    std::function<void(int)> onAiExpandAmountSelected;

private:
    // Contenu SCROLLABLE de la section AI -- meme idiome que AboutOverlay::
    // ParagraphColumn (calcule sa propre hauteur via layout(width), le
    // Viewport parent s'en sert pour savoir jusqu'ou scroller).
    class AiPanel : public juce::Component
    {
    public:
        explicit AiPanel (OptionsOverlay& ownerIn) : owner (ownerIn) {}

        void build()
        {
            static const char* aiTargetLabels[kNumAiTargets] =
                { "OFF", "APPLE MUSIC", "YOUTUBE", "SPOTIFY", "DEEZER", "SOUNDCLOUD", "TV", "CD", "SIDE KEY", "MATCH" };
            static const char* compressionLabels[kNumCompressionModes] = { "OFF", "SOFT", "MEDIUM", "HARD" };
            static const char* characterLabels[kNumAttackReleaseCharacters] = { "OFF", "SMOOTH", "MEDIUM", "PUNCHY" };
            static const char* grLabels[kNumGainReductionModes] = { "OFF", "SMOOTH", "MID", "HARD" };
            static const char* scopeLabels[kNumScopes] = { "CROSSOVER", "DETECTORS", "COMPRESSION" };
            static const char* styleLabels[kNumStyles] = { "OFF", "BASS", "LOUD", "ROCK", "POP", "ELECTRONIC" };
            static const char* expandModeLabels[kNumExpandModes] = { "OFF", "SOFT", "MEDIUM", "HARD" };
            static const char* expandCharacterLabels[kNumExpandCharacters] = { "OFF", "SMOOTH", "MEDIUM", "PUNCHY" };
            static const char* expandAmountLabels[kNumExpandAmountModes] = { "OFF", "SMOOTH", "MID", "HARD" };

            for (int i = 0; i < kNumAiTargets; ++i)
            {
                addAndMakeVisible (aiTargetButtons[i]);
                aiTargetButtons[i].setButtonText (aiTargetLabels[i]);
                aiTargetButtons[i].setClickingTogglesState (true);
                aiTargetButtons[i].setRadioGroupId (9103, juce::dontSendNotification);
                aiTargetButtons[i].onClick = [this, i]
                {
                    if (aiTargetButtons[i].getToggleState() && owner.onAiTargetSelected)
                        owner.onAiTargetSelected (i);
                };
                aiTargetButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                aiTargetButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledCyan);
                aiTargetButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                aiTargetButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            for (int i = 0; i < kNumCompressionModes; ++i)
            {
                addAndMakeVisible (compressionModeButtons[i]);
                compressionModeButtons[i].setButtonText (compressionLabels[i]);
                compressionModeButtons[i].setClickingTogglesState (true);
                compressionModeButtons[i].setRadioGroupId (9104, juce::dontSendNotification);
                compressionModeButtons[i].onClick = [this, i]
                {
                    if (compressionModeButtons[i].getToggleState() && owner.onAiCompressionModeSelected)
                        owner.onAiCompressionModeSelected (i);
                };
                compressionModeButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                compressionModeButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledCyan);
                compressionModeButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                compressionModeButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            for (int i = 0; i < kNumAttackReleaseCharacters; ++i)
            {
                addAndMakeVisible (characterButtons[i]);
                characterButtons[i].setButtonText (characterLabels[i]);
                characterButtons[i].setClickingTogglesState (true);
                characterButtons[i].setRadioGroupId (9105, juce::dontSendNotification);
                characterButtons[i].onClick = [this, i]
                {
                    if (characterButtons[i].getToggleState() && owner.onAiCharacterSelected)
                        owner.onAiCharacterSelected (i);
                };
                characterButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                characterButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledCyan);
                characterButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                characterButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            // AI GAIN REDUCTION -- demande explicite (suite a "ca compresse
            // beaucoup") : combien de gain reduction au total, Smooth/Mid/
            // Hard, axe SUPPLEMENTAIRE independant du mode de compression
            // et du caractere attack/release ci-dessus.
            for (int i = 0; i < kNumGainReductionModes; ++i)
            {
                addAndMakeVisible (gainReductionButtons[i]);
                gainReductionButtons[i].setButtonText (grLabels[i]);
                gainReductionButtons[i].setClickingTogglesState (true);
                gainReductionButtons[i].setRadioGroupId (9106, juce::dontSendNotification);
                gainReductionButtons[i].onClick = [this, i]
                {
                    if (gainReductionButtons[i].getToggleState() && owner.onAiGainReductionSelected)
                        owner.onAiGainReductionSelected (i);
                };
                gainReductionButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                gainReductionButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledCyan);
                gainReductionButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                gainReductionButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            for (int i = 0; i < kNumScopes; ++i)
            {
                addAndMakeVisible (scopeButtons[i]);
                scopeButtons[i].setButtonText (scopeLabels[i]);
                scopeButtons[i].setClickingTogglesState (true);
                scopeButtons[i].onClick = [this, i]
                {
                    if (owner.onAiScopeChanged)
                        owner.onAiScopeChanged (i, scopeButtons[i].getToggleState());
                };
                scopeButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                scopeButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledGreen);
                scopeButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                scopeButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            // AI STYLE -- demande explicite : "plusieurs styles musicaux
            // qui vont changer la balance des outputs en fonction du
            // style" -- radio, "OFF" = aucun offset (comportement d'origine).
            for (int i = 0; i < kNumStyles; ++i)
            {
                addAndMakeVisible (styleButtons[i]);
                styleButtons[i].setButtonText (styleLabels[i]);
                styleButtons[i].setClickingTogglesState (true);
                styleButtons[i].setRadioGroupId (9107, juce::dontSendNotification);
                styleButtons[i].onClick = [this, i]
                {
                    if (styleButtons[i].getToggleState() && owner.onAiStyleSelected)
                        owner.onAiStyleSelected (i);
                };
                styleButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                styleButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledCyan);
                styleButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                styleButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            // AI FIDELITY -- demande explicite : "une fonction qui reprend
            // la balance bass mid aigue du mix pour la remettre pareil
            // apres compression" -- interrupteur independant, meme style
            // que les boutons AI SCOPE ci-dessus.
            addAndMakeVisible (fidelityButton);
            fidelityButton.setButtonText ("FIDELITY");
            fidelityButton.setClickingTogglesState (true);
            fidelityButton.onClick = [this]
            {
                if (owner.onAiFidelityChanged)
                    owner.onAiFidelityChanged (fidelityButton.getToggleState());
            };
            fidelityButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
            fidelityButton.setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledGreen);
            fidelityButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
            fidelityButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

            // Triade AI EXPAND (demande explicite : "pour l'ia de expand,
            // il faut des reglages comme pour l'ia comp") -- meme
            // mecanique que les 3 radios Compression Mode/Character/Gain
            // Reduction plus haut, mais dediee aux bandes en Expand/Both.
            for (int i = 0; i < kNumExpandModes; ++i)
            {
                addAndMakeVisible (expandModeButtons[i]);
                expandModeButtons[i].setButtonText (expandModeLabels[i]);
                expandModeButtons[i].setClickingTogglesState (true);
                expandModeButtons[i].setRadioGroupId (9109, juce::dontSendNotification);
                expandModeButtons[i].onClick = [this, i]
                {
                    if (expandModeButtons[i].getToggleState() && owner.onAiExpandModeSelected)
                        owner.onAiExpandModeSelected (i);
                };
                expandModeButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                // Violet -- coherent avec le reste du theme Expand (diode
                // Comp/Expand, knobs teintes, rangee GR dediee), retour
                // utilisateur : "il faut faire correspondre les couleurs
                // pour les options de l'expandeur en violet comme la diode
                // expand".
                expandModeButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledPurple);
                expandModeButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                expandModeButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            for (int i = 0; i < kNumExpandCharacters; ++i)
            {
                addAndMakeVisible (expandCharacterButtons[i]);
                expandCharacterButtons[i].setButtonText (expandCharacterLabels[i]);
                expandCharacterButtons[i].setClickingTogglesState (true);
                expandCharacterButtons[i].setRadioGroupId (9110, juce::dontSendNotification);
                expandCharacterButtons[i].onClick = [this, i]
                {
                    if (expandCharacterButtons[i].getToggleState() && owner.onAiExpandCharacterSelected)
                        owner.onAiExpandCharacterSelected (i);
                };
                expandCharacterButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                expandCharacterButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledPurple);
                expandCharacterButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                expandCharacterButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }

            for (int i = 0; i < kNumExpandAmountModes; ++i)
            {
                addAndMakeVisible (expandAmountButtons[i]);
                expandAmountButtons[i].setButtonText (expandAmountLabels[i]);
                expandAmountButtons[i].setClickingTogglesState (true);
                expandAmountButtons[i].setRadioGroupId (9111, juce::dontSendNotification);
                expandAmountButtons[i].onClick = [this, i]
                {
                    if (expandAmountButtons[i].getToggleState() && owner.onAiExpandAmountSelected)
                        owner.onAiExpandAmountSelected (i);
                };
                expandAmountButtons[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2e33));
                expandAmountButtons[i].setColour (juce::TextButton::buttonOnColourId, SuppressorColours::ledPurple);
                expandAmountButtons[i].setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
                expandAmountButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            }
        }

        void setCurrentAiTargetIndex (int idx)
        {
            for (int i = 0; i < kNumAiTargets; ++i)
                aiTargetButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiCompressionMode (int idx)
        {
            for (int i = 0; i < kNumCompressionModes; ++i)
                compressionModeButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiCharacter (int idx)
        {
            for (int i = 0; i < kNumAttackReleaseCharacters; ++i)
                characterButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiGainReduction (int idx)
        {
            for (int i = 0; i < kNumGainReductionModes; ++i)
                gainReductionButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setAiScopeStates (bool crossoverOn, bool detectorsOn, bool compressionOn)
        {
            scopeButtons[0].setToggleState (crossoverOn,   juce::dontSendNotification);
            scopeButtons[1].setToggleState (detectorsOn,   juce::dontSendNotification);
            scopeButtons[2].setToggleState (compressionOn, juce::dontSendNotification);
        }

        void setCurrentAiStyle (int idx)
        {
            for (int i = 0; i < kNumStyles; ++i)
                styleButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setAiFidelityState (bool enabled)
        {
            fidelityButton.setToggleState (enabled, juce::dontSendNotification);
        }

        void setCurrentAiExpandMode (int idx)
        {
            for (int i = 0; i < kNumExpandModes; ++i)
                expandModeButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiExpandCharacter (int idx)
        {
            for (int i = 0; i < kNumExpandCharacters; ++i)
                expandCharacterButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiExpandAmount (int idx)
        {
            for (int i = 0; i < kNumExpandAmountModes; ++i)
                expandAmountButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }

        // Calcule sa propre hauteur de contenu a partir de la largeur --
        // le Viewport parent (OptionsOverlay::aiViewport) s'en sert pour
        // savoir jusqu'ou scroller. Meme idiome que AboutOverlay::
        // ParagraphColumn::layout().
        void layout (int width)
        {
            const float gap = 8.0f;
            float y = 0.0f;
            auto row = [&] (juce::TextButton* buttons, int count, float h)
            {
                const float bw = (width - gap * (count - 1)) / (float) count;
                float x = 0.0f;
                for (int i = 0; i < count; ++i)
                {
                    buttons[i].setBounds (juce::Rectangle<float> (x, y, bw, h).toNearestInt());
                    x += bw + gap;
                }
                y += h;
            };

            aiTargetLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            // 10 boutons sur 2 rangees de 5 (Match ajoute en demande
            // explicite : "une case Match pour volume match", 10e choix de
            // la meme liste -- grille impaire 3x3 devenue 2x5).
            constexpr int perRow = 5;
            constexpr int numRows = 2;
            for (int r = 0; r < numRows; ++r)
            {
                row (aiTargetButtons + r * perRow, perRow, 34.0f);
                y += (r < numRows - 1 ? 6.0f : 0.0f);
            }

            y += 20.0f;
            compressionModeLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (compressionModeButtons, kNumCompressionModes, 36.0f);

            y += 20.0f;
            characterLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (characterButtons, kNumAttackReleaseCharacters, 36.0f);

            y += 20.0f;
            gainReductionLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (gainReductionButtons, kNumGainReductionModes, 36.0f);

            y += 20.0f;
            scopeLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (scopeButtons, kNumScopes, 36.0f);

            y += 20.0f;
            styleLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (styleButtons, kNumStyles, 34.0f);

            y += 20.0f;
            fidelityLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            fidelityButton.setBounds (juce::Rectangle<float> (0.0f, y, (float) width, 36.0f).toNearestInt());
            y += 36.0f;

            y += 20.0f;
            expandModeLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (expandModeButtons, kNumExpandModes, 36.0f);

            y += 20.0f;
            expandCharacterLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (expandCharacterButtons, kNumExpandCharacters, 36.0f);

            y += 20.0f;
            expandAmountLabelBounds = { 0, (int) y, width, 18 };
            y += 22.0f;
            row (expandAmountButtons, kNumExpandAmountModes, 36.0f);

            y += 10.0f;
            setSize (width, juce::roundToInt (y));
        }

        void paint (juce::Graphics& g) override
        {
            auto drawSectionLabel = [&] (const juce::Rectangle<int>& area, const juce::String& text)
            {
                g.setColour (SuppressorColours::ledBlue);
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
                g.drawFittedText (text, area, juce::Justification::centredLeft, 1);
            };

            drawSectionLabel (aiTargetLabelBounds, "AI MASTERING TARGET (LUFS)");
            drawSectionLabel (compressionModeLabelBounds, "AI COMPRESSION MODE");
            drawSectionLabel (characterLabelBounds, "AI ATTACK / RELEASE CHARACTER");
            drawSectionLabel (gainReductionLabelBounds, "AI GAIN REDUCTION");
            drawSectionLabel (scopeLabelBounds, "AI SCOPE (WHAT THE AI BUTTON IS ALLOWED TO TOUCH)");
            drawSectionLabel (styleLabelBounds, "AI STYLE (OUTPUT BALANCE)");
            drawSectionLabel (fidelityLabelBounds, "AI FIDELITY (RESTORE ORIGINAL TONAL BALANCE)");
            drawSectionLabel (expandModeLabelBounds, "AI EXPAND MODE");
            drawSectionLabel (expandCharacterLabelBounds, "AI EXPAND ATTACK / RELEASE CHARACTER");
            drawSectionLabel (expandAmountLabelBounds, "AI EXPAND AMOUNT");

            g.setColour (SuppressorColours::gridLine);
            g.drawLine (0.0f, compressionModeLabelBounds.getY() - 12.0f, (float) getWidth(), compressionModeLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, characterLabelBounds.getY() - 12.0f, (float) getWidth(), characterLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, gainReductionLabelBounds.getY() - 12.0f, (float) getWidth(), gainReductionLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, scopeLabelBounds.getY() - 12.0f, (float) getWidth(), scopeLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, styleLabelBounds.getY() - 12.0f, (float) getWidth(), styleLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, fidelityLabelBounds.getY() - 12.0f, (float) getWidth(), fidelityLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, expandModeLabelBounds.getY() - 12.0f, (float) getWidth(), expandModeLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, expandCharacterLabelBounds.getY() - 12.0f, (float) getWidth(), expandCharacterLabelBounds.getY() - 12.0f, 1.0f);
            g.drawLine (0.0f, expandAmountLabelBounds.getY() - 12.0f, (float) getWidth(), expandAmountLabelBounds.getY() - 12.0f, 1.0f);
        }

    private:
        static constexpr int kNumAiTargets = 10;
        static constexpr int kNumCompressionModes = 4;
        static constexpr int kNumAttackReleaseCharacters = 4;
        static constexpr int kNumGainReductionModes = 4;
        static constexpr int kNumScopes = 3;
        static constexpr int kNumStyles = 6;
        static constexpr int kNumExpandModes = 4;
        static constexpr int kNumExpandCharacters = 4;
        static constexpr int kNumExpandAmountModes = 4;

        OptionsOverlay& owner;

        juce::TextButton aiTargetButtons[kNumAiTargets];
        juce::TextButton compressionModeButtons[kNumCompressionModes];
        juce::TextButton characterButtons[kNumAttackReleaseCharacters];
        juce::TextButton gainReductionButtons[kNumGainReductionModes];
        juce::TextButton scopeButtons[kNumScopes];
        juce::TextButton styleButtons[kNumStyles];
        juce::TextButton fidelityButton;
        juce::TextButton expandModeButtons[kNumExpandModes];
        juce::TextButton expandCharacterButtons[kNumExpandCharacters];
        juce::TextButton expandAmountButtons[kNumExpandAmountModes];

        juce::Rectangle<int> aiTargetLabelBounds, compressionModeLabelBounds, characterLabelBounds, gainReductionLabelBounds, scopeLabelBounds;
        juce::Rectangle<int> styleLabelBounds, fidelityLabelBounds;
        juce::Rectangle<int> expandModeLabelBounds, expandCharacterLabelBounds, expandAmountLabelBounds;
    };

    static constexpr int kNumScales = 6;
    static constexpr int kNumLatency = 3;
    static constexpr float kScaleValues[kNumScales] = { 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.0f };

    juce::TextButton scaleButtons[kNumScales];
    juce::TextButton latencyButtons[kNumLatency];
    juce::TextButton licenseButton;
    juce::TextButton closeButton;
    juce::TextButton closeCrossButton;

    juce::Viewport aiViewport;
    AiPanel aiPanel { *this };

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<int> scaleLabelBounds, latencyLabelBounds;
    juce::Rectangle<int> generalHeaderBounds, aiHeaderBounds;
    float aiDividerY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsOverlay)
};
