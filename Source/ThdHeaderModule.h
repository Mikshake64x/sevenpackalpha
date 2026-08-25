#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"
#include "ThdEngageSwitch.h"
#include "RackPushButton.h"

/**
    ThdHeaderModule
    ---------------
    Version COMPACTE du THD Valve, deplacee dans la barre de controle
    (retour utilisateur : "on va liberer le rack du bas ou il y a THD ...
    juste le on/off et les options du THD dans le header, a gauche de AI
    Learn et Analog/Linear") -- remplace l'ancien ThdValveModule (chassis
    pleine largeur + 3 aiguilles par bande, voir SixpackEditorContent::
    vuPlateBounds, desormais supprime). Garde uniquement l'actionneur ON/
    OFF (ThdEngageSwitch), le choix de mode SOFT/TAPE/IRON/AIR (4
    RackPushButton en radio exclusif -- meme style neumorphic "dark mono"
    que SOLO/SEL/LEARN, retour utilisateur : "on peut faire en
    neumorphic ... alors le A") et UNE mesure discrete (barre LED, dans un
    groove creuse, amplitude GLOBALE de saturation --
    SixpackAudioProcessor::clipMeterAmount -- pas 3 mesures par bande).
*/
class ThdHeaderModule : public juce::Component, private juce::Timer
{
public:
    explicit ThdHeaderModule (std::atomic<float>& softClipOnToWatch, std::atomic<float>& clipAmountToWatch)
        : softToggle ("SOFT", SixpackColours::ledGreen, false, &softClipOnToWatch),
          tapeToggle ("TAPE", SixpackColours::ledAmber, false, &softClipOnToWatch),
          ironToggle ("IRON", SixpackColours::ledRed,   false, &softClipOnToWatch),
          airToggle  ("AIR",  SixpackColours::ledCyan,  false, &softClipOnToWatch),
          softClipOn (softClipOnToWatch),
          clipAmount (clipAmountToWatch)
    {
        addAndMakeVisible (engageSwitch);
        // Voir RackPushButton::setHaloMargin() -- reduit ici (3px, pas le
        // defaut 6px) : la grille 2x2 est tres serree (rangees de 26px,
        // 3px de jeu entre elles), pas la place pour un halo aussi
        // genereux que SOLO/SEL sans deborder largement sur les voisins.
        engageSwitch.setHaloMargin (6.0f);

        // Meme mecanique que l'ancien ThdValveModule (voir sa doc) : un
        // vrai groupe radio JUCE, sinon cliquer le bouton deja actif le
        // fait basculer OFF cote JUCE avant que le callback externe (voir
        // PluginEditor::wireThdModeButton()) ne force le bon etat.
        constexpr int thdModeRadioGroupId = 9402;
        for (auto* t : { &softToggle, &tapeToggle, &ironToggle, &airToggle })
        {
            t->setRadioGroupId (thdModeRadioGroupId, juce::dontSendNotification);
            t->setHaloMargin (3.0f);
            addAndMakeVisible (*t);
        }

        startTimerHz (20); // rafraichit la barre LED (amplitude de saturation)
    }

    ~ThdHeaderModule() override { stopTimer(); }

    // Actionneur physique -- attache a idSoftClip via ButtonAttachment
    // dans PluginEditor (meme pattern que l'ancien ThdValveModule).
    ThdEngageSwitch engageSwitch;
    RackPushButton softToggle, tapeToggle, ironToggle, airToggle;

    void resized() override
    {
        auto content = getLocalBounds().toFloat();

        // Marge haute reservee (retour utilisateur : "le bouton on/off thd
        // fait coupe sur le dessus") -- l'interrupteur (ThdEngageSwitch)
        // a un halo qui doit deborder de sa pilule visuelle (voir
        // ThdEngageSwitch::setHaloMargin(), 6px, deja pris en compte dans
        // son .expanded(6.0f) ci-dessous) ; sans cette marge haute, il n'y
        // aurait nulle part ou ce halo puisse deborder sans depasser les
        // bounds de ThdHeaderModule lui-meme. Ce module est aussi decale
        // plus haut dans son bandeau (voir PluginEditor::resized(),
        // thdHeader) pour degager davantage d'air au-dessus.
        content.removeFromTop (6.0f);

        // Libelle "THD" deplace a DROITE de l'interrupteur (retour
        // utilisateur : "l'ecriture THD va a droite du bouton on/off, et
        // elle fait la meme taille et meme typo que power") -- switchArea
        // reduit lateralement pour aligner le bord gauche de l'interrupteur
        // sur celui de la grille de boutons ci-dessous.
        auto switchRow = content.removeFromTop (38.0f);
        auto switchArea = switchRow.reduced (10.0f, 0.0f);
        engageSwitch.setBounds (juce::Rectangle<float> (38.0f, 38.0f)
                                     .withCentre ({ switchArea.getX() + 19.0f, switchArea.getCentreY() })
                                     .expanded (6.0f)
                                     .toNearestInt());
        labelBounds = { switchArea.getX() + 38.0f + 8.0f, switchArea.getY(),
                        switchArea.getRight() - (switchArea.getX() + 38.0f + 8.0f), switchArea.getHeight() };

        content.removeFromTop (3.0f);

        // Grille 2x2 (et non 1x4) -- une seule rangee de 4 boutons dans
        // ~176px ne laisse qu'~20px de texte par bouton, "SOFT"/"TAPE"
        // se tronquent alors en "SO..."/"TA..." (verifie en capture). La
        // grille double la largeur utile de chaque bouton pour le meme
        // encombrement total. Marge laterale ajoutee (retour utilisateur :
        // "les boutons du THD sont un peu larges") -- la grille reste
        // centree, juste retrecie par rapport a la pleine largeur.
        //
        // BUG CORRIGE (retour utilisateur : "le vu metre doit etre en
        // dessous des boutons, pas de chevauchement" -- explique enfin
        // pourquoi rien ne semblait changer malgre les agrandissements
        // successifs) : l'ancien code faisait `auto gridArea =
        // content.reduced(10,0)`, une COPIE independante de content, puis
        // consommait row1/gap/row2 SUR gridArea -- content lui-meme n'etait
        // jamais raccourci d'autant, donc meterBounds ci-dessous demarrait
        // juste apres l'interrupteur, PILE sous SOFT/TAPE, cache par ces
        // boutons peu importe sa taille. Chaque ligne est maintenant
        // extraite DIRECTEMENT de content (qui retrecit correctement au
        // fur et a mesure), la marge laterale appliquee ligne par ligne.
        constexpr float gap = 3.0f;
        auto row1 = content.removeFromTop (26.0f).reduced (10.0f, 0.0f);
        content.removeFromTop (gap);
        auto row2 = content.removeFromTop (26.0f).reduced (10.0f, 0.0f);
        const float cellW = (row1.getWidth() - gap) * 0.5f;
        softToggle.setBounds (row1.removeFromLeft (cellW).expanded (3.0f).toNearestInt());
        row1.removeFromLeft (gap);
        tapeToggle.setBounds (row1.expanded (3.0f).toNearestInt());
        ironToggle.setBounds (row2.removeFromLeft (cellW).expanded (3.0f).toNearestInt());
        row2.removeFromLeft (gap);
        airToggle.setBounds (row2.expanded (3.0f).toNearestInt());

        // Espace agrandi avant le VU metre (retour utilisateur : "laisser
        // un peu d'espace entre les boutons et le vu metre" -- 3px collait
        // trop pres de la grille de boutons).
        content.removeFromTop (10.0f);
        // LEDs rehaussees 6->12 (retour utilisateur : "c'est trop petit et
        // il y a des collisions avec le vu metre" -- le groove neumorphic
        // (teinte quasi identique au fond en theme clair) etait devenu
        // pratiquement invisible en plus d'etre fin -- voir paint(), le
        // groove redevient un puits FIXE sombre, meme langage que
        // FaderVerticalLookAndFeel/GRMeter (un afficheur reste sombre quel
        // que soit le theme du chassis), plus le contraste depend jamais
        // de si la teinte de base devinee matche le panneau reel.
        meterBounds = content.reduced (12.0f, 0.0f).removeFromTop (12.0f);
    }

    void paint (juce::Graphics& g) override
    {
        // Libelle "THD" a droite de l'interrupteur (voir resized()) --
        // meme police/graisse/kerning que "POWER" (retour utilisateur :
        // "elle fait la meme taille et meme typo que power", voir
        // PluginEditor::paint()) via le meme helper drawEngravedText.
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        SixpackLookAndFeel::drawEngravedText (g, "THD", labelBounds.toNearestInt(), SixpackColours::textDim,
                                                juce::Justification::centredLeft, 1);

        // Puits FIXE sombre (retour utilisateur : "c'est trop petit et il
        // y a des collisions avec le vu metre" -- le groove neumorphic
        // teinte sur la base du theme devenait quasi invisible en theme
        // clair, sa teinte se fondant dans le panneau) -- meme langage que
        // les autres afficheurs du plugin (FaderVerticalLookAndFeel,
        // GRMeter) : un puits reste sombre et net quel que soit le theme
        // du chassis, jamais teinte.
        {
            auto groove = meterBounds.expanded (4.0f, 3.0f);
            const float grooveCorner = juce::jmin (6.0f, groove.getHeight() * 0.4f);
            g.setColour (juce::Colour (0xff08090a));
            g.fillRoundedRectangle (groove, grooveCorner);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (groove.reduced (0.5f), grooveCorner, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (groove.reduced (1.6f), grooveCorner - 1.0f, 1.0f);
        }

        // Rangee de LEDs -- amplitude GLOBALE de saturation (voir doc de
        // classe), degrade vert->ambre->rouge (meme helper que les autres
        // mesures de GR du plugin, voir SixpackLookAndFeel::
        // ledGradientColour()). AUCUNE diode allumee si le THD est
        // desengage (retour utilisateur : "le VU metre du THD ne doit
        // avoir aucune led allumee lorsqu'il ne fonctionne pas") -- amt
        // pouvait rester non nul (mesure residuelle) meme softclip coupe.
        // Utilise displayAmt (voir timerCallback()) -- deja purge des
        // valeurs figees -- PAS clipAmount.load() en direct ici (retour
        // utilisateur : "le vu metre reste fixe quand je stop").
        constexpr int numSegs = 14;
        const float amt = displayAmt;
        const float segW = (meterBounds.getWidth() - (float) (numSegs - 1) * 2.0f) / (float) numSegs;
        for (int i = 0; i < numSegs; ++i)
        {
            const float t = (float) i / (float) (numSegs - 1);
            const bool lit = amt * (float) numSegs > (float) i;
            g.setColour (lit ? SixpackLookAndFeel::ledGradientColour (t) : SixpackColours::ledOff);
            g.fillRoundedRectangle (meterBounds.getX() + (float) i * (segW + 2.0f), meterBounds.getY(), segW, meterBounds.getHeight(), 1.0f);
        }

        // Retour utilisateur : "clignotte comme un neon a l'extinction
        // seulement" -- cette barre s'eteint deja indirectement quand
        // POWER coupe (clipMeterAmount est remis a 0 cote processeur),
        // meme passe de flicker superposee que GRMeter/BandMetersRack.
        const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = laf == nullptr || laf->isPowered();
        const float mix = laf != nullptr ? laf->getPowerTransitionMix() : 1.0f;
        if (! powered && mix > 0.001f)
        {
            g.beginTransparencyLayer (mix);
            for (int i = 0; i < numSegs; ++i)
            {
                const float t = (float) i / (float) (numSegs - 1);
                g.setColour (SixpackLookAndFeel::ledGradientColour (t));
                g.fillRoundedRectangle (meterBounds.getX() + (float) i * (segW + 2.0f), meterBounds.getY(), segW, meterBounds.getHeight(), 1.0f);
            }
            g.endTransparencyLayer();
        }
    }

private:
    // Optimisation CPU : ne redessine que si l'amplitude affichee a
    // reellement bouge (meme principe que GRMeter::timerCallback()), OU si
    // l'etat engage/desengage a change -- sinon un flip de l'interrupteur
    // pendant que clipAmount reste au-dessus du seuil de 0.01 ne repeint
    // pas tout de suite (paint() lit bien softClipOn en direct, mais
    // encore faut-il declencher le repaint()).
    //
    // Retour utilisateur : "le vu metre THD reste fixe quand je stop" --
    // la ballistique de clipAmount (decay *0.995/echantillon, voir
    // SoftClipper::processSample()) ne peut decroitre QUE si processBlock()
    // continue d'etre appele ; si l'hote arrete completement de l'appeler
    // (transport stoppe...), clipAmount se fige a sa derniere valeur pour
    // de bon. Un filet de securite cote processeur existe deja
    // (SixpackAudioProcessor::timerCallback(), 250ms de staleness ->
    // remise a 0 des atomics), mais CE Timer-ci detecte aussi la staleness
    // LOCALEMENT (valeur brute inchangee >150ms) et decide alors seul
    // d'afficher 0 -- double filet, ne depend d'aucune hypothese sur le
    // comportement exact de l'hote.
    void timerCallback() override
    {
        const bool engaged = softClipOn.load() > 0.5f;
        float raw = engaged ? juce::jlimit (0.0f, 1.0f, clipAmount.load()) : 0.0f;

        // Zone morte (retour utilisateur : "il n'y a pas de son et la
        // diode reste allumee dans THD") -- le vrai bug n'etait pas un gel
        // du backend mais l'absence de seuil minimal : un bruit de fond
        // quasiment inaudible (dither, plancher de bruit de l'interface...)
        // suffit a regenerer en continu une cible non-nulle infime dans
        // SoftClipper::processSample(), qui n'atteint donc JAMAIS
        // exactement 0.0f -- et `amt*14 > 0` s'allume des le moindre residu
        // flottant. Sous ce seuil, on considere qu'il n'y a "pas de son".
        constexpr float deadZone = 0.02f;
        if (raw < deadZone)
            raw = 0.0f;

        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (raw != lastRawSeen)
        {
            lastRawSeen = raw;
            lastRawChangeMs = now;
        }
        const bool backendStale = (now - lastRawChangeMs) > 150;
        displayAmt = backendStale ? 0.0f : raw;

        // Retour utilisateur : "clignotte comme un neon a l'extinction
        // seulement" -- voir paint()/SixpackLookAndFeel::getPowerTransitionMix().
        const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
        const float flickerMix = laf != nullptr ? laf->getPowerTransitionMix() : 1.0f;

        if (std::abs (displayAmt - lastPaintedAmount) > 0.01f || engaged != lastPaintedEngaged
            || std::abs (flickerMix - lastPaintedFlickerMix) > 0.01f)
        {
            lastPaintedEngaged = engaged;
            lastPaintedAmount = displayAmt;
            lastPaintedFlickerMix = flickerMix;
            repaint();
        }
    }

    float lastPaintedAmount = -1.0f;
    bool lastPaintedEngaged = true;
    float lastPaintedFlickerMix = 1.0f;
    float displayAmt = 0.0f;
    float lastRawSeen = -1.0f;
    juce::uint32 lastRawChangeMs = 0;
    std::atomic<float>& softClipOn;
    std::atomic<float>& clipAmount;
    juce::Rectangle<float> meterBounds;
    juce::Rectangle<float> labelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdHeaderModule)
};
