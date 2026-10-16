#pragma once
#include <JuceHeader.h>
#include "SevenpackLookAndFeel.h"
#include "AlphaSkin.h"

/**
    PresetBrowserOverlay
    ---------------------
    Navigateur de presets plein ecran (voile + panneau), avec barre de
    recherche, filtre par categorie (All / Factory / User) et liste
    scrollable -- meme mecanisme que Nuke Mext (PresetBrowserOverlay.h),
    porte ici avec SevenpackColours/SevenpackLookAndFeel, PLUS le dossier
    depliable "Mastering" (regroupe tous les presets dont le nom commence
    par "Mastering", demande explicite -- reintroduit apres l'avoir
    initialement omis).

    Volontairement DECOUPLE du type PresetData de l'editeur (qui vit dans
    PluginEditor.h, creant une inclusion circulaire) : l'editeur fournit
    une liste d'Entry (nom/auteur/usine) via setEntries(), et recoit
    l'index choisi via onPresetChosen. L'editeur reste seul responsable
    d'appliquer reellement le preset (applyPresetData()).
*/
class PresetBrowserOverlay : public juce::Component,
                              public juce::ListBoxModel
{
public:
    struct Entry
    {
        juce::String name;
        juce::String author;
        bool isFactory = false;
    };

    PresetBrowserOverlay()
    {
        // Ecran de recherche style LCD (meme fond que les knobs/ecran de
        // preset) au lieu du TextEditor blanc par defaut.
        searchBox.setTextToShowWhenEmpty ("Search presets...", AlphaPalette::get().sheetTextSoft());
        searchBox.setFont (juce::Font (juce::FontOptions (16.0f)));
        searchBox.setColour (juce::TextEditor::backgroundColourId, SevenpackColours::lcdBg);
        searchBox.setColour (juce::TextEditor::textColourId, SevenpackColours::ledCyan);
        searchBox.setColour (juce::TextEditor::outlineColourId, juce::Colours::black.withAlpha (0.7f));
        searchBox.setColour (juce::TextEditor::focusedOutlineColourId, AlphaPalette::get().heading().withAlpha (0.6f));
        searchBox.onTextChange = [this] { rebuildFilteredList(); };
        addAndMakeVisible (searchBox);

        for (int i = 0; i < 3; ++i)
        {
            auto& b = categoryButtons[(size_t) i];
            b.setClickingTogglesState (true);
            b.setRadioGroupId (777);
            b.onClick = [this] { rebuildFilteredList(); };
            addAndMakeVisible (b);
        }
        categoryButtons[0].setButtonText ("All");
        categoryButtons[1].setButtonText ("Factory");
        categoryButtons[2].setButtonText ("User");
        categoryButtons[0].setToggleState (true, juce::dontSendNotification);

        addAndMakeVisible (listBox);
        listBox.setModel (this);
        listBox.setRowHeight (40);
        listBox.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

        closeButton.setButtonText (juce::CharPointer_UTF8 ("\xc3\x97"));
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);

        setWantsKeyboardFocus (true);
    }

    // Remplace l'integralite de la liste (appele par l'editeur quand la
    // liste de presets change : ajout/suppression/rechargement).
    void setEntries (std::vector<Entry> newEntries)
    {
        allEntries = std::move (newEntries);
        rebuildFilteredList();
    }

    void setCurrentIndex (int index)
    {
        currentIndex = index;
        listBox.repaint();
    }

    std::function<void (int)> onPresetChosen; // index dans la liste ORIGINALE (allEntries)

    void show()
    {
        AlphaDraw::styleControls (*this); // palette Alpha (theme courant)
        setVisible (true);
        toFront (true);
        searchBox.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        // Style Alpha (voile + panneau clair, accents bleus).
        auto bounds = getLocalBounds().toFloat();
        AlphaDraw::overlayVeil (g, bounds);
        AlphaDraw::overlaySheet (g, panelBounds, 12.0f);

        g.setColour (AlphaPalette::get().heading());
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("PRESETS", titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        if (filteredIndices.isEmpty())
        {
            g.setColour (AlphaPalette::get().sheetTextSoft());
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText ("No presets match your search.",
                               listBox.getBounds(), juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        // Meme taille que la fenetre Options (engrenage, voir OptionsOverlay::
        // resized()) -- retour utilisateur explicite : "il faut quelle soit
        // de la taille de la fenetre option", boutons/police agrandis d'autant
        // (etait 520x560, calque sur l'ancien Nuke Mext, trop petit ici).
        const int panelW = juce::jmin (620, bounds.getWidth() - 40);
        const int panelH = juce::jmin (720, bounds.getHeight() - 40);
        auto panel = bounds.withSizeKeepingCentre (panelW, panelH);
        panelBounds = panel.toFloat();

        auto content = panel.reduced (26, 20);

        auto header = content.removeFromTop (36);
        titleArea = header.removeFromLeft (180);
        closeButton.setBounds (header.removeFromRight (32));

        content.removeFromTop (14);
        searchBox.setBounds (content.removeFromTop (38));
        content.removeFromTop (10);

        auto catRow = content.removeFromTop (34);
        const int catW = catRow.getWidth() / 3;
        categoryButtons[0].setBounds (catRow.removeFromLeft (catW).reduced (3, 0));
        categoryButtons[1].setBounds (catRow.removeFromLeft (catW).reduced (3, 0));
        categoryButtons[2].setBounds (catRow.reduced (3, 0));

        content.removeFromTop (14);
        listBox.setBounds (content);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Clic sur le voile (hors panneau) = fermeture, comme AboutOverlay.
        if (! panelBounds.contains (e.position))
            setVisible (false);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            rebuildFilteredList();
    }

    // --- juce::ListBoxModel ---
    int getNumRows() override { return filteredIndices.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= filteredIndices.size())
            return;

        auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (2.0f);

        // --- Ligne d'en-tete du dossier "Mastering" (sentinelle -1) ---
        if (filteredIndices[rowNumber] == -1)
        {
            if (rowIsSelected)
            {
                g.setColour (AlphaPalette::get().accent.withAlpha (0.08f));
                g.fillRoundedRectangle (bounds, 4.0f);
            }

            auto textBounds = bounds.reduced (12.0f, 0.0f);
            auto chevronArea = textBounds.removeFromLeft (20.0f);

            juce::Path chevron;
            const auto c = chevronArea.getCentre();
            if (masteringExpanded)
                chevron.addTriangle (c.x - 5.0f, c.y - 4.0f, c.x + 5.0f, c.y - 4.0f, c.x, c.y + 5.0f);
            else
                chevron.addTriangle (c.x - 4.0f, c.y - 5.0f, c.x - 4.0f, c.y + 5.0f, c.x + 5.0f, c.y);
            g.setColour (AlphaPalette::get().heading());
            g.fillPath (chevron);

            g.setFont (juce::Font (juce::FontOptions (15.5f, juce::Font::bold)).withExtraKerningFactor (0.04f));
            g.drawFittedText ("MASTERING", textBounds.toNearestInt(), juce::Justification::centredLeft, 1);
            return;
        }

        const auto& e = allEntries[(size_t) filteredIndices[rowNumber]];
        const bool isCurrent = (filteredIndices[rowNumber] == currentIndex);
        const bool isNested = e.name.startsWithIgnoreCase ("Mastering"); // sous le dossier -> indente

        if (isCurrent)
        {
            g.setColour (AlphaPalette::get().heading().withAlpha (0.16f));
            g.fillRoundedRectangle (bounds, 4.0f);
        }
        else if (rowIsSelected)
        {
            g.setColour (AlphaPalette::get().accent.withAlpha (0.08f));
            g.fillRoundedRectangle (bounds, 4.0f);
        }

        auto textBounds = bounds.reduced (12.0f, 0.0f);
        if (isNested)
            textBounds.removeFromLeft (22.0f);
        auto authorArea = textBounds.removeFromRight (120.0f);

        g.setColour (isCurrent ? AlphaPalette::get().heading() : AlphaPalette::get().sheetText());
        g.setFont (juce::Font (juce::FontOptions (16.0f, isCurrent ? juce::Font::bold : juce::Font::plain)));
        g.drawFittedText (e.name, textBounds.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setColour (e.isFactory ? AlphaPalette::get().accent.withAlpha (0.8f) : AlphaPalette::get().sheetTextSoft());
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::plain)).withExtraKerningFactor (0.04f));
        g.drawFittedText (e.author, authorArea.toNearestInt(), juce::Justification::centredRight, 1);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        // Clic simple sur l'en-tete "Mastering" = depli/repli, comme un
        // dossier de fichiers -- un vrai preset se choisit toujours par
        // double-clic ou Entree (chooseRow(), inchange).
        if (row >= 0 && row < filteredIndices.size() && filteredIndices[row] == -1)
        {
            masteringExpanded = ! masteringExpanded;
            rebuildFilteredList();
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        chooseRow (row);
    }

    void selectedRowsChanged (int /*lastRowSelected*/) override {}

    void returnKeyPressed (int lastRowSelected) override
    {
        chooseRow (lastRowSelected);
    }

private:
    void chooseRow (int filteredRow)
    {
        if (filteredRow < 0 || filteredRow >= filteredIndices.size())
            return;

        const int realIndex = filteredIndices[filteredRow];
        if (realIndex == -1)
            return; // en-tete de dossier "Mastering" -- pas un preset, rien a choisir

        if (onPresetChosen)
            onPresetChosen (realIndex);
        setVisible (false);
    }

    void rebuildFilteredList()
    {
        filteredIndices.clear();

        const juce::String query = searchBox.getText().trim().toLowerCase();
        const int category = categoryButtons[1].getToggleState() ? 1 : (categoryButtons[2].getToggleState() ? 2 : 0);

        // Regroupe tous les presets dont le nom commence par "Mastering"
        // sous un dossier depliable dedie (comme un dossier de fichiers),
        // plutot que de les laisser eparpilles dans la liste plate.
        juce::Array<int> masteringIndices, otherIndices;

        for (int i = 0; i < (int) allEntries.size(); ++i)
        {
            const auto& e = allEntries[(size_t) i];

            if (category == 1 && ! e.isFactory) continue;
            if (category == 2 && e.isFactory)   continue;

            if (query.isNotEmpty()
                && ! e.name.toLowerCase().contains (query)
                && ! e.author.toLowerCase().contains (query))
                continue;

            if (e.name.startsWithIgnoreCase ("Mastering"))
                masteringIndices.add (i);
            else
                otherIndices.add (i);
        }

        // -1 = sentinelle : ligne d'en-tete du dossier "Mastering", pas un
        // vrai preset. N'apparait que s'il reste au moins un preset
        // Mastering apres filtrage (recherche/categorie).
        if (! masteringIndices.isEmpty())
        {
            filteredIndices.add (-1);
            if (masteringExpanded)
                filteredIndices.addArray (masteringIndices);
        }

        filteredIndices.addArray (otherIndices);

        listBox.updateContent();
        listBox.repaint();
        repaint();
    }

    std::vector<Entry> allEntries;
    juce::Array<int> filteredIndices;
    int currentIndex = -1;

    // Etat depli/repli du dossier "Mastering" -- deplie par defaut.
    bool masteringExpanded = true;

    juce::TextEditor searchBox;
    std::array<juce::TextButton, 3> categoryButtons;
    juce::ListBox listBox;
    juce::TextButton closeButton;

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<int> titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowserOverlay)
};
