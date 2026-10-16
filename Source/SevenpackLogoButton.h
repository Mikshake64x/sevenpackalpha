#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"

/**
    SevenpackLogoButton
    ---------------------
    Le logo (SEVENPACK, sevenpacklogo3.png -- retour utilisateur explicite :
    "j'ai ajoute logosevenpack3 ... sur le logo 2 dans le 6 de sevenpack, les
    barres de couleurs etaient statiques ... je souhaite les rendre
    dynamique") EST le declencheur de la page About -- remplace le
    monogramme SK separe (retire, demande explicite anterieure). Meme
    mecanique cliquable que l'ancien SidekeyMonogram.h.

    Contrairement a logo2 (barres peintes en dur dans le PNG), logo3 laisse
    le fut du "6" ENTIEREMENT transparent a cet endroit (trou mesure :
    x[657,1345] y[283,730] sur le canevas 1774x887, soit environ
    holeFrac ci-dessous en fractions normalisees). On peint donc 6 barres
    (une par bande, memes couleurs que BandMetersRack/CrossoverSpectrumEditor)
    animees en boucle DANS ce rectangle, PUIS le logo par-dessus : les traits
    opaques du "6" masquent naturellement tout ce qui deborde de son vrai
    contour (pas un rectangle exact), seul le trou reellement transparent
    laisse voir les barres. Animation decorative libre (retour utilisateur :
    pas liee au signal), un simple Timer pour la boucle.
*/
class SevenpackLogoButton : public juce::Component,
                           private juce::Timer
{
public:
    SevenpackLogoButton()
    {
        image = juce::ImageCache::getFromMemory (BinaryData::sevenpacklogo3_png, BinaryData::sevenpacklogo3_pngSize);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (30);
    }

    ~SevenpackLogoButton() override { stopTimer(); }

    void resized() override { updateBounds(); }

    void paint (juce::Graphics& g) override
    {
        if (image.isNull())
            return;

        paintDynamicBars (g);

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

    void timerCallback() override { repaint(); }

    // 6 barres verticales (une par bande, meme famille de couleurs que
    // BandMetersRack/CrossoverSpectrumEditor), hauteur animee par une simple
    // sinusoide dephasee/desynchronisee par barre pour un mouvement
    // organique plutot qu'un "clignotement" synchrone. Ancrees en bas du
    // trou, comme un petit spectre/EQ. Dessinees AVANT le logo (voir
    // paint()) -- pas besoin de decouper la vraie forme du "6", les traits
    // opaques du PNG masquent tout ce qui deborde du trou reel.
    void paintDynamicBars (juce::Graphics& g) const
    {
        if (drawBounds.isEmpty())
            return;

        auto hole = juce::Rectangle<float> (drawBounds.getX() + holeFrac.getX() * drawBounds.getWidth(),
                                             drawBounds.getY() + holeFrac.getY() * drawBounds.getHeight(),
                                             holeFrac.getWidth()  * drawBounds.getWidth(),
                                             holeFrac.getHeight() * drawBounds.getHeight());
        if (hole.getWidth() < 2.0f || hole.getHeight() < 2.0f)
            return;

        static const juce::Colour barColours[] =
        {
            SevenpackColours::ledCyan, SevenpackColours::ledGreen, SevenpackColours::ledAmber,
            SevenpackColours::ledOrange, SevenpackColours::ledPurple, SevenpackColours::ledRed
        };
        // La forme reelle du "6" retrecit sur son bord droit (bbox mesuree
        // = rectangle englobant, pas le vrai contour) -- les 2 barres les
        // plus a droite depassaient donc du trait a cet endroit (retour
        // utilisateur). Layout TOUJOURS calcule sur 6 (numBars, pour garder
        // exactement la meme largeur/espacement des 4 barres restantes),
        // mais seules les 4 premieres sont dessinees (numVisibleBars).
        constexpr int numBars = 6;
        constexpr int numVisibleBars = 4;
        constexpr float minHeightFrac = 0.22f;
        const float gap = hole.getWidth() * 0.06f;
        const float barW = (hole.getWidth() - gap * (numBars - 1)) / (float) numBars;

        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;

        for (int i = 0; i < numVisibleBars; ++i)
        {
            // Phase et vitesse legerement differentes par barre -- evite
            // l'effet "toutes les barres pulsent a l'unisson".
            const double phase = i * 1.15;
            const double speed = 1.3 + i * 0.11;
            const float wave = (float) (0.5 + 0.5 * std::sin (t * speed + phase));
            const float heightFrac = minHeightFrac + (1.0f - minHeightFrac) * wave;
            const float barHeight = hole.getHeight() * heightFrac;

            // Construite DIRECTEMENT avec son Y final (top = bas du trou
            // moins sa hauteur) -- withBottom().withHeight() chaine mal ici
            // (withBottom() calcule sa hauteur par rapport au Y de DEPART,
            // 0.0f e'tait le haut du COMPOSANT entier, pas le bas du trou :
            // les barres partaient du sommet du logo au lieu d'etre
            // ancrees en bas du trou -- bug rapporte : "ca depasse du
            // logo").
            auto bar = juce::Rectangle<float> (hole.getX() + i * (barW + gap), hole.getBottom() - barHeight,
                                                barW, barHeight);
            g.setColour (barColours[i]);
            g.fillRect (bar);
        }
    }

    juce::Image image;
    juce::Rectangle<int> drawBounds, clickBounds;

    // Fraction normalisee (x, y, largeur, hauteur) du trou transparent dans
    // le "6" de sevenpacklogo3.png -- mesure sur le canevas source 1774x887 :
    // bbox reelle x[657,1345] y[283,730] (flood fill des pixels transparents
    // non atteignables depuis le bord, donc reellement ENCLAVES dans le
    // trait, pas le fond transparent autour du logo).
    inline static const juce::Rectangle<float> holeFrac { 657.0f / 1774.0f, 283.0f / 887.0f,
                                                            (1345.0f - 657.0f) / 1774.0f, (730.0f - 283.0f) / 887.0f };
};
