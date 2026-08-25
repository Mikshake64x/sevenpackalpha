#pragma once
#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "SixpackLookAndFeel.h"

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
    SixpackEditorContent::selectedBand).

    CROSSOVER "ENTIEREMENT MODULABLE" (retour utilisateur) : un crossover
    DESACTIVE (bande fusionnee, voir SixpackAudioProcessor::
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
*/
class CrossoverSpectrumEditor : public juce::Component,
                                 private juce::Timer,
                                 private juce::TextEditor::Listener
{
public:
    explicit CrossoverSpectrumEditor (SixpackAudioProcessor& p) : processorRef (p)
    {
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
        {
            freqParam[(size_t) x]    = processorRef.apvts.getParameter (SixpackAudioProcessor::idCrossover[x]);
            enabledParam[(size_t) x] = processorRef.apvts.getParameter (SixpackAudioProcessor::idCrossoverEnabled[x]);
        }
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            soloParam[(size_t) band] = processorRef.apvts.getParameter (SixpackAudioProcessor::idSolo[band]);
            inputParam[(size_t) band] = processorRef.apvts.getParameter (
                SixpackAudioProcessor::bandParamId (SixpackAudioProcessor::idInput, band));
            expandRangeParam[(size_t) band] = processorRef.apvts.getParameter (
                SixpackAudioProcessor::bandParamId (SixpackAudioProcessor::idInputExpand, band));
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
    // crossover ni un bouton solo) -- l'appelant (SixpackEditorContent)
    // l'utilise pour decider quel panneau de bande afficher.
    std::function<void (int)> onBandSelected;

    void setSelectedBand (int band) noexcept
    {
        if (selectedBand != band) { selectedBand = band; repaint(); }
    }

    void resized() override { commitFreqEditor(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a1620));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto* laf = dynamic_cast<SixpackLookAndFeel*> (&getLookAndFeel());
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
        float boundaryHz[(size_t) SixpackAudioProcessor::NumBands + 1];
        boundaryHz[0] = 20.0f;
        boundaryHz[SixpackAudioProcessor::NumBands] = 20000.0f;
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
            boundaryHz[x + 1] = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());

        // --- Bandeau fin (groupes actifs) + rangee solo ---
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band)
                continue;
            int groupEnd = band;
            while (groupEnd + 1 < SixpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band)
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
            drawBandControlLines (g, lay.plot, band, x0, x1, accent);
        }

        const auto curveY = drawSpectrum (g, lay.plot, owner, boundaryHz);

        // prevPillRight : quand un crossover desactive est pousse contre son
        // voisin actif (voir pushDisabledNeighborsIntoOrder()), leurs deux
        // pastilles de frequence se retrouvent au meme endroit et le texte
        // se superpose en charabia illisible -- on saute la pastille (mais
        // pas le trait/la poignee, toujours draggable) si elle chevauche la
        // derniere pastille reellement dessinee.
        float prevPillRight = -1.0e9f;
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
            drawCrossoverLine (g, lay, x, boundaryHz[(size_t) (x + 1)], curveY, prevPillRight);

        g.restoreState();
        g.setColour (juce::Colour (0xff2a3540));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        commitFreqEditor();
        const auto lay = computeLayout();

        // 1. Rangee SOLO -- un bouton par groupe actif.
        if (lay.soloRow.contains (e.position))
        {
            const auto owner = computeGroupOwners();
            float boundaryHz[(size_t) SixpackAudioProcessor::NumBands + 1];
            boundaryHz[0] = 20.0f; boundaryHz[SixpackAudioProcessor::NumBands] = 20000.0f;
            for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
                boundaryHz[x + 1] = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());

            for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
            {
                if (owner[(size_t) band] != band) continue;
                int groupEnd = band;
                while (groupEnd + 1 < SixpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band) ++groupEnd;
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
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
        {
            const float freq = freqParam[(size_t) x]->convertFrom0to1 (freqParam[(size_t) x]->getValue());
            const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
            if (std::abs (e.position.x - lineX) > kLineHitRadius)
                continue;

            const float handleY = spectrumYAt (lay.plot, freq);
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

        // 3. Lignes IN (haut, descendre = plus de compression) / RANGE
        // (bas, monter = plus d'expansion) -- une paire par groupe de
        // bande actif, voir drawBandControlLines().
        if (lay.plot.contains (e.position))
        {
            const auto owner = computeGroupOwners();
            float boundaryHz[(size_t) SixpackAudioProcessor::NumBands + 1];
            boundaryHz[0] = 20.0f; boundaryHz[SixpackAudioProcessor::NumBands] = 20000.0f;
            for (int cx = 0; cx < SixpackAudioProcessor::NumCrossovers; ++cx)
                boundaryHz[cx + 1] = freqParam[(size_t) cx]->convertFrom0to1 (freqParam[(size_t) cx]->getValue());

            for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
            {
                if (owner[(size_t) band] != band) continue;
                int groupEnd = band;
                while (groupEnd + 1 < SixpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band) ++groupEnd;
                const float x0 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) band]) * lay.plot.getWidth();
                const float x1 = lay.plot.getX() + freqToXFrac (boundaryHz[(size_t) (groupEnd + 1)]) * lay.plot.getWidth();
                if (e.position.x < x0 || e.position.x >= x1)
                    continue;

                // Uniquement le POINT central (pas toute la ligne) --
                // retour utilisateur explicite : "on ne doit pas pouvoir
                // les monter et descendre en cliquant sur la ligne, mais
                // seulement en cliquant sur le point au milieu. il faut
                // elargir la zone de clique autour du point" -- distance
                // au point (cx, y), pas juste proximite verticale sur
                // toute la largeur de la bande. Rayon genereux
                // (kControlPointHitRadius, voir plus bas) pour ne pas
                // avoir a viser pile le petit cercle de 4px dessine.
                const float cx = (x0 + x1) * 0.5f;

                const float inputDb = inputParam[(size_t) band]->convertFrom0to1 (inputParam[(size_t) band]->getValue());
                if (juce::Point<float> (cx, inLineY (lay.plot, inputDb)).getDistanceFrom (e.position) <= kControlPointHitRadius)
                {
                    draggingInBand = band;
                    inputParam[(size_t) band]->beginChangeGesture();
                    dragInLineTo (band, e.position, lay.plot);
                    return;
                }

                const float rangeDb = expandRangeParam[(size_t) band]->convertFrom0to1 (expandRangeParam[(size_t) band]->getValue());
                if (juce::Point<float> (cx, rangeLineY (lay.plot, rangeDb)).getDistanceFrom (e.position) <= kControlPointHitRadius)
                {
                    draggingRangeBand = band;
                    expandRangeParam[(size_t) band]->beginChangeGesture();
                    dragRangeLineTo (band, e.position, lay.plot);
                    return;
                }

                break; // zone horizontale trouvee, inutile de tester les autres bandes
            }
        }

        // 4. Sinon : selectionne la bande dont la zone contient ce point X.
        if (lay.plot.contains (e.position))
        {
            const float freq = xFracToFreq (juce::jlimit (0.0f, 1.0f, (e.position.x - lay.plot.getX()) / lay.plot.getWidth()));
            selectBandAtFrequency (freq);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingCrossover >= 0) { dragTo (draggingCrossover, e.position, computeLayout().plot); return; }
        if (draggingInBand >= 0) { dragInLineTo (draggingInBand, e.position, computeLayout().plot); return; }
        if (draggingRangeBand >= 0) { dragRangeLineTo (draggingRangeBand, e.position, computeLayout().plot); return; }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingCrossover >= 0)
        {
            freqParam[(size_t) draggingCrossover]->endChangeGesture();
            draggingCrossover = -1;
        }
        if (draggingInBand >= 0)
        {
            inputParam[(size_t) draggingInBand]->endChangeGesture();
            draggingInBand = -1;
        }
        if (draggingRangeBand >= 0)
        {
            expandRangeParam[(size_t) draggingRangeBand]->endChangeGesture();
            draggingRangeBand = -1;
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
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
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
        for (int x = 0; x < SixpackAudioProcessor::NumCrossovers; ++x)
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
    static constexpr float kControlPointHitRadius = 13.0f; // IN/RANGE -- point central uniquement, voir mouseDown()
    static constexpr float kMinRatio = 1.05f;

    struct Layout
    {
        juce::Rectangle<float> strip, soloRow, plot, freqRow;
    };

    Layout computeLayout() const noexcept
    {
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

    void timerCallback() override { repaint(); }

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

    // Meme calcul que SixpackAudioProcessor::computeGroupOwners() --
    // reimplemente ici cote UI (lecture des parametres uniquement).
    std::array<int, (size_t) SixpackAudioProcessor::NumBands> computeGroupOwners() const noexcept
    {
        std::array<int, (size_t) SixpackAudioProcessor::NumBands> owner {};
        owner[0] = 0;
        for (int b = 1; b < SixpackAudioProcessor::NumBands; ++b)
        {
            const bool enabled = enabledParam[(size_t) (b - 1)]->getValue() > 0.5f;
            owner[(size_t) b] = enabled ? b : owner[(size_t) (b - 1)];
        }
        return owner;
    }

    void selectBandAtFrequency (float freq)
    {
        const auto owner = computeGroupOwners();
        for (int band = SixpackAudioProcessor::NumBands - 1; band >= 0; --band)
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
        for (int i = x + 1; i < SixpackAudioProcessor::NumCrossovers; ++i)
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
        for (int i = movedIndex + 1; i < SixpackAudioProcessor::NumCrossovers; ++i)
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

    // --- Lignes IN (Input, retour utilisateur : "en haut ... descendre
    // augmente le input") / RANGE (Expand Range, "en bas ... monter
    // augmente le range") -- mapping lineaire sur TOUTE la hauteur du
    // plot, sens opposes (voir doc de classe). ---

    static float inLineY (juce::Rectangle<float> plot, float inputDb) noexcept
    {
        const float yFrac = juce::jlimit (0.0f, 1.0f, (inputDb + 30.0f) / 60.0f); // -30..+30 -> 0..1
        return plot.getY() + yFrac * plot.getHeight();
    }

    static float rangeLineY (juce::Rectangle<float> plot, float rangeDb) noexcept
    {
        const float yFrac = juce::jlimit (0.0f, 1.0f, 1.0f - rangeDb / 24.0f); // 0..24 -> bas..haut
        return plot.getY() + yFrac * plot.getHeight();
    }

    static float yToInputDb (juce::Rectangle<float> plot, float y) noexcept
    {
        const float yFrac = juce::jlimit (0.0f, 1.0f, (y - plot.getY()) / plot.getHeight());
        return juce::jmap (yFrac, 0.0f, 1.0f, -30.0f, 30.0f);
    }

    static float yToRangeDb (juce::Rectangle<float> plot, float y) noexcept
    {
        const float yFrac = juce::jlimit (0.0f, 1.0f, (y - plot.getY()) / plot.getHeight());
        return juce::jmap (yFrac, 0.0f, 1.0f, 24.0f, 0.0f);
    }

    void dragInLineTo (int band, juce::Point<float> pos, juce::Rectangle<float> plot)
    {
        const float db = yToInputDb (plot, pos.y);
        inputParam[(size_t) band]->setValueNotifyingHost (inputParam[(size_t) band]->convertTo0to1 (db));
        repaint();
    }

    void dragRangeLineTo (int band, juce::Point<float> pos, juce::Rectangle<float> plot)
    {
        const float db = yToRangeDb (plot, pos.y);
        expandRangeParam[(size_t) band]->setValueNotifyingHost (expandRangeParam[(size_t) band]->convertTo0to1 (db));
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

    // Ligne IN (haut, descendre = plus de compression) + ligne RANGE (bas,
    // monter = plus d'expansion) pour une bande -- retour utilisateur, voir
    // doc de classe. Seul le POINT central est draggable (voir mouseDown()
    // et kControlPointHitRadius) ; le trait n'est qu'un repere visuel.
    // Style "fondu vers le point" (direction B validee en preview) -- trait
    // PLEIN (plus de pointilles) dont l'opacite decroit vers les bords via
    // un ColourGradient, pour attirer l'oeil sur la zone cliquable plutot
    // que de suggerer que toute la ligne se drag.
    void drawBandControlLines (juce::Graphics& g, juce::Rectangle<float> plot, int band, float x0, float x1, juce::Colour accent)
    {
        const float inputDb = inputParam[(size_t) band]->convertFrom0to1 (inputParam[(size_t) band]->getValue());
        const float rangeDb = expandRangeParam[(size_t) band]->convertFrom0to1 (expandRangeParam[(size_t) band]->getValue());
        const float cx = (x0 + x1) * 0.5f;

        auto drawLine = [&] (float y)
        {
            juce::ColourGradient grad (accent.withAlpha (0.0f), x0 + 2.0f, y,
                                        accent.withAlpha (0.0f), x1 - 2.0f, y, false);
            grad.addColour (0.5, accent.withAlpha (0.85f));
            grad.addColour (0.4, accent.withAlpha (0.7f));
            grad.addColour (0.6, accent.withAlpha (0.7f));
            g.setGradientFill (grad);
            g.drawLine (x0 + 2.0f, y, x1 - 2.0f, y, 1.3f);

            constexpr float r = 4.0f;
            g.setColour (juce::Colour (0xff0a1620));
            g.fillEllipse (cx - r, y - r, r * 2.0f, r * 2.0f);
            g.setColour (accent);
            g.drawEllipse (cx - r, y - r, r * 2.0f, r * 2.0f, 1.5f);
        };

        drawLine (inLineY (plot, inputDb));
        drawLine (rangeLineY (plot, rangeDb));
    }

    void drawCrossoverLine (juce::Graphics& g, const Layout& lay, int x, float freq, const std::array<float, kCurveSamples>& curveY,
                             float& prevPillRight)
    {
        const bool enabled = enabledParam[(size_t) x]->getValue() > 0.5f;
        const float lineX = lay.plot.getX() + freqToXFrac (freq) * lay.plot.getWidth();
        const float handleY = sampleCurveY (curveY, lay.plot, lineX);
        // Fond FIXE (0xff0a1620, jamais retheme, voir paint()) -- texte
        // FIXE clair aussi (chipTextBright, pas textBright qui s'assombrit
        // en theme clair).
        const juce::Colour colour = enabled ? SixpackColours::chipTextBright.withAlpha (0.85f)
                                             : SixpackColours::chipTextBright.withAlpha (0.35f);

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
                                                     const std::array<int, (size_t) SixpackAudioProcessor::NumBands>& owner,
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
        juce::ColourGradient neutralGrad (SixpackColours::chipTextBright.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                           SixpackColours::chipTextBright.withAlpha (0.16f), plot.getX(), plot.getY(), false);
        g.setGradientFill (neutralGrad);
        g.fillPath (fill);
        // Halo neon discret sur le contour meme hors compression/expansion
        // (style "B" valide en preview) -- reste sobre (alpha faible) pour
        // ne pas ecraser les couches vert/bleu ci-dessous quand actives.
        g.setColour (SixpackColours::chipTextBright.withAlpha (0.06f));
        g.strokePath (fill, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (SixpackColours::chipTextBright.withAlpha (0.32f));
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
        for (int band = 0; band < SixpackAudioProcessor::NumBands; ++band)
        {
            if (owner[(size_t) band] != band)
                continue;
            int groupEnd = band;
            while (groupEnd + 1 < SixpackAudioProcessor::NumBands && owner[(size_t) (groupEnd + 1)] == band)
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
                juce::ColourGradient grad (SixpackColours::ledGreen.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                            SixpackColours::ledGreen.withAlpha (0.75f * compT), plot.getX(), plot.getY(), false);
                g.setGradientFill (grad);
                g.fillPath (fill);
                g.setColour (SixpackColours::ledGreen.withAlpha (0.18f * compT));
                g.strokePath (fill, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (SixpackColours::ledGreen.withAlpha (0.5f + 0.4f * compT));
                g.strokePath (fill, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (expandT > 0.02f)
            {
                juce::ColourGradient grad (SixpackColours::ledBlue.withAlpha (0.0f), plot.getX(), plot.getBottom(),
                                            SixpackColours::ledBlue.withAlpha (0.75f * expandT), plot.getX(), plot.getY(), false);
                g.setGradientFill (grad);
                g.fillPath (fill);
                g.setColour (SixpackColours::ledBlue.withAlpha (0.18f * expandT));
                g.strokePath (fill, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (SixpackColours::ledBlue.withAlpha (0.5f + 0.4f * expandT));
                g.strokePath (fill, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.restoreState();
        }

        return ys;
    }

    float spectrumYAt (juce::Rectangle<float> plot, float freq) const noexcept
    {
        auto& spec = processorRef.spectrumAnalyzer;
        if (spec.getSampleRate() <= 0.0)
            return plot.getBottom();
        const float mag = spec.getMagnitudeInterpolated (freq);
        return plot.getBottom() - mag * plot.getHeight() * 0.85f;
    }

    static float sampleCurveY (const std::array<float, kCurveSamples>& curveY, juce::Rectangle<float> plot, float x) noexcept
    {
        const float xt = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        const int i = juce::jlimit (0, kCurveSamples - 1, juce::roundToInt (xt * (float) (kCurveSamples - 1)));
        return curveY[(size_t) i];
    }

    // Fonction-locale STATIC (pas un membre statique de classe defini hors
    // ligne) -- evite tout risque d'ordre d'initialisation entre globales
    // inline de 2 headers differents (voir SixpackColours::ledCyan etc.
    // dans SixpackLookAndFeel.h) : garantie standard C++11, initialisee
    // au premier passage, apres coup, jamais avant que ses dependances ne
    // le soient deja.
    static juce::Colour bandAccent (int band) noexcept
    {
        static const std::array<juce::Colour, (size_t) SixpackAudioProcessor::NumBands> colours =
            { SixpackColours::ledCyan, SixpackColours::ledGreen, SixpackColours::ledAmber, SixpackColours::ledOrange, SixpackColours::ledPurple, SixpackColours::ledRed };
        return colours[(size_t) band];
    }

    SixpackAudioProcessor& processorRef;
    std::array<juce::RangedAudioParameter*, (size_t) SixpackAudioProcessor::NumCrossovers> freqParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SixpackAudioProcessor::NumCrossovers> enabledParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SixpackAudioProcessor::NumBands> soloParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SixpackAudioProcessor::NumBands> inputParam {};
    std::array<juce::RangedAudioParameter*, (size_t) SixpackAudioProcessor::NumBands> expandRangeParam {};

    juce::TextEditor freqEditor;
    int editingCrossover = -1;

    int selectedBand = (int) SixpackAudioProcessor::Mid;
    int draggingCrossover = -1;

    // Ligne IN (haut, descendre = plus de compression) / RANGE (bas,
    // monter = plus d'expansion) actuellement draggee, PAR BANDE OWNER --
    // voir doc de classe et drawBandControlLines()/mouseDown(). -1 = aucune.
    int draggingInBand = -1;
    int draggingRangeBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverSpectrumEditor)
};
