#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    SupportTickerBar
    -----------------
    Bande de texte defilant (marquee), sous la barre de presets -- rappelle
    a l'utilisateur de soutenir le developpeur independant. Version FREE
    uniquement (voir demande explicite : pas de mecanisme de retrait pour
    l'instant, sera gere plus tard via un build separe "Pro" sans cette
    bande).

    OPTIMISATION CPU : le texte n'est RASTERISE QU'UNE SEULE FOIS dans une
    Image mise en cache (textCache) -- paint() se contente ensuite de
    blitter cette image (g.drawImageAt, pas de layout de police ni de
    dessin de glyphes a chaque frame). Le timer tourne a 24Hz (au lieu de
    60) et le fond fixe (cadre/bordure) est lui aussi mis en cache -- le
    seul travail par frame est un decalage entier de coordonnee + 2 blits
    d'image. repaint() n'est appele que si le decalage a effectivement
    change d'au moins 1px (arrondi), pour ne jamais repeindre inutilement.
*/
class SupportTickerBar : public juce::Component, private juce::Timer
{
public:
    SupportTickerBar()
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (24);
    }

    ~SupportTickerBar() override { stopTimer(); }

    void resized() override
    {
        frameCache = juce::Image();  // force la reconstruction au prochain paint()
        textCache = juce::Image();
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight();
        if (w < 4 || h < 4)
            return;

        if (! frameCache.isValid())
            rebuildFrameCache (w, h);
        g.drawImageAt (frameCache, 0, 0);

        if (! textCache.isValid())
            rebuildTextCache (h);

        const int textW = textCache.getWidth();
        if (textW <= 0)
            return;

        g.saveState();
        g.reduceClipRegion (0, 0, w, h);

        const int y = (h - textCache.getHeight()) / 2;
        int x = -(scrollXPx % textW);
        while (x < w)
        {
            g.drawImageAt (textCache, x, y);
            x += textW;
        }

        g.restoreState();
    }

private:
    void rebuildFrameCache (int w, int h)
    {
        frameCache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (frameCache);
        auto bounds = frameCache.getBounds().toFloat();

        // Retour aux couleurs de la toute premiere version (demande
        // explicite) -- fond sombre assorti au reste du chassis, texte ambre.
        g.setColour (juce::Colour (0xff0d0f11));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }

    void rebuildTextCache (int barHeight)
    {
        const juce::Font font (juce::FontOptions (11.5f, juce::Font::bold).withStyle ("Italic"));

        juce::GlyphArrangement ga;
        ga.addLineOfText (font, message, 0.0f, 0.0f);
        const int textW = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth());
        if (textW <= 0)
            return;

        textCache = juce::Image (juce::Image::ARGB, textW, barHeight, true);
        juce::Graphics g (textCache);
        g.setFont (font);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.9f));
        const int textY = (barHeight - (int) font.getHeight()) / 2;
        g.drawText (message, 0, textY, textW, (int) font.getHeight() + 4, juce::Justification::centredLeft, false);
    }

    void timerCallback() override
    {
        scrollXAccum += scrollSpeedPxPerSec / 24.0f;

        // Reboucle sur la largeur du texte pour ne jamais laisser
        // scrollXAccum grandir sans limite sur une session tres longue
        // (perte de precision float a terme, purement cosmetique mais
        // evitable gratuitement).
        if (textCache.isValid())
        {
            const float textW = (float) textCache.getWidth();
            if (scrollXAccum >= textW)
                scrollXAccum = std::fmod (scrollXAccum, textW);
        }

        const int newScrollXPx = (int) scrollXAccum;
        if (newScrollXPx != scrollXPx)
        {
            scrollXPx = newScrollXPx;
            repaint();
        }
    }

    const juce::String message = juce::CharPointer_UTF8 (
        "FREE VERSION  \xe2\x80\xa2  If you use this mastering box, please support the developer, "
        "buy a copy or leave a donation.  \xe2\x80\xa2  Support independent developers.  "
        "\xe2\x80\xa2  Buy your license to remove this banner.  \xe2\x80\xa2  ");

    // Vitesse reduite (demande explicite : "plus lentement") -- 14px/s au
    // lieu de 42px/s.
    static constexpr float scrollSpeedPxPerSec = 14.0f;

    float scrollXAccum = 0.0f;
    int scrollXPx = 0;

    juce::Image frameCache, textCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupportTickerBar)
};
