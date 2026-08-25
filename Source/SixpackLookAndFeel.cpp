#include "SixpackLookAndFeel.h"

SixpackLookAndFeel::SixpackLookAndFeel()
{
    refreshFixedColours();
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

// Repousse les juce::Colour IDs fixes par le constructeur vers les valeurs
// COURANTES de SixpackColours -- appele une fois a la construction, puis
// a chaque bascule de theme (voir setLightTheme()) pour que les composants
// JUCE standard (juce::Label par defaut, etc.) qui lisent findColour() au
// lieu de peindre directement suivent le theme actif.
void SixpackLookAndFeel::refreshFixedColours()
{
    setColour (juce::Slider::textBoxTextColourId, SixpackColours::textBright);
    setColour (juce::Label::textColourId, SixpackColours::textDim);
}

// ---------------------------------------------------------------------
// Knob "anneau metal" : anneau brosse portant les chiffres 0-10 (qui
// tournent avec le knob, comme sur le hardware de reference), double
// bezel noir en creux, capuchon brosse central avec reflet glossy
// mobile. Un repere FIXE (petit trait clair) est imprime sur le panneau
// juste au-dessus du knob : le chiffre aligne avec ce repere indique la
// valeur courante.
// ---------------------------------------------------------------------
void SixpackLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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

    // -- 1. Halo d'assise + ombre portee (fixes, ne tournent pas) -- retour
    // utilisateur : "j'aime bien le B avec l'ombre portee" (variante B de
    // la preview Artifact "Reliefs de Knob"). Une 1ere tentative de fix
    // anti-troncature RETRECISSAIT le halo pour le faire tenir dans la
    // marge de clip du composant -- rejetee ("je preferais l'ombre
    // d'avant ... il faut la laisser par-dessus le tout") : la vraie
    // solution est knob.slider.setPaintingIsUnclipped (true) (voir
    // setupKnob() dans PluginEditor.cpp), qui laisse ce halo deborder
    // dans le parent au lieu d'etre coupe -- on peut donc garder la
    // taille/l'intensite d'origine ici sans contrainte de marge.
    {
        const float haloW = R * 1.7f, haloH = R * 0.5f;
        const juce::Point<float> haloCentre (centre.x, centre.y + R);
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (1.0f, haloH / haloW, haloCentre.x, haloCentre.y));
        juce::ColourGradient haloGrad (Colours::black.withAlpha (0.40f), haloCentre.x, haloCentre.y,
                                        Colours::transparentBlack, haloCentre.x + haloW * 0.5f, haloCentre.y, true);
        g.setGradientFill (haloGrad);
        g.fillEllipse (haloCentre.x - haloW * 0.5f, haloCentre.y - haloW * 0.5f, haloW, haloW);
        g.restoreState();
    }
    for (int i = 14; i > 0; i -= 2)
    {
        const float a = 0.09f * (1.0f - i / 14.0f);
        g.setColour (Colours::black.withAlpha (a));
        g.fillEllipse (centre.x - R - i, centre.y - R - i + 6.0f, (R + i) * 2.0f, (R + i) * 2.0f);
    }

    // -- 2. TOUT le corps du knob (bague noire + graduations + face creme)
    // tourne ensemble, comme UN SEUL bloc solidaire -- pas de cap separe
    // dans une bague fixe. Seul le repere du panneau (etape 4) et le LCD
    // (etape 5) restent fixes. --
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

    // Bague noire texturee -- teinte tres legere si le slider porte les
    // proprietes "knobAccentAtMin"/"knobAccentAtMax" (voir
    // SixpackLookAndFeel::setKnobAccentGradient(), utilise seulement par
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

    // Biseau de la bague (variante B, voir commentaire du halo ci-dessus) --
    // chanfrein clair en haut / sombre en bas, restreint a une fine bande
    // pres du bord exterieur (anneau outer\R -> 0.86*R), pour simuler un
    // bord metallique biseaute qui capte la lumiere ambiante. Tourne avec
    // le reste du bloc (bord physique du knob).
    {
        juce::Path ring;
        ring.addEllipse (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f);
        ring.addEllipse (centre.x - R * 0.86f, centre.y - R * 0.86f, R * 1.72f, R * 1.72f);
        ring.setUsingNonZeroWinding (false);
        g.saveState();
        g.reduceClipRegion (ring);
        juce::ColourGradient bevelGrad (Colours::white.withAlpha (0.22f), centre.x, centre.y - R,
                                         Colours::black.withAlpha (0.55f), centre.x, centre.y + R, false);
        bevelGrad.addColour (0.5, Colours::transparentBlack);
        g.setGradientFill (bevelGrad);
        g.fillRect (juce::Rectangle<float> (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f));
        g.restoreState();
    }

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

    // Dome accentue sur le capuchon -- meme technique de biseau que la
    // bague ci-dessus, pour renforcer l'effet convexe/bombe (variante B).
    {
        juce::Path capRing;
        capRing.addEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
        capRing.addEllipse (centre.x - capR * 0.82f, centre.y - capR * 0.82f, capR * 1.64f, capR * 1.64f);
        capRing.setUsingNonZeroWinding (false);
        g.saveState();
        g.reduceClipRegion (capRing);
        juce::ColourGradient capBevel (Colours::white.withAlpha (0.35f), centre.x, centre.y - capR,
                                        Colours::black.withAlpha (0.30f), centre.x, centre.y + capR, false);
        capBevel.addColour (0.5, Colours::transparentBlack);
        g.setGradientFill (capBevel);
        g.fillRect (juce::Rectangle<float> (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f));
        g.restoreState();
    }

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

            g.setColour (SixpackColours::lcdText);
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, lcdR * 0.5f), juce::Font::bold)));
            g.drawFittedText (numberText, numberArea.toNearestInt(), juce::Justification::centred, 1);

            // Unite (dB/ms) : encore agrandie (2e demande) -- doit se lire
            // aussi facilement que le chiffre.
            g.setColour (SixpackColours::lcdText.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (12.0f, lcdR * 0.55f), juce::Font::bold)));
            g.drawFittedText (unitText, unitArea.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    // Knob physique : bague/graduations/capuchon restent visibles hors
    // tension -- seul l'ecran LCD (etape 5 ci-dessus) s'eteint.
}

juce::Font SixpackLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.06f);
}

void SixpackLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
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

    // Effet "grave" (relief 3D) en theme CLAIR uniquement -- retour
    // utilisateur explicite : "sur le mode clair, les ecritures type
    // gravure de plaque sont noires... ajouter un effet grave" -- voir
    // drawEngravedText().
    g.setFont (label.getFont());
    drawEngravedText (g, label.getText().toUpperCase(), label.getLocalBounds(),
                       label.findColour (juce::Label::textColourId), label.getJustificationType());
}

void SixpackLookAndFeel::renderAnthraciteBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::ColourGradient grad (juce::Colour (0xff121417), bounds.getX(), bounds.getY(),
                                juce::Colour (0xff1a1d21), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (bounds);

    juce::ColourGradient ambient (SixpackColours::ledBlue.withAlpha (0.04f),
                                   bounds.getCentreX(), bounds.getY(),
                                   juce::Colours::transparentBlack,
                                   bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.6f, true);
    g.setGradientFill (ambient);
    g.fillRect (bounds);
}

void SixpackLookAndFeel::renderMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Theme CLAIR (retour utilisateur explicite, voir SixpackColours::
    // applyLightTheme()) : degrade "metal brosse argente" gris moyen au
    // lieu du noir metal d'origine -- meme relief brosse (traits blancs
    // fins), juste beaucoup plus clair.
    if (SixpackLookAndFeel::isLightThemeStatic())
    {
        juce::ColourGradient grad (SixpackColours::lightPanelGradTop, bounds.getX(), bounds.getY(),
                                    SixpackColours::lightPanelGradBottom, bounds.getX(), bounds.getBottom(), false);
        grad.addColour (0.5, SixpackColours::lightPanelGradMid);
        g.setGradientFill (grad);
        g.fillRect (bounds);

        g.saveState();
        g.reduceClipRegion (bounds.toNearestInt());
        for (float y = bounds.getY(); y < bounds.getBottom(); y += 20.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawLine (bounds.getX(), y, bounds.getRight(), y, 0.5f);
        }
        g.restoreState();
        return;
    }

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

void SixpackLookAndFeel::renderDividerLine (juce::Graphics& g, float x, float top, float bottom)
{
    const auto colour = SixpackLookAndFeel::isLightThemeStatic() ? juce::Colour (0xff2a2c2e).withAlpha (0.35f)
                                                                     : juce::Colour (0xffe8e8e6).withAlpha (0.2f);
    g.setColour (colour);
    g.drawLine (x, top, x, bottom, 1.0f);
}

void SixpackLookAndFeel::renderGlowDividerLine (juce::Graphics& g, float x, float top, float bottom)
{
    const auto colour = SixpackLookAndFeel::isLightThemeStatic() ? juce::Colour (0xff1c1d1e).withAlpha (0.55f)
                                                                     : juce::Colour (0xffe8e8e6).withAlpha (0.5f);
    juce::Path line;
    line.startNewSubPath (x, top);
    line.lineTo (x, bottom);
    juce::DropShadow (colour, 8, { 0, 0 }).drawForPath (g, line);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.5f));
}


void SixpackLookAndFeel::renderGlassPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    juce::ColourGradient grad (SixpackColours::panelLight.withAlpha (0.92f), bounds.getX(), bounds.getY(),
                                SixpackColours::panel.withAlpha (0.92f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, cornerRadius);
}

void SixpackLookAndFeel::renderGlossyWhitePanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
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

void SixpackLookAndFeel::renderGroupOutline (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                                                 const juce::String& labelText)
{
    g.setColour (SixpackColours::textDim.withAlpha (0.45f));
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

        g.setColour (SixpackColours::bg);
        g.fillRect (labelArea);
        g.setColour (SixpackColours::textBright);
        g.drawFittedText (labelText, labelArea.toNearestInt(), juce::Justification::centred, 1);
    }
}

void SixpackLookAndFeel::renderBrushedMetal (juce::Graphics& g, juce::Rectangle<float> bounds)
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

void SixpackLookAndFeel::renderGlossySheen (juce::Graphics& g, juce::Rectangle<float> bounds)
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

void SixpackLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
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

void SixpackLookAndFeel::renderCornerScrews (juce::Graphics& g, juce::Rectangle<float> bounds, float inset, float screwRadius)
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

void SixpackLookAndFeel::renderKnobSocket (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    // Discret : juste une fine rainure en creux au ras du knob (pas de
    // disque degrade ni de halo d'ombre etendu -- ca faisait trop lourd).
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawEllipse (centre.x - radius - 1.2f, centre.y - radius - 1.2f, (radius + 1.2f) * 2.0f, (radius + 1.2f) * 2.0f, 0.8f);
}

juce::Colour SixpackLookAndFeel::ledGradientColour (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f,  SixpackColours::ledGreen  },
        { 0.35f, SixpackColours::ledGreen  },
        { 0.6f,  SixpackColours::ledYellow },
        { 0.8f,  SixpackColours::ledOrange },
        { 1.0f,  SixpackColours::ledRed    }
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

juce::Colour SixpackLookAndFeel::ledGradientColourExpand (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f, SixpackColours::ledBlue   },
        { 0.5f, SixpackColours::ledCyan   },
        { 1.0f, SixpackColours::ledPurple }
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

juce::Colour SixpackLookAndFeel::ratioLedColour (float t)
{
    struct Stop { float pos; juce::Colour colour; };
    static const Stop stops[] =
    {
        { 0.0f,  SixpackColours::ledCyan   },
        { 0.25f, SixpackColours::ledGreen  },
        { 0.5f,  SixpackColours::ledYellow },
        { 0.75f, SixpackColours::ledOrange },
        { 1.0f,  SixpackColours::ledRed    }
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

float SixpackLookAndFeel::meterDisplayRatio (float db, float minDb, float maxDb)
{
    db = juce::jlimit (minDb, maxDb, db);
    return (db - minDb) / (maxDb - minDb);
}

void SixpackLookAndFeel::drawGlossyDot (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, bool lit)
{
    if (! lit)
    {
        g.setColour (SixpackColours::ledOff);
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

void SixpackLookAndFeel::paintNeumorphicButton (juce::Graphics& g, juce::Rectangle<float> bounds, bool lit, bool isDown,
                                                  juce::Colour accent, const juce::String& text, float textAlphaMultiplier,
                                                  float haloMargin)
{
    // haloMargin reserve un vrai espace REEL autour du bouton visuel (voir
    // les appelants -- la mise en page expanse deja les bounds du
    // composant d'autant) : b est la pilule visuellement voulue, bounds
    // (plus grand) est tout ce que ce composant peut peindre SANS jamais
    // avoir besoin de deborder de ses propres limites.
    auto b = bounds.reduced (haloMargin);
    const bool recessed = lit || isDown;
    // Rayon agrandi (8 -> jusqu'a 10) : sur les petits boutons du header
    // (26-30px de haut), un rayon de 8 face a un flou de DropShadow de
    // meme ordre de grandeur laissait les bords lateraux quasi droits --
    // la silhouette floutee restait alors visiblement RECTANGULAIRE
    // (retour utilisateur : "suppression des effets carres autour des
    // push quand ils sont desactives"). Un rayon plus genereux rapproche
    // la forme d'une pilule, moins sujette a cet effet.
    const float corner = juce::jmin (11.0f, b.getHeight() * 0.42f);

    const bool lightTheme = isLightThemeStatic();
    // Base neutre UNIQUE, jamais teintee par bande (retour utilisateur,
    // avec captures d'ecran comparant plusieurs bandes cote a cote :
    // "je veux que sur chaque bande, chaque bouton dans le header soit
    // identique a l'enfoncement ... je veux le meme style que le sel de
    // la bande jaune actuelle"). Plusieurs tentatives precedentes de
    // teinter cette base avec la couleur DE LA BANDE (interpolation RVB,
    // puis teinte HSB a luminosite egale) donnaient un resultat different
    // d'une bande a l'autre selon la teinte de l'accent (cyan/vert
    // versus ambre) -- exactement l'inconsistance rapportee. Base
    // alignee sur SixpackColours::lightPanelGradTop (haut du degrade
    // metal, voir BandMetersRack::rebuildBackgroundCacheIfNeeded())
    // SANS aucune teinte d'accent : rendu strictement IDENTIQUE partout
    // (chaque bande + le header), garanti par construction.
    const juce::Colour base = lightTheme ? SixpackColours::lightPanelGradTop : juce::Colour (0xff262422);

    juce::Path shape;
    shape.addRoundedRectangle (b, corner);

    // Design "C" (retour utilisateur, reference visuelle -- capture CSS
    // "C -- extreme") : le halo doit VRAIMENT deborder hors du bouton, pas
    // juste un degrade interne (structurellement impossible a obtenir
    // sans laisser deborder quelque chose -- retour utilisateur : "ça
    // ressemble en rien à ça"). Cette fois l'espace pour deborder est
    // REEL et RESERVE dans la mise en page (haloMargin, voir plus haut)
    // -- le DropShadow peut deborder de `b` en restant TOUJOURS dans les
    // limites de `bounds`, sans setPaintingIsUnclipped().
    if (! recessed)
    {
        const float shadowRadius = juce::jmax (2.0f, haloMargin * 0.7f);
        const int offsetPx = juce::roundToInt (haloMargin * 0.45f);
        juce::DropShadow (juce::Colours::black.withAlpha (lightTheme ? 0.55f : 0.7f), (int) shadowRadius, { offsetPx, offsetPx }).drawForPath (g, shape);
        juce::DropShadow (juce::Colours::white.withAlpha (lightTheme ? 0.9f : 0.18f), (int) shadowRadius, { -offsetPx, -offsetPx }).drawForPath (g, shape);
        g.setColour (base);
        g.fillRoundedRectangle (b, corner);
    }
    else
    {
        // Etat CREUX : reste ENTIEREMENT INTERNE (la reference "C" ne
        // montre AUCUN halo qui deborde sur l'etat enfonce non plus,
        // juste un aspect sombre/enfonce) -- degrades clippes dans la
        // forme, jamais de debordement necessaire ici.
        g.setColour (base.darker (lightTheme ? 0.10f : 0.16f));
        g.fillRoundedRectangle (b, corner);

        g.saveState();
        g.reduceClipRegion (shape);
        juce::ColourGradient darkCorner (juce::Colours::black.withAlpha (lightTheme ? 0.35f : 0.6f), b.getX(), b.getY(),
                                          juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(), true);
        g.setGradientFill (darkCorner);
        g.fillRoundedRectangle (b, corner);
        juce::ColourGradient lightCorner (juce::Colours::white.withAlpha (lightTheme ? 0.5f : 0.22f), b.getRight(), b.getBottom(),
                                           juce::Colours::transparentWhite, b.getCentreX(), b.getCentreY(), true);
        g.setGradientFill (lightCorner);
        g.fillRoundedRectangle (b, corner);
        g.restoreState();
    }

    auto textColour = lit ? (lightTheme ? accent.darker (0.2f) : accent).withAlpha (textAlphaMultiplier)
                           : (lightTheme ? juce::Colour (0xff1a1a1a) : juce::Colours::white).withAlpha (0.88f);
    g.setColour (textColour);
    // Multiplicateur remonte 0.4->0.55 (retour utilisateur : "c'est ecrit
    // trop petit" -- sur les petits boutons THD (~24px de haut), 0.4 ne
    // donnait qu'environ 8pt, illisible ; 0.55 vise plutot 12pt a cette
    // hauteur, tout en restant plafonne a 15 pour les boutons plus hauts
    // (SOLO/SEL/LEARN, ~38px).
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (15.0f, b.getHeight() * 0.55f), juce::Font::bold)).withExtraKerningFactor (0.03f));
    g.drawFittedText (text, b.toNearestInt(), juce::Justification::centred, 1);
}

juce::Image SixpackLookAndFeel::buildSignatureImage (juce::Colour ink, int targetW)
{
    auto master = juce::ImageCache::getFromMemory (BinaryData::signature_mikshake_master_png,
                                                     BinaryData::signature_mikshake_master_pngSize);
    if (master.isNull())
        return {};

    juce::Image argb (juce::Image::ARGB, master.getWidth(), master.getHeight(), true);
    int minX = master.getWidth(), minY = master.getHeight(), maxX = -1, maxY = -1;
    {
        juce::Image::BitmapData s (master, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData d (argb, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < s.height; ++y)
        {
            for (int x = 0; x < s.width; ++x)
            {
                // Alpha = couverture d'encre reelle (encre noire sur fond
                // blanc dans le maitre), preserve l'anti-aliasing natif du
                // fichier source au lieu de le detruire par un seuillage
                // binaire (voir doc de declaration dans SixpackLookAndFeel.h).
                const float alpha = juce::jlimit (0.0f, 1.0f, 1.0f - s.getPixelColour (x, y).getPerceivedBrightness());
                d.setPixelColour (x, y, ink.withAlpha (alpha));
                if (alpha > 0.03f)
                {
                    minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                    minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
                }
            }
        }
    }
    if (maxX < minX)
        return {};

    constexpr int margin = 15;
    const auto bbox = juce::Rectangle<int> (minX, minY, maxX - minX + 1, maxY - minY + 1)
                           .expanded (margin)
                           .getIntersection ({ 0, 0, argb.getWidth(), argb.getHeight() });
    auto cropped = argb.getClippedImage (bbox);

    const int targetH = juce::roundToInt ((float) targetW * (float) cropped.getHeight() / (float) cropped.getWidth());

    // Reduction par paliers x2 (chaque palier reste dans un ratio ou
    // l'area-averaging de JUCE est fiable) jusqu'a approcher la taille
    // finale, puis un dernier ajustement exact -- evite un seul palier a
    // ratio extreme (~17:1 depuis le maitre) qui degraderait le resultat.
    juce::Image cur = cropped;
    while (cur.getWidth() > targetW * 2 && cur.getHeight() > targetH * 2)
        cur = cur.rescaled (cur.getWidth() / 2, cur.getHeight() / 2, juce::Graphics::highResamplingQuality);
    return cur.rescaled (targetW, targetH, juce::Graphics::highResamplingQuality);
}
