#include "SuppressorLookAndFeel.h"

SuppressorLookAndFeel::SuppressorLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, SuppressorColours::textBright);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, SuppressorColours::textDim);
}

// ---------------------------------------------------------------------
// Knob "anneau metal" : anneau brosse portant les chiffres 0-10 (qui
// tournent avec le knob, comme sur le hardware de reference), double
// bezel noir en creux, capuchon brosse central avec reflet glossy
// mobile. Un repere FIXE (petit trait clair) est imprime sur le panneau
// juste au-dessus du knob : le chiffre aligne avec ce repere indique la
// valeur courante.
// ---------------------------------------------------------------------
void SuppressorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float rotaryStartAngle,
                                               float rotaryEndAngle, juce::Slider& slider)
{
    using namespace juce;

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = diameter * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float R = radius * 0.92f;

    // -- 1. Ombre portee (fixe, ne tourne pas) --
    for (int i = 8; i > 0; i -= 2)
    {
        const float a = 0.06f * (1.0f - i / 8.0f);
        g.setColour (Colours::black.withAlpha (a));
        g.fillEllipse (centre.x - R - i, centre.y - R - i + 4.0f, (R + i) * 2.0f, (R + i) * 2.0f);
    }

    // -- 2. TOUT le corps du knob (bague noire + graduations + face creme)
    // tourne ensemble, comme UN SEUL bloc solidaire -- pas de cap separe
    // dans une bague fixe. Seul le repere du panneau (etape 4) et le LCD
    // (etape 5) restent fixes. --
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

    // Bague noire texturee -- teinte tres legere si le slider porte les
    // proprietes "knobAccentAtMin"/"knobAccentAtMax" (voir
    // SuppressorLookAndFeel::setKnobAccentGradient(), utilise seulement par
    // les knobs Xover) : le MELANGE des 2 suit sliderPos (position REELLE
    // du knob dans sa plage), pas un degrade spatial fixe -- sinon
    // comportement d'origine inchange.
    juce::Colour ringTop (0xff33363a), ringMid (0xff17181a), ringBottom (0xff050506);
    auto& sliderProps = slider.getProperties();
    if (sliderProps.contains ("knobAccentAtMin") && sliderProps.contains ("knobAccentAtMax"))
    {
        const auto accentAtMin = juce::Colour::fromString (sliderProps["knobAccentAtMin"].toString());
        const auto accentAtMax = juce::Colour::fromString (sliderProps["knobAccentAtMax"].toString());
        const float t = juce::jlimit (0.0f, 1.0f, sliderPos);
        const auto blended = accentAtMin.interpolatedWith (accentAtMax, t);
        ringTop = ringTop.interpolatedWith (blended, 0.26f);
        ringMid = ringMid.interpolatedWith (blended, 0.18f);
        ringBottom = ringBottom.interpolatedWith (blended, 0.26f);
    }
    juce::ColourGradient ringGrad (ringTop, centre.x, centre.y - R, ringBottom, centre.x, centre.y + R, false);
    ringGrad.addColour (0.75, ringMid);
    g.setGradientFill (ringGrad);
    g.fillEllipse (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f);

    // Sous-graduations : 5 par intervalle entre deux graduations majeures
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    for (int k = 0; k < 10; ++k)
    {
        const float a0 = rotaryStartAngle + (float) k * 0.1f * (rotaryEndAngle - rotaryStartAngle);
        const float step = 0.1f * (rotaryEndAngle - rotaryStartAngle) / 6.0f;
        for (int m = 1; m <= 5; ++m)
        {
            const float a = a0 + (float) m * step;
            const auto p1 = centre.getPointOnCircumference (R * 0.84f, a);
            const auto p2 = centre.getPointOnCircumference (R * 0.94f, a);
            g.drawLine ({ p1, p2 }, 0.9f);
        }
    }

    // Graduations majeures 0..10, celle du milieu (5) plus grande
    for (int k = 0; k <= 10; ++k)
    {
        const float a = rotaryStartAngle + (float) k * 0.1f * (rotaryEndAngle - rotaryStartAngle);
        const bool isFive = (k == 5);
        const float innerR = isFive ? R * 0.70f : R * 0.78f;
        const auto p1 = centre.getPointOnCircumference (innerR, a);
        const auto p2 = centre.getPointOnCircumference (R * 0.96f, a);
        g.setColour (juce::Colours::white.withAlpha (isFive ? 0.88f : 0.65f));
        g.drawLine ({ p1, p2 }, isFive ? 2.8f : 1.8f);
    }

    // Face creme centrale (fait partie du meme bloc rotatif)
    const float capR = R * 0.62f;
    juce::ColourGradient capGrad (juce::Colour (0xfffbf6ea), centre.x - capR * 0.35f, centre.y - capR * 0.35f,
                                   juce::Colour (0xffa89970), centre.x + capR * 0.6f, centre.y + capR * 0.6f, true);
    capGrad.addColour (0.55, juce::Colour (0xffecE4cf));
    g.setGradientFill (capGrad);
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (juce::Colour (0xff786a44).withAlpha (0.8f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.2f);

    g.restoreState();

    // -- 3. Reflet glossy : reste FIXE par rapport a l'ecran (lumiere
    // ambiante fixe) -- on le contre-tourne pour qu'il glisse sur la
    // surface au lieu de tourner avec elle. Dessine hors du bloc rotatif. --
    {
        const float capR = R * 0.62f;
        g.saveState();
        juce::Path capClip;
        capClip.addEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
        g.reduceClipRegion (capClip);

        constexpr float lightAngle = -0.9f;
        const auto glintPos = centre.getPointOnCircumference (capR * 0.42f, lightAngle - angle);
        juce::ColourGradient glintGrad (juce::Colours::white.withAlpha (0.5f), glintPos.x, glintPos.y,
                                         juce::Colours::white.withAlpha (0.0f), glintPos.x, glintPos.y + capR * 0.7f, true);
        g.setGradientFill (glintGrad);
        g.fillEllipse (juce::Rectangle<float> (capR * 1.2f, capR * 0.8f).withCentre (glintPos));

        const auto glint2 = centre.getPointOnCircumference (capR * 0.5f, lightAngle - angle + juce::MathConstants<float>::pi);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillEllipse (juce::Rectangle<float> (capR * 0.7f, capR * 0.5f).withCentre (glint2));

        g.restoreState();
    }

    // -- 4. (repere fixe du panneau retire sur demande -- les graduations
    // qui tournent + le LCD suffisent) --

    // -- 5. Afficheur LCD au centre : FIXE, ne tourne JAMAIS -- valeur sur
    // une ligne, unite (dB/ms) plus petite en dessous. Eteint (ecran noir,
    // pas de texte) si Power est off -- comme une machine debranchee. --
    {
        const float lcdR = R * 0.62f * 0.62f; // meme proportion que l'ancienne face LCD (0.62 de capR)
        g.setColour (juce::Colour (0xff0a1620));
        g.fillEllipse (centre.x - lcdR, centre.y - lcdR, lcdR * 2.0f, lcdR * 2.0f);
        g.setColour (juce::Colour (0xff05262e).withAlpha (0.9f));
        g.drawEllipse (centre.x - lcdR, centre.y - lcdR, lcdR * 2.0f, lcdR * 2.0f, 1.0f);

        if (powered)
        {
            // Au-dela de 100 (ms ou dB), plus de dixiemes/centiemes -- lisible
            // d'un coup d'oeil sur les valeurs de Release notamment.
            const double rawValue = slider.getValue();
            const int decimalsToShow = (std::abs (rawValue) >= 100.0) ? 0 : slider.getNumDecimalPlacesToDisplay();
            const juce::String numberText = juce::String (rawValue, decimalsToShow);
            const juce::String unitText = slider.getTextValueSuffix().trim();

            auto lcdArea = juce::Rectangle<float> (lcdR * 1.9f, lcdR * 1.7f).withCentre (centre);
            auto numberArea = lcdArea.removeFromTop (lcdArea.getHeight() * 0.5f);
            auto unitArea = lcdArea;

            g.setColour (SuppressorColours::lcdText);
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, lcdR * 0.5f), juce::Font::bold)));
            g.drawFittedText (numberText, numberArea.toNearestInt(), juce::Justification::centred, 1);

            // Unite (dB/ms) : encore agrandie (2e demande) -- doit se lire
            // aussi facilement que le chiffre.
            g.setColour (SuppressorColours::lcdText.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (12.0f, lcdR * 0.55f), juce::Font::bold)));
            g.drawFittedText (unitText, unitArea.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    // Knob physique : bague/graduations/capuchon restent visibles hors
    // tension -- seul l'ecran LCD (etape 5 ci-dessus) s'eteint.
}

juce::Font SuppressorLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.06f);
}

void SuppressorLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isColourSpecified (juce::Label::backgroundColourId))
    {
        auto bg = label.findColour (juce::Label::backgroundColourId);
        if (bg != juce::Colours::transparentBlack)
        {
            auto bounds = label.getLocalBounds().toFloat();
            g.setColour (bg);
            g.fillRoundedRectangle (bounds, 6.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
        }
    }

    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());
    g.drawFittedText (label.getText().toUpperCase(), label.getLocalBounds(),
                       label.getJustificationType(), 1);
}

void SuppressorLookAndFeel::renderAnthraciteBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::ColourGradient grad (juce::Colour (0xff121417), bounds.getX(), bounds.getY(),
                                juce::Colour (0xff1a1d21), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (bounds);

    juce::ColourGradient ambient (SuppressorColours::ledBlue.withAlpha (0.04f),
                                   bounds.getCentreX(), bounds.getY(),
                                   juce::Colours::transparentBlack,
                                   bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.6f, true);
    g.setGradientFill (ambient);
    g.fillRect (bounds);
}

void SuppressorLookAndFeel::renderMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Noir metal (facade du vrai Distressor), pas le gris acier clair
    // d'avant -- degrade toujours present pour le relief brosse, juste
    // beaucoup plus sombre.
    juce::ColourGradient grad (juce::Colour (0xff202224), bounds.getX(), bounds.getY(),
                                juce::Colour (0xff0a0b0d), bounds.getX(), bounds.getBottom(), false);
    grad.addColour (0.5, juce::Colour (0xff151618));
    g.setGradientFill (grad);
    g.fillRect (bounds);

    g.saveState();
    g.reduceClipRegion (bounds.toNearestInt());
    for (float y = bounds.getY(); y < bounds.getBottom(); y += 20.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawLine (bounds.getX(), y, bounds.getRight(), y, 0.5f);
    }
    g.restoreState();
}

void SuppressorLookAndFeel::renderDividerLine (juce::Graphics& g, float x, float top, float bottom)
{
    g.setColour (juce::Colour (0xffe8e8e6).withAlpha (0.2f));
    g.drawLine (x, top, x, bottom, 1.0f);
}


void SuppressorLookAndFeel::renderGlassPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    juce::ColourGradient grad (SuppressorColours::panelLight.withAlpha (0.92f), bounds.getX(), bounds.getY(),
                                SuppressorColours::panel.withAlpha (0.92f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, cornerRadius);
}

void SuppressorLookAndFeel::renderGlossyWhitePanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    // Base plastique moulee : degrade vertical blanc casse -> gris clair.
    juce::ColourGradient base (juce::Colour (0xfff6f6f4), bounds.getX(), bounds.getY(),
                                juce::Colour (0xffd4d4d0), bounds.getX(), bounds.getBottom(), false);
    base.addColour (0.5, juce::Colour (0xffeaeae7));
    g.setGradientFill (base);
    g.fillRoundedRectangle (bounds, cornerRadius);

    // Reflet glossy diagonal (haut du capot), fondu vers le bas.
    auto hlArea = bounds.reduced (bounds.getWidth() * 0.03f, bounds.getHeight() * 0.08f)
                        .removeFromTop (bounds.getHeight() * 0.4f);
    juce::Path highlight;
    highlight.addRoundedRectangle (hlArea, cornerRadius * 0.7f);
    juce::ColourGradient hlGrad (juce::Colours::white.withAlpha (0.65f), hlArea.getX(), hlArea.getY(),
                                  juce::Colours::white.withAlpha (0.0f), hlArea.getX(), hlArea.getBottom(), false);
    g.setGradientFill (hlGrad);
    g.fillPath (highlight);

    // Fine ombre basse + liseret clair -- profondeur 3D.
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (1.6f), cornerRadius - 1.0f, 1.0f);
}

void SuppressorLookAndFeel::renderGroupOutline (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                                                 const juce::String& labelText)
{
    g.setColour (SuppressorColours::textDim.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

    if (labelText.isNotEmpty())
    {
        juce::Font f (juce::FontOptions (10.0f, juce::Font::bold));
        f = f.withExtraKerningFactor (0.14f);
        g.setFont (f);

        juce::GlyphArrangement ga;
        ga.addLineOfText (f, labelText, 0.0f, 0.0f);
        const float textW = juce::jmin (bounds.getWidth() - 16.0f, ga.getBoundingBox (0, -1, true).getWidth() + 12.0f);
        auto labelArea = juce::Rectangle<float> (textW, 14.0f).withCentre ({ bounds.getCentreX(), bounds.getY() });

        g.setColour (SuppressorColours::bg);
        g.fillRect (labelArea);
        g.setColour (SuppressorColours::textBright);
        g.drawFittedText (labelText, labelArea.toNearestInt(), juce::Justification::centred, 1);
    }
}

void SuppressorLookAndFeel::renderBrushedMetal (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::ColourGradient grad (juce::Colour (0xff4a4d52), bounds.getX(), bounds.getY(),
                                juce::Colour (0xff2a2c30), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (bounds);

    for (float yy = bounds.getY(); yy < bounds.getBottom(); yy += 2.0f)
    {
        const float alpha = 0.02f + 0.02f * std::abs (std::sin (yy * 0.7f));
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawLine (bounds.getX(), yy, bounds.getRight(), yy, 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawRect (bounds, 1.0f);
}

void SuppressorLookAndFeel::renderGlossySheen (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::Path sheen;
    sheen.startNewSubPath (bounds.getX() - bounds.getWidth() * 0.1f, bounds.getY() + bounds.getHeight() * 0.04f);
    sheen.lineTo (bounds.getX() + bounds.getWidth() * 0.62f, bounds.getY() - bounds.getHeight() * 0.08f);
    sheen.lineTo (bounds.getX() + bounds.getWidth() * 0.88f, bounds.getY() - bounds.getHeight() * 0.08f);
    sheen.lineTo (bounds.getX() + bounds.getWidth() * 0.18f, bounds.getY() + bounds.getHeight() * 0.58f);
    sheen.closeSubPath();

    juce::ColourGradient sheenGrad (juce::Colours::white.withAlpha (0.06f),
                                     bounds.getX(), bounds.getY(),
                                     juce::Colours::transparentWhite,
                                     bounds.getX(), bounds.getY() + bounds.getHeight() * 0.6f, false);
    g.setGradientFill (sheenGrad);
    g.fillPath (sheen);

    g.setColour (juce::Colours::white.withAlpha (0.015f));
    g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getBottom() - bounds.getHeight() * 0.16f,
                                          bounds.getWidth(), bounds.getHeight() * 0.16f));
}

void SuppressorLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const float angle = std::fmod (centre.x * 0.37f + centre.y * 0.61f, juce::MathConstants<float>::pi);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.4f, radius * 2.4f).withCentre (centre));

    juce::ColourGradient headGrad (juce::Colour (0xff6a6e74), centre.x - radius * 0.4f, centre.y - radius * 0.4f,
                                    juce::Colour (0xff222428), centre.x + radius * 0.5f, centre.y + radius * 0.55f, true);
    headGrad.addColour (0.6, juce::Colour (0xff45484d));
    g.setGradientFill (headGrad);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 0.8f);

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.45f)
                       .withCentre (centre.translated (-radius * 0.3f, -radius * 0.32f)));

    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRect (juce::Rectangle<float> (radius * 1.3f, radius * 0.22f).withCentre (centre));
    g.fillRect (juce::Rectangle<float> (radius * 0.22f, radius * 1.3f).withCentre (centre));
    g.restoreState();
}

void SuppressorLookAndFeel::renderCornerScrews (juce::Graphics& g, juce::Rectangle<float> bounds, float inset, float screwRadius)
{
    if (bounds.getWidth() < inset * 3.0f || bounds.getHeight() < inset * 3.0f)
        return;

    const juce::Point<float> corners[4] =
    {
        { bounds.getX() + inset,     bounds.getY() + inset },
        { bounds.getRight() - inset, bounds.getY() + inset },
        { bounds.getX() + inset,     bounds.getBottom() - inset },
        { bounds.getRight() - inset, bounds.getBottom() - inset }
    };

    for (auto& c : corners)
        drawScrew (g, c, screwRadius);
}

void SuppressorLookAndFeel::renderKnobSocket (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Discret : juste une fine rainure en creux au ras du knob (pas de
    // disque degrade ni de halo d'ombre etendu -- ca faisait trop lourd).
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawEllipse (centre.x - radius - 1.2f, centre.y - radius - 1.2f, (radius + 1.2f) * 2.0f, (radius + 1.2f) * 2.0f, 0.8f);
}

juce::Colour SuppressorLookAndFeel::ledGradientColour (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f,  SuppressorColours::ledGreen  },
        { 0.35f, SuppressorColours::ledGreen  },
        { 0.6f,  SuppressorColours::ledYellow },
        { 0.8f,  SuppressorColours::ledOrange },
        { 1.0f,  SuppressorColours::ledRed    }
    };

    t = juce::jlimit (0.0f, 1.0f, t);
    for (int i = 0; i < 4; ++i)
    {
        if (t <= stops[i + 1].pos)
        {
            const float local = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
            return stops[i].colour.interpolatedWith (stops[i + 1].colour, local);
        }
    }
    return stops[4].colour;
}

juce::Colour SuppressorLookAndFeel::ledGradientColourExpand (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f, SuppressorColours::ledBlue   },
        { 0.5f, SuppressorColours::ledCyan   },
        { 1.0f, SuppressorColours::ledPurple }
    };

    t = juce::jlimit (0.0f, 1.0f, t);
    for (int i = 0; i < 2; ++i)
    {
        if (t <= stops[i + 1].pos)
        {
            const float local = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
            return stops[i].colour.interpolatedWith (stops[i + 1].colour, local);
        }
    }
    return stops[2].colour;
}

juce::Colour SuppressorLookAndFeel::ratioLedColour (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f,  SuppressorColours::ledCyan   },
        { 0.25f, SuppressorColours::ledGreen  },
        { 0.5f,  SuppressorColours::ledYellow },
        { 0.75f, SuppressorColours::ledOrange },
        { 1.0f,  SuppressorColours::ledRed    }
    };

    t = juce::jlimit (0.0f, 1.0f, t);
    for (int i = 0; i < 4; ++i)
    {
        if (t <= stops[i + 1].pos)
        {
            const float local = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
            return stops[i].colour.interpolatedWith (stops[i + 1].colour, local);
        }
    }
    return stops[4].colour;
}

float SuppressorLookAndFeel::meterDisplayRatio (float db, float minDb, float maxDb)
{
    db = juce::jlimit (minDb, maxDb, db);
    return (db - minDb) / (maxDb - minDb);
}

void SuppressorLookAndFeel::drawGlossyDot (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, bool lit)
{
    if (! lit)
    {
        g.setColour (SuppressorColours::ledOff);
        g.fillEllipse (bounds);
        return;
    }

    const auto centre = bounds.getCentre();
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    g.setColour (colour.withAlpha (0.18f));
    g.fillEllipse (bounds.expanded (r * 0.22f));

    juce::ColourGradient grad (colour.interpolatedWith (juce::Colours::white, 0.55f),
                                centre.x - r * 0.3f, centre.y - r * 0.35f,
                                colour.interpolatedWith (juce::Colours::black, 0.45f),
                                centre.x + r * 0.6f, centre.y + r * 0.6f, true);
    grad.addColour (0.35, colour);
    g.setGradientFill (grad);
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.fillEllipse (juce::Rectangle<float> (r * 0.55f, r * 0.34f)
                       .withCentre ({ centre.x - r * 0.32f, centre.y - r * 0.36f }));
}
