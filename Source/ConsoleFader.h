#pragma once
#include <JuceHeader.h>
#include "SixpackLookAndFeel.h"

/**
    ConsoleFader
    ------------
    Fader vertical style console de mixage -- porte tel quel depuis
    SimpleMasterView.h (projet Smart Master), retour utilisateur explicite
    de CETTE session : "je veux les faders du smart master ... les fader
    qui sont dans le mode simple". Design deja valide sur preview HTML
    avant portage dans Smart Master ("ils doivent ressembler a des vrai
    fader de console, avec relief, brillance de lumiere, reflet dynamique,
    marque sur le cote") -- juce::Slider standard (LinearVertical) pour
    l'interaction native (drag au clic), seul le paint() est personnalise.
*/
class ConsoleFader : public juce::Slider
{
public:
    ConsoleFader() : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox)
    {
        setWantsKeyboardFocus (false);
        // Retour utilisateur explicite : "quand je clique sur les faders,
        // ils ne doivent pas se deplacer ... il monte d'un coup c'est pas
        // bon. ils doivent glisser, pas se teleporter" -- comportement
        // JUCE par defaut (clic n'importe ou sur la piste = saut instantane
        // a cette valeur). false = un clic ailleurs que sur le capuchon ne
        // fait RIEN sauter ; il faut attraper le capuchon et glisser (le
        // drag reste relatif a la position de depart), comme un vrai fader.
        setSliderSnapsToMousePosition (false);
    }

    // Drag "maison" (remplace le drag absolu par defaut de juce::Slider),
    // meme raisonnement/code que KnobSlider (voir son commentaire) : le
    // mode "velocity" integre de JUCE (Ctrl par defaut) teleportait le
    // curseur au relachement de la touche ("gros a-coups" rapportes), et ne
    // donnait de toute facon aucune precision supplementaire. Ici Ctrl
    // reduit reellement la sensibilite (4x), avec reancrage au moment ou
    // Ctrl change d'etat pour ne jamais sauter.
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartProportion = valueToProportionOfLength (getValue());
        dragStartY = e.position.y;
        ctrlWasDown = e.mods.isCtrlDown();
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const bool ctrlDown = e.mods.isCtrlDown();
        if (ctrlDown != ctrlWasDown)
        {
            dragStartProportion = valueToProportionOfLength (getValue());
            dragStartY = e.position.y;
            ctrlWasDown = ctrlDown;
        }

        constexpr float normalDragPixels = 250.0f;
        constexpr float fineDragPixels = 1000.0f;
        const float pixelsForFullRange = ctrlDown ? fineDragPixels : normalDragPixels;

        const double deltaProportion = (double) (dragStartY - e.position.y) / (double) pixelsForFullRange;
        const double newProportion = juce::jlimit (0.0, 1.0, dragStartProportion + deltaProportion);
        setValue (proportionOfLengthToValue (newProportion), juce::sendNotificationSync);
    }

    void setAccentColour (juce::Colour c) noexcept { accentColour = c; repaint(); }

    // Pas de -- volontairement PAS d'algorithme "joli chiffre" : l'appelant
    // connait deja la plage exacte (Output -30/+30 => 10/2).
    void setTickSteps (float majorStepIn, float minorStepIn) noexcept
    {
        majorStep = majorStepIn; minorStep = minorStepIn; repaint();
    }

    // Cache l'echelle chiffree laterale -- inutile ici, le rack a deja sa
    // propre echelle GR/Expand a cote (evite de dupliquer les chiffres
    // dans un espace tres etroit).
    void setShowTicks (bool shouldShow) noexcept { showTicks = shouldShow; repaint(); }

    // Marge de securite AUTOUR du rail (pas dessus) : agrandir les bounds
    // du composant de ce meme montant (voir BandMetersRack::resized(),
    // setBounds(...expanded(edgePadding))) laisse la place a l'ombre
    // portee du cap sans jamais la faire tronquer par le bord du
    // composant -- retour utilisateur explicite : "il doit juste pas se
    // couper" (PAS de marge sur le rail lui-meme, qui garde exactement la
    // meme position/longueur -- seule la zone de clipping s'agrandit).
    //
    // paddingTop volontairement PLUS PETIT que paddingBottom (retour
    // utilisateur : "j'ai la bande du fader qui coupe bizarement et
    // horizontalement les push [SOLO/SEL] ... on ne doit pas voir cette
    // bande") -- l'ombre du cap grandit avec travelFrac (jusqu'a
    // rayon 12 + decalage 9 pres du BAS de la course, voir paint()), donc
    // le cote bas garde une marge genereuse ; mais avec une marge haute
    // symetrique (8px), le clip du composant ne laissait qu'~1px de jeu
    // avec le bas de la rangee SOLO/SEL (voir BandMetersRack::
    // computeLayout()) -- l'ombre du cap tronquee PILE a cette limite se
    // voyait comme une bande sombre a bord dur, juste sous/contre les
    // boutons. Une marge haute reduite ne perd rien : pres du HAUT de la
    // course (travelFrac proche de 0), l'ombre est de toute facon la plus
    // petite (rayon 5, decalage 3).
    void setEdgePadding (float paddingX, float paddingTopIn, float paddingBottomIn) noexcept
    {
        edgePaddingX = paddingX; edgePaddingTop = paddingTopIn; edgePaddingBottom = paddingBottomIn; repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();
        auto b = juce::Rectangle<float> (full.getX() + edgePaddingX, full.getY() + edgePaddingTop,
                                          full.getWidth() - edgePaddingX * 2.0f,
                                          full.getHeight() - edgePaddingTop - edgePaddingBottom);
        auto tickArea = showTicks ? b.removeFromLeft (juce::jmin (34.0f, b.getWidth() * 0.42f)) : juce::Rectangle<float>();
        auto trackArea = b; // reste : rail + cap

        const float capW = juce::jmin (40.0f, trackArea.getWidth() * 0.95f);
        // Cap allonge (demande explicite d'origine : "il faut juste alonger
        // un peu les caps, pas les rails") -- seule la poignee grandit, le
        // rail (railTop/railBottom ci-dessous) reste calcule a partir de
        // cette meme valeur donc la course visible s'adapte naturellement.
        const float capH = juce::jmax (30.0f, juce::jmin (74.0f, trackArea.getHeight() * 0.20f));
        const float railX = trackArea.getCentreX();
        const float railTop = trackArea.getY() + capH * 0.5f, railBottom = trackArea.getBottom() - capH * 0.5f;
        const float railH = railBottom - railTop;

        // --- Echelle laterale : reguliere, degrade majeur/mineur, chiffres
        // alignes a droite juste avant le trait. ---
        const double minV = getMinimum(), maxV = getMaximum();
        if (showTicks && maxV > minV && minorStep > 0.0f)
        {
            for (float v = (float) maxV; v >= (float) minV - 0.001f; v -= minorStep)
            {
                const float frac = (float) ((v - minV) / (maxV - minV));
                const float y = railTop + (1.0f - frac) * railH;
                const bool isMajor = std::abs (std::fmod (v - (float) minV, majorStep)) < 0.01f
                                   || std::abs (std::fmod (v - (float) minV, majorStep) - majorStep) < 0.01f;

                g.setColour (isMajor ? juce::Colour (0xff9aa0a7) : juce::Colour (0xff4a4e54));
                g.fillRect (tickArea.getRight() - (isMajor ? 11.0f : 5.0f), y - (isMajor ? 0.75f : 0.5f),
                            isMajor ? 11.0f : 5.0f, isMajor ? 1.5f : 1.0f);

                if (isMajor)
                {
                    g.setColour (juce::Colour (0xffc4cad2));
                    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                    g.drawText (juce::String (juce::roundToInt (v)), tickArea.withHeight (14.0f).withCentre ({ tickArea.getCentreX() - 6.0f, y }),
                                juce::Justification::centredRight);
                }
            }
        }

        // --- Rail (rainure sombre). ---
        {
            juce::Rectangle<float> rail (railX - 3.0f, railTop, 6.0f, railH);
            juce::ColourGradient railGrad (juce::Colour (0xff050607), rail.getX(), rail.getY(),
                                            juce::Colour (0xff1a1d21), rail.getCentreX(), rail.getY(), false);
            railGrad.addColour (0.5, juce::Colour (0xff1a1d21));
            railGrad.addColour (1.0, juce::Colour (0xff050607));
            g.setGradientFill (railGrad);
            g.fillRoundedRectangle (rail, 3.0f);
        }

        // --- Cap (poignee) : degrade metallique clair->sombre, reflet
        // glossy horizontal, repere lumineux couleur bande, marques
        // laterales gravees. ---
        {
            const float pos01 = maxV > minV ? (float) ((getValue() - minV) / (maxV - minV)) : 0.0f;
            const float capCy = railTop + (1.0f - pos01) * railH;
            auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ railX, capCy });

            // Source de lumiere virtuelle FIXE en haut du rail (coherente
            // avec le reste du chassis, deja eclaire par le haut -- voir
            // renderMetalPanel/renderGlossySheen) -- retour utilisateur
            // explicite : "ombre projetee dynamique en fonction de la
            // position des faders, et un eclairage dynamique". travelFrac
            // = 0 en haut du rail (cap pres de la lumiere, contact court et
            // net) -> 1 en bas (cap loin de la lumiere, ombre plus longue,
            // plus diffuse, et reflet plus faible, comme un vrai spot
            // place en haut de la tranche).
            const float travelFrac = railH > 0.0f ? (capCy - railTop) / railH : 0.0f;

            juce::Path capShadowPath;
            capShadowPath.addRoundedRectangle (cap, 5.0f);
            const int shadowRadius = juce::roundToInt (5.0f + travelFrac * 7.0f);
            const int shadowOffsetY = juce::roundToInt (3.0f + travelFrac * 6.0f);
            const float shadowAlpha = 0.68f - travelFrac * 0.28f;
            juce::DropShadow (juce::Colours::black.withAlpha (shadowAlpha), shadowRadius, { 0, shadowOffsetY }).drawForPath (g, capShadowPath);

            juce::ColourGradient body (juce::Colour (0xff4a4e54), cap.getX(), cap.getY(),
                                        juce::Colour (0xff050506), cap.getX(), cap.getBottom(), false);
            body.addColour (0.08, juce::Colour (0xff26292d));
            body.addColour (0.50, juce::Colour (0xff1a1c1f));
            body.addColour (0.92, juce::Colour (0xff0d0e10));
            g.setGradientFill (body);
            g.fillRoundedRectangle (cap, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawRoundedRectangle (cap.reduced (0.5f), 5.0f, 1.0f);

            // Voile d'assombrissement global (retour utilisateur : "ombre
            // ok, mais l'eclairage ne change rien" -- la seule modulation
            // du reflet ci-dessous etait trop discrete, un simple degrade
            // d'alpha sur une fine bande, pour etre perceptible). Ici tout
            // le corps du cap s'assombrit progressivement en s'eloignant de
            // la lumiere du haut -- effet global, immediatement visible,
            // qui va de pair avec l'ombre portee ci-dessus.
            g.setColour (juce::Colours::black.withAlpha (travelFrac * 0.4f));
            g.fillRoundedRectangle (cap, 5.0f);

            // Reflet glossy horizontal (haut du cap) -- intensite liee a la
            // meme travelFrac que l'ombre (plus vif pres de la lumiere en
            // haut, plus terne en bas), pour que les 2 effets restent
            // coherents entre eux au lieu de varier independamment.
            const float glossAlpha = 0.75f - travelFrac * 0.65f;
            auto gloss = cap.removeFromTop (cap.getHeight() * 0.14f).reduced (cap.getWidth() * 0.12f, 0.0f);
            juce::ColourGradient glossGrad (juce::Colours::transparentWhite, gloss.getX(), gloss.getCentreY(),
                                             juce::Colours::white.withAlpha (glossAlpha), gloss.getCentreX(), gloss.getCentreY(), false);
            glossGrad.addColour (0.5, juce::Colours::white.withAlpha (glossAlpha));
            glossGrad.addColour (1.0, juce::Colours::transparentWhite);
            g.setGradientFill (glossGrad);
            g.fillRoundedRectangle (gloss, 1.5f);

            // Repere lumineux couleur bande.
            auto mark = juce::Rectangle<float> (cap.getX(), cap.getY() + cap.getHeight() * 0.42f, cap.getWidth(), 3.0f);
            g.setColour (accentColour);
            g.fillRect (mark);
            g.setColour (accentColour.withAlpha (0.5f));
            g.fillRect (mark.expanded (0.0f, 2.5f));

            // Marques laterales gravees (2 petits traits verticaux, comme
            // les crans d'un vrai cap de fader).
            g.setColour (juce::Colour (0xff7a7f86).withAlpha (0.6f));
            g.fillRoundedRectangle (cap.getX() + cap.getWidth() * 0.10f, cap.getBottom() - cap.getHeight() * 0.34f, 2.5f, cap.getHeight() * 0.28f, 1.0f);
            g.fillRoundedRectangle (cap.getRight() - cap.getWidth() * 0.10f - 2.5f, cap.getBottom() - cap.getHeight() * 0.34f, 2.5f, cap.getHeight() * 0.28f, 1.0f);
        }
    }

private:
    juce::Colour accentColour = SixpackColours::ledCyan;
    float majorStep = 10.0f, minorStep = 2.0f;
    bool showTicks = true;
    float edgePaddingX = 0.0f, edgePaddingTop = 0.0f, edgePaddingBottom = 0.0f;

    double dragStartProportion = 0.0;
    float dragStartY = 0.0f;
    bool ctrlWasDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleFader)
};
