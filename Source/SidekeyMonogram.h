#pragma once
#include <JuceHeader.h>

/**
    SidekeyMonogram
    ---------------
    Affiche le monogramme Side Key System tel quel (design non retouche),
    charge depuis les ressources binaires du projet.

    IMPORTANT (etape Projucer requise) : le fichier sidekey_monogram.png
    doit etre ajoute au projet comme ressource binaire pour que
    BinaryData::sidekey_monogram_png existe :
      1. Dans Projucer, glisser sidekey_monogram.png dans le groupe "Source"
         (ou tout autre groupe du projet).
      2. Cocher "Binary Resource" sur ce fichier dans les proprietes (clic
         droit sur le fichier > "Add to binary resources").
      3. Sauvegarder le projet (Projucer regenere BinaryData.h/.cpp) puis
         recompiler.
*/
class SidekeyMonogram : public juce::Component
{
public:
    SidekeyMonogram()
    {
        image = juce::ImageCache::getFromMemory (BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        if (image.isNull())
            return;

        auto bounds = getLocalBounds().toFloat();

        // Mise a l'echelle en conservant le ratio d'aspect (881x732 a la
        // source), centree dans les bornes du composant -- design intact,
        // juste redimensionne.
        const float scale = juce::jmin (bounds.getWidth() / (float) image.getWidth(),
                                         bounds.getHeight() / (float) image.getHeight());
        const int w = (int) (image.getWidth() * scale);
        const int h = (int) (image.getHeight() * scale);
        const int dx = (int) (bounds.getCentreX() - w * 0.5f);
        const int dy = (int) (bounds.getCentreY() - h * 0.5f);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, dx, dy, w, h, 0, 0, image.getWidth(), image.getHeight());
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }

    std::function<void()> onClick;

private:
    juce::Image image;
};
