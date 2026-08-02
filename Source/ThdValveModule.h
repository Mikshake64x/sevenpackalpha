#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "ThdEngageSwitch.h"
#include "NeedleVU.h"
#include "ToggleSwitch.h"

/**
    ThdValveModule ("THD VALVE 2")
    ------------------------------
    UN SEUL etage de saturation globale (post-sommation des 3 bandes, un
    seul Drive/Engage -- voir SoftClipper/idSoftClip), mais affiche 3
    MESURES independantes (Low/Mid/High) : le residu de distorsion genere
    par cet etage unique est analyse par un crossover (voir
    PluginProcessor::processBlock(), clipMeterAmountLow/Mid/High) pour
    savoir ou tombent les harmoniques generees, pas 3 saturations
    separees.

    REUTILISE TEL QUEL le composant NeedleVU (le meme VU-metre a aiguille
    que ceux de la barre de controle, en mode THD) pour chacune des 3
    bandes, chacun teinte de la couleur d'accent DE SA BANDE (Low=cyan,
    Mid=ambre, High=rouge) au lieu du cyan/ivoire par defaut -- voir
    NeedleVU::NeedleVU (constructeur avec externalAmountSource/
    accentOverride/needleColourOverride).

    PAS DE CHASSIS PROPRE (retire, demande explicite) : ce module occupe
    desormais l'INTEGRALITE de la plaque rack 1U du bas (vuPlateBounds
    dans PluginEditor), qui fournit deja son propre cadre metal + vis
    (voir rebuildBackgroundCache()) -- les 3 aiguilles prennent donc toute
    la hauteur disponible de cette plaque, chacune gardant son PROPRE petit
    cadre "visse" (NeedleVU::rebuildStaticCache()).

    L'actionneur ON/OFF est le sprite ThdEngageSwitch (Button-08-S-
    Filmstrip.png, engageSwitch, membre public attache au parametre
    SOFTCLIP par PluginEditor).
*/
class ThdValveModule : public juce::Component, private juce::Timer
{
public:
    static constexpr int numBands = 3;

    ThdValveModule (SuppressorAudioProcessor& processorIn,
                     std::atomic<float>& lowAmount, std::atomic<float>& midAmount, std::atomic<float>& highAmount,
                     std::atomic<float>& softClipOnToWatch)
        : softToggle ("SOFT", SuppressorColours::ledGreen, &softClipOnToWatch),
          tapeToggle ("TAPE", SuppressorColours::ledAmber, &softClipOnToWatch),
          ironToggle ("IRON", SuppressorColours::ledRed,   &softClipOnToWatch),
          airToggle  ("AIR",  SuppressorColours::ledCyan,  &softClipOnToWatch),
          softClipOnParam (softClipOnToWatch),
          lowNeedle  (processorIn, NeedleVU::Mode::ThdAmount, NeedleVU::Channel::Left, &lowAmount,  SuppressorColours::ledCyan,
                       juce::Colour (0xFFF4E9C8), &softClipOnToWatch),
          midNeedle  (processorIn, NeedleVU::Mode::ThdAmount, NeedleVU::Channel::Left, &midAmount,  SuppressorColours::ledAmber,
                       juce::Colour (0xFFF4E9C8), &softClipOnToWatch),
          highNeedle (processorIn, NeedleVU::Mode::ThdAmount, NeedleVU::Channel::Left, &highAmount, SuppressorColours::ledRed,
                       juce::Colour (0xFFF4E9C8), &softClipOnToWatch)
    {
        setPaintingIsUnclipped (true);
        addAndMakeVisible (engageSwitch);

        // Vrai groupe radio JUCE (id partage) -- SANS ca, cliquer sur le
        // bouton DEJA actif le fait basculer OFF cote JUCE avant meme que
        // notre callback ne force l'etat correct, laissant les 4 boutons
        // eteints (bug rapporte). Avec un radioGroupId, JUCE refuse de
        // desactiver un bouton deja actif au clic -- meme mecanique que
        // RatioSelector.
        constexpr int thdModeRadioGroupId = 9401;
        for (auto* t : { &softToggle, &tapeToggle, &ironToggle, &airToggle })
        {
            t->setRadioGroupId (thdModeRadioGroupId, juce::dontSendNotification);
            addAndMakeVisible (*t);
        }
        addAndMakeVisible (lowNeedle);
        addAndMakeVisible (midNeedle);
        addAndMakeVisible (highNeedle);

        startTimerHz (10); // rafraichit uniquement la diode d'etat -- les 3 aiguilles ont leur propre timer 60Hz (NeedleVU)
    }

    ~ThdValveModule() override { stopTimer(); }

    // Actionneur physique -- attache a idSoftClip via ButtonAttachment
    // dans PluginEditor (comme n'importe quel autre ToggleSwitch).
    ThdEngageSwitch engageSwitch;

    // 4 "flavors" de saturation type mastering (voir SoftClipper::SatMode/
    // idThdMode) -- une diode de couleur differente par fonction (demande
    // explicite), meme style bouton + meme animation que le reste (voir
    // ToggleSwitch/PressAnimator). Radio exclusif pilote depuis PluginEditor
    // (thdModeAttachment) : SOFT est desormais un bouton explicite (au lieu
    // d'un etat "rien de selectionne"), toujours exactement un actif -- CLIP
    // retire (pas assez esthetique a l'usage). Empiles sur UNE SEULE colonne,
    // dans l'ordre SOFT/TAPE/IRON/AIR (demande explicite).
    // Accent RGB par mode ; externalEnabledSource = idSoftClip (voir
    // constructeur) -- diode eteinte quand le THD est desengage, meme
    // traitement que le Power global (au lieu d'un cadre d'assombrissement).
    ToggleSwitch softToggle;
    ToggleSwitch tapeToggle;
    ToggleSwitch ironToggle;
    ToggleSwitch airToggle;

    // Colonne de gauche reservee au nom du module + diode + bouton
    // d'engagement -- le reste de la largeur va integralement aux 3
    // aiguilles. Elargie encore pour un logo PLUS GROS que les logos LOW/
    // MID/HIGH (demande explicite), voir titleBounds ci-dessous.
    static constexpr float engageColW = 150.0f;

    // Colonne des 4 boutons de mode (UNE SEULE colonne empilee), prise sur
    // la largeur auparavant entierement dediee aux 3 aiguilles -- les
    // retrecit donc legerement (demande explicite : "diminue un peu la
    // largeur des VU metres pour faire de la place").
    static constexpr float modeColW = 74.0f;

    void resized() override
    {
        auto content = getLocalBounds().toFloat().reduced (10.0f, 6.0f);
        auto engageCol = content.removeFromLeft (engageColW);

        auto modeCol = content.removeFromLeft (modeColW);
        content.removeFromLeft (10.0f);
        {
            constexpr float gap = 5.0f;
            const float cellH = (modeCol.getHeight() - gap * 3.0f) * 0.25f;
            auto layoutCell = [&] (ToggleSwitch& t)
            {
                t.setBounds (modeCol.removeFromTop (cellH).reduced (2.0f, 1.0f).toNearestInt());
                modeCol.removeFromTop (gap);
            };
            layoutCell (softToggle);
            layoutCell (tapeToggle);
            layoutCell (ironToggle);
            airToggle.setBounds (modeCol.reduced (2.0f, 1.0f).toNearestInt());
        }

        // Logo THD -- agrandi (130x40, plus grand que les logos de bande),
        // decale (bas + droite) pour degager la vis de coin de la plaque
        // rack (vissee a x=15,y=15 rayon 5, voir renderCornerScrews dans
        // PluginEditor::rebuildBackgroundCache()).
        engageCol.removeFromTop (4.0f);
        titleBounds = engageCol.removeFromTop (40.0f).withTrimmedLeft (20.0f).toNearestInt();

        // Diode SOUS le bouton ON/OFF (au lieu d'a cote du titre) --
        // reservee en bas de la colonne AVANT de centrer le bouton dans
        // l'espace restant.
        auto ledRow = engageCol.removeFromBottom (14.0f);
        ledCentre = { engageCol.getCentreX(), ledRow.getCentreY() };

        const float btnSize = juce::jmin (56.0f, engageCol.getWidth(), engageCol.getHeight());
        engageSwitch.setBounds (juce::Rectangle<float> (btnSize, btnSize).withCentre (engageCol.getCentre()).toNearestInt());

        content.removeFromLeft (10.0f);

        // 3 aiguilles, chacune prenant TOUTE la hauteur restante de la
        // plaque (demande explicite) -- NeedleVU centre son propre cadran
        // circulaire dans la plus petite des deux dimensions, la largeur
        // excedentaire devient simplement une marge laterale, pas de
        // distorsion.
        constexpr float bandGap = 16.0f;
        const float bandW = (content.getWidth() - bandGap * 2.0f) / 3.0f;
        auto layoutNeedle = [&] (NeedleVU& needle)
        {
            needle.setBounds (content.removeFromLeft (bandW).toNearestInt());
        };
        layoutNeedle (lowNeedle);
        content.removeFromLeft (bandGap);
        layoutNeedle (midNeedle);
        content.removeFromLeft (bandGap);
        layoutNeedle (highNeedle);
    }

    void paint (juce::Graphics& g) override
    {
        const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();

        // Logo "THD" (LogoThd.png) -- ajuste par CONTIENT (aspect ratio
        // preserve). Reste a pleine opacite meme hors tension, comme les
        // autres logos (LOW/MID/HIGH/SUPPRESSOR) qui ne s'assombrissent
        // jamais avec le Power -- demande explicite.
        if (auto img = juce::ImageCache::getFromMemory (BinaryData::LogoThd_png, BinaryData::LogoThd_pngSize); ! img.isNull())
        {
            const float scale = juce::jmin ((float) titleBounds.getWidth() / (float) img.getWidth(),
                                             (float) titleBounds.getHeight() / (float) img.getHeight());
            const int w = juce::roundToInt (img.getWidth() * scale);
            const int h = juce::roundToInt (img.getHeight() * scale);
            auto dest = juce::Rectangle<int> (w, h).withX (titleBounds.getX()).withCentre ({ titleBounds.getX() + w / 2, titleBounds.getCentreY() });
            g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(), 0, 0, img.getWidth(), img.getHeight());
        }

        // --- Diode d'etat (seul indicateur ON/OFF, pas de rideau) ---
        const bool engaged = softClipOnParam.load() > 0.5f;
        const float ledDiameter = 8.0f;
        auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter).withCentre (ledCentre);
        const bool ledLit = powered && engaged;
        g.setColour (ledLit ? SuppressorColours::ledGreen : SuppressorColours::ledGreen.interpolatedWith (juce::Colours::black, 0.85f));
        g.fillEllipse (ledBounds);
    }

private:
    // Repaint uniquement quand l'etat engage/desengage change -- la diode
    // est le seul element de CE module qui bouge (les aiguilles se
    // repeignent elles-memes via leur propre timer NeedleVU).
    void timerCallback() override
    {
        const bool engagedNow = softClipOnParam.load() > 0.5f;
        if (engagedNow != lastPaintedEngaged)
        {
            lastPaintedEngaged = engagedNow;
            repaint();
        }
    }

    std::atomic<float>& softClipOnParam;
    bool lastPaintedEngaged = false;

    NeedleVU lowNeedle, midNeedle, highNeedle;

    juce::Rectangle<int> titleBounds;
    juce::Point<float> ledCentre;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdValveModule)
};
