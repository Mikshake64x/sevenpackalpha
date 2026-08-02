#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    DetectorFilterGraph
    --------------------
    Cadre X/Y sous DETECTOR/AUDIO : point en croix draggable, courbe de
    correction reelle, encart frequence/gain, animation pulse.

    DESORMAIS BRANCHE SUR DE VRAIS PARAMETRES (plus un prototype visuel) :
    - HP (vert)   : X = detectorHpFreq (20-2000Hz), Y = detectorHpSlope
      (1-8, x6dB/octave -- glisser vers le haut augmente la pente
      jusqu'a 48dB/oct, comme demande).
    - BELL (ambre): X = detectorBellFreq (20-2000Hz), Y = detectorBellGain
      (0-18dB).

    Chaque parametre continu recoit un juce::RangedAudioParameter& (pour
    convertTo0to1 lors de l'ecriture -- gestes propres avec begin/end
    change gesture) ; la lecture se fait directement sur la valeur reelle
    (Hz/dB/steps) via les atomics, reconvertie en position ecran avec la
    MEME formule log que le trace, pour un suivi 1:1 sans a-coups.

    PLUS d'analyse spectrale (FFT) dans ce cadre -- retiree pour
    economiser les ressources : seuls les points de sidechain (HP/BELL,
    crosshair + courbe de correction) restent affiches.
*/
class DetectorFilterGraph : public juce::Component, private juce::Timer
{
public:
    // isLowPassIn : bande HIGH -- le point HP devient LP (meme croix XY,
    // meme pente variable, juste le type de filtre qui change, voir
    // FeedbackCompressor::setDetectorHpIsLowPass()).
    // viewMinHzParam/viewMaxHzParam : bornes de l'axe X, ADAPTEES EN TEMPS
    // REEL a la bande (LOW: 20->XLO, MID: XLO->XHI, HIGH: XHI->20000) --
    // voir SuppressorAudioProcessorEditor::buildBandPanel(). Ce sont les
    // memes atomics que les knobs XLO/XHI de la barre de controle, pas une
    // copie -- suit donc les Xover en direct, meme quand ils bougent via
    // automation hote.
    // bell2* : OPTIONNELS (nullptr par defaut) -- quand fournis, active un
    // 2e point Bell independant (meme mecanique X=freq/Y=gain, couleur
    // bleue) EN PLUS du point HP -- utilise UNIQUEMENT par la bande MID
    // (voir PluginEditor::buildBandPanel()), qui n'a pas de bouton HP visible.
    DetectorFilterGraph (std::atomic<float>& hpOnParamToWatch, juce::RangedAudioParameter& hpFreqP, juce::RangedAudioParameter& hpSlopeP,
                         std::atomic<float>& bellOnParamToWatch, juce::RangedAudioParameter& bellFreqP, juce::RangedAudioParameter& bellGainP,
                         bool isLowPassIn, std::atomic<float>& viewMinHzParam, std::atomic<float>& viewMaxHzParam,
                         std::atomic<float>* bell2OnParamToWatch = nullptr, juce::RangedAudioParameter* bell2FreqP = nullptr,
                         juce::RangedAudioParameter* bell2GainP = nullptr)
        : hpOnParam (hpOnParamToWatch), hpFreqParam (hpFreqP), hpSlopeParam (hpSlopeP),
          bellOnParam (bellOnParamToWatch), bellFreqParam (bellFreqP), bellGainParam (bellGainP),
          isLowPass (isLowPassIn), viewMinHz (viewMinHzParam), viewMaxHz (viewMaxHzParam),
          bell2OnParam (bell2OnParamToWatch), bell2FreqParam (bell2FreqP), bell2GainParam (bell2GainP)
    {
        updateViewRange();
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a1620));
        g.fillRoundedRectangle (bounds, 3.0f);

        const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();
        if (! powered)
        {
            g.setColour (juce::Colour (0xff2a3540));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
            return; // ecran eteint -- comme debranche, aucun contenu
        }

        auto plot = bounds.reduced (4.0f);
        const bool hpOn = hpOnParam.load() > 0.5f;
        const bool bellOn = bellOnParam.load() > 0.5f;
        const bool bell2On = hasBell2() && bell2OnParam->load() > 0.5f;

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (bounds, 3.0f);
        g.reduceClipRegion (clip);

        if (hpOn)
        {
            drawHpCorrectionCurve (g, plot);
            hpNodeCentre = hpNodeScreenPos (plot);
            drawCrosshairNode (g, plot, hpNodeCentre, SuppressorColours::ledGreen, hpPulse);
        }

        if (bellOn)
        {
            drawBellCorrectionCurve (g, plot, bellFreqParam, bellGainParam);
            bellNodeCentre = bellNodeScreenPos (plot, bellFreqParam, bellGainParam);
            drawCrosshairNode (g, plot, bellNodeCentre, SuppressorColours::ledAmber, bellPulse);
        }

        if (bell2On)
        {
            drawBellCorrectionCurve (g, plot, *bell2FreqParam, *bell2GainParam);
            bell2NodeCentre = bellNodeScreenPos (plot, *bell2FreqParam, *bell2GainParam);
            drawCrosshairNode (g, plot, bell2NodeCentre, SuppressorColours::ledBlue, bell2Pulse);
        }

        g.restoreState();

        if (readoutFade > 0.01f && draggingNode != None)
            drawReadout (g, plot);

        if (! hpOn && ! bellOn && ! bell2On)
        {
            g.setColour (SuppressorColours::textBright.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawFittedText (hasBell2() ? "BELL / BELL 2" : (isLowPass ? "LP / BELL" : "HP / BELL"),
                               bounds.toNearestInt(), juce::Justification::centred, 1);
        }

        g.setColour (juce::Colour (0xff2a3540));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto plot = getLocalBounds().toFloat().reduced (4.0f);
        const bool hpOn = hpOnParam.load() > 0.5f;
        const bool bellOn = bellOnParam.load() > 0.5f;
        const bool bell2On = hasBell2() && bell2OnParam->load() > 0.5f;

        const float dHp    = hpOn    ? hpNodeScreenPos (plot).getDistanceFrom (e.position) : 1.0e6f;
        const float dBell  = bellOn  ? bellNodeScreenPos (plot, bellFreqParam, bellGainParam).getDistanceFrom (e.position) : 1.0e6f;
        const float dBell2 = bell2On ? bellNodeScreenPos (plot, *bell2FreqParam, *bell2GainParam).getDistanceFrom (e.position) : 1.0e6f;

        if (dHp <= dBell && dHp <= dBell2 && hpOn) draggingNode = Hp;
        else if (dBell <= dBell2 && bellOn) draggingNode = Bell;
        else if (bell2On) draggingNode = Bell2;
        else draggingNode = None;

        if (draggingNode == Hp)    { hpFreqParam.beginChangeGesture();    hpSlopeParam.beginChangeGesture(); }
        if (draggingNode == Bell)  { bellFreqParam.beginChangeGesture();  bellGainParam.beginChangeGesture(); }
        if (draggingNode == Bell2) { bell2FreqParam->beginChangeGesture(); bell2GainParam->beginChangeGesture(); }

        readoutFade = 1.0f;
        dragTo (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        readoutFade = 1.0f;
        dragTo (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingNode == Hp)    { hpFreqParam.endChangeGesture();    hpSlopeParam.endChangeGesture(); }
        if (draggingNode == Bell)  { bellFreqParam.endChangeGesture();  bellGainParam.endChangeGesture(); }
        if (draggingNode == Bell2) { bell2FreqParam->endChangeGesture(); bell2GainParam->endChangeGesture(); }
        draggingNode = None;
    }

private:
    enum DragTarget { None, Hp, Bell, Bell2 };

    bool hasBell2() const noexcept { return bell2OnParam != nullptr; }

    // Bornes de l'axe X -- plus des constantes fixes, recalculees en
    // direct depuis viewMinHz/viewMaxHz (voir updateViewRange(), appelee
    // depuis timerCallback()) pour suivre les Xover en temps reel.
    void updateViewRange() noexcept
    {
        const float lo = juce::jmax (20.0f, viewMinHz.load());
        const float hi = juce::jmax (lo + 1.0f, viewMaxHz.load());
        logMin = std::log10 (lo);
        logMax = std::log10 (hi);
    }

    float xFracToFreq (float xt) const noexcept { return std::pow (10.0f, logMin + xt * (logMax - logMin)); }
    float freqToXFrac (float freq) const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (std::log10 (juce::jmax (20.0f, freq)) - logMin) / (logMax - logMin));
    }

    juce::Point<float> hpNodeScreenPos (juce::Rectangle<float> plot) const
    {
        const float freq = hpFreqParam.convertFrom0to1 (hpFreqParam.getValue());
        const float steps = hpSlopeParam.convertFrom0to1 (hpSlopeParam.getValue());
        const float xt = freqToXFrac (freq);
        const float yt = juce::jlimit (0.0f, 1.0f, (steps - 1.0f) / 7.0f);
        const float x = plot.getX() + xt * plot.getWidth();
        const float y = plot.getBottom() - yt * plot.getHeight();
        return { x, y };
    }

    juce::Point<float> bellNodeScreenPos (juce::Rectangle<float> plot, juce::RangedAudioParameter& freqP, juce::RangedAudioParameter& gainP) const
    {
        const float freq = freqP.convertFrom0to1 (freqP.getValue());
        const float gainDb = gainP.convertFrom0to1 (gainP.getValue());
        const float xt = freqToXFrac (freq);
        const float yt = juce::jlimit (0.0f, 1.0f, gainDb / 18.0f);
        const float x = plot.getX() + xt * plot.getWidth();
        const float y = plot.getBottom() - yt * plot.getHeight();
        return { x, y };
    }

    void drawHpCorrectionCurve (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        constexpr double fsDisplay = 48000.0;
        const float hpFreq = hpFreqParam.convertFrom0to1 (hpFreqParam.getValue());
        const int steps = (int) std::round (hpSlopeParam.convertFrom0to1 (hpSlopeParam.getValue()));

        juce::Path curve;
        const int numPoints = juce::jmax (2, (int) plot.getWidth() / 2);
        for (int i = 0; i < numPoints; ++i)
        {
            const float xt = (float) i / (float) (numPoints - 1);
            const float f = xFracToFreq (xt);

            // Reponse cumulee de la cascade (steps x 6dB/oct), meme calcul
            // que SidechainEQ : numBiquads x 12dB (2nd ordre) + 1 pole
            // (6dB) optionnel si steps est impair.
            double magDb = 0.0;
            const int numBiquads = steps / 2;
            for (int b = 0; b < numBiquads; ++b)
            {
                auto c = isLowPass
                            ? juce::dsp::IIR::Coefficients<float>::makeLowPass ((float) fsDisplay, hpFreq)
                            : juce::dsp::IIR::Coefficients<float>::makeHighPass ((float) fsDisplay, hpFreq);
                magDb += juce::Decibels::gainToDecibels (c->getMagnitudeForFrequency (f, fsDisplay));
            }
            if (steps % 2 == 1)
            {
                auto c = isLowPass
                            ? juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass ((float) fsDisplay, hpFreq)
                            : juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass ((float) fsDisplay, hpFreq);
                magDb += juce::Decibels::gainToDecibels (c->getMagnitudeForFrequency (f, fsDisplay));
            }

            const float db = juce::jlimit (-30.0f, 0.0f, (float) magDb);
            const float x = plot.getX() + xt * plot.getWidth();
            const float y = plot.getBottom() - ((db + 30.0f) / 30.0f) * plot.getHeight();

            if (i == 0) curve.startNewSubPath (x, y);
            else curve.lineTo (x, y);
        }

        g.setColour (SuppressorColours::textBright.withAlpha (0.85f));
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawBellCorrectionCurve (juce::Graphics& g, juce::Rectangle<float> plot, juce::RangedAudioParameter& freqP, juce::RangedAudioParameter& gainP) const
    {
        constexpr double fsDisplay = 48000.0;
        const float freq = freqP.convertFrom0to1 (freqP.getValue());
        const float gainDb = gainP.convertFrom0to1 (gainP.getValue());
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter ((float) fsDisplay, juce::jlimit (20.0f, 20000.0f, freq), 1.4f, gainLin);

        juce::Path curve;
        const int numPoints = juce::jmax (2, (int) plot.getWidth() / 2);
        for (int i = 0; i < numPoints; ++i)
        {
            const float xt = (float) i / (float) (numPoints - 1);
            const float f = xFracToFreq (xt);
            const double mag = coeffs->getMagnitudeForFrequency (f, fsDisplay);
            const float db = juce::jlimit (0.0f, 18.0f, (float) juce::Decibels::gainToDecibels (mag));
            const float x = plot.getX() + xt * plot.getWidth();
            const float y = plot.getBottom() - (db / 18.0f) * plot.getHeight();

            if (i == 0) curve.startNewSubPath (x, y);
            else curve.lineTo (x, y);
        }

        const bool isSecondBell = (&freqP == bell2FreqParam);
        g.setColour ((isSecondBell ? SuppressorColours::ledBlue : SuppressorColours::ledAmber).withAlpha (0.6f));
        g.strokePath (curve, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawCrosshairNode (juce::Graphics& g, juce::Rectangle<float> plot, juce::Point<float> centre,
                             juce::Colour accent, float pulse) const
    {
        g.setColour (accent.withAlpha (0.28f + pulse * 0.25f));
        g.drawLine (centre.x, plot.getY(), centre.x, plot.getBottom(), 1.0f);
        g.drawLine (plot.getX(), centre.y, plot.getRight(), centre.y, 1.0f);

        const float radius = 4.0f + pulse * 3.0f;
        g.setColour (accent.withAlpha (0.12f + pulse * 0.18f));
        g.fillEllipse (juce::Rectangle<float> ((radius + 6.0f) * 2.0f, (radius + 6.0f) * 2.0f).withCentre (centre));

        g.setColour (accent.withAlpha (0.9f));
        g.drawEllipse (juce::Rectangle<float> ((radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f).withCentre (centre), 1.2f);

        juce::ColourGradient grad (juce::Colours::white.withAlpha (0.9f), centre.x - radius * 0.4f, centre.y - radius * 0.4f,
                                    accent, centre.x + radius * 0.3f, centre.y + radius * 0.3f, true);
        g.setGradientFill (grad);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    void drawReadout (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        juce::String text;

        if (draggingNode == Hp)
        {
            const float freq = hpFreqParam.convertFrom0to1 (hpFreqParam.getValue());
            const int steps = (int) std::round (hpSlopeParam.convertFrom0to1 (hpSlopeParam.getValue()));
            const juce::String freqText = freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (freq)) + " Hz";
            text = freqText + "  " + juce::String (steps * 6) + " dB/oct";
        }
        else if (draggingNode == Bell2)
        {
            const float freq = bell2FreqParam->convertFrom0to1 (bell2FreqParam->getValue());
            const float gainDb = bell2GainParam->convertFrom0to1 (bell2GainParam->getValue());
            const juce::String freqText = freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (freq)) + " Hz";
            text = freqText + "  +" + juce::String (gainDb, 1) + " dB";
        }
        else
        {
            const float freq = bellFreqParam.convertFrom0to1 (bellFreqParam.getValue());
            const float gainDb = bellGainParam.convertFrom0to1 (bellGainParam.getValue());
            const juce::String freqText = freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (freq)) + " Hz";
            text = freqText + "  +" + juce::String (gainDb, 1) + " dB";
        }

        juce::Font font (juce::FontOptions (10.0f, juce::Font::bold));
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        const float textW = ga.getBoundingBox (0, -1, true).getWidth();

        const auto nodeCentre = draggingNode == Hp ? hpNodeCentre : draggingNode == Bell2 ? bell2NodeCentre : bellNodeCentre;
        auto readoutBounds = juce::Rectangle<float> (textW + 10.0f, 14.0f).withCentre ({ nodeCentre.x, nodeCentre.y - 14.0f });
        readoutBounds = readoutBounds.constrainedWithin (plot);

        g.setColour (juce::Colour (0xff0a1620).withAlpha (0.9f * readoutFade));
        g.fillRoundedRectangle (readoutBounds, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (readoutFade));
        g.setFont (font);
        g.drawText (text, readoutBounds.toNearestInt(), juce::Justification::centred, false);
    }

    void dragTo (juce::Point<float> mousePos)
    {
        auto plot = getLocalBounds().toFloat().reduced (4.0f);
        if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f || draggingNode == None)
            return;

        const float xt = juce::jlimit (0.0f, 1.0f, (mousePos.x - plot.getX()) / plot.getWidth());
        const float yt = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - mousePos.y) / plot.getHeight());

        if (draggingNode == Hp)
        {
            const float freq = xFracToFreq (xt);
            const int steps = juce::jlimit (1, 8, 1 + (int) std::round (yt * 7.0f));

            hpFreqParam.setValueNotifyingHost (hpFreqParam.convertTo0to1 (freq));
            hpSlopeParam.setValueNotifyingHost (hpSlopeParam.convertTo0to1 ((float) steps));
            hpPulse = 1.0f;
        }
        else if (draggingNode == Bell)
        {
            const float freq = xFracToFreq (xt);
            const float gainDb = yt * 18.0f;

            bellFreqParam.setValueNotifyingHost (bellFreqParam.convertTo0to1 (freq));
            bellGainParam.setValueNotifyingHost (bellGainParam.convertTo0to1 (gainDb));
            bellPulse = 1.0f;
        }
        else if (draggingNode == Bell2)
        {
            const float freq = xFracToFreq (xt);
            const float gainDb = yt * 18.0f;

            bell2FreqParam->setValueNotifyingHost (bell2FreqParam->convertTo0to1 (freq));
            bell2GainParam->setValueNotifyingHost (bell2GainParam->convertTo0to1 (gainDb));
            bell2Pulse = 1.0f;
        }

        repaint();
    }

    void timerCallback() override
    {
        updateViewRange(); // suit XLO/XHI en temps reel

        hpPulse *= 0.82f; if (hpPulse < 0.01f) hpPulse = 0.0f;
        bellPulse *= 0.82f; if (bellPulse < 0.01f) bellPulse = 0.0f;
        bell2Pulse *= 0.82f; if (bell2Pulse < 0.01f) bell2Pulse = 0.0f;

        if (draggingNode == None)
        {
            readoutFade *= 0.88f;
            if (readoutFade < 0.01f) readoutFade = 0.0f;
        }

        repaint();
    }

    std::atomic<float>& hpOnParam;
    juce::RangedAudioParameter& hpFreqParam;
    juce::RangedAudioParameter& hpSlopeParam;
    std::atomic<float>& bellOnParam;
    juce::RangedAudioParameter& bellFreqParam;
    juce::RangedAudioParameter& bellGainParam;

    bool isLowPass = false; // bande HIGH : HP -> LP
    std::atomic<float>& viewMinHz; // bornes de l'axe X, suivies en direct
    std::atomic<float>& viewMaxHz; // (voir updateViewRange())
    float logMin = 1.301029996f; // log10(20), reecrit par updateViewRange()
    float logMax = 3.301029996f; // log10(2000), reecrit par updateViewRange()

    // 2e point Bell OPTIONNEL (voir constructeur) -- nullptr sur Low/High,
    // fournis uniquement pour Mid.
    std::atomic<float>* bell2OnParam = nullptr;
    juce::RangedAudioParameter* bell2FreqParam = nullptr;
    juce::RangedAudioParameter* bell2GainParam = nullptr;

    juce::Point<float> hpNodeCentre, bellNodeCentre, bell2NodeCentre;
    float hpPulse = 0.0f, bellPulse = 0.0f, bell2Pulse = 0.0f;
    float readoutFade = 0.0f;
    DragTarget draggingNode = None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetectorFilterGraph)
};
