#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SevenpackLookAndFeel.h"
// Interface Alpha : seuls les en-tetes reellement utilises sont inclus
// (audit -- les 17 en-tetes de l'ancienne interface Sixpack ont ete retires).
#include "ToggleSwitch.h"
#include "DetectorFilterGraph.h"
#include "AboutOverlay.h"
#include "OptionsOverlay.h"
#include "PresetBrowserOverlay.h"
#include "ParamRamper.h"
#include "LicenseOverlay.h"
#include "CrossoverSpectrumEditor.h"
#include "AlphaSkin.h"
#include "AlphaMeters.h"
#include "AlphaStrip.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <map>
 #include <set>
#endif

/**
    HiddenBandControls
    ------------------
    Detector (HP/LP, Bell, Bell 2, Link), Audio HP et Color (TUBE/FUZZ) +
    graphe XY (DetectorFilterGraph) d'UNE bande -- CABLES (attachements
    intacts) mais JAMAIS AFFICHES (addChildComponent, voir
    SevenpackEditorContent::buildHiddenBandControls()) -- retour utilisateur
    explicite (Sixpack) : "je souhaite retirer detector, color et le XY.
    laisse dans le code mais retire du visuel si un jour je veux remettre".
    Tout le reste de la bande (ratio, knobs, metres, Dry/Wet, Spread, SOLO)
    vit desormais dans sa tranche, voir BandStrip.h.
*/
struct HiddenBandControls
{
    ToggleSwitch detectorHpToggle   { "HP",   SevenpackColours::ledGreen };
    ToggleSwitch detectorBellToggle { "BELL", SevenpackColours::ledYellow };
    // BANDES INTERMEDIAIRES : 2e point Bell independant sur le detecteur,
    // voir DetectorFilterGraph.
    ToggleSwitch detectorBell2Toggle { "BELL 2", SevenpackColours::ledBlue };
    ToggleSwitch detectorLinkToggle { "LINK", SevenpackColours::ledRed };
    ToggleSwitch audioHpToggle      { "HP",     SevenpackColours::ledBlue };
    // Libelles "TUBE"/"FUZZ" -- idAudioDist2/idAudioDist3 inchanges (IDs de
    // parametre APVTS, ne pas casser les sessions/presets existants).
    ToggleSwitch audioDist2Toggle   { "TUBE", SevenpackColours::ledAmber };
    ToggleSwitch audioDist3Toggle   { "FUZZ", SevenpackColours::ledAmber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        detectorHpAttachment, detectorBellAttachment, detectorBell2Attachment, detectorLinkAttachment,
        audioHpAttachment, audioDist2Attachment, audioDist3Attachment;

    std::unique_ptr<DetectorFilterGraph> filterGraph;
};

/**
    SevenpackEditorContent
    ------------------------
    TOUT le contenu visuel du plugin (barre de controle, plaque VU/THD, 3
    bandes), a TAILLE LOGIQUE FIXE (1400x750, jamais modifiee). C'est un
    juce::Component ORDINAIRE (pas un AudioProcessorEditor) -- voir
    SevenpackAudioProcessorEditor plus bas, qui l'heberge comme UNIQUE
    enfant avec une transformation d'echelle (setTransform). Cette
    separation est necessaire car un AudioProcessorEditor qui possede son
    propre "peer" (fenetre native de l'hote) ignore/ne propage pas
    correctement setTransform pour le redimensionnement -- seul un enfant
    JUCE ordinaire (non-peer) peut etre mis a l'echelle de facon fiable, le
    parent (l'editeur) annonce alors sa VRAIE taille (mise a l'echelle) a
    l'hote via setSize(), ce que tous les DAW respectent.

    POWER : plus d'overlay rectangulaire -- chaque composant (knobs,
    ToggleSwitch, RatioSelector, DetectorFilterGraph, NeedleVU,
    CrossoverSpectrumEditor) interroge lookAndFeel.isPowered() dans son
    propre paint() et se rend lui-meme "eteint", comme une machine
    debranchee. Voir timerCallback().
*/
class SevenpackEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit SevenpackEditorContent (SevenpackAudioProcessor&);
    ~SevenpackEditorContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // baseHeight REDUIT par rapport a l'ancien Tripressor 3 bandes (etait
    // 790) : l'editeur de crossover (CrossoverSpectrumEditor, 140px) +
    // UN SEUL panneau de detail de bande (144px, voir BandPanel/
    // selectedBand) prend moins de place que d'empiler plusieurs rangees
    // completes, meme avec 5 bandes au lieu de 3. Le module THD lui-meme
    // est desormais compact dans la barre de controle (voir
    // ThdHeaderModule.h), mais la plaque rack du bas (vuPlateBounds) reste
    // en place VOLONTAIREMENT VIDE pour l'instant (retour utilisateur :
    // "il faut garder le rack vide en bas pour le moment") -- reservee
    // pour un usage futur, pas encore retiree. Voir resized()
    // (crossoverEditorBounds/bandPanelBounds/vuPlateBounds/presetBarTop).
    // baseHeight elargi de 144 (etait 644) -- rack du bas passe en 2U (test
    // demande explicitement : "on peut tester un rack 2U ... c'est juste
    // les faders simplement sur un rack 1U"), voir vuPlateBounds dans
    // resized().
    // +40 (etait 788) -- rangee SOLO/SEL ajoutee au rack du bas, voir
    // BandMetersRack::buttonRowH et vuPlateBounds dans resized().
    // Tentative +56 (884) pour agrandir bandPanelBounds -- REVERTEE (retour
    // utilisateur explicite : "c'est pas ce que j'ai demande. tu a agrandi
    // la fenetre du plugin la") : la fenetre ne doit PAS grandir, seul
    // l'alignement (X, voir layoutBandRow()) etait demande.
    // -20 (808, etait 828) -- 2 ecarts de 10px supprimes (retour
    // utilisateur : "il reste une bande grise en haut et en bas du
    // compresseur"), voir resized().
    // Sevenpack : taille INCHANGEE -- l'ancien panneau de detail (144px) +
    // le rack du bas (328px) sont remplaces a surface egale par les 7
    // tranches (stripsPlateBounds, 472px, voir resized()).
    static constexpr int baseWidth = 1400, baseHeight = 808;

    // Appele quand l'utilisateur choisit une taille dans OptionsOverlay --
    // branche par SevenpackAudioProcessorEditor (le vrai proprietaire du
    // redimensionnement hote, voir plus bas).
    std::function<void (float)> onScaleRequested;

    // Called when the user clicks "License" on the About screen — branched
    // by SevenpackAudioProcessorEditor to show its LicenseOverlay (which
    // lives outside this scaled content, see PluginEditor's constructor).
    std::function<void()> onOpenLicenseRequested;

    // Doit etre public : des fonctions utilitaires de sauvegarde/chargement
    // (hors de la classe, voir writePresetXml()/readPresetXml() dans le
    // .cpp) ont besoin de nommer ce type. Meme systeme que Nuke Mext
    // (PresetBrowserOverlay, barre Prev/Next/Save/Delete/Copy/Paste), mais
    // avec les parametres REELS de Suppressor (moteur different de Mext --
    // pas de softKnee/colour/sidechain EQ, mais Detector HP/Bell/Link,
    // Audio Dist2/Dist3, Optical/British, Mix/Width par bande).
    struct PresetData
    {
        juce::String name;
        std::array<float, (size_t) SevenpackAudioProcessor::NumCrossovers> crossoverHz
            { { Crossover::kSplitDefaultHz[0], Crossover::kSplitDefaultHz[1], Crossover::kSplitDefaultHz[2], Crossover::kSplitDefaultHz[3], Crossover::kSplitDefaultHz[4] } };
        std::array<bool, (size_t) SevenpackAudioProcessor::NumCrossovers> crossoverEnabled { { true, true, true, true, true } };
        bool linearMode = false; // WIDE (false) / LINEAR (true)
        bool softClipOn = true;  // THD Valve (global, engage par defaut)
        int thdMode = 0;         // 0=Soft 1=Tape 2=Iron 3=Air (voir SoftClipper::SatMode)

        struct Band
        {
            float input = 0.0f;
            int ratioIndex = 3;
            float attack = SevenpackAudioProcessor::defaultAttackMs;
            float release = SevenpackAudioProcessor::defaultReleaseMs;
            float output = 0.0f;

            // Above/Below/Both + jeu de reglages Expand DEDIE (demande
            // explicite : "recalibre les valeurs range" -- recharger un
            // preset, "Default" en particulier, doit remettre l'expander
            // dans son etat d'usine calibre, Range=0dB inclus, pas juste
            // laisser les valeurs live intactes). Memes defauts que
            // createParameterLayout() dans PluginProcessor.cpp.
            int aboveBelow = 0; // 0=Comp (voir FeedbackCompressor::AboveBelowMode)
            float rangeExpand = 0.0f; // Range (dB), PAS un "Input" -- voir idInputExpand
            int ratioIndexExpand = 0; // 2:1
            float attackExpand = SevenpackAudioProcessor::defaultAttackMs;
            float releaseExpand = SevenpackAudioProcessor::defaultReleaseMs;
            float outputExpand = 0.0f;

            bool detectorHp = false;
            float detectorHpFreq = 100.0f;
            int detectorHpSlope = 2;
            bool detectorBell = false;
            float detectorBellFreq = 650.0f;
            float detectorBellGain = 6.0f;
            bool detectorBell2 = false;
            float detectorBell2Freq = 3000.0f;
            float detectorBell2Gain = 6.0f;
            bool detectorLink = false;
            bool sidechainEnable = false;
            bool audioHp = false;
            bool audioDist2 = false;
            bool audioDist3 = false;
            bool optical = false;
            bool british = false;
            float bandMix = 100.0f;
            float bandWidth = 100.0f;
        };
        Band bands[(size_t) SevenpackAudioProcessor::NumBands];

        juce::String author;
        bool isFactory = false; // les presets usine ne peuvent pas etre supprimes
    };

private:
    void rebuildBackgroundCache();
    // Charge le bois choisi (processorRef.woodStyle) et refait le fond.
    void loadWood();
    void refreshThemeReactiveColours();
    void timerCallback() override;

    // Detector/Color/XY caches (voir HiddenBandControls) -- les controles
    // visibles de chaque bande vivent dans sa BandStrip.
    void buildHiddenBandControls (int band);

    void wireInputLink();  // lien relatif entre les 7 knobs Input (voir linkInSwitch)
    void wireOutputLink(); // lien relatif entre les 7 knobs Output (voir linkOutSwitch)
    void updateInputLinkVisuals();
    void updateOutputLinkVisuals();
    void updateLinkVisuals();

    void showOptionsOverlay();

    void showCalculatorOverlay();

    void hideCalculatorOverlay();
    // Clic sur LEARN : bulle de confirmation (sauf "Don't ask again" ou
    // ecoute deja en cours), sinon LEARN direct.
    void requestLearn();

    // --- Interface Alpha ---
    // Applique un theme (0=Sombre, 1=Clair, 2=DOS) partout : interrupteur
    // lune du header ET radio DESIGN des options passent par ici.
    void applyUiTheme (int themeIndex);
    // SEL d'une tranche ou clic dans une zone du graphe -- meme logique que
    // Sixpack : clic simple = la bande devient la selection et le groupe lie
    // est vide ; Ctrl/Cmd+clic = ajoute/retire la bande du groupe lie
    // (faders OUT deplaces ensemble, delta relatif, voir wireOutputLink()).
    void selectBand (int band, bool isMultiSelectModifier);
    void refreshSelectionVisuals();
    bool isInSelectionGroup (int band) const noexcept { return band == selectedBand || linkedBands[(size_t) band]; }

    // --- Presets (meme mecanisme que Nuke Mext, voir PresetData ci-dessus) ---
    void setParamValue (const juce::String& paramID, float rawValue);
    // Meme signature que setParamValue(), mais anime la transition (voir
    // ParamRamper) -- utilise pour les knobs continus (Input/Attack/
    // Release/Output/Crossover) au chargement d'un preset, pour qu'ils
    // tournent au lieu de teleporter.
    void rampParamTo (const juce::String& paramID, float rawValue, int durationMs = 260);
    void buildPresetList();  // remplit "presets" avec les presets d'usine Suppressor
    void loadUserPresets();  // scanne le dossier presets utilisateur sur le disque et les ajoute a la liste
    void goToPreset (int delta);
    void openPresetBrowser(); // ouvre le navigateur plein ecran (voir PresetBrowserOverlay.h)
    void syncPresetBrowserEntries(); // reconstruit la liste d'Entry envoyee au navigateur depuis "presets"
    void applyPresetData (const PresetData& p);
    PresetData captureCurrentPresetData (const juce::String& name, const juce::String& author, bool isFactory);
    void refreshPresetDisplay(); // met a jour nom/auteur affiches + l'etat actif/grise de la corbeille
    void promptSavePreset();   // ouvre la boite de dialogue "nom du preset" puis sauvegarde
    void promptDeletePreset(); // demande confirmation puis supprime le preset utilisateur courant
    // --- Annuler / retablir (voir undoHistory) ---
    std::vector<float> captureParamValues() const;
    void pushUndoSnapshotIfChanged();
    void restoreUndoSnapshot (int index);
    void undo();
    void redo();
    void refreshUndoButtons();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void copyStateToClipboard();
    void pasteStateFromClipboard();

    // Bouton AI (voir AiLearnAnalyzer.h) : applique le resultat calcule
    // exactement comme un preset (rampParamTo pour les knobs continus,
    // setParamValue pour le ratio discret) -- appele depuis timerCallback()
    // des que processorRef.aiLearnAnalyzer.getState() == Done.
    void applyAiLearnResult (const AiLearnAnalyzer::Result& result);

    SevenpackAudioProcessor& processorRef;
    SevenpackLookAndFeel lookAndFeel;

    // Detector/Color/XY caches par bande (voir HiddenBandControls).
    std::array<HiddenBandControls, (size_t) SevenpackAudioProcessor::NumBands> hiddenBandControls;
    // --- Interface v2 "Sevenpack Alpha" (maquette 7PACK, voir AlphaSkin.h) ---

    // Les 7 tranches (voir AlphaStrip.h) -- aiguille THD, SOLO/SEL, carte.
    std::array<std::unique_ptr<AlphaStrip>, (size_t) SevenpackAudioProcessor::NumBands> bandStrips;
    // Selection (comme Sixpack : MID au depart, pas un parametre APVTS) +
    // bandes liees par Ctrl/Cmd+clic SEL (selectedBand n'y figure jamais,
    // il est membre implicite du groupe).
    int selectedBand = (int) SevenpackAudioProcessor::Mid;
    std::array<bool, (size_t) SevenpackAudioProcessor::NumBands> linkedBands {};

    // Spectre + 6 crossovers + 7 points IN (style Alpha, voir
    // CrossoverSpectrumEditor::paintAlpha()).
    CrossoverSpectrumEditor crossoverSpectrum { processorRef };

    // --- En-tete ---
    AlphaPillToggle powerToggle { AlphaPillToggle::Icon::Power };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    AlphaPillToggle abToggle    { AlphaPillToggle::Icon::Letter }; // A/B (voir SevenpackAudioProcessor::selectABSlot())
    AlphaPillToggle themeToggle { AlphaPillToggle::Icon::Moon };   // theme sombre (demande explicite : "gestion du theme integre dans l'interface")
    AlphaPillToggle contrastToggle { AlphaPillToggle::Icon::Contrast }; // contraste renforce, sous A/B ("pour les ecrans pourris")
    void applyHighContrast (bool shouldBeOn);

    AlphaButton learnButton { "LEARN" };
    AlphaButton analogButton { "ANALOG" }, linearButton { "LINEAR" };
    AlphaButton linkInSwitch  { "LINK", "IN" };
    AlphaButton linkOutSwitch { "LINK", "OUT" };
    bool linkInGuard = false, linkOutGuard = false;
    std::array<double, (size_t) SevenpackAudioProcessor::NumBands> lastInputValue {};
    std::array<double, (size_t) SevenpackAudioProcessor::NumBands> lastOutputValue {};

    AlphaLogo logoButton;

    // THD global (idSoftClip) + 4 modes radio (idThdMode : 0 SOFT, 1 TAPE,
    // 2 IRON, 3 AIR).
    AlphaButton thdButton { "THD" };
    std::array<std::unique_ptr<AlphaButton>, 4> thdModeButtons;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> softClipAttachment;
    std::unique_ptr<juce::ParameterAttachment> thdModeAttachment;

    // Mode des aiguilles des 7 tranches (THD / GR / GONIO, boutons a gauche
    // des tranches) -- processorRef.meterMode.
    std::array<std::unique_ptr<AlphaButton>, 3> meterModeButtons;
    void syncMeterModeButtons()
    {
        const int mode = processorRef.meterMode.load();
        for (int i = 0; i < 3; ++i)
            if (meterModeButtons[(size_t) i] != nullptr)
                meterModeButtons[(size_t) i]->setToggleState (mode == i, juce::dontSendNotification);
    }

    // Menu "3 points" -- remplace l'engrenage (demande explicite), ouvre
    // OptionsOverlay.
    AlphaKebab optionsButton;

    AboutOverlay aboutOverlay;
    OptionsOverlay optionsOverlay;
    // Reglages de LEARN (ex "AI Learn" des Options) : engrenage a droite de
    // LEARN + panneau "Calculator" (voir OptionsOverlay.h).
    CalculatorOverlay calculatorOverlay;
    CalculatorGearButton calculatorGear;
    // Confirmation avant LEARN (bulle sous LEARN, voir OptionsOverlay.h).
    LearnConfirmOverlay learnConfirm;

    // Animation globale pendant LEARN (calque au-dessus du graphe et des
    // tranches, voir AlphaMeters.h).
    AlphaLearnFx learnFx { processorRef };

    juce::Rectangle<float> panelBounds, headerBounds, crossoverEditorBounds, stripsBounds, footerBounds;
    // Filets verticaux entre les groupes de l'en-tete (demande explicite :
    // "bien separer dans le header les fonctions" -- ON/OFF A/B THEME |
    // LEARN | ANALOG LINEAR | LINK IN/OUT).
    // 3 a gauche (ON/OFF A/B THEME | LEARN | ANALOG LINEAR | LINK) + 2 a
    // droite (avant le groupe THD et avant le bouton options -- audit).
    std::array<float, 6> headerSeparatorX {};
    juce::Rectangle<float> learnProgressBounds;
    juce::Image backgroundCache;
    // Echelle physique du cache de fond (echelle de la fenetre x mise a
    // l'echelle de l'ecran) : le fond est calcule a la resolution reelle,
    // net a 200 % comme sur un ecran 4K (voir paint()).
    float backgroundCacheScale = 1.0f;
    juce::Image woodImage;

    // --- Presets rapides (barre du bas, meme mecanisme que Nuke Mext). ---
    std::vector<PresetData> presets;
    ParamRamper knobRamper; // voir rampParamTo() -- anime les knobs au changement de preset
    int currentPresetIndex = -1;

    // Annuler / retablir (maquette + audit). Historique par instantanes de
    // tous les parametres, voir pushUndoSnapshotIfChanged().
    AlphaIconButton undoButton { AlphaIconButton::Icon::Undo, "Undo (Ctrl+Z)" };
    AlphaIconButton redoButton { AlphaIconButton::Icon::Redo, "Redo (Ctrl+Y)" };
    std::vector<std::vector<float>> undoHistory; // valeurs normalisees de processorRef.getParameters()
    int undoIndex = -1;                          // instantane courant dans undoHistory
    bool userTouchedParams = false;              // un geste/preset/LEARN depuis le dernier instantane
    juce::uint32 lastParamChangeMs = 0;
    std::vector<float> lastSeenParams;

    AlphaIconButton prevPresetButton { AlphaIconButton::Icon::Prev, "Previous preset" };
    AlphaIconButton nextPresetButton { AlphaIconButton::Icon::Next, "Next preset" };
    AlphaPresetPill presetNameButton; // ouvre le navigateur plein ecran (PresetBrowserOverlay)
    juce::Label presetAuthorLabel;    // garde pour refreshPresetDisplay() -- non affiche dans l'interface Alpha
    AlphaIconButton copyButton  { AlphaIconButton::Icon::Copy,  "Copy settings" };
    AlphaIconButton pasteButton { AlphaIconButton::Icon::Paste, "Paste settings" };
    AlphaIconButton saveButton  { AlphaIconButton::Icon::Save,  "Save preset" };
    AlphaIconButton deletePresetButton { AlphaIconButton::Icon::Trash, "Delete preset" }; // desactive pour les presets Side Key

    // Plafond de sortie du THD (-0.5 a -3 dB) : fleches + afficheur.
    std::unique_ptr<AlphaCeilControl> thdCeiling;
    // Barre du bas, a droite (option A) : surechantillonnage de l'etage THD
    // (OFF / x2 / x4 / x8) et forme du clip (SOFT / HARD).
    std::unique_ptr<AlphaSegmentBar> osBar, clipBar;


    PresetBrowserOverlay presetBrowser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SevenpackEditorContent)
};

/**
    SevenpackAudioProcessorEditor
    -------------------------------
    Coquille MINCE -- le VRAI AudioProcessorEditor enregistre aupres de
    l'hote. Possede un unique enfant, SevenpackEditorContent (taille
    logique FIXE, jamais modifiee), auquel elle applique une
    transformation d'echelle (voir applyUiScale()). C'est CETTE classe --
    et non le contenu -- qui appelle setSize() avec la taille MISE A
    L'ECHELLE : c'est le seul mecanisme que tous les hotes VST3 respectent
    de facon fiable pour redimensionner reellement la fenetre du plugin
    (75/85/100%, voir OptionsOverlay).
*/
class SevenpackAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SevenpackAudioProcessorEditor (SevenpackAudioProcessor& p)
        : juce::AudioProcessorEditor (&p), processorRef (p), content (p), licenseOverlay (p.licenseManager)
    {
        // Opaque (retour utilisateur : "quand je change la taille du VST
        // ... clignotement blanc ... le temps qu'il calcule les positions
        // c'est pas propre") -- sans ca, le peer de fenetre efface la zone
        // exposee par setSize() en blanc (fond par defaut de l'OS/JUCE)
        // AVANT que paint() ne redessine, le temps d'un frame -- visible a
        // chaque changement d'echelle (menu Options). setOpaque(true) +
        // paint() qui remplit vraiment tout le fond (voir plus bas) evite
        // cette etape d'effacement intermediaire.
        setOpaque (true);
        addAndMakeVisible (content);
        content.setBounds (0, 0, SevenpackEditorContent::baseWidth, SevenpackEditorContent::baseHeight);
        content.onScaleRequested = [this] (float scale) { applyUiScale (scale); };
        content.onOpenLicenseRequested = [this] { licenseOverlay.showManually(); };

        // Sits OUTSIDE the scaled content (sibling, not child) so it isn't
        // stretched/shrunk by content's setTransform — always drawn at the
        // editor's real pixel size, see resized(). addChildComponent (NOT
        // addAndMakeVisible) -- the latter forces visible=true, which would
        // stomp on the hidden state LicenseOverlay's own constructor just
        // computed (grace-period check).
        addChildComponent (licenseOverlay);
        licenseOverlay.toFront (false);

        // allowHostToResize=FALSE (voir explication utilisateur) : quand
        // c'est TRUE, l'hote (ex. Ableton Live) considere la fenetre comme
        // "redimensionnable par l'utilisateur" et ajoute ses PROPRES
        // affordances de resize sur la fenetre flottante du plugin --
        // notamment le double-clic sur la barre de titre qui bascule en
        // plein ecran, comme n'importe quelle fenetre native. Massive X/
        // Serum 2 n'ont PAS ce comportement car ils gerent LEUR PROPRE
        // menu d'echelle interne (comme notre OptionsOverlay) et declarent
        // canResize=false a l'hote -- nos appels setSize() explicites
        // (voir applyUiScale() ci-dessous) restent honores par l'hote
        // meme avec ce flag a false, car c'est le PLUGIN qui demande sa
        // taille, pas un drag utilisateur (chemin different cote hote).
        setResizable (false, false);
        setResizeLimits ((int) (SevenpackEditorContent::baseWidth  * 0.75f), (int) (SevenpackEditorContent::baseHeight * 0.75f),
                         (int) (SevenpackEditorContent::baseWidth  * kMaxScale), (int) (SevenpackEditorContent::baseHeight * kMaxScale));

        // Re-applique une seconde fois des que la fenetre a un vrai peer
        // natif -- voir parentHierarchyChanged() ci-dessous (retour
        // utilisateur : "je suis oblige de changer la taille du plugin
        // pour avoir le bon design d'enfoncement" -- CET appel-ci, dans le
        // constructeur, a lieu AVANT que la fenetre ait un peer, et
        // suffit pour la mise en page/taille mais pas pour que le rendu
        // neumorphic (setPaintingIsUnclipped) parte sur de bonnes bases).
        applyUiScale (processorRef.uiScale.load());
    }

    ~SevenpackAudioProcessorEditor() override
    {
       #if JUCE_WINDOWS
        unhookTitleBarDoubleClick();
       #endif
    }

    // requested : 0 = AUTO (calee sur l'ecran), sinon l'echelle choisie.
    void applyUiScale (float requested)
    {
        processorRef.uiScale.store (requested);
        const float scale = requested > 0.0f ? juce::jlimit (0.5f, kMaxScale, requested) : autoScale();
        appliedScale = scale;
        content.setTransform (juce::AffineTransform::scale (scale));
        setSize (juce::roundToInt (SevenpackEditorContent::baseWidth  * scale),
                 juce::roundToInt (SevenpackEditorContent::baseHeight * scale));
    }

    static constexpr float kMaxScale = 2.0f;

    // Taille AUTO (demande explicite : "sur un ecran en 4k il etait tout
    // petit ... que le plug in soit adaptatif en fonction des
    // resolutions") : hauteur de la fenetre = 70 % de la zone utile de
    // l'ecran (pixels logiques, mise a l'echelle de Windows/macOS deja
    // comprise), au pas de 5 %, entre 75 % et 200 %. Ecran 1080p : 90 %
    // (le defaut d'avant) ; 1440p : 120 % ; 4K a 100 % : 180 %. Ecran : celui
    // de la fenetre une fois affichee, sinon celui de la souris (le plugin
    // s'ouvre la ou l'on vient de cliquer).
    float autoScale() const
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const juce::Displays::Display* d = nullptr;
        if (auto* peer = getPeer())
            d = displays.getDisplayForRect (peer->getBounds());
        if (d == nullptr)
            d = displays.getDisplayForPoint (juce::Desktop::getMousePosition());
        if (d == nullptr)
            d = displays.getPrimaryDisplay();
        if (d == nullptr)
            return 0.90f;
        const auto area = d->userArea.toFloat();
        // Plus grande taille qui tient a l'ecran ; sur les petits ecrans
        // (portables) on garde 90 % tant que ca tient, comme avant.
        const float fit = juce::jmin (area.getHeight() * 0.92f / (float) SevenpackEditorContent::baseHeight,
                                      area.getWidth()  * 0.95f / (float) SevenpackEditorContent::baseWidth);
        float s = juce::jmax (area.getHeight() * 0.70f / (float) SevenpackEditorContent::baseHeight, juce::jmin (0.90f, fit));
        s = juce::jmin (s, fit);
        s = std::floor (s * 20.0f + 0.001f) / 20.0f;
        return juce::jlimit (0.75f, kMaxScale, s);
    }

    // AUTO : recale la taille sur l'ecran reel une fois la fenetre affichee
    // (au constructeur, il n'y a pas encore de fenetre).
    void refitAutoScale()
    {
        if (processorRef.uiScale.load() > 0.0f || getPeer() == nullptr)
            return;
        if (! juce::approximatelyEqual (autoScale(), appliedScale))
            applyUiScale (0.0f);
    }

    // Remplit tout le fond (voir setOpaque(true) au constructeur) -- sinon
    // le rectangle expose par un changement d'echelle resterait blanc
    // jusqu'a ce que `content` (plus petit ou plus grand selon le sens du
    // resize) le recouvre a son tour.
    void paint (juce::Graphics& g) override { g.fillAll (SevenpackColours::bg); }

    void resized() override
    {
        licenseOverlay.setBounds (getLocalBounds());
    }

   #if JUCE_WINDOWS
    // Ableton Live est un cas particulier documente dans JUCE lui-meme
    // (voir juce_audio_plugin_client_VST3.cpp, canResize()/checkSizeConstraint()) :
    // il IGNORE canResize=false pour le double-clic sur la barre de titre
    // -- ce geste reste un WM_SYSCOMMAND/SC_MAXIMIZE Windows natif, gere
    // par la fenetre HOTE (proprietaire de la barre de titre), pas par
    // notre propre HWND enfant. setResizable(false, ...) seul ne suffit
    // donc pas chez Ableton -- il faut intercepter ce message directement
    // sur la fenetre racine (GA_ROOT) et l'avaler avant que Windows ne
    // maximise. Massive X/Serum 2 font tres probablement la meme chose en
    // coulisses. Windows uniquement (WM_NCLBUTTONDBLCLK/SC_MAXIMIZE
    // n'existent pas sur macOS -- il faudrait l'equivalent NSWindow cote
    // Mac si le plugin est un jour compile pour cette plateforme).
    void parentHierarchyChanged() override
    {
        juce::AudioProcessorEditor::parentHierarchyChanged();
        hookTitleBarDoubleClick();
        refitAutoScale();

        // 7 tentatives basees sur "il faut redeclencher un repaint au bon
        // moment/a la bonne echelle" ont toutes echoue (retour
        // utilisateur repete). Analyse revisee : ce n'est PROBABLEMENT PAS
        // un bug de timing/repaint du tout -- voir SevenpackLookAndFeel.cpp,
        // paintNeumorphicButton() : l'etat SOULEVE (bouton eteint, au
        // repos) utilise un relief EXTERIEUR discret (double DropShadow),
        // volontairement adouci lors d'un tour precedent pour eliminer un
        // "carre" visible autour du bouton -- ce reglage l'a sans doute
        // rendu trop subtil/plat. L'etat CREUX (allume/selectionne)
        // utilise un contraste INTERNE bien plus marque (degrades noir/
        // blanc clippes dans la forme). Cliquer un bouton (SOLO) le fait
        // passer en creux -- PERMANENT tant qu'il reste actif -- d'ou
        // l'impression d'un "avant/apres" qui n'est en realite qu'un
        // changement d'ETAT LOGIQUE (eteint/allume), pas un correctif de
        // rendu. Fix reel applique directement dans paintNeumorphicButton()
        // (contraste du relief souleve remonte).
    }

    void hookTitleBarDoubleClick()
    {
        auto* peer = getPeer();
        if (peer == nullptr)
            return;

        auto ownHwnd = (HWND) peer->getNativeHandle();
        if (ownHwnd == nullptr)
            return;

        // Notre PROPRE HWND (retour utilisateur : "clignotement blanc ...
        // quand je change la taille" -- persistant meme apres setOpaque()
        // + paint() de fond, voir SevenpackAudioProcessorEditor::paint()
        // ci-dessus) : Windows envoie WM_ERASEBKGND avant WM_PAINT a chaque
        // resize, qui efface la zone avec le pinceau de fond PAR DEFAUT de
        // la classe de fenetre (blanc) -- un frame visible avant que notre
        // propre paint() ne repasse dessus. On avale ce message sur NOTRE
        // fenetre (contrairement au hook de la barre de titre ci-dessous,
        // qui cible la fenetre RACINE de l'hote) : renvoyer 1 sans rien
        // dessiner dit a Windows "deja efface, ne fais rien", ce qui
        // elimine l'etape d'effacement intermediaire.
        if (ownHwndHooked != ownHwnd)
        {
            auto originalOwn = (WNDPROC) SetWindowLongPtrW (ownHwnd, GWLP_WNDPROC, (LONG_PTR) &titleBarWndProcThunk);
            originalProcs()[ownHwnd] = originalOwn;
            hookedOwnHwnds().insert (ownHwnd);
            ownHwndHooked = ownHwnd;
        }

        if (titleBarHwnd != nullptr)
            return; // barre de titre deja accrochee

        auto root = GetAncestor (ownHwnd, GA_ROOT);
        if (root == nullptr)
            return;

        // BUG CORRIGE (retour utilisateur : "le standalone sur windows ne
        // se lance pas") : en Standalone (pas d'hote), notre propre fenetre
        // EST la racine (root == ownHwnd, pas de fenetre parente separee
        // possedee par un DAW). Sans cette garde, le bloc ci-dessus venait
        // de sous-classer ownHwnd, PUIS ce second SetWindowLongPtrW
        // sous-classait la MEME HWND une deuxieme fois -- lisant NOTRE
        // PROPRE thunk comme "procedure d'origine" et l'enregistrant comme
        // telle dans originalProcs(). Resultat : callOriginal() rappelait
        // le thunk lui-meme indefiniment des le premier message concerne
        // (WM_ERASEBKGND, envoye au tout premier affichage de la fenetre)
        // -- recursion infinie, stack overflow, crash immediat au
        // lancement. Le hook ownHwnd ci-dessus gere deja WM_ERASEBKGND et
        // le double-clic/maximize sur cette meme fenetre : rien de plus a
        // accrocher ici quand root et ownHwnd coincident.
        if (root == ownHwnd)
        {
            titleBarHwnd = root;
            return;
        }

        titleBarHwnd = root;
        auto original = (WNDPROC) SetWindowLongPtrW (root, GWLP_WNDPROC, (LONG_PTR) &titleBarWndProcThunk);

        // NB : on n'utilise PAS GWLP_USERDATA pour stocker la procedure
        // d'origine -- cette fenetre appartient a l'HOTE (Ableton), qui
        // s'en sert probablement pour son propre usage interne ; l'ecraser
        // risquerait de casser des choses cote hote qui n'ont rien a voir
        // avec nous. On garde la table originalProcs a nous, en statique.
        originalProcs()[root] = original;
    }

    void unhookTitleBarDoubleClick()
    {
        auto& procs = originalProcs();

        if (ownHwndHooked != nullptr)
        {
            if (auto it = procs.find (ownHwndHooked); it != procs.end())
            {
                SetWindowLongPtrW (ownHwndHooked, GWLP_WNDPROC, (LONG_PTR) it->second);
                procs.erase (it);
            }
            hookedOwnHwnds().erase (ownHwndHooked);
            ownHwndHooked = nullptr;
        }

        if (titleBarHwnd == nullptr)
            return;

        if (auto it = procs.find (titleBarHwnd); it != procs.end())
        {
            SetWindowLongPtrW (titleBarHwnd, GWLP_WNDPROC, (LONG_PTR) it->second);
            procs.erase (it);
        }

        titleBarHwnd = nullptr;
    }

    static std::map<HWND, WNDPROC>& originalProcs()
    {
        static std::map<HWND, WNDPROC> procs;
        return procs;
    }

    // HWNDs subclassees pour NOUS (voir hookTitleBarDoubleClick) plutot que
    // pour la barre de titre de l'hote -- distingue les deux cas dans le
    // thunk partage ci-dessous (WM_ERASEBKGND ne doit etre avale QUE sur
    // notre propre fenetre, jamais sur celle de l'hote).
    static std::set<HWND>& hookedOwnHwnds()
    {
        static std::set<HWND> hwnds;
        return hwnds;
    }

    static LRESULT CALLBACK titleBarWndProcThunk (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Sur NOTRE fenetre : avale WM_ERASEBKGND (retour utilisateur :
        // "clignotement blanc quand je change la taille") -- Windows
        // efface sinon la zone exposee avec le pinceau de fond PAR DEFAUT
        // de la classe de fenetre (blanc) avant chaque WM_PAINT, un frame
        // visible avant que notre propre paint() ne repasse dessus.
        // Renvoyer 1 dit a Windows "deja efface, ne fais rien".
        if (msg == WM_ERASEBKGND && hookedOwnHwnds().count (hwnd) != 0)
            return 1;

        // Sur la barre de titre de l'hote : avale le double-clic (maximize
        // direct) et toute demande explicite de maximisation/
        // redimensionnement via le menu systeme -- le reste des messages
        // est transmis tel quel a la procedure d'origine de l'hote, on ne
        // touche a rien d'autre.
        if (msg == WM_NCLBUTTONDBLCLK
            || (msg == WM_SYSCOMMAND && ((wParam & 0xFFF0) == SC_MAXIMIZE || (wParam & 0xFFF0) == SC_SIZE)))
            return 0;

        auto& procs = originalProcs();
        auto it = procs.find (hwnd);
        auto callOriginal = [&] (UINT m, WPARAM w, LPARAM l) -> LRESULT
        {
            if (it != procs.end() && it->second != nullptr)
                return CallWindowProcW (it->second, hwnd, m, w, l);
            return DefWindowProcW (hwnd, m, w, l);
        };

        // Rapporte a Windows demande explicite : "je peux etre sur le bord
        // gauche/droit/bas, mais rien la-dedans n'est un poignee de
        // redimensionnement" -- HTBORDER au lieu de HTLEFT/HTRIGHT/HTTOP/
        // HTBOTTOM/HT*CORNER empeche a la fois le curseur "double fleche" ET
        // le drag-to-resize sur ces zones, sans toucher au reste de la
        // fenetre (barre de titre, boutons systeme, contenu du plugin).
        if (msg == WM_NCHITTEST)
        {
            const LRESULT hit = callOriginal (msg, wParam, lParam);
            switch (hit)
            {
                case HTLEFT: case HTRIGHT: case HTTOP: case HTBOTTOM:
                case HTTOPLEFT: case HTTOPRIGHT: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
                    return HTBORDER;
                default:
                    return hit;
            }
        }

        return callOriginal (msg, wParam, lParam);
    }
   #else
    void parentHierarchyChanged() override
    {
        juce::AudioProcessorEditor::parentHierarchyChanged();
        refitAutoScale();
    }
   #endif

private:
    SevenpackAudioProcessor& processorRef;
    float appliedScale = 1.0f;

    // Sentinelle : applique le theme sauvegarde aux globales
    // SevenpackColours AVANT que `content` (juste apres) ne commence a se
    // construire -- voir SevenpackLookAndFeel::applyGlobalTheme() pour le
    // detail du probleme d'ordre d'initialisation que ce membre corrige.
    // L'ordre d'initialisation suit l'ordre de DECLARATION (pas l'ordre
    // dans la liste d'init du constructeur), d'ou ce placement precis juste
    // avant `content`.
    bool themeAppliedBeforeContent = (SevenpackLookAndFeel::applyGlobalTheme (processorRef.uiTheme.load()), true);

    SevenpackEditorContent content;
    LicenseOverlay licenseOverlay;

    // Rendu OpenGL TENTE puis RETIRE (retour utilisateur : "oula le plug in
    // rame enormement" -- pire qu'avant plutot que mieux, cas connu de
    // certains hotes/pilotes qui gerent mal un contexte OpenGL attache a
    // une fenetre de plugin flottante). Revenir au rasterizer logiciel par
    // defaut de JUCE si une optimisation de performance est souhaitee a
    // l'avenir, chercher plutot du cote de la frequence des Timer (voir
    // GRMeter/NeedleVU/BandMetersRack) ou de repaint() cible plus fin.

   #if JUCE_WINDOWS
    HWND titleBarHwnd = nullptr;
    HWND ownHwndHooked = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SevenpackAudioProcessorEditor)
};
