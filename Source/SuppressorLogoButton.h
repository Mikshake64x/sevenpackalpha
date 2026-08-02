#pragma once
#include <JuceHeader.h>

/**
    SuppressorLogoButton
    ---------------------
    Le logo "SUPPRESSOR" (LogoSuppressor.png) EST desormais le declencheur
    de la page About -- remplace le monogramme SK separe (retire, demande
    explicite). Meme mecanique cliquable que l'ancien SidekeyMonogram.h.
*/
class SuppressorLogoButton : public juce::Component
{
public:
    SuppressorLogoButton()
    {
        // TrippressorlogoCropped.png : meme logo que Trippressorlogo.png,
        // mais recadre sur son contenu reel (bbox alpha) -- le fichier
        // fourni par l'utilisateur avait ENORMEMENT de marge transparente
        // autour du texte (canevas 1536x1024, contenu reel seulement
        // 1178x335), ce qui le faisait apparaitre minuscule une fois mis a
        // l'echelle dans sa boite (le ratio d'aspect du CANEVAS pilotait le
        // calcul, pas celui du texte). Le recadrage seul fait gagner ~3x en
        // surface de rendu reelle sans toucher aux dimensions de la boite
        // (voir suppressorLogoButton.setBounds() dans PluginEditor.cpp).
        image = juce::ImageCache::getFromMemory (BinaryData::TrippressorlogoCropped_png, BinaryData::TrippressorlogoCropped_pngSize);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void resized() override { updateBounds(); }

    void paint (juce::Graphics& g) override
    {
        if (image.isNull())
            return;

        // Le logo GARDE sa taille/position d'affichage normale (demande
        // explicite : ne pas toucher a la taille du logo) -- seule la zone
        // de clic (drawBounds retreci, voir clickBounds/hitTest) est
        // reduite, independamment de ce qui est dessine ici.
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, drawBounds.getX(), drawBounds.getY(), drawBounds.getWidth(), drawBounds.getHeight(),
                     0, 0, image.getWidth(), image.getHeight());
    }

    // Empreinte de clic REDUITE par rapport a l'image affichee (drawBounds)
    // -- son bord droit deborde sur le bouton Bypass voisin et lui volait
    // ses clics (bug rapporte), alors qu'on ne veut toucher ni a la taille
    // ni a la position visuelle du logo. On retreint donc seulement la
    // zone reactive aux clics, symetriquement, en gardant son centre.
    bool hitTest (int x, int y) override
    {
        return clickBounds.contains (x, y);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }

    std::function<void()> onClick;

private:
    void updateBounds()
    {
        if (image.isNull())
            return;

        auto bounds = getLocalBounds().toFloat();

        // Mise a l'echelle en conservant le ratio d'aspect, ALIGNE A
        // GAUCHE (pas centre) -- coherent avec la position du wordmark
        // d'origine dans la barre de controle.
        const float scale = juce::jmin (bounds.getWidth() / (float) image.getWidth(),
                                         bounds.getHeight() / (float) image.getHeight());
        const int w = (int) (image.getWidth() * scale);
        const int h = (int) (image.getHeight() * scale);
        const int dx = (int) bounds.getX();
        const int dy = (int) (bounds.getCentreY() - h * 0.5f);
        drawBounds = { dx, dy, w, h };

        // 25% retreint sur chaque cote (centre conserve) -- assez pour ne
        // plus toucher le bouton Bypass voisin sans avoir a deplacer/
        // redimensionner le logo lui-meme.
        clickBounds = drawBounds.reduced ((int) (w * 0.25f), (int) (h * 0.25f));
    }

    juce::Image image;
    juce::Rectangle<int> drawBounds, clickBounds;
};
