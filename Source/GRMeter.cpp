#include "GRMeter.h"

GRMeter::GRMeter (std::atomic<float>& grValueDbToWatch, std::atomic<float>& expandGrValueDbToWatch,
                    std::atomic<float>& powerParamToWatch, std::atomic<float>& aboveBelowParamToWatch)
    : grValueDb (grValueDbToWatch), expandGrValueDb (expandGrValueDbToWatch),
      powerParam (powerParamToWatch), aboveBelowParam (aboveBelowParamToWatch)
{
    startTimerHz (30);
}

GRMeter::~GRMeter()
{
    stopTimer();
}

void GRMeter::timerCallback()
{
    powered = powerParam.load() > 0.5f;
    displayedGrDb = juce::jlimit (0.0f, maxRangeDb, grValueDb.load());
    displayedExpandGrDb = juce::jlimit (0.0f, kExpandLedDb * (float) kNumExpandLeds, expandGrValueDb.load());
    isExpandMode = juce::roundToInt (aboveBelowParam.load()) == 1; // 1 = Expand pur

    if (std::abs (displayedGrDb - lastPaintedGrDb) > 0.02f
        || std::abs (displayedExpandGrDb - lastPaintedExpandGrDb) > 0.02f
        || powered != lastPaintedPowered || isExpandMode != lastPaintedIsExpand)
    {
        lastPaintedGrDb = displayedGrDb;
        lastPaintedExpandGrDb = displayedExpandGrDb;
        lastPaintedPowered = powered;
        lastPaintedIsExpand = isExpandMode;
        repaint();
    }
}

void GRMeter::resized() {}

float GRMeter::dbToFraction (float db) noexcept
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

void GRMeter::paint (juce::Graphics& g)
{
    // PAS de plaque/bezel separee : les diodes sont directement sur le
    // panneau principal. Rangee DENSE (36 diodes, comme le hardware de
    // reference/Nuke Mk1) pour un mouvement fluide, avec des reperes
    // chiffres EGALEMENT espaces (voir dbToFraction) plutot qu'a leur
    // position dB reelle.
    auto bounds = getLocalBounds().toFloat();

    // Rangee EXPAND dediee (demande explicite : "au lieu de mettre les
    // chiffres, je prefererais une autre bande de diode qui affiche
    // l'expander en temps reel, une diode = +0.1dB d'expansion") -- fine
    // bande TOUJOURS visible au-dessus des diodes principales, alimentee
    // par un compteur SEPARE (voir GRMeter.h) qui ne recoit que le boost,
    // jamais la reduction -- reste lisible independamment du mode
    // d'affichage (mirroir ou non) de la rangee principale ci-dessous.
    // Prelevee AVANT le split ledArea/scaleArea habituel -- ce dernier
    // garde donc EXACTEMENT ses proportions (68/32) d'origine sur le
    // reste, le composant a juste ete agrandi en haut (voir
    // layoutBandRow()) pour l'accueillir. Hauteur portee a 9px (etait 6) :
    // les diodes Expand font maintenant la MEME taille que celles de la
    // rangee compression (demande explicite), donc plus de place requise.
    auto expandRowArea = bounds.removeFromTop (9.0f);
    auto ledArea = bounds.removeFromTop (bounds.getHeight() * 0.68f);
    auto scaleArea = bounds;

    // --- Rangee EXPAND : 0.1dB/diode, MEME taille/espacement que la
    // rangee compression ci-dessous (meme formule de diametre, meme gap,
    // meme nombre de diodes -- demande explicite : "les diodes de
    // l'expandeur doivent etre de la meme taille que celle du
    // compresseur"), TOUJOURS a partir de la gauche (pas de mirroir ici --
    // une seule direction possible, "boost croissant"). Degrade DEDIE
    // bleu -> cyan -> violet (demande explicite : "un systeme de couleur
    // ... mais pas les memes que celle du gain reduction"), calcule sur la
    // position DANS LA RANGEE (pas sur la valeur dB) -- meme logique que
    // ledGradientColour ci-dessous pour la rangee principale. ---
    {
        constexpr int numExpandLeds = kNumExpandLeds;
        const float egap = 1.5f;
        const float eDiameter = juce::jmin (8.0f, (expandRowArea.getWidth() - egap * (numExpandLeds - 1)) / (float) numExpandLeds);
        const float eRowWidth = numExpandLeds * eDiameter + (numExpandLeds - 1) * egap;
        const float eStartX = expandRowArea.getX() + (expandRowArea.getWidth() - eRowWidth) * 0.5f;
        const int litExpandCount = powered ? juce::jlimit (0, numExpandLeds,
            (int) std::round (displayedExpandGrDb / kExpandLedDb)) : 0;

        for (int i = 0; i < numExpandLeds; ++i)
        {
            auto dot = juce::Rectangle<float> (eDiameter, eDiameter)
                           .withY (expandRowArea.getCentreY() - eDiameter * 0.5f)
                           .withX (eStartX + i * (eDiameter + egap));

            if (i < litExpandCount)
            {
                const float t = (float) i / (float) (numExpandLeds - 1);
                g.setColour (SuppressorLookAndFeel::ledGradientColourExpand (t));
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (SuppressorColours::ledOff);
                g.fillEllipse (dot);
            }
        }
    }

    constexpr int numLeds = 36;
    const float gap = 1.5f;
    const float ledDiameter = juce::jmin (8.0f, (ledArea.getWidth() - gap * (numLeds - 1)) / (float) numLeds);
    const float rowWidth = numLeds * ledDiameter + (numLeds - 1) * gap;
    const float startX = ledArea.getX() + (ledArea.getWidth() - rowWidth) * 0.5f;

    // NOTE SENS : 0dB (peu de compression) a DROITE, max (30dB) a GAUCHE.
    // Remplissage depuis la droite quand la compression augmente.
    // dbToFraction() applique la MEME echelle non-lineaire que les
    // reperes chiffres ci-dessous : la diode allumee correspond toujours
    // exactement a la valeur reelle de reduction, meme entre deux reperes.
    //
    // PLUS de mirroir en Expand (retour utilisateur : "quand on clique sur
    // expand le GR se retourne dans l'autre sens pour afficher la
    // compression ... il doit pas bouger le gain reduction de
    // compression") -- desormais Above (compression) tourne EN PERMANENCE
    // quel que soit Comp/Expand (voir FeedbackCompressor::AboveBelowMode),
    // cette rangee affiche donc TOUJOURS la meme chose (la reduction de
    // gain reelle) et n'a plus de raison de changer de sens au clic. La
    // rangee EXPAND dediee ci-dessus reste le seul indicateur du boost.
    const float fillRatio = dbToFraction (displayedGrDb);
    const int litCount = powered ? (int) std::round (fillRatio * numLeds) : 0;

    for (int i = 0; i < numLeds; ++i)
    {
        const int j = i;
        const bool lit = j >= (numLeds - litCount);
        const float t = 1.0f - (float) j / (float) (numLeds - 1);
        const juce::Colour ledColour = SuppressorLookAndFeel::ledGradientColour (t);

        auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter)
                              .withY (ledArea.getCentreY() - ledDiameter * 0.5f)
                              .withX (startX + i * (ledDiameter + gap));

        if (lit)
        {
            SuppressorLookAndFeel::drawGlossyDot (g, ledBounds, ledColour, true);
        }
        else
        {
            g.setColour (SuppressorColours::ledOff);
            g.fillEllipse (ledBounds);
            g.setColour (SuppressorColours::gridLine);
            g.drawEllipse (ledBounds, 0.4f);
        }
    }

    // --- Reperes chiffres : MEME formule dbToFraction() que le
    // remplissage ci-dessus -- garantit qu'une diode allumee correspond
    // toujours a la valeur reelle de reduction indiquee par le repere. Par
    // construction de l'echelle (voir GRMeter.h), ces reperes tombent a
    // intervalles EGAUX en pixels (0, -2, -4, -6, -8, -10, -15, -20, -25,
    // -30), meme si leurs valeurs dB ne sont pas lineaires. ---
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.setColour (SuppressorColours::textBright.withAlpha (0.78f));

    constexpr float labelW = 26.0f;
    for (float db : scaleMarksDb)
    {
        const int litCountAtMark = (int) std::round (dbToFraction (db) * numLeds);
        const int ledIndex = juce::jlimit (0, numLeds - 1, numLeds - litCountAtMark);
        const float xPos = startX + ledIndex * (ledDiameter + gap) + ledDiameter * 0.5f;
        const juce::String text = (db <= 0.0f) ? juce::String (0) : "-" + juce::String ((int) db);

        g.drawFittedText (text, juce::Rectangle<float> (xPos - labelW * 0.5f, scaleArea.getY(), labelW, scaleArea.getHeight()).toNearestInt(),
                           juce::Justification::centred, 1);
    }
}
