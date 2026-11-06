#pragma once
#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "SevenpackLookAndFeel.h"
#include "AlphaSkin.h"

/**
    CrossoverSpectrumEditor
    ------------------------
    Remplace les anciens knobs XLO/XHI (Tripressor 3 bandes) : un bandeau
    de couleur fin (groupes de bandes actifs), une rangee de boutons SOLO
    (un par groupe), un spectre d'entree lisse avec 4 lignes de crossover
    DRAGGABLES (poignee posee sur la courbe -- glisser le CORPS de la
    ligne change la frequence, cliquer la POIGNEE active/desactive), et
    une frequence AFFICHEE ET EDITABLE (double-clic pour taper une valeur)
    sous chaque ligne. Cliquer une zone de bande (ailleurs qu'une ligne ou
    le bandeau solo) la SELECTIONNE (voir onBandSelected) -- pilote quel
    panneau de bande unique est affiche en dessous (voir
    SevenpackEditorContent::selectedBand).

    CROSSOVER "ENTIEREMENT MODULABLE" (retour utilisateur) : un crossover
    DESACTIVE (bande fusionnee, voir SevenpackAudioProcessor::
    computeGroupOwners()) ne bloque JAMAIS le glissement d'un crossover
    ACTIF voisin -- voir computeActiveDragBounds() (qui ne regarde que les
    voisins actifs pour borner le drag) et pushDisabledNeighborsIntoOrder()
    (qui repousse silencieusement les crossovers desactives hors du
    chemin pour preserver l'ordre croissant STRICT entre les 4, necessaire
    au DSP -- voir Crossover.h -- mais invisible/inaudible puisqu'ils sont
    desactives). Voir aussi Crossover::kSplitMinHz/MaxHz (desormais
    20Hz-20kHz uniformement, plus de bornes etroites par "slot").

    Meme pattern de geste JUCE (beginChangeGesture/setValueNotifyingHost/
    endChangeGesture) et meme mapping log-frequence (xFracToFreq/
    freqToXFrac), meme edition texte au double-clic (voir KnobSlider.h)
    que le reste du plugin.

    LIGNE ELASTIQUE (retour utilisateur, remplace les anciennes lignes
    draggables IN/RANGE) : un indicateur visuel non-interactif par bande,
    voir drawElasticLine() -- tendu (vert) sous la compression, detendu
    (bleu) sous l'expansion, reagit aux valeurs REELLES du DSP (meme
    source que la colorisation du spectre, voir drawSpectrum()). Input et
    Expand Range se reglent desormais uniquement via les knobs du panneau
    de bande, plus depuis ce composant.
*/
class CrossoverSpectrumEditor : public juce::Component,
                                 private juce::Timer,
                                 private juce::TextEditor::Listener
{
public:
    explicit CrossoverSpectrumEditor (SevenpackAudioProcessor& p) : processorRef (p)
    {
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            freqParam[(size_t) x]    = processorRef.apvts.getParameter (SevenpackAudioProcessor::idCrossover[x]);
            enabledParam[(size_t) x] = processorRef.apvts.getParameter (SevenpackAudioProcessor::idCrossoverEnabled[x]);
        }
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
        {
            soloParam[(size_t) band] = processorRef.apvts.getParameter (SevenpackAudioProcessor::idSolo[band]);
            inputParam[(size_t) band] = processorRef.apvts.getParameter (
                SevenpackAudioProcessor::bandParamId (SevenpackAudioProcessor::idInput, band));
        }

        freqEditor.setJustification (juce::Justification::centred);
        freqEditor.setInputRestrictions (6, "0123456789.");
        freqEditor.setSelectAllWhenFocused (true);
        freqEditor.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        freqEditor.addListener (this);
        freqEditor.setVisible (false);
        addChildComponent (freqEditor);

        startTimerHz (30);
    }

    // Appele au clic sur une zone de bande (pas sur une ligne de
    // crossover ni un bouton solo) -- l'appelant (SevenpackEditorContent)
    // l'utilise pour decider quel panneau de bande afficher.
    std::function<void (int)> onBandSelected;

    void setSelectedBand (int band) noexcept
    {
        if (selectedBand != band) { selectedBand = band; repaint(); }
    }

    // Sevenpack : les 7 tranches sont toujours visibles (voir BandStrip.h),
    // plus de "bande selectionnee" -- desactive le clic de selection sur
    // les zones de bande et la mise en avant de la zone choisie. Drag/
    // activation des crossovers et rangee SOLO restent inchanges.
    void setBandSelectionEnabled (bool shouldBeEnabled) noexcept
    {
        bandSelectionEnabled = shouldBeEnabled;
        if (! shouldBeEnabled)
            setSelectedBand (-1);
    }

    // Interface v2 "Sevenpack Alpha" (maquette 7PACK) : fond clair arrondi,
    // zones de bande en pastel, plus de bandeau/rangee SOLO (le SOLO est
    // sous l'aiguille de chaque tranche), 7 points IN par bande deplacables
    // a la souris (glisser vertical = IN de la bande), animation des
    // balayage lumineux pendant l'ecoute du bouton LEARN (LEARN lui-meme identique a Sixpack). Voir paintAlpha().
    void setAlphaStyle (bool shouldUseAlpha)
    {
        alphaStyle = shouldUseAlpha;
        repaint();
    }

    void resized() override { commitFreqEditor(); }

    void paint (juce::Graphics& g) override
    {
        if (alphaStyle)
        {
            paintAlpha (g);
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a1620));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto* laf = dynamic_cast<SevenpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();
        if (! powered)
        {
            g.setColour (juce::Colour (0xff2a3540));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
            return;
        }

        const auto lay = computeLayout();
        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (bounds, 4.0f);
        g.reduceClipRegion (clip);

        const auto owner = computeGroupOwners();
        float boundaryHz[(size_t) SevenpackAudioProcessor::NumBands + 1];
        boundaryHz[0] = 20.0f;
        boundaryHz[SevenpackAudioProcessor::NumBands] = 20000.0f;
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
            boundaryHz[x + 1] = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());

        // --- Bandeau fin (groupes actifs) + rangee solo ---
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band)
                continue;
            int groupEnd = band;
            while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band)
                ++groupEnd;

            const float x0 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * lay.plot.getWidth();
            const float x1 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * lay.plot.getWidth();
            const bool isSelected = (selectedBand >= band && selectedBand <= groupEnd);
            const juce::Colour accent = bandAccent (band);
            juce::Rectangle<float> zone (x0, lay.strip.getY(), juce::jmax (1.0f, x1 - x0), lay.strip.getHeight());

            g.setColour (accent);
            g.fillRect (zone);

            // Zone de selection (sur le spectre, sous la rangee solo).
            juce::Rectangle<float> plotZone (x0, lay.plot.getY(), juce::jmax (1.0f, x1 - x0), lay.plot.getHeight());
            g.setColour (accent.withAlpha (isSelected ? 0.18f : 0.07f));
            g.fillRect (plotZone);

            // CADRE neon complet (haut/bas/cotes), pas juste un trait vertical
            // -- retour utilisateur explicite : "ça fait des barres blanches
            // alors que sur le design c'est des cadres". Legerement resserre
            // (gap) pour se detacher visuellement du voisin, avec son propre
            // halo dans la couleur de la bande (le trait de crossover, lui,
            // reste fin/discret -- voir drawCrossoverLine()).
            constexpr float frameGap = 2.0f;
            auto frameRect = plotZone.reduced (frameGap, 0.0f);
            juce::Path frameOutline;
            frameOutline.addRoundedRectangle (frameRect, 3.0f);
            // DropShadow blurrit la silhouette REMPLIE d'un Path -- sur un
            // rectangle plein ca noie tout l'interieur de couleur (verifie
            // en capture). Il faut lui donner un anneau fin (deja stroke),
            // pas la forme pleine, pour que le halo ne rayonne que depuis
            // la bordure.
            juce::Path frameRing;
            juce::PathStrokeType (1.4f).createStrokedPath (frameRing, frameOutline);
            juce::DropShadow (accent.withAlpha (isSelected ? 0.85f : 0.55f), isSelected ? 7 : 5, { 0, 0 }).drawForPath (g, frameRing);
            g.setColour (accent.withAlpha (isSelected ? 0.95f : 0.75f));
            g.strokePath (frameOutline, juce::PathStrokeType (isSelected ? 1.6f : 1.3f));

            drawSoloButton (g, lay.soloRow, x0, x1, band, accent);
            drawElasticLine (g, lay.plot, band, x0, x1, accent);
        }

        drawSpectrum (g, lay.plot, owner, boundaryHz);

        // prevPillRight : quand un crossover desactive est pousse contre son
        // voisin actif (voir pushDisabledNeighborsIntoOrder()), leurs deux
        // pastilles de frequence se retrouvent au meme endroit et le texte
        // se superpose en charabia illisible -- on saute la pastille (mais
        // pas le trait/la poignee, toujours draggable) si elle chevauche la
        // derniere pastille reellement dessinee.
        float prevPillRight = -1.0e9f;
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
            drawCrossoverLine (g, lay, x, boundaryHz[(size_t) (x + 1)], prevPillRight);

        g.restoreState();
        g.setColour (juce::Colour (0xff2a3540));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        commitFreqEditor();
        const auto lay = computeLayout();

        if (alphaStyle)
        {
            const int hit = hitInputPoint (e.position, lay.plot);
            if (hit >= 0)
            {
                landStartMs = 0.0; // le glisser prend la main sur l'animation LEARN
                draggingPoint = hit;
                inputParam[(size_t) hit]->beginChangeGesture();
                return;
            }
        }

        // 1. Rangee SOLO -- un bouton par groupe actif.
        if (lay.soloRow.contains (e.position))
        {
            const auto owner = computeGroupOwners();
            float boundaryHz[(size_t) SevenpackAudioProcessor::NumBands + 1];
            boundaryHz[0] = 20.0f; boundaryHz[SevenpackAudioProcessor::NumBands] = 20000.0f;
            for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
                boundaryHz[x + 1] = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());

            for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
            {
                if (owner[(size_t) band] != band) continue;
                int groupEnd = band;
                while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band) ++groupEnd;
                const float x0 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * lay.plot.getWidth();
                const float x1 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * lay.plot.getWidth();
                if (e.position.x >= x0 && e.position.x < x1)
                {
                    auto* sp = soloParam[(size_t) band];
                    const bool on = sp->getValue() > 0.5f;
                    sp->beginChangeGesture();
                    sp->setValueNotifyingHost (on ? 0.0f : 1.0f);
                    sp->endChangeGesture();
                    repaint();
                    return;
                }
            }
            return;
        }

        // 2. Lignes de crossover -- poignee (toggle enabled) vs corps (drag).
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            const float freq = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
            const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
            if (std::abs (e.position.x - lineX) > kLineHitRadius)
                continue;

            const float handleY = crossoverHandleY (lay.plot);
            if (juce::Point<float> (lineX, handleY).getDistanceFrom (e.position) <= kHandleHitRadius)
                toggleCrossoverEnabled (x);
            else
            {
                draggingCrossover = x;
                freqParam[(size_t) x]->beginChangeGesture();
                dragTo (x, e.position, lay.plot);
            }
            return;
        }

        // 3. Sinon : selectionne la bande dont la zone contient ce point X.
        if (bandSelectionEnabled && lay.plot.contains (e.position))
        {
            const float freq = xFracToFreq (juce::jlimit (0.0f, 1.0f, (e.position.x - lay.plot.getX()) / lay.plot.getWidth()));
            selectBandAtFrequency (freq);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingCrossover >= 0) { dragTo (draggingCrossover, e.position, computeLayout().plot); return; }
        if (draggingPoint >= 0)
        {
            auto* p = inputParam[(size_t) draggingPoint];
            p->setValueNotifyingHost (p->convertTo0to1 (yToInputDb (e.position.y, computeLayout().plot)));
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingPoint >= 0)
        {
            inputParam[(size_t) draggingPoint]->endChangeGesture();
            draggingPoint = -1;
        }
        if (draggingCrossover >= 0)
        {
            freqParam[(size_t) draggingCrossover]->endChangeGesture();
            draggingCrossover = -1;
        }
    }

    // Retour utilisateur : "quand le curseur de souris est sur une bande
    // blanche, le curseur passe en mode fleche dans les deux sens pour
    // dire visuellement qu'on est sur une partie mobile" -- meme rayon de
    // detection que mouseDown() (kLineHitRadius), sur le CORPS du trait
    // (pas juste la poignee) puisque tout le trait est draggable.
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto lay = computeLayout();
        if (alphaStyle && hitInputPoint (e.position, lay.plot) >= 0)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            return;
        }
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            const float freq = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
            const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
            if (lay.plot.getY() <= e.position.y && e.position.y <= lay.plot.getBottom()
                && std::abs (e.position.x - lineX) <= kLineHitRadius)
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto lay = computeLayout();
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            const auto pill = freqPillBounds (lay, x);
            if (pill.contains (e.position))
            {
                beginEditingCrossover (x, pill);
                return;
            }
        }
    }

private:
    static constexpr int kCurveSamples = 1024;
    static constexpr float kLogMin = 1.30103f;  // log10(20)
    static constexpr float kLogMax = 4.30103f;  // log10(20000)
    static constexpr float kLineHitRadius = 7.0f;
    static constexpr float kHandleHitRadius = 8.0f;
    static constexpr float kMinRatio = 1.05f;

    struct Layout
    {
        juce::Rectangle<float> strip, soloRow, plot, freqRow;
    };

    Layout computeLayout() const noexcept
    {
        if (alphaStyle)
        {
            // Pas de bandeau ni de rangee SOLO : le plot occupe tout, la
            // frequence s'ecrit en haut de chaque ligne (freqRow).
            Layout lay;
            lay.plot = getLocalBounds().toFloat().reduced (3.0f);
            lay.freqRow = lay.plot.withHeight (16.0f).translated (0.0f, 8.0f); // sous le bandeau pastel
            return lay;
        }

        auto content = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
        Layout lay;
        lay.strip = content.removeFromTop (5.0f);
        content.removeFromTop (2.0f);
        lay.soloRow = content.removeFromTop (15.0f);
        content.removeFromTop (2.0f);
        lay.freqRow = content.removeFromBottom (19.0f); // retour utilisateur : "les chiffres sont trop petits"
        content.removeFromBottom (2.0f);
        lay.plot = content;
        return lay;
    }

    void timerCallback() override
    {
        // Enveloppe de l'animation LEARN (montee/descente douces, voir
        // paintAlpha()) -- active pendant l'ecoute et l'analyse.
        const auto st = processorRef.aiLearnAnalyzer.getState();
        const bool learning = st == AiLearnAnalyzer::State::Capturing || st == AiLearnAnalyzer::State::Analyzing;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (learning && ! learningNow)
        {
            learnStartMs = now;
            landStartMs = 0.0;
        }
        // Fin de l'ecoute : les points partent de leur position affichee
        // (agitee) et glissent avec rebond vers la valeur reelle (2.6 s).
        if (! learning && learningNow)
        {
            landStartMs = now;
            landFromY = shownY;
        }
        if (landStartMs > 0.0 && now - landStartMs > kLandMs)
            landStartMs = 0.0;
        learningNow = learning;

        learnEnv += ((learning ? 1.0f : 0.0f) - learnEnv) * (learning ? 0.12f : 0.25f);
        if (! learning && learnEnv < 0.002f)
            learnEnv = 0.0f;

        const int wantedHz = (learning || landStartMs > 0.0) ? 60 : 30;
        if (wantedHz != timerHz)
        {
            timerHz = wantedHz;
            startTimerHz (wantedHz);
        }
        repaint();
    }

    // Halo "neon glow" -- vrai flou (juce::DropShadow, meme classe que
    // l'ombre embossee de ToggleSwitch::paintButton()) plutot qu'une
    // approximation en traits superposes : rendu rond/doux fidele a la
    // preview mcp__visualize (feGaussianBlur + merge), direction "B"
    // validee par l'utilisateur.
    // Traits/poignees de crossover : DISCRETS -- c'est le cadre par bande
    // (voir paint()) qui porte le neon, pas le trait. Un petit halo suffit
    // a lire "partie mobile" sans dominer visuellement le panneau.
    static void glowLine (juce::Graphics& g, float x, float yTop, float yBottom, juce::Colour core)
    {
        juce::Path p;
        p.addRectangle (x - 0.6f, yTop, 1.2f, yBottom - yTop);
        juce::DropShadow (core.withAlpha (0.45f), 3, { 0, 0 }).drawForPath (g, p);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillPath (p);
    }

    static void glowDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour core)
    {
        juce::Path p;
        p.addEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        juce::DropShadow (core.withAlpha (0.45f), 3, { 0, 0 }).drawForPath (g, p);
        g.setColour (core);
        g.fillPath (p);
    }

    static float xFracToFreq (float xt) noexcept { return std::pow (10.0f, kLogMin + xt * (kLogMax - kLogMin)); }
    static float freqToXFrac (float freq) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (std::log10 (juce::jmax (20.0f, freq)) - kLogMin) / (kLogMax - kLogMin));
    }

    // Meme calcul que SevenpackAudioProcessor::computeGroupOwners() --
    // reimplemente ici cote UI (lecture des parametres uniquement).
    std::array<int, (size_t) SevenpackAudioProcessor::NumBands> computeGroupOwners() const noexcept
    {
        std::array<int, (size_t) SevenpackAudioProcessor::NumBands> owner {};
        owner[0] = 0;
        for (int b = 1; b < SevenpackAudioProcessor::NumBands; ++b)
        {
            const bool enabled = enabledParam[(size_t) (b - 1)]->getValue() > 0.5f;
            owner[(size_t) b] = enabled ? b : owner[(size_t) (b - 1)];
        }
        return owner;
    }

    void selectBandAtFrequency (float freq)
    {
        const auto owner = computeGroupOwners();
        for (int band = SevenpackAudioProcessor::NumBands - 1; band >= 0; --band)
        {
            const float lowerBound = (band == 0) ? 20.0f
                : freqParam[(size_t) (band - 1)]->convertFrom0to1 (freqParam[(size_t) (band - 1)]->getValue());
            if (freq >= lowerBound)
            {
                const int b = owner[(size_t) band];
                setSelectedBand (b);
                if (onBandSelected != nullptr)
                    onBandSelected (b);
                return;
            }
        }
    }

    // --- "Entierement modulable" (voir doc de classe) ---

    struct DragBounds { float lo, hi; };

    // Ne regarde que les voisins ACTIFS pour borner le drag de x -- un
    // crossover desactive entre x et son prochain voisin actif est
    // totalement transparent pour cette limite (jamais un mur).
    DragBounds computeActiveDragBounds (int x) const noexcept
    {
        DragBounds b { 20.0f, 20000.0f };
        for (int i = x - 1; i >= 0; --i)
            if (enabledParam[(size_t) i]->getValue() > 0.5f)
            { b.lo = freqParam[(size_t) i]->convertFrom0to1 (freqParam[(size_t) i]->getValue()) * kMinRatio; break; }
        for (int i = x + 1; i < SevenpackAudioProcessor::NumCrossovers; ++i)
            if (enabledParam[(size_t) i]->getValue() > 0.5f)
            { b.hi = freqParam[(size_t) i]->convertFrom0to1 (freqParam[(size_t) i]->getValue()) / kMinRatio; break; }
        return b;
    }

    // Apres avoir deplace le crossover ACTIF movedIndex a movedFreq,
    // repousse silencieusement tout crossover DESACTIVE contigu (des deux
    // cotes, jusqu'au premier ACTIF rencontre) qui se retrouverait sinon
    // hors de l'ordre croissant STRICT -- necessaire au DSP (voir
    // Crossover.h) mais invisible/inaudible puisque desactive.
    void pushDisabledNeighborsIntoOrder (int movedIndex, float movedFreq)
    {
        float floor = movedFreq;
        for (int i = movedIndex + 1; i < SevenpackAudioProcessor::NumCrossovers; ++i)
        {
            if (enabledParam[(size_t) i]->getValue() > 0.5f) break;
            const float hz = freqParam[(size_t) i]->convertFrom0to1 (freqParam[(size_t) i]->getValue());
            if (hz < floor)
                freqParam[(size_t) i]->setValueNotifyingHost (freqParam[(size_t) i]->convertTo0to1 (floor));
        }
        float ceiling = movedFreq;
        for (int i = movedIndex - 1; i >= 0; --i)
        {
            if (enabledParam[(size_t) i]->getValue() > 0.5f) break;
            const float hz = freqParam[(size_t) i]->convertFrom0to1 (freqParam[(size_t) i]->getValue());
            if (hz > ceiling)
                freqParam[(size_t) i]->setValueNotifyingHost (freqParam[(size_t) i]->convertTo0to1 (ceiling));
        }
    }

    void toggleCrossoverEnabled (int x)
    {
        auto* ep = enabledParam[(size_t) x];
        const bool wasEnabled = ep->getValue() > 0.5f;

        if (! wasEnabled)
        {
            // Re-activation : si la frequence figee est degeneree (hors de
            // l'intervalle desormais delimite par ses voisins actifs), la
            // recentre plutot que de reveler une bande quasi nulle.
            const auto bounds = computeActiveDragBounds (x);
            const float hz = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
            if (hz < bounds.lo || hz > bounds.hi)
            {
                const float mid = std::sqrt (bounds.lo * bounds.hi);
                freqParam[(size_t) x]->setValueNotifyingHost (freqParam[(size_t) x]->convertTo0to1 (mid));
            }
        }

        ep->beginChangeGesture();
        ep->setValueNotifyingHost (wasEnabled ? 0.0f : 1.0f);
        ep->endChangeGesture();
        repaint();
    }

    void dragTo (int x, juce::Point<float> pos, juce::Rectangle<float> plot)
    {
        const float xt = juce::jlimit (0.0f, 1.0f, (pos.x - plot.getX()) / plot.getWidth());
        const auto bounds = computeActiveDragBounds (x);
        const float freq = juce::jlimit (bounds.lo, bounds.hi, xFracToFreq (xt));
        freqParam[(size_t) x]->setValueNotifyingHost (freqParam[(size_t) x]->convertTo0to1 (freq));
        pushDisabledNeighborsIntoOrder (x, freq);
        repaint();
    }

    // --- Edition texte de la frequence (double-clic, meme pattern que KnobSlider) ---

    juce::Rectangle<float> freqPillBounds (const Layout& lay, int x) const noexcept
    {
        const float freq = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
        const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
        return juce::Rectangle<float> (54.0f, lay.freqRow.getHeight()).withCentre ({ lineX, lay.freqRow.getCentreY() });
    }

    void beginEditingCrossover (int x, juce::Rectangle<float> pill)
    {
        editingCrossover = x;
        const float freq = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
        freqEditor.setText (juce::String (juce::roundToInt (freq)), juce::dontSendNotification);
        freqEditor.setBounds (pill.toNearestInt().expanded (2, 1));
        freqEditor.setVisible (true);
        freqEditor.toFront (true);
        freqEditor.grabKeyboardFocus();
        freqEditor.selectAll();
    }

    void commitFreqEditor()
    {
        if (editingCrossover < 0)
            return;
        const int x = editingCrossover;
        editingCrossover = -1;
        freqEditor.setVisible (false);

        const float typed = freqEditor.getText().getFloatValue();
        if (typed <= 0.0f)
            return;
        const auto bounds = computeActiveDragBounds (x);
        const float freq = juce::jlimit (bounds.lo, bounds.hi, typed);
        freqParam[(size_t) x]->beginChangeGesture();
        freqParam[(size_t) x]->setValueNotifyingHost (freqParam[(size_t) x]->convertTo0to1 (freq));
        freqParam[(size_t) x]->endChangeGesture();
        pushDisabledNeighborsIntoOrder (x, freq);
        repaint();
    }

    void textEditorReturnKeyPressed (juce::TextEditor&) override { commitFreqEditor(); }
    void textEditorFocusLost (juce::TextEditor&) override { commitFreqEditor(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { editingCrossover = -1; freqEditor.setVisible (false); }

    // --- Dessin ---

    void drawSoloButton (juce::Graphics& g, juce::Rectangle<float> soloRow, float x0, float x1, int band, juce::Colour accent)
    {
        const auto centre = juce::Point<float> ((x0 + x1) * 0.5f, soloRow.getCentreY());
        constexpr float r = 6.0f;
        const bool on = soloParam[(size_t) band]->getValue() > 0.5f;

        if (on)
        {
            glowDot (g, centre, r, accent);
            g.setColour (juce::Colour (0xff0a1620));
        }
        else
        {
            g.setColour (accent.withAlpha (0.8f));
            g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.5f);
        }
        g.setFont (juce::Font (juce::FontOptions (7.5f, juce::Font::bold)));
        g.drawFittedText ("S", juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre).toNearestInt(),
                           juce::Justification::centred, 1);
    }

    // Ligne "elastique" par bande (retour utilisateur : remplace les deux
    // anciennes lignes draggables IN/RANGE par un indicateur visuel unique,
    // en lecture seule, qui reagit a la compression ET l'expansion REELLES
    // -- meme source (grMeterDb/grMeterExpandDb) que la colorisation du
    // spectre ci-dessous, voir drawSpectrum()). Au repos : trait droit au
    // centre vertical du plot, comme un elastique detendu. La compression
    // le TEND vers le haut (vert, plus epais/lumineux avec l'intensite),
    // l'expansion le DETEND vers le bas (bleu) -- deux forces opposees sur
    // le meme elastique, la position nette refletant laquelle domine a cet
    // instant. Courbe de Bezier quadratique (un seul point de controle au
    // centre) pour la silhouette "corde pincee" plutot qu'un trait droit.
    void drawElasticLine (juce::Graphics& g, juce::Rectangle<float> plot, int band, float x0, float x1, juce::Colour accent)
    {
        const float compT   = juce::jlimit (0.0f, 1.0f, processorRef.grMeterDb[(size_t) band].load() / 15.0f);
        const float expandT = juce::jlimit (0.0f, 1.0f, processorRef.grMeterExpandDb[(size_t) band].load() / 15.0f);
        const float tension = juce::jmax (compT, expandT);

        const float restY = plot.getCentreY();
        const float maxBow = plot.getHeight() * 0.32f;
        const float bowY = restY - (compT - expandT) * maxBow;

        const float cx = (x0 + x1) * 0.5f;
        juce::Path elastic;
        elastic.startNewSubPath (x0 + 2.0f, restY);
        elastic.quadraticTo (cx, bowY, x1 - 2.0f, restY);

        const juce::Colour tint = compT >= expandT ? SevenpackColours::ledGreen : SevenpackColours::ledBlue;
        const juce::Colour colour = accent.interpolatedWith (tint, tension * 0.85f);

        if (tension > 0.02f)
        {
            juce::Path stroked;
            juce::PathStrokeType (1.4f + tension * 1.3f).createStrokedPath (stroked, elastic);
            juce::DropShadow (colour.withAlpha (0.5f), juce::roundToInt (3.0f + tension * 4.0f), { 0, 0 }).drawForPath (g, stroked);
        }
        g.setColour (colour.withAlpha (0.55f + 0.4f * tension));
        g.strokePath (elastic, juce::PathStrokeType (1.4f + tension * 1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Position Y FIXE de la poignee (retour utilisateur : "des fois il faut
    // chercher le point d'activation et desactivation du cross over" --
    // avant, la poignee suivait la hauteur de la courbe du spectre, donc
    // se retrouvait n'importe ou selon le niveau du signal a cette
    // frequence, parfois ecrasee en haut ou en bas du plot). Toujours au
    // meme endroit, juste au-dessus des lignes elastiques, quel que soit
    // le contenu audio -- meme calcul utilise pour le dessin et le
    // hit-test (voir mouseDown()).
    float crossoverHandleY (juce::Rectangle<float> plot) const noexcept
    {
        // Alpha : poignee en BAS de la ligne (le haut porte la frequence).
        return alphaStyle ? plot.getBottom() - 11.0f : plot.getY() + 13.0f;
    }

    // --- Interface Alpha ---------------------------------------------------

    static float inputDbToY (float db, juce::Rectangle<float> plot) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + 30.0f) / 60.0f);
        return plot.getY() + 24.0f + (1.0f - t) * (plot.getHeight() - 48.0f);
    }

    static float yToInputDb (float y, juce::Rectangle<float> plot) noexcept
    {
        const float t = 1.0f - (y - plot.getY() - 24.0f) / juce::jmax (1.0f, plot.getHeight() - 48.0f);
        return juce::jlimit (-30.0f, 30.0f, -30.0f + 60.0f * t);
    }

    // Position (x = milieu de la zone de la bande, y = IN) des 7 points --
    // un point par bande PROPRIETAIRE (une bande fusionnee n'en a pas).
    std::array<juce::Point<float>, (size_t) SevenpackAudioProcessor::NumBands> inputPoints (juce::Rectangle<float> plot,
                                                                                         const float* boundaryHz,
                                                                                         const std::array<int, (size_t) SevenpackAudioProcessor::NumBands>& owner) const
    {
        std::array<juce::Point<float>, (size_t) SevenpackAudioProcessor::NumBands> pts {};
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band) { pts[(size_t) band] = { -1000.0f, -1000.0f }; continue; }
            int groupEnd = band;
            while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band) ++groupEnd;
            const float x0 = plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * plot.getWidth();
            const float x1 = plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * plot.getWidth();
            const auto* p = inputParam[(size_t) band];
            pts[(size_t) band] = { (x0 + x1) * 0.5f, inputDbToY (p->convertFrom0to1 (p->getValue()), plot) };
        }
        return pts;
    }

    // Les lignes affichent toujours les VRAIES frequences : LEARN reste
    // strictement identique a Sixpack (demande explicite), seul un balayage
    // lumineux signale l'ecoute (voir paintAlpha()).
    void currentBoundaries (float* boundaryHz) const
    {
        boundaryHz[0] = 20.0f;
        boundaryHz[SevenpackAudioProcessor::NumBands] = 20000.0f;
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
            boundaryHz[x + 1] = juce::jlimit (20.0f, 20000.0f, freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue()));
    }

    int hitInputPoint (juce::Point<float> pos, juce::Rectangle<float> plot) const
    {
        float boundaryHz[(size_t) SevenpackAudioProcessor::NumBands + 1];
        currentBoundaries (boundaryHz);
        const auto pts = inputPoints (plot, boundaryHz, computeGroupOwners());
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
            if (pts[(size_t) band].getDistanceFrom (pos) <= 9.0f)
                return band;
        return -1;
    }

    // Ecran LCD turquoise (demande explicite, d'apres l'image fournie par
    // l'utilisateur) : fond #198d8b -> #128f91, 5 lignes horizontales a la
    // couleur pastel de chaque zone, lignes de crossover a la couleur de
    // leur bande, reflet triangulaire en haut a gauche. L'ecran est ENFONCE
    // dans le rack : ombre interieure forte en haut ici, cuvette biseautee
    // peinte autour par l'editeur (rebuildBackgroundCache()). Couleurs FIXES
    // quel que soit le theme (c'est un ecran).
    void paintAlpha (juce::Graphics& g)
    {
        const auto pal = AlphaPalette::get();
        auto bounds = getLocalBounds().toFloat().reduced (0.75f);
        constexpr float corner = 14.0f;
        const auto* laf = dynamic_cast<SevenpackLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();
        const juce::Colour screenInk (0xff0b4f4e); // contour sombre des pastilles

        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (bounds, corner);
        g.reduceClipRegion (clip);
        if (pal.sunny)
        {
            // Design de Sunny : turquoise tres sombre au centre, lueur claire
            // sur les bords haut et bas.
            juce::ColourGradient base (juce::Colour (0xff0c787a), 0.0f, bounds.getY(), juce::Colour (0xff289090), 0.0f, bounds.getBottom(), false);
            base.addColour (0.16, juce::Colour (0xff0a3c3d));
            base.addColour (0.30, juce::Colour (0xff092d2d));
            base.addColour (0.72, juce::Colour (0xff092d2d));
            base.addColour (0.90, juce::Colour (0xff165f60));
            g.setGradientFill (base);
            g.fillRect (bounds);
        }
        else
        {
            juce::ColourGradient base (juce::Colour (0xff198d8b), bounds.getX(), 0.0f,
                                       juce::Colour (0xff128f91), bounds.getRight(), 0.0f, false);
            g.setGradientFill (base);
            g.fillRect (bounds);
        }

        const auto lay = computeLayout();
        const auto& plot = lay.plot;
        const auto owner = computeGroupOwners();
        float boundaryHz[(size_t) SevenpackAudioProcessor::NumBands + 1];
        currentBoundaries (boundaryHz);
        const int colourMode = juce::jlimit (0, 2, processorRef.bandColourMode.load());
        auto pastelOf = [colourMode] (int band)
        {
            return colourMode == 2 ? juce::Colours::white : AlphaPalette::bandPastel (band);
        };

        // Zones de bande (groupes fusionnes compris) : x de debut/fin.
        struct Zone { int band; float x0, x1; };
        std::vector<Zone> zones;
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band) continue;
            int groupEnd = band;
            while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band) ++groupEnd;
            const float x0 = band == 0 ? bounds.getX() : plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * plot.getWidth();
            const float x1 = groupEnd == SevenpackAudioProcessor::NumBands - 1
                                 ? bounds.getRight() : plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * plot.getWidth();
            zones.push_back ({ band, x0, x1 });

            auto zone = juce::Rectangle<float> (x0, bounds.getY(), juce::jmax (1.0f, x1 - x0), bounds.getHeight());
            if (colourMode == 1) // TEINTE : voile pastel sur toute la zone
            {
                g.setColour (AlphaPalette::bandPastel (band).withAlpha (0.10f));
                g.fillRect (zone);
            }
            if (selectedBand >= band && selectedBand <= groupEnd)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRect (zone);
            }
        }

        // Spectre d'entree. Style au choix (Options > Meters > Spectrum,
        // apercu claude.ai/artifact/CQSz1SFTqHyx7Ggqb6PzCr, demande
        // explicite : "la ligne qui dessine la musique ... pas tres
        // visible", puis "dans le menu option avec la possibilite de
        // desactiver") : 0 CLASSIC (d'origine : voile blanc translucide sous
        // les lignes horizontales), 1 LINE, 2 GLOW, 3 BANDS, 4 FILLED
        // (dessines PAR-DESSUS les lignes horizontales).
        const int spectrumStyle = juce::jlimit (0, 4, processorRef.spectrumStyle.load());
        const bool spectrumOn = powered && processorRef.spectrumAnalyzer.getSampleRate() > 0.0;
        juce::Path specFill, specLine, peakLine;
        if (spectrumOn)
        {
            constexpr int n = kSpectrumPoints;
            std::array<float, n> raw {}, mag {};
            for (int i = 0; i < n; ++i)
                raw[(size_t) i] = processorRef.spectrumAnalyzer.getMagnitudeInterpolated (xFracToFreq ((float) i / (float) (n - 1)));
            if (processorRef.spectrumSmooth.load() == 0)
            {
                // Lissage d'origine : 3 points (1-2-1) sur les magnitudes.
                for (int i = 0; i < n; ++i)
                {
                    const int i0 = juce::jmax (0, i - 1), i2 = juce::jmin (n - 1, i + 1);
                    mag[(size_t) i] = (raw[(size_t) i0] + 2.0f * raw[(size_t) i] + raw[(size_t) i2]) * 0.25f;
                }
            }
            else
            {
                // Plus doux : gaussienne d'environ 1/6 d'octave (256 points
                // sur 10 octaves).
                static const auto weights = []
                {
                    std::array<float, 2 * kSmoothRadius + 1> w {};
                    for (int k = -kSmoothRadius; k <= kSmoothRadius; ++k)
                        w[(size_t) (k + kSmoothRadius)] = std::exp (-0.5f * (float) (k * k) / (2.6f * 2.6f));
                    return w;
                }();
                for (int i = 0; i < n; ++i)
                {
                    float s = 0.0f, sw = 0.0f;
                    for (int k = -kSmoothRadius; k <= kSmoothRadius; ++k)
                    {
                        const int j = i + k;
                        if (j < 0 || j >= n) continue;
                        const float w = weights[(size_t) (k + kSmoothRadius)];
                        s += w * raw[(size_t) j];
                        sw += w;
                    }
                    mag[(size_t) i] = s / sw;
                }
            }

            const double now = juce::Time::getMillisecondCounterHiRes();
            const float dt = lastSpectrumMs > 0.0 ? (float) juce::jlimit (0.0, 0.2, (now - lastSpectrumMs) / 1000.0) : 0.0f;
            lastSpectrumMs = now;
            const bool peakOn = processorRef.spectrumPeak.load();

            specFill.startNewSubPath (plot.getX(), plot.getBottom());
            for (int i = 0; i < n; ++i)
            {
                const float db = juce::Decibels::gainToDecibels (juce::jmax (0.0001f, mag[(size_t) i]), -54.0f);
                const float x = plot.getX() + (float) i / (float) (n - 1) * plot.getWidth();
                const float y = dbToSpectrumY (plot, db);
                specFill.lineTo (x, y);
                if (i == 0) specLine.startNewSubPath (x, y); else specLine.lineTo (x, y);

                // Ligne des cretes : maintien 0,9 s puis retombee 9 dB/s.
                auto& pk = peakDb[(size_t) i];
                auto& hold = peakHold[(size_t) i];
                if (db >= pk) { pk = db; hold = 0.9f; }
                else if ((hold -= dt) < 0.0f) pk = juce::jmax (-54.0f, pk - 9.0f * dt);
                if (peakOn)
                {
                    const float py = dbToSpectrumY (plot, pk);
                    if (i == 0) peakLine.startNewSubPath (x, py); else peakLine.lineTo (x, py);
                }
            }
            specFill.lineTo (plot.getRight(), plot.getBottom());
            specFill.closeSubPath();

            if (spectrumStyle == 0)
            {
                juce::ColourGradient sg (juce::Colours::white.withAlpha (0.20f), 0.0f, plot.getY(),
                                         juce::Colours::white.withAlpha (0.02f), 0.0f, plot.getBottom(), false);
                g.setGradientFill (sg);
                g.fillPath (specFill);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (specFill, juce::PathStrokeType (1.0f));
            }
        }

        // 5 lignes horizontales, a la couleur de la zone traversee.
        for (const auto& z : zones)
        {
            g.setColour (pastelOf (z.band).withAlpha (colourMode == 2 ? 0.35f : 0.72f));
            for (int k = 1; k <= 5; ++k)
            {
                const float y = std::round (plot.getY() + plot.getHeight() * (float) k / 6.0f);
                g.fillRect (z.x0, y, z.x1 - z.x0, 1.0f);
            }
        }

        if (spectrumOn)
        {
            const auto vGrad = [&plot] (juce::Colour c, float a0, float a1)
            {
                return juce::ColourGradient (c.withAlpha (a0), 0.0f, plot.getY(), c.withAlpha (a1), 0.0f, plot.getBottom(), false);
            };
            const auto stroke = [&g, &specLine] (juce::Colour c, float w)
            {
                g.setColour (c);
                g.strokePath (specLine, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            };
            const juce::Colour edgeShadow (0xff002628);
            switch (spectrumStyle)
            {
                case 1: // LINE : trait blanc net + lisere sombre dessous
                    g.setGradientFill (vGrad (juce::Colours::white, 0.24f, 0.03f));
                    g.fillPath (specFill);
                    stroke (edgeShadow.withAlpha (0.45f), 4.0f);
                    stroke (juce::Colours::white.withAlpha (0.95f), 1.8f);
                    break;
                case 2: // GLOW : phosphore d'oscilloscope (halo en traits superposes)
                    g.setGradientFill (vGrad (juce::Colour (0xffc8fff5), 0.16f, 0.0f));
                    g.fillPath (specFill);
                    stroke (juce::Colour (0xffbefff5).withAlpha (0.08f), 8.0f);
                    stroke (juce::Colour (0xffbefff5).withAlpha (0.16f), 5.0f);
                    stroke (juce::Colour (0xffd2fff8).withAlpha (0.38f), 2.8f);
                    stroke (juce::Colour (0xfff0fffc), 1.4f);
                    break;
                case 3: // BANDS : couleur pastel de chaque zone
                    for (const auto& z : zones)
                    {
                        juce::Graphics::ScopedSaveState zoneState (g);
                        g.reduceClipRegion (juce::Rectangle<float> (z.x0, bounds.getY(), juce::jmax (1.0f, z.x1 - z.x0), bounds.getHeight())
                                                .getSmallestIntegerContainer());
                        const auto c = pastelOf (z.band);
                        g.setGradientFill (vGrad (c, 0.30f, 0.04f));
                        g.fillPath (specFill);
                        stroke (edgeShadow.withAlpha (0.40f), 4.0f);
                        stroke (c, 2.0f);
                    }
                    break;
                case 4: // FILLED : montagne pleine claire + bord blanc
                    g.setGradientFill (vGrad (juce::Colours::white, 0.46f, 0.10f));
                    g.fillPath (specFill);
                    stroke (juce::Colours::white.withAlpha (0.95f), 1.4f);
                    break;
                default: break;
            }

            if (! peakLine.isEmpty())
            {
                const float dashes[] = { 3.0f, 3.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, peakLine, dashes, 2);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillPath (dashed);
            }
        }

        // Graduations du bas.
        g.setFont (AlphaDraw::font (9.0f));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            if (! pal.sunny || kSunnyShowScale)
        {
            const float x = plot.getX() + freqToXFrac (f) * plot.getWidth();
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "K" : juce::String ((int) f),
                        juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 14.0f, 34.0f, 12.0f), juce::Justification::centredLeft, false);
        }

        // Lignes de crossover : couleur pastel de la bande a leur gauche.
        g.setFont (AlphaDraw::lcdFont (AlphaType::value));
        float prevLabelRight = -1.0e9f;
        for (int x = 0; x < SevenpackAudioProcessor::NumCrossovers; ++x)
        {
            const bool enabled = enabledParam[(size_t) x]->getValue() > 0.5f;
            const float hz = boundaryHz[(size_t) (x + 1)];
            const float lx = plot.getX() + freqToXFrac (hz) * plot.getWidth();
            const auto lineColour = pastelOf (x);
            if (enabled)
            {
                g.setColour (lineColour);
                g.fillRect (lx - 1.0f, plot.getY(), 2.0f, plot.getHeight());
            }
            else
            {
                juce::Path dashed; dashed.startNewSubPath (lx, plot.getY()); dashed.lineTo (lx, plot.getBottom());
                const float dashes[] = { 4.0f, 3.0f };
                juce::Path stroked; juce::PathStrokeType (1.2f).createDashedStroke (stroked, dashed, dashes, 2);
                g.setColour (lineColour.withAlpha (0.55f));
                g.fillPath (stroked);
            }

            if (pal.sunny && ! kSunnyShowScale)
                continue;

            const float hy = crossoverHandleY (plot);
            auto handle = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ lx, hy });
            g.setColour (enabled ? lineColour : juce::Colour (0xff198d8b));
            g.fillEllipse (handle);
            g.setColour (enabled ? screenInk : lineColour);
            g.drawEllipse (handle, 1.5f);

            const juce::String label = hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + "k"
                                                     : juce::String (juce::roundToInt (hz));
            const auto pill = freqPillBounds (lay, x);
            if (pill.getX() >= prevLabelRight)
            {
                g.setColour (lineColour.withAlpha (enabled ? 1.0f : 0.55f));
                g.drawText (label, juce::Rectangle<float> (lx + 5.0f, lay.freqRow.getY(), 50.0f, lay.freqRow.getHeight()),
                            juce::Justification::centredLeft, false);
                prevLabelRight = pill.getRight();
            }
        }

        // Points IN par bande -- position AFFICHEE animee pendant LEARN comme
        // dans la preview HTML Sevenpack (demande explicite : les points
        // s'agitent pendant l'ecoute, puis glissent avec un rebond vers leur
        // nouvelle valeur). Purement visuel : les valeurs reellement
        // appliquees restent celles de Sixpack (etats : voir timerCallback()).
        {
            auto pts = inputPoints (plot, boundaryHz, owner);
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double tw = (now - learnStartMs) / 1000.0;
            for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
            {
                auto& p = pts[(size_t) band];
                if (p.x < plot.getX()) continue;
                const float wob = (float) std::sin (tw * 18.0 + band * 1.7) * kWobblePx;
                float y = p.y;
                if (learningNow)
                    y = p.y + wob * learnEnv;
                else if (landStartMs > 0.0)
                {
                    const float k = (float) juce::jlimit (0.0, 1.0, (now - landStartMs) / kLandMs);
                    constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
                    const float e = 1.0f + c3 * std::pow (k - 1.0f, 3.0f) + c1 * std::pow (k - 1.0f, 2.0f);
                    y = landFromY[(size_t) band] + (p.y - landFromY[(size_t) band]) * e
                        + (k < 0.7f ? wob * (1.0f - k / 0.7f) : 0.0f);
                }
                p.y = juce::jlimit (plot.getY() + 4.0f, plot.getBottom() - 4.0f, y);
                shownY[(size_t) band] = p.y;
            }

            juce::Path line;
            bool started = false;
            for (const auto& p : pts)
            {
                if (p.x < plot.getX()) continue;
                if (! started) { line.startNewSubPath (p); started = true; }
                else line.lineTo (p);
            }
            g.setColour (pal.sunny ? juce::Colour (0xff4fd0f5) : juce::Colours::white.withAlpha (0.7f));
            g.strokePath (line, juce::PathStrokeType (pal.sunny ? 2.4f : 1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
            {
                const auto& p = pts[(size_t) band];
                if (p.x < plot.getX()) continue;
                const auto colour = pastelOf (band);
                if (pal.sunny)
                {
                    // Design de Sunny : pastille pastel, fin contour clair.
                    auto dot = juce::Rectangle<float> (14.0f, 14.0f).withCentre (p);
                    g.setColour (juce::Colours::black.withAlpha (0.3f));
                    g.fillEllipse (dot.translated (0.0f, 1.0f));
                    g.setColour (colour);
                    g.fillEllipse (dot);
                    g.setColour (draggingPoint == band ? pal.accent : juce::Colours::white.withAlpha (0.7f));
                    g.drawEllipse (dot, draggingPoint == band ? 2.2f : 1.2f);
                    continue;
                }
                g.setColour (colour.withAlpha (0.3f));
                g.fillEllipse (juce::Rectangle<float> (21.0f, 21.0f).withCentre (p));
                auto dot = juce::Rectangle<float> (14.0f, 14.0f).withCentre (p);
                g.setColour (colour);
                g.fillEllipse (dot);
                // Contour sombre (accent pendant le glisser).
                g.setColour (draggingPoint == band ? pal.accent : screenInk);
                g.drawEllipse (dot, draggingPoint == band ? 2.2f : 1.6f);
            }
        }

        if (learnEnv > 0.05f)
        {
            auto tag = juce::Rectangle<float> (190.0f, 18.0f).withCentre ({ bounds.getCentreX(), bounds.getY() + 16.0f });
            g.setColour (pal.accent.withAlpha (learnEnv));
            g.fillRoundedRectangle (tag, 9.0f);
            AlphaDraw::spacedText (g, "LEARNING", tag, juce::Colours::white.withAlpha (learnEnv), AlphaType::label, 0.35f);
        }

        if (pal.sunny)
            paintSunnyGlass (g, bounds);
        else
        // Encastrement : ombre interieure (forte en haut, sous le rebord du
        // rack), arete sombre en haut, reflet discret sur l'arete basse.
        {
            const juce::Colour shade (0xff001416);
            auto edge = [&g, shade] (juce::Rectangle<float> r, juce::Point<float> from, juce::Point<float> to, float alpha)
            {
                g.setGradientFill (juce::ColourGradient (shade.withAlpha (alpha), from, shade.withAlpha (0.0f), to, false));
                g.fillRect (r);
            };
            const auto b = bounds;
            edge (b.withHeight (24.0f), { 0.0f, b.getY() }, { 0.0f, b.getY() + 24.0f }, 0.72f);
            edge (b.withTrimmedTop (b.getHeight() - 10.0f), { 0.0f, b.getBottom() }, { 0.0f, b.getBottom() - 10.0f }, 0.35f);
            edge (b.withWidth (16.0f), { b.getX(), 0.0f }, { b.getX() + 16.0f, 0.0f }, 0.5f);
            edge (b.withTrimmedLeft (b.getWidth() - 10.0f), { b.getRight(), 0.0f }, { b.getRight() - 10.0f, 0.0f }, 0.4f);

            // Vitre bombee (variante 1 validee sur l'apercu, a la place du
            // reflet triangulaire : "on voit qu'un rectangle a gauche, ca
            // fait pas ecran") : coins legerement assombris...
            {
                g.saveState();
                const auto c = b.getCentre();
                const float outer = b.getWidth() * 0.56f, squash = b.getHeight() / b.getWidth() * 4.2f;
                juce::ColourGradient vg (shade.withAlpha (0.0f), c, juce::Colour (0xff000f12).withAlpha (0.28f), c.translated (outer, 0.0f), true);
                vg.addColour (juce::jlimit (0.0, 1.0, (double) (b.getHeight() * 0.6f / outer)), shade.withAlpha (0.0f));
                g.addTransform (juce::AffineTransform::scale (1.0f, squash, c.x, c.y));
                g.setGradientFill (vg);
                g.fillRect (b.withSizeKeepingCentre (b.getWidth() * 1.2f, b.getHeight() / squash * 1.2f));
                g.restoreState();
            }
            // ... reflet doux sur toute la largeur du haut, bord inferieur
            // courbe, et fin liseret lumineux sous le bord.
            {
                juce::Path glass;
                glass.startNewSubPath (b.getX(), b.getY());
                glass.lineTo (b.getRight(), b.getY());
                glass.lineTo (b.getRight(), b.getY() + b.getHeight() * 0.30f);
                glass.cubicTo (b.getX() + b.getWidth() * 0.72f, b.getY() + b.getHeight() * 0.50f,
                               b.getX() + b.getWidth() * 0.28f, b.getY() + b.getHeight() * 0.50f,
                               b.getX(), b.getY() + b.getHeight() * 0.30f);
                glass.closeSubPath();
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0.0f, b.getY(),
                                                         juce::Colours::white.withAlpha (0.0f), 0.0f, b.getY() + b.getHeight() * 0.48f, false));
                g.fillPath (glass);
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.fillRect (b.getX() + 18.0f, b.getY() + 3.5f, b.getWidth() - 36.0f, 1.0f);
            }

            g.setColour (juce::Colour (0x8c000f12));
            g.fillRect (b.withHeight (1.5f));
            g.setColour (juce::Colour (0x29befffa));
            g.fillRect (b.withTrimmedTop (b.getHeight() - 1.5f));
        }

        if (! powered)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (bounds);
        }

        g.restoreState();
        g.setColour (juce::Colour (0xb3141c20));
        g.drawRoundedRectangle (bounds, corner, 1.5f);
    }

    // Vitre du design de Sunny : voile clair et bandes de reflet en biais sur
    // la gauche, tache de lumiere en haut a gauche.
    static void paintSunnyGlass (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const auto slant = [&g, b] (float topX0, float topX1, float botX0, float botX1, float alpha)
        {
            juce::Path p;
            p.startNewSubPath (b.getX() + b.getWidth() * topX0, b.getY());
            p.lineTo (b.getX() + b.getWidth() * topX1, b.getY());
            p.lineTo (b.getX() + b.getWidth() * botX1, b.getBottom());
            p.lineTo (b.getX() + b.getWidth() * botX0, b.getBottom());
            p.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.fillPath (p);
        };
        slant (0.0f, 0.10f, 0.0f, 0.02f, 0.15f);
        slant (0.12f, 0.14f, 0.04f, 0.06f, 0.15f);
        slant (0.145f, 0.158f, 0.065f, 0.078f, 0.09f);

        const juce::Point<float> spot (b.getX() + b.getWidth() * 0.05f, b.getY() + b.getHeight() * 0.17f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.5f), spot,
                                                 juce::Colours::white.withAlpha (0.0f), spot.translated (b.getWidth() * 0.055f, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (b.getWidth() * 0.11f, b.getHeight() * 0.3f).withCentre (spot));
    }

    void drawCrossoverLine (juce::Graphics& g, const Layout& lay, int x, float freq, float& prevPillRight)
    {
        const bool enabled = enabledParam[(size_t) x]->getValue() > 0.5f;
        const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
        const float handleY = crossoverHandleY (lay.plot);
        // Fond FIXE (0xff0a1620, jamais retheme, voir paint()) -- texte
        // FIXE clair aussi (chipTextBright, pas textBright qui s'assombrit
        // en theme clair).
        const juce::Colour colour = enabled ? SevenpackColours::chipTextBright.withAlpha (0.85f)
                                             : SevenpackColours::chipTextBright.withAlpha (0.35f);

        // Style "Neon Glow" (direction B validee en preview) -- un trait
        // actif rayonne un halo lumineux plutot que le simple relief
        // embouti plat d'avant, pour signaler visuellement "partie mobile"
        // (complete le curseur main gauche/droite, voir mouseMove()).
        if (enabled)
        {
            glowLine (g, lineX, lay.plot.getY(), lay.plot.getBottom(), colour);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.drawLine (lineX + 1.0f, lay.plot.getY(), lineX + 1.0f, lay.plot.getBottom(), 1.0f);

            juce::Path dashed;
            dashed.startNewSubPath (lineX, lay.plot.getY());
            dashed.lineTo (lineX, lay.plot.getBottom());
            const float dashLengths[] = { 4.0f, 3.0f };
            juce::Path strokedDashed;
            juce::PathStrokeType (1.1f).createDashedStroke (strokedDashed, dashed, dashLengths, 2);
            g.setColour (colour);
            g.fillPath (strokedDashed);
        }

        constexpr float r = 5.5f;
        if (enabled)
            glowDot (g, { lineX, handleY }, r, colour);
        else
        {
            g.setColour (colour);
            g.drawEllipse (lineX - r, handleY - r, r * 2.0f, r * 2.0f, 1.4f);
        }
        g.setColour (juce::Colour (0xff0a1620));
        g.drawEllipse (lineX - r, handleY - r, r * 2.0f, r * 2.0f, 1.0f);

        // Pastille frequence, editable au double-clic (voir mouseDoubleClick()) --
        // sautee si elle chevauche la precedente (voir commentaire d'appel).
        const auto pill = freqPillBounds (lay, x);
        if (pill.getX() >= prevPillRight)
        {
            g.setColour (juce::Colour (0xff0e1e29).withAlpha (enabled ? 1.0f : 0.5f));
            g.fillRoundedRectangle (pill, 3.0f);
            g.setColour (colour);
            g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
            const juce::String freqText = freq >= 1000.0f
                ? juce::String (freq / 1000.0f, 2) + " kHz"
                : juce::String (juce::roundToInt (freq)) + " Hz";
            g.drawFittedText (freqText, pill.toNearestInt(), juce::Justification::centred, 1);
            prevPillRight = pill.getRight();
        }
    }

    // Renvoie la position Y (courbe du spectre) pour chaque point
    // d'echantillonnage -- rempli par drawSpectrum(), reutilise par
    // drawCrossoverLine() pour poser les poignees SUR la courbe plutot
    // qu'a une hauteur fixe.
    std::array<float, kCurveSamples> drawSpectrum (juce::Graphics& g, juce::Rectangle<float> plot,
                                                     const std::array<int, (size_t) SevenpackAudioProcessor::NumBands>& owner,
                                                     const float* boundaryHz)
    {
        std::array<float, kCurveSamples> ys {};
        auto& spec = processorRef.spectrumAnalyzer;
        if (spec.getSampleRate() <= 0.0)
        {
            ys.fill (plot.getBottom());
            return ys;
        }

        // Lissage triangulaire sur les magnitudes BRUTES (deja interpolees
        // par bin fractionnaire ET normalisees en niveau -- voir
        // SpectrumAnalyzerState::getMagnitudeInterpolated()/autoGainRef)
        // avant de tracer -- elimine l'aspect "en pixels/marches" signale
        // et garde une hauteur de courbe comparable quel que soit le
        // niveau d'entree.
        std::array<float, kCurveSamples> raw {};
        for (int i = 0; i < kCurveSamples; ++i)
            raw[(size_t) i] = spec.getMagnitudeInterpolated (xFracToFreq ((float) i / (float) (kCurveSamples - 1)));

        juce::Path fill;
        fill.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < kCurveSamples; ++i)
        {
            const int i0 = juce::jmax (0, i - 2), i1 = juce::jmax (0, i - 1);
            const int i3 = juce::jmin (kCurveSamples - 1, i + 1), i4 = juce::jmin (kCurveSamples - 1, i + 2);
            const float mag = (raw[(size_t) i0] + 2.0f * raw[(size_t) i1] + 3.0f * raw[(size_t) i] + 2.0f * raw[(size_t) i3] + raw[(size_t) i4]) / 9.0f;

            // Echelle dB (retour utilisateur : "j'ai uniquement les basses
            // et j'ai plus rien dans les mediums aigues, on doit voir le
            // signal partout") -- mag est une AMPLITUDE lineaire normalisee
            // par le pic global (SpectrumAnalyzerState::autoGainRef), or la
            // plupart des programmes ont une pente spectrale marquee
            // (graves >> aigus en amplitude lineaire) : un mapping lineaire
            // ecrasait tout ce qui n'est pas le grave pres de zero, invisible
            // a l'oeil. Plancher -54dB choisi empiriquement (assez large
            // pour rendre visible un programme normalement incline, sans
            // remonter le bruit de fond en permanence) mappe sur toute la
            // hauteur du plot -- meme mag (donc meme courbe/poignees) que
            // ci-dessus, seule la conversion en hauteur d'affichage change.
            const float db = juce::Decibels::gainToDecibels (juce::jmax (0.0001f, mag), -54.0f);
            const float heightFrac = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);

            const float xt = (float) i / (float) (kCurveSamples - 1);
            const float x = plot.getX() + xt * plot.getWidth();
            const float y = plot.getBottom() - heightFrac * plot.getHeight() * 0.85f;
            ys[(size_t) i] = y;
            fill.lineTo (x, y);
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();

        // Couche neutre (base), degrade transparent (bas) -> clair (haut).
        // chipTextBright (fixe) -- fond du plot toujours fonce (0xff0a1620),
        // voir paint().
        juce::ColourGradient neutralGrad (SevenpackColours::chipTextBright.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                           SevenpackColours::chipTextBright.withAlpha (0.16f), plot.getX(), plot.getY(), false);
        g.setGradientFill (neutralGrad);
        g.fillPath (fill);
        // Halo neon discret sur le contour meme hors compression/expansion
        // (style "B" valide en preview) -- reste sobre (alpha faible) pour
        // ne pas ecraser les couches vert/bleu ci-dessous quand actives.
        g.setColour (SevenpackColours::chipTextBright.withAlpha (0.06f));
        g.strokePath (fill, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (SevenpackColours::chipTextBright.withAlpha (0.32f));
        g.strokePath (fill, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // --- Colorisation compression (vert) / expansion (bleu), PAR
        // BANDE OWNER (retour utilisateur : "je souhaite qu'on voit la
        // compression et l'expansion appliquee sur le FFT en colorisant
        // la ligne FFT en vert pour la compression et l'expansion en
        // bleu ... degrade dans le style du Claw") -- meme Path deja
        // calcule, re-clippe par groupe et redessine avec un degrade
        // colore dont l'intensite suit la reduction de gain/le boost
        // expand REELS de cette bande (deja lisses par le compresseur
        // lui-meme, voir FeedbackCompressor).
        for (int band = 0; band < SevenpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band)
                continue;
            int groupEnd = band;
            while (groupEnd + 1 < SevenpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band)
                ++groupEnd;

            const float compT   = juce::jlimit (0.0f, 1.0f, processorRef.grMeterDb[(size_t) band].load() / 15.0f);
            const float expandT = juce::jlimit (0.0f, 1.0f, processorRef.grMeterExpandDb[(size_t) band].load() / 15.0f);
            if (compT <= 0.02f && expandT <= 0.02f)
                continue;

            const float x0 = plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * plot.getWidth();
            const float x1 = plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * plot.getWidth();

            g.saveState();
            g.reduceClipRegion (juce::Rectangle<float> (x0, plot.getY(), juce::jmax (1.0f, x1 - x0), plot.getHeight()).toNearestInt());

            if (compT > 0.02f)
            {
                juce::ColourGradient grad (SevenpackColours::ledGreen.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                            SevenpackColours::ledGreen.withAlpha (0.75f * compT), plot.getX(), plot.getY(), false);
                g.setGradientFill (grad);
                g.fillPath (fill);
                g.setColour (SevenpackColours::ledGreen.withAlpha (0.18f * compT));
                g.strokePath (fill, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (SevenpackColours::ledGreen.withAlpha (0.5f + 0.4f * compT));
                g.strokePath (fill, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (expandT > 0.02f)
            {
                juce::ColourGradient grad (SevenpackColours::ledBlue.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                            SevenpackColours::ledBlue.withAlpha (0.75f * expandT), plot.getX(), plot.getY(), false);
                g.setGradientFill (grad);
                g.fillPath (fill);
                g.setColour (SevenpackColours::ledBlue.withAlpha (0.18f * expandT));
                g.strokePath (fill, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (SevenpackColours::ledBlue.withAlpha (0.5f + 0.4f * expandT));
                g.strokePath (fill, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.restoreState();
        }

        return ys;
    }

    // Fonction-locale STATIC (pas un membre statique de classe defini hors
    // ligne) -- evite tout risque d'ordre d'initialisation entre globales
    // inline de 2 headers differents (voir SevenpackColours::ledCyan etc.
    // dans SevenpackLookAndFeel.h) : garantie standard C++11, initialisee
    // au premier passage, apres coup, jamais avant que ses dependances ne
    // le soient deja.
    static juce::Colour bandAccent (int band) noexcept
    {
        static const std::array<juce::Colour, (size_t) SevenpackAudioProcessor::NumBands> colours =
            { SevenpackColours::ledCyan, SevenpackColours::ledGreen, SevenpackColours::ledAmber, SevenpackColours::ledOrange, SevenpackColours::ledPurple, SevenpackColours::ledRed, SevenpackColours::ledPink };
        return colours[(size_t) band];
    }

    SevenpackAudioProcessor& processorRef;
    std::array<juce::RangedAudioParameter*, (size_t) SevenpackAudioProcessor::NumCrossovers> freqParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SevenpackAudioProcessor::NumCrossovers> enabledParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SevenpackAudioProcessor::NumBands> soloParam {};

    juce::TextEditor freqEditor;
    int editingCrossover = -1;

    int selectedBand = (int) SevenpackAudioProcessor::Mid;
    bool bandSelectionEnabled = true;

    // Interface Alpha (voir setAlphaStyle()).
    bool alphaStyle = false;
    std::array<juce::RangedAudioParameter*, (size_t) SevenpackAudioProcessor::NumBands> inputParam {};
    int draggingPoint = -1;
    float learnEnv = 0.0f;
    double learnStartMs = 0.0;
    // Animation des points IN facon preview HTML (voir paintAlpha()).
    static constexpr float kWobblePx = 12.0f;
    static constexpr double kLandMs = 2600.0;
    bool learningNow = false;
    double landStartMs = 0.0;
    std::array<float, (size_t) SevenpackAudioProcessor::NumBands> shownY {}, landFromY {};
    int timerHz = 30;
    int draggingCrossover = -1;

    // Spectre de l'interface Alpha (voir paintAlpha()) : 256 points, ligne
    // des cretes (dB par point, maintien restant en s).
    // Design de Sunny : pas de graduations, de frequences ni de poignees sur
    // l'ecran (gardees dans le code : passer a true pour les remettre).
    static constexpr bool kSunnyShowScale = false;
    static constexpr int kSpectrumPoints = 256;
    static constexpr int kSmoothRadius = 6;
    std::array<float, kSpectrumPoints> peakDb = [] { std::array<float, kSpectrumPoints> a {}; a.fill (-54.0f); return a; }();
    std::array<float, kSpectrumPoints> peakHold {};
    double lastSpectrumMs = 0.0;

    static float dbToSpectrumY (juce::Rectangle<float> plot, float db) noexcept
    {
        const float h = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
        return plot.getBottom() - h * plot.getHeight() * 0.85f;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverSpectrumEditor)
};
