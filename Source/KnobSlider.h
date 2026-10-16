#pragma once
#include <JuceHeader.h>

/**
    KnobSlider
    ----------
    Rotary knob avec un double clic contextuel :
    - sur l'ecran LCD central : ouvre un editeur de texte pour taper la
      valeur directement au clavier ;
    - ailleurs sur le corps du knob (bague/graduations) : comportement
      standard JUCE de retour a la valeur par defaut (voir
      setDoubleClickReturnValue, applique par l'appelant dans setupKnob()).

    La zone LCD est calculee avec EXACTEMENT la meme formule geometrique
    que SevenpackLookAndFeel::drawRotarySlider (bounds reduits de 2,
    R = rayon*0.92, lcdR = R*0.62*0.62) -- a garder synchronisee si ce
    dessin change.
*/
class KnobSlider : public juce::Slider,
                    private juce::TextEditor::Listener
{
public:
    KnobSlider() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        editor.setJustification (juce::Justification::centred);
        editor.setInputRestrictions (0, "0123456789.-");
        editor.setSelectAllWhenFocused (true);
        editor.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        editor.addListener (this);
        editor.setVisible (false);
        addChildComponent (editor);
    }

    // Mode COMPACT (tranches Sevenpack, voir BandStrip.h) : plus d'ecran
    // LCD au centre (illisible a ~46px de diametre) -- la valeur est
    // ecrite SOUS le knob par le parent. SevenpackLookAndFeel::
    // drawRotarySlider lit la propriete "compactKnob" ; ici, lcdRadius=0
    // desactive la saisie au double-clic sur le centre (le double-clic
    // revient alors partout a la valeur par defaut).
    void setCompact (bool shouldBeCompact)
    {
        compact = shouldBeCompact;
        getProperties().set ("compactKnob", shouldBeCompact);
        resized();
        repaint();
    }

    void resized() override
    {
        juce::Slider::resized();

        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const float R = diameter * 0.5f * 0.92f;

        lcdCentre = bounds.getCentre();
        lcdRadius = compact ? 0.0f : R * 0.62f * 0.62f;

        editor.setBounds (juce::Rectangle<float> (lcdRadius * 1.7f, lcdRadius * 1.2f)
                               .withCentre (lcdCentre).toNearestInt());
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (lcdRadius > 0.0f && lcdCentre.getDistanceFrom (e.position) <= lcdRadius)
        {
            editor.setText (juce::String (getValue(), getNumDecimalPlacesToDisplay()), juce::dontSendNotification);
            editor.setVisible (true);
            editor.toFront (true);
            editor.grabKeyboardFocus();
            editor.selectAll();
            return;
        }

        juce::Slider::mouseDoubleClick (e);
    }

    // Drag "maison" (remplace entierement le drag absolu par defaut de
    // juce::Slider) -- retour utilisateur : "je souhaite que quand on
    // appuie sur control on ait un controle plus precis". Le mode
    // "velocity" integre a JUCE (declenche par Ctrl par defaut) teleportait
    // le curseur au relachement de la touche, d'ou les "gros a-coups"
    // rapportes -- au lieu de patcher ce mecanisme prive, on calcule la
    // valeur nous-memes a partir du deplacement vertical de la souris,
    // avec une sensibilite reduite (donc plus precis) pendant que Ctrl est
    // enfonce. Le REANCRAGE (dragStartY/dragStartProportion remis a la
    // valeur/position courantes) au moment exact ou Ctrl est presse OU
    // relache est ce qui garantit l'absence de saut : le delta repart de
    // zero avec la nouvelle sensibilite plutot que d'etre recalcule sur
    // toute la distance parcourue depuis le debut du drag.
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

        // 250px pour la course complete = meme sensibilite que le defaut
        // JUCE (pixelsForFullDragExtent) en mode normal -- 4x plus large
        // (donc 4x plus precis) quand Ctrl est enfonce.
        constexpr float normalDragPixels = 250.0f;
        constexpr float fineDragPixels = 1000.0f;
        const float pixelsForFullRange = ctrlDown ? fineDragPixels : normalDragPixels;

        const double deltaProportion = (double) (dragStartY - e.position.y) / (double) pixelsForFullRange;
        const double newProportion = juce::jlimit (0.0, 1.0, dragStartProportion + deltaProportion);
        setValue (proportionOfLengthToValue (newProportion), juce::sendNotificationSync);
    }

private:
    void commitEditor()
    {
        setValue (juce::jlimit (getMinimum(), getMaximum(), (double) editor.getText().getFloatValue()),
                  juce::sendNotificationSync);
        editor.setVisible (false);
    }

    void textEditorReturnKeyPressed (juce::TextEditor&) override { commitEditor(); }
    void textEditorFocusLost (juce::TextEditor&) override { commitEditor(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { editor.setVisible (false); }

    juce::TextEditor editor;
    juce::Point<float> lcdCentre;
    float lcdRadius = 0.0f;
    bool compact = false;

    double dragStartProportion = 0.0;
    float dragStartY = 0.0f;
    bool ctrlWasDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobSlider)
};
