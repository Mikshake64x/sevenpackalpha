#include "GRMeter.h"

GRMeter::GRMeter (std::atomic<float>& grValueDbToWatch, std::atomic<float>& powerParamToWatch)
    : grValueDb (grValueDbToWatch), powerParam (powerParamToWatch)
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

    if (std::abs (displayedGrDb - lastPaintedGrDb) > 0.02f || powered != lastPaintedPowered)
    {
        lastPaintedGrDb = displayedGrDb;
        lastPaintedPowered = powered;
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

    auto ledArea = bounds.removeFromTop (bounds.getHeight() * 0.68f);
    auto scaleArea = bounds;

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
    const float fillRatio = dbToFraction (displayedGrDb);
    const int litCount = powered ? (int) std::round (fillRatio * numLeds) : 0;

    for (int i = 0; i < numLeds; ++i)
    {
        const bool lit = i >= (numLeds - litCount);
        const float t = 1.0f - (float) i / (float) (numLeds - 1);
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
