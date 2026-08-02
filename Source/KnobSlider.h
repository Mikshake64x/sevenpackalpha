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
    que SuppressorLookAndFeel::drawRotarySlider (bounds reduits de 2,
    R = rayon*0.92, lcdR = R*0.62*0.62) -- a garder synchronisee si ce
    dessin change.
*/
class KnobSlider : public juce::Slider,
                    private juce::TextEditor::Listener
{
public:
    KnobSlider() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
    {
        editor.setJustification (juce::Justification::centred);
        editor.setInputRestrictions (0, "0123456789.-");
        editor.setSelectAllWhenFocused (true);
        editor.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        editor.addListener (this);
        editor.setVisible (false);
        addChildComponent (editor);
    }

    void resized() override
    {
        juce::Slider::resized();

        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const float R = diameter * 0.5f * 0.92f;

        lcdCentre = bounds.getCentre();
        lcdRadius = R * 0.62f * 0.62f;

        editor.setBounds (juce::Rectangle<float> (lcdRadius * 1.7f, lcdRadius * 1.2f)
                               .withCentre (lcdCentre).toNearestInt());
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (lcdCentre.getDistanceFrom (e.position) <= lcdRadius)
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobSlider)
};
