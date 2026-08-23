#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "PluginProcessor.h"
#include "ConsoleFader.h"
#include "RackPushButton.h"
#include "FaderControl.h"

/**
    BandMetersRack
    ---------------
    Occupe la plaque rack du bas (vuPlateBounds, jusque-la vide -- voir
    "il faut garder le rack vide en bas pour le moment"), avec un mini
    vu-metre par bande. Retour utilisateur explicite : reprendre EXACTEMENT
    le style diode du GRMeter existant (degrade cyan->vert->jaune->orange
    ->rouge pour la compression, rangee EXPAND dediee en bleu, echelle dB
    non-lineaire alignee sur les diodes -- voir GRMeter.cpp/dbToFraction())
    mais en colonnes VERTICALES (diodes rondes) plutot qu'en rangee
    horizontale, pour tenir dans la hauteur du rack avec 5 bandes cote a
    cote (moins de diodes que le GRMeter pleine largeur : 14 au lieu de 36,
    meme principe/couleurs, juste moins de segments faute de place).

    Plaque ELARGIE (compactBandW 150 -> 250, retour utilisateur explicite) :
    Dry/Wet et Spread ont demenage ici depuis BandPanel (voir doc de classe
    dans PluginEditor.h) -- meme composant FaderControl/barre LED segmentee
    qu'avant (preview "barre segmentee" validee), juste deplaces a droite du
    fader VOLUME, separes par une ligne "design" (renderGlowDividerLine).
*/
// Fader VOLUME (output) par bande -- retour utilisateur explicite : "je
// veux les faders du smart master ... les fader qui sont dans le mode
// simple" -- ConsoleFader.h, porte tel quel depuis SimpleMasterView.h
// (Smart Master), deja valide par l'utilisateur sur ce meme projet.
struct BandVolumeFader
{
    ConsoleFader slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Dry/Wet (idBandMix) + Spread (idBandWidth) -- ex-BandPanel::mixFader/
// widthFader, demenages ici tels quels (meme FaderControl/barre LED
// segmentee, voir FaderControl.h).
struct BandMixWidthFaders
{
    FaderControl mix, width;
};

// SOLO (toggle persistant, idSolo) + SEL (retour utilisateur explicite :
// "sel ne doit pas rester enfonce, c'est pour selectionner" -- simple
// clic, jamais de ButtonAttachment/etat persistant, voir RackPushButton).
struct BandButtons
{
    explicit BandButtons (juce::Colour selAccent)
        : solo ("SOLO", SixpackColours::ledRed), sel ("SEL", selAccent)
    {
    }

    RackPushButton solo, sel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
};

class BandMetersRack : public juce::Component, private juce::Timer
{
public:
    // Appele quand l'utilisateur clique SEL sur une bande -- le parent
    // (PluginEditor) branche ceci sur le meme mecanisme que le clic sur
    // une zone du spectre crossover (selectedBand + refreshBandPanelVisibility()).
    std::function<void (int)> onBandSelectRequested;

    // Les libelles WET/DRY/SPREAD/MONO "bakent" leur couleur a la
    // construction (setupEndLabel, voir ci-dessous) -- a rappeler par le
    // parent apres tout changement de theme en cours d'execution (meme
    // bug que SixpackEditorContent::refreshThemeReactiveColours()).
    void refreshThemeReactiveColours()
    {
        const auto colour = SixpackColours::textBright.withAlpha (0.75f);
        for (auto& mw : mixWidthFaders)
        {
            mw->mix.topLabel.setColour (juce::Label::textColourId, colour);
            mw->mix.bottomLabel.setColour (juce::Label::textColourId, colour);
            mw->width.topLabel.setColour (juce::Label::textColourId, colour);
            mw->width.bottomLabel.setColour (juce::Label::textColourId, colour);
        }
    }

    explicit BandMetersRack (SixpackAudioProcessor& p)
        : processorRef (p), powerParam (*p.apvts.getRawParameterValue (SixpackAudioProcessor::idPower))
    {
        // 30 -> 60Hz (retour utilisateur : "vraiment plus fluide" -- un
        // taux plus BAS ne peut jamais rendre l'affichage plus fluide qu'un
        // taux plus haut ; 60Hz correspond au taux de rafraichissement
        // standard de la plupart des ecrans, au-dela duquel les frames
        // supplementaires ne seraient de toute facon pas affichees).
        // Sans risque : paint() est redevenu bon marche (cache d'arriere-
        // plan, voir rebuildBackgroundCacheIfNeeded()) et timerCallback()
        // ne redessine plus que les colonnes qui bougent reellement (voir
        // ci-dessous).
        startTimerHz (60);

        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            // Fader OUTPUT par bande -- meme parametre que le knob OUT du
            // panneau de detail (idOutput, -30..30dB).
            auto fader = std::make_unique<BandVolumeFader>();
            fader->slider.setAccentColour (bandAccent (band));
            fader->slider.setTickSteps (10.0f, 2.0f);
            fader->slider.setShowTicks (false); // echelle GR/Expand deja presente a cote, pas de place pour une 2e
            fader->slider.setEdgePadding (edgePadding, edgePadding); // voir resized() -- bounds agrandis d'autant
            addAndMakeVisible (fader->slider);
            fader->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processorRef.apvts, SixpackAudioProcessor::bandParamId (SixpackAudioProcessor::idOutput, band), fader->slider);
            faders[(size_t) band] = std::move (fader);

            // Dry/Wet (idBandMix) + Spread (idBandWidth) -- demenages ici
            // depuis BandPanel (voir doc de classe), meme setup qu'avant
            // (SixpackEditorContent::setupFader(), desormais supprimee).
            auto mw = std::make_unique<BandMixWidthFaders>();
            mw->mix.lookAndFeel.setAccentColour (SixpackColours::ledCyan);
            FaderVerticalLookAndFeel::setNeutralValue (mw->mix.slider, 0.0);
            addAndMakeVisible (mw->mix.slider);
            mw->mix.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processorRef.apvts, SixpackAudioProcessor::bandParamId (SixpackAudioProcessor::idBandMix, band), mw->mix.slider);

            mw->width.lookAndFeel.setAccentColour (SixpackColours::ledAmber);
            mw->width.lookAndFeel.setAccentColourBelowNeutral (SixpackColours::ledCyan);
            FaderVerticalLookAndFeel::setNeutralValue (mw->width.slider, 100.0);
            addAndMakeVisible (mw->width.slider);
            mw->width.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processorRef.apvts, SixpackAudioProcessor::bandParamId (SixpackAudioProcessor::idBandWidth, band), mw->width.slider);

            auto setupEndLabel = [this] (juce::Label& l, const juce::String& text)
            {
                l.setText (text, juce::dontSendNotification);
                l.setJustificationType (juce::Justification::centred);
                l.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.05f));
                l.setColour (juce::Label::textColourId, SixpackColours::textBright.withAlpha (0.75f));
                addAndMakeVisible (l);
            };
            setupEndLabel (mw->mix.topLabel, "WET");
            setupEndLabel (mw->mix.bottomLabel, "DRY");
            setupEndLabel (mw->width.topLabel, "SPREAD");
            setupEndLabel (mw->width.bottomLabel, "MONO");
            mixWidthFaders[(size_t) band] = std::move (mw);

            // SOLO (toggle, idSolo) + SEL (clic simple, voir onBandSelectRequested).
            auto btns = std::make_unique<BandButtons> (bandAccent (band));
            // ButtonAttachment NE configure PAS setClickingTogglesState() --
            // sans cet appel, getToggleState() ne change jamais au clic (le
            // listener de l'attachment lit getToggleState() APRES le clic
            // pour decider quoi ecrire au parametre, voir
            // ButtonParameterAttachment::buttonClicked() dans le code
            // source JUCE) : SANS ce flag, le bouton restait visuellement
            // toujours relache -- retour utilisateur explicite : "le solo
            // ne reste pas enfonce".
            btns->solo.setClickingTogglesState (true);
            addAndMakeVisible (btns->solo);
            addAndMakeVisible (btns->sel);
            btns->soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processorRef.apvts, SixpackAudioProcessor::idSolo[band], btns->solo);
            btns->sel.onClick = [this, band]
            {
                if (onBandSelectRequested != nullptr)
                    onBandSelectRequested (band);
            };
            bandButtons[(size_t) band] = std::move (btns);
        }
    }

    ~BandMetersRack() override { stopTimer(); }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            const auto layout = computeLayout (bounds, band);
            // expanded(edgePadding) : agrandit les bounds du COMPOSANT sans
            // deplacer le rail (ConsoleFader::paint() retranche la meme
            // marge avant de calculer sa mise en page) -- laisse la place a
            // l'ombre portee du cap sans jamais la faire tronquer, retour
            // utilisateur explicite : "il doit juste pas se couper".
            faders[(size_t) band]->slider.setBounds (layout.faderArea.expanded (edgePadding).toNearestInt());
            bandButtons[(size_t) band]->solo.setBounds (layout.soloArea.toNearestInt());
            bandButtons[(size_t) band]->sel.setBounds (layout.selArea.toNearestInt());

            auto layoutMixWidthFader = [] (FaderControl& fc, juce::Rectangle<float> slot)
            {
                constexpr float endLabelH = 11.0f;
                fc.topLabel.setBounds (slot.removeFromTop (endLabelH).toNearestInt());
                fc.bottomLabel.setBounds (slot.removeFromBottom (endLabelH).toNearestInt());
                fc.slider.setBounds (slot.reduced (2.0f, 1.0f).toNearestInt());
            };
            layoutMixWidthFader (mixWidthFaders[(size_t) band]->mix,   layout.wetFaderArea);
            layoutMixWidthFader (mixWidthFaders[(size_t) band]->width, layout.spreadFaderArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Cache d'arriere-plan (retour utilisateur explicite : "c'est pas
        // fluide" / "toujours pareil c'est pas fluide") : cette fonction
        // redessinait AVANT 6 plaques COMPLETES (ombre portee floutee,
        // degrade, double liseret embosse, 4 vis, reperes chiffres) a
        // CHAQUE frame -- meme quand seuls les vu-metres/diodes changent.
        // DropShadow::drawForPath() (flou) est de loin l'operation la plus
        // couteuse de tout le fichier ; la repeter 6x/frame a 30Hz faisait
        // decrocher le rendu (perception de "crante" independante de la
        // precision du calcul audio, deja corrigee cote DSP). La partie
        // plaque/vis/texte ne depend QUE de la taille du composant et du
        // theme (jamais des valeurs audio) : rebuildBackgroundCacheIfNeeded()
        // la dessine une seule fois dans une image mise en cache ; paint()
        // se contente de la blitter puis dessine par-dessus les seuls
        // elements reellement dynamiques (diodes GR/EXPAND, vu-metre
        // master) -- voir drawBandStatic()/drawBandDynamic().
        const bool powered = powerParam.load() > 0.5f;
        rebuildBackgroundCacheIfNeeded();
        g.drawImageAt (backgroundCache, 0, 0);

        auto bounds = getLocalBounds().toFloat();
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            const auto layout = computeLayout (bounds, band);

            // Section droite : vu-metre MASTER de la bande + fader VOLUME A
            // COTE -- le fader lui-meme est un vrai enfant Component
            // positionne dans resized(), seul le vu-metre est dessine ici.
            drawMasterMeter (g, layout.meterArea, band, powered);

            drawBandDynamic (g, layout.leftSection, band, powered);
        }
    }

    void rebuildBackgroundCacheIfNeeded()
    {
        const bool lightTheme = SixpackLookAndFeel::isLightThemeStatic();
        if (backgroundCache.isValid()
            && backgroundCache.getWidth() == getWidth() && backgroundCache.getHeight() == getHeight()
            && cachedLightTheme == lightTheme)
            return; // deja a jour (voir doc de paint())

        backgroundCache = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics g (backgroundCache);
        auto bounds = getLocalBounds().toFloat();

        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            auto colOuter = juce::Rectangle<float> (bounds.getX() + compactBandW * (float) band, bounds.getY(), compactBandW, bounds.getHeight());
            auto plate = colOuter.reduced (3.0f);

            // Plaque VISSEE, distincte du chassis -- retour utilisateur
            // explicite : "je souhaite une plaque vissee genre. comme un
            // ajout sur le rack. ça doit faire realiste et avec un effet
            // extrusion" (et : "pas besoin d'ecrire low mid... sur les
            // bandes", donc aucun label texte). Ombre portee (elle
            // "flotte" legerement sur le chassis), degrade acier noir
            // brosse teinte couleur de bande (meme base que
            // renderMetalPanel), bordure embossee double liseret + 4 vis
            // d'angle (meme dessin que SixpackLookAndFeel::drawScrew,
            // celles du chassis principal).
            juce::Path platePath;
            platePath.addRoundedRectangle (plate, 4.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 5, { 0, 2 }).drawForPath (g, platePath);

            const auto accent = bandAccent (band);
            // Theme CLAIR (retour utilisateur explicite, voir
            // SixpackLookAndFeel::renderMetalPanel()) : meme degrade
            // "metal brosse argente" que le chassis principal au lieu du
            // brosse noir d'origine.
            juce::ColourGradient base (lightTheme ? SixpackColours::lightPanelGradTop : juce::Colour (0xff262422), plate.getX(), plate.getY(),
                                        lightTheme ? SixpackColours::lightPanelGradBottom : juce::Colour (0xff0e0d0c), plate.getX(), plate.getBottom(), false);
            g.setGradientFill (base);
            g.fillPath (platePath);
            g.setColour (accent.withAlpha (0.16f));
            g.fillPath (platePath);

            juce::Path outerBevel; outerBevel.addRoundedRectangle (plate.expanded (0.5f), 4.5f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.strokePath (outerBevel, juce::PathStrokeType (1.2f));
            juce::Path innerBevel; innerBevel.addRoundedRectangle (plate.reduced (1.2f), 3.3f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.strokePath (innerBevel, juce::PathStrokeType (1.0f));
            g.setColour (accent.withAlpha (0.55f));
            g.strokePath (platePath, juce::PathStrokeType (1.0f));

            // Vis aux 4 VRAIS coins de la plaque -- la rangee SOLO/SEL est
            // desormais EN HAUT (retour utilisateur explicite : "je pense
            // que les boutons solo et sel doivent etre au dessus du fader
            // ... pour eviter les grands deplacements de souris") avec sa
            // propre marge haute dediee (voir computeLayout()) qui degage
            // largement ces 2 vis -- fini le chevauchement bouton/vis
            // (retour utilisateur : "collision avec les vis sur la
            // console en bas").
            // Rayon 2.6 -> 3.4 (retour utilisateur : "les vis sont
            // noires" -- a 2.6px le degrade metallique/reflet de
            // SixpackLookAndFeel::drawScrew() n'a presque plus de
            // pixels pour exister et se voit comme un simple point plat,
            // surtout comparee aux grandes vis de renderCornerScrews()
            // (rayon 5) juste au-dessus depuis que les plaques se
            // touchent, voir resized()).
            constexpr float screwInset = 9.0f, screwRadius = 3.4f;
            SixpackLookAndFeel::drawScrew (g, { plate.getX() + screwInset, plate.getY() + screwInset }, screwRadius);
            SixpackLookAndFeel::drawScrew (g, { plate.getRight() - screwInset, plate.getY() + screwInset }, screwRadius);
            SixpackLookAndFeel::drawScrew (g, { plate.getX() + screwInset, plate.getBottom() - screwInset }, screwRadius);
            SixpackLookAndFeel::drawScrew (g, { plate.getRight() - screwInset, plate.getBottom() - screwInset }, screwRadius);

            const auto layout = computeLayout (bounds, band);

            // Trait blanc pour delimiter GR+EXPAND (a gauche) de la section
            // VOLUME (a droite) -- retour utilisateur explicite : "j'aime
            // bien les traits blancs pour delimiter", au MILIEU de la zone
            // reservee au separateur (jamais a l'interieur d'une des 2
            // sections -- c'etait le bug precedent : GR/EXPAND et le
            // vu-metre master se chevauchaient, voir computeLayout()).
            SixpackLookAndFeel::renderDividerLine (g, layout.dividerX, layout.leftSection.getY(), layout.leftSection.getBottom());

            // Separation VOLUME / WET+SPREAD -- meme style "design" (halo
            // lumineux) que la separation Comp/Expand du panneau de detail
            // (retour utilisateur explicite, voir BandMixWidthFaders).
            SixpackLookAndFeel::renderGlowDividerLine (g, layout.mixWidthDividerX, layout.leftSection.getY(), layout.leftSection.getBottom());

            drawBandStatic (g, layout.leftSection);
        }

        cachedLightTheme = lightTheme;
    }

private:
    // Optimisation CPU (passe demandee explicitement par l'utilisateur) :
    // ne redessine que si l'affichage a REELLEMENT change, au lieu d'un
    // repaint() inconditionnel -- meme principe deja etabli par
    // GRMeter::timerCallback(). AFFINEE (retour utilisateur : "vraiment
    // plus fluide") -- un repaint() SANS rectangle invalide TOUT le rack
    // (les 6 plaques), meme quand une seule bande bouge (cas frequent :
    // une bande solo, ou juste 1-2 bandes actives) ; le cache d'arriere-
    // plan (voir paint()) a deja supprime le gros du cout mais la
    // recomposition/blit de 6 plaques pour 1 seule qui change restait un
    // gaspillage. repaint(rect) cible desormais UNIQUEMENT la/les colonnes
    // dont GR/EXPAND/OUT ont reellement change ce tick.
    void timerCallback() override
    {
        const bool powered = powerParam.load() > 0.5f;
        const bool poweredChanged = (powered != lastPowered);
        lastPowered = powered;

        auto bounds = getLocalBounds().toFloat();
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            const float gr  = processorRef.grMeterDb[(size_t) band].load();
            const float ex  = processorRef.grMeterExpandDb[(size_t) band].load();
            const float out = processorRef.bandOutputLevelDb[(size_t) band].load();
            const bool bandChanged = poweredChanged
                || std::abs (gr - lastGrDb[(size_t) band]) > 0.05f
                || std::abs (ex - lastExDb[(size_t) band]) > 0.02f
                || std::abs (out - lastOutDb[(size_t) band]) > 0.1f;
            if (! bandChanged)
                continue;

            lastGrDb[(size_t) band] = gr;
            lastExDb[(size_t) band] = ex;
            lastOutDb[(size_t) band] = out;

            auto colOuter = juce::Rectangle<float> (bounds.getX() + compactBandW * (float) band, bounds.getY(), compactBandW, bounds.getHeight());
            repaint (colOuter.getSmallestIntegerContainer());
        }
    }

    bool lastPowered = true;
    std::array<float, (size_t) SixpackAudioProcessor::NumBands> lastGrDb {}, lastExDb {}, lastOutDb {};

    // Cache d'arriere-plan des 6 plaques (voir doc de paint() /
    // rebuildBackgroundCacheIfNeeded()) -- invalide et reconstruit
    // automatiquement des que la taille ou le theme changent.
    juce::Image backgroundCache;
    bool cachedLightTheme = false;

    // Largeurs de section EXPLICITES (pas de centrage/soustractions en
    // chaine, qui avaient fini par faire chevaucher EXPAND et le vu-metre
    // master -- retour utilisateur explicite : "le meter master et les
    // expand se chevauchent"). computeLayout() est la SEULE source de
    // verite des positions, utilisee identiquement par paint() et
    // resized().
    static constexpr float leftSectionW = 42.0f;   // GR + echelle + EXPAND
    // Zones separatrices/gaps RETRECIES (18->14, 14->10, 10->6 -- voir
    // compactBandW) pour retrouver la place perdue par le passage a 6
    // bandes SANS reduire la taille des controles eux-memes (meterW/
    // faderW/mixWidthFaderW inchanges) -- sinon le budget fixe total
    // (212px) ne tenait plus dans le content disponible a 228px de
    // colonne (202px), et le fader SPREAD (2e des 2 mixWidthFaderW)
    // se faisait tronquer silencieusement par removeFromLeft().
    static constexpr float dividerZoneW = 14.0f;   // marge + trait + marge
    static constexpr float meterW = 10.0f;
    static constexpr float meterFaderGap = 10.0f;  // > edgePadding pour garder un vrai espace visible
    static constexpr float faderW = 40.0f;         // largeur NOMINALE (avant expansion par edgePadding)
    // Zone WET/DRY + SPREAD, a droite du fader VOLUME -- retour utilisateur
    // explicite : "le dry wet et le spread on va les integrer dans la
    // console en bas... on va elagir chacune des tranches" -- meme largeur
    // de zone separatrice que dividerZoneW, faders plus etroits (barre LED
    // segmentee, pas de capuchon a proteger comme ConsoleFader) que
    // faderW.
    static constexpr float mixWidthDividerZoneW = 14.0f;
    static constexpr float mixWidthFaderW = 30.0f;
    static constexpr float mixWidthFaderGap = 6.0f;
    // Retrecie 250 -> 228 (retour utilisateur explicite, ajout de la 6e
    // bande "Presence") puis 228 -> 224 (retour utilisateur : "j'ai une
    // plaque a gauche derriere la console mais elle n'est pas a droite du
    // plugin" -- PluginEditor.cpp::resized() retranche desormais 24px a
    // DROITE du rackArea en plus des 24px a gauche, pour une marge
    // symetrique -- voir la aussi pour le detail) : 6 x 224 = 1344px,
    // tient dans les ~1346px disponibles (1394 - 24 - 24, voir
    // PluginEditor.cpp resized()) -- la marge libre a droite du rack
    // (reservee par une demande passee, "je veux mettre d'autres
    // fonctions a droite") se reduit en consequence mais la fenetre et la
    // barre de controle du haut restent inchangees.
    static constexpr float compactBandW = 224.0f;  // 150 + zone WET/DRY+SPREAD
    static constexpr float edgePadding = 8.0f;     // voir ConsoleFader::setEdgePadding()
    // SOLO/SEL EN HAUT de la plaque (retour utilisateur explicite : "je
    // pense que les boutons solo et sel doivent etre au dessus du fader et
    // pas en dessous pour eviter les grands deplacements de souris") --
    // buttonRowH inclut une marge haute dediee (screwClearance) pour
    // degager largement les 2 vis de coin superieures (voir paint()),
    // avant meme la reduction visuelle des boutons eux-memes.
    static constexpr float buttonRowH = 52.0f;
    static constexpr float buttonScrewClearance = 20.0f;
    static constexpr float buttonGap = 6.0f;

    struct Layout
    {
        juce::Rectangle<float> leftSection, meterArea, faderArea, wetFaderArea, spreadFaderArea, soloArea, selArea;
        float dividerX, mixWidthDividerX;
    };

    Layout computeLayout (juce::Rectangle<float> bounds, int band) const noexcept
    {
        auto colOuter = juce::Rectangle<float> (bounds.getX() + compactBandW * (float) band, bounds.getY(), compactBandW, bounds.getHeight());
        auto plate = colOuter.reduced (3.0f);
        auto buttonRow = plate.removeFromTop (buttonRowH);
        buttonRow.removeFromTop (buttonScrewClearance);
        buttonRow = buttonRow.reduced (10.0f, 4.0f);
        auto content = plate.reduced (10.0f, 15.0f);

        Layout lay;
        lay.leftSection = content.removeFromLeft (leftSectionW);
        lay.dividerX = content.getX() + dividerZoneW * 0.5f;
        content.removeFromLeft (dividerZoneW);
        lay.meterArea = content.removeFromLeft (meterW);
        content.removeFromLeft (meterFaderGap);
        lay.faderArea = content.removeFromLeft (faderW);

        lay.mixWidthDividerX = content.getX() + mixWidthDividerZoneW * 0.5f;
        content.removeFromLeft (mixWidthDividerZoneW);
        lay.wetFaderArea = content.removeFromLeft (mixWidthFaderW);
        content.removeFromLeft (mixWidthFaderGap);
        lay.spreadFaderArea = content.removeFromLeft (mixWidthFaderW);

        lay.soloArea = buttonRow.removeFromLeft ((buttonRow.getWidth() - buttonGap) * 0.5f);
        buttonRow.removeFromLeft (buttonGap);
        lay.selArea = buttonRow;
        return lay;
    }

    // Meme echelle non-lineaire que GRMeter::dbToFraction() (resolution
    // fine pres de 0dB) -- garde le meme "feel" que le vu-metre existant.
    static constexpr float scaleMarksDb[] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
    static constexpr int numScaleMarks = 10;

    static float dbToFraction (float db) noexcept
    {
        db = juce::jlimit (0.0f, scaleMarksDb[numScaleMarks - 1], db);
        for (int i = 0; i < numScaleMarks - 1; ++i)
        {
            const float lo = scaleMarksDb[i], hi = scaleMarksDb[i + 1];
            if (db <= hi)
            {
                const float segFrac = (hi > lo) ? (db - lo) / (hi - lo) : 0.0f;
                return (float (i) + segFrac) / float (numScaleMarks - 1);
            }
        }
        return 1.0f;
    }

    static juce::Colour bandAccent (int band) noexcept
    {
        static const std::array<juce::Colour, (size_t) SixpackAudioProcessor::NumBands> colours =
            { SixpackColours::ledCyan, SixpackColours::ledGreen, SixpackColours::ledAmber, SixpackColours::ledOrange, SixpackColours::ledPurple, SixpackColours::ledRed };
        return colours[(size_t) band];
    }

    // Vu-metre MASTER de la bande, style Ableton -- retour utilisateur
    // explicite : "on peut le faire genre les vu metre de ableton. ça
    // evitera la confusion" (avec les diodes rondes de GR/EXPAND juste a
    // cote) -- barre LISSE (pas de diodes), degrade vert -> jaune -> rouge
    // SANS cyan (pour ne pas se confondre avec le bleu/cyan d'EXPAND),
    // etiree sur toute la hauteur de la plaque comme le reste.
    void drawMasterMeter (juce::Graphics& g, juce::Rectangle<float> track, int band, bool powered)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (track, 2.0f);

        juce::ColourGradient grad (SixpackColours::ledGreen, track.getX(), track.getBottom(),
                                    SixpackColours::ledRed, track.getX(), track.getY(), false);
        grad.addColour (0.72, SixpackColours::ledGreen);
        grad.addColour (0.90, SixpackColours::ledYellow);

        constexpr float minDb = -48.0f, maxDb = 3.0f;
        const float outDb = powered ? processorRef.bandOutputLevelDb[(size_t) band].load() : minDb;
        const float frac = juce::jlimit (0.0f, 1.0f, (outDb - minDb) / (maxDb - minDb));
        const float litY = track.getBottom() - frac * track.getHeight();

        if (litY < track.getBottom())
        {
            auto lit = track.withTop (litY);
            g.saveState();
            g.reduceClipRegion (lit.toNearestInt());
            g.setGradientFill (grad);
            g.fillRoundedRectangle (track, 2.0f);
            g.restoreState();
        }

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRoundedRectangle (track, 2.0f, 1.0f);
    }

    // Reperes chiffres de l'echelle GR/EXPAND -- STATIQUES (ne dependent
    // que du layout, jamais des valeurs audio) : dessines UNE FOIS dans le
    // cache d'arriere-plan (voir rebuildBackgroundCacheIfNeeded()), plus
    // jamais a chaque frame. Scinde depuis l'ancien drawBand() unique, qui
    // melangeait ce texte statique avec les diodes dynamiques ci-dessous.
    void drawBandStatic (juce::Graphics& g, juce::Rectangle<float> col)
    {
        constexpr int numLeds = 18;
        constexpr float dotGap = 1.3f, scaleW = 20.0f, colGap = 5.0f;
        const float step = col.getHeight() / (float) numLeds;
        const float dotSize = juce::jmax (2.0f, step - dotGap);
        const float bottom = col.getBottom();
        const float x = col.getX() + dotSize + colGap; // apres la colonne GR, voir drawBandDynamic()

        // Meme formule dbToFraction() que le remplissage des diodes GR
        // (bug corrige, retour utilisateur : "les marques avec les
        // numeros... doivent correspondre a la bonne diode" -- voir
        // l'historique dans drawBandDynamic()).
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        for (int m = 0; m < numScaleMarks; ++m)
        {
            const int ledIndex = juce::jlimit (0, numLeds - 1, (int) std::round (dbToFraction (scaleMarksDb[m]) * (float) numLeds) - 1);
            const float y = bottom - dotSize * 0.5f - (float) ledIndex * step;
            const juce::String text = scaleMarksDb[m] <= 0.0f ? juce::String (0) : "-" + juce::String ((int) scaleMarksDb[m]);
            SixpackLookAndFeel::drawEngravedText (g, text, juce::Rectangle<float> (x, y - 5.0f, scaleW, 10.0f).toNearestInt(),
                                                      SixpackColours::textBright.withAlpha (0.6f), juce::Justification::centredLeft);
        }
    }

    // Diodes GR + EXPAND -- DYNAMIQUES (dependent des valeurs audio en
    // temps reel) : seule partie encore redessinee a chaque frame, voir
    // doc de paint()/rebuildBackgroundCacheIfNeeded().
    void drawBandDynamic (juce::Graphics& g, juce::Rectangle<float> col, int band, bool powered)
    {
        // Retour utilisateur : "je souhaite que les mesures prennent toute
        // la hauteur dans le rack du bas" -- nombre de diodes FIXE, mais
        // leur pas (donc la hauteur totale de la colonne) est calcule pour
        // occuper exactement col.getHeight(), plutot qu'une taille de
        // diode fixe qui laissait un vide en dessous.
        constexpr int numLeds = 18;
        constexpr float kExpandLedDb = 0.25f; // plage totale ~4.5dB avant saturation
        constexpr float dotGap = 1.3f, exWidth = 4.0f, scaleW = 20.0f, colGap = 5.0f;
        const float step = col.getHeight() / (float) numLeds;
        const float dotSize = juce::jmax (2.0f, step - dotGap);

        const float grDb    = juce::jlimit (0.0f, scaleMarksDb[numScaleMarks - 1], processorRef.grMeterDb[(size_t) band].load());
        const float exDbRaw = juce::jlimit (0.0f, kExpandLedDb * (float) numLeds, processorRef.grMeterExpandDb[(size_t) band].load());
        const int litCount  = powered ? (int) std::round (dbToFraction (grDb) * (float) numLeds) : 0;
        const int litExpand = powered ? (int) std::round (exDbRaw / kExpandLedDb) : 0;

        const float bottom = col.getBottom();

        // GR + EXPAND dans la MEME section (retour utilisateur explicite :
        // "le GR et exp doivent etre dans la meme section") -- le VOLUME
        // (fader) vit desormais dans sa propre section a droite, separee
        // par le trait blanc (voir appelant). Cale a GAUCHE (pas centre) :
        // avec un centrage, le bord droit d'EXPAND finissait colle a la
        // limite de la section (donc au vu-metre master juste apres) des
        // que leftSectionW etait un peu juste -- voir computeLayout().
        float x = col.getX();

        // --- Colonne GR (compression), bas -> haut, degrade identique au
        // GRMeter existant (SixpackLookAndFeel::ledGradientColour). ---
        for (int i = 0; i < numLeds; ++i)
        {
            const float t = (float) i / (float) (numLeds - 1);
            const float y = bottom - dotSize - (float) i * step;
            auto dot = juce::Rectangle<float> (x, y, dotSize, dotSize);
            if (i < litCount)
                g.setColour (SixpackLookAndFeel::ledGradientColour (t));
            else
                g.setColour (SixpackColours::ledOff);
            g.fillEllipse (dot);
        }
        x += dotSize + colGap;
        x += scaleW + colGap; // saute la colonne des reperes chiffres (statique, voir drawBandStatic())

        // --- Colonne EXPAND dediee, bas -> haut, degrade bleu/cyan/violet
        // (SixpackLookAndFeel::ledGradientColourExpand), meme principe
        // que la rangee fine du GRMeter existant. ---
        for (int i = 0; i < numLeds; ++i)
        {
            const float t = (float) i / (float) (numLeds - 1);
            const float y = bottom - dotSize - (float) i * step;
            auto dot = juce::Rectangle<float> (x, y, exWidth, dotSize);
            if (i < litExpand)
                g.setColour (SixpackLookAndFeel::ledGradientColourExpand (t));
            else
                g.setColour (SixpackColours::ledOff);
            g.fillRoundedRectangle (dot, 1.0f);
        }
    }

    SixpackAudioProcessor& processorRef;
    std::atomic<float>& powerParam;
    std::array<std::unique_ptr<BandVolumeFader>, (size_t) SixpackAudioProcessor::NumBands> faders;
    std::array<std::unique_ptr<BandMixWidthFaders>, (size_t) SixpackAudioProcessor::NumBands> mixWidthFaders;
    std::array<std::unique_ptr<BandButtons>, (size_t) SixpackAudioProcessor::NumBands> bandButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandMetersRack)
};
