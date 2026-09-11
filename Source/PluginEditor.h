#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SixpackLookAndFeel.h"
#include "GRMeter.h"
#include "RatioSelector.h"
#include "PowerRocker.h"
#include "ToggleSwitch.h"
#include "RackPushButton.h"
#include "DetectorFilterGraph.h"
#include "NeedleVU.h"
#include "NeedleModeSelector.h"
#include "ThdHeaderModule.h"
#include "BandMetersRack.h"
#include "SixpackLogoButton.h"
#include "AboutOverlay.h"
#include "OptionsOverlay.h"
#include "KnobSlider.h"
#include "XoverModeButton.h"
#include "LatencyModeButton.h"
#include "AiLearnButton.h"
#include "FaderControl.h"
#include "SoloSwitch.h"
#include "ABSwitch.h"
#include "TrashButton.h"
#include "RackButton.h"
#include "PresetNameButton.h"
#include "PresetBrowserOverlay.h"
#include "ParamRamper.h"
#include "LicenseOverlay.h"
#include "CrossoverSpectrumEditor.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <map>
 #include <set>
#endif

struct KnobControl
{
    KnobSlider   slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
    BandPanel
    ---------
    Une rangee de compression complete pour UNE bande -- GR meter/Ratio,
    Compresseur (Input/Attack/Release/Output) ET Expandeur (Range/Attack/
    Output) cote a cote, Optical/Overkill/Sidechain. Detector/Color/XY
    (DetectorFilterGraph) restent CABLES (attachements/DSP intacts) mais
    NE SONT PLUS AFFICHES (voir buildBandPanel() -- addChildComponent au
    lieu de addAndMakeVisible) -- retour utilisateur explicite : "je
    souhaite retirer detector, color et le XY. laisse dans le code mais
    retire du visuel si un jour je veux remettre". Dry/Wet et Spread ont
    demenage dans BandMetersRack (rack du bas). MOINS le Power rocker
    (devenu global, voir barre de controle). Les 5 instances existent
    TOUJOURS (attachements crees une fois, voir buildBandPanel()), mais
    UNE SEULE est visible a la fois -- celle choisie en cliquant une zone
    de bande dans CrossoverSpectrumEditor (voir SixpackEditorContent::
    selectedBand/refreshBandPanelVisibility()).
*/
struct BandPanel
{
    ToggleSwitch opticalButton { "OPTICAL", SixpackColours::ledAmber };
    ToggleSwitch britishButton { "OVERKILL", SixpackColours::ledRed };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> opticalAttachment, britishAttachment;

    // Ancien bouton Comp/Expand -- RETIRE DU VISUEL (addChildComponent,
    // jamais addAndMakeVisible, voir buildBandPanel()) : Compresseur et
    // Expandeur ont maintenant chacun leurs propres knobs permanents, plus
    // besoin de choisir un mode. Le composant reste instancie (idAboveBelow
    // reste dans l'APVTS pour la compat des presets/automations
    // existants), mais plus aucun ParameterAttachment ne le pilote.
    ToggleSwitch aboveBelowButton { "COMP", SixpackColours::ledBlue };

    ToggleSwitch sidechainToggle { "SC", SixpackColours::ledCyan };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    // Solo -- a gauche de la rangee, sous le tag LOW/MID/HIGH (voir
    // SoloSwitch.h). Exclut les 2 autres bandes du mix tant qu'actif.
    std::unique_ptr<SoloSwitch> soloSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;

    std::unique_ptr<GRMeter> grMeter;

    RatioSelector ratioSelector;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    ToggleSwitch detectorHpToggle   { "HP",   SixpackColours::ledGreen };
    ToggleSwitch detectorBellToggle { "BELL", SixpackColours::ledYellow };
    // BANDES INTERMEDIAIRES (LowMid/Mid/HighMid, remplace detectorHpToggle,
    // cache pour ces bandes -- deja bornees par les crossovers voisins, un
    // HP y est redondant) : 2e point Bell independant sur le detecteur,
    // voir DetectorFilterGraph.
    ToggleSwitch detectorBell2Toggle { "BELL 2", SixpackColours::ledBlue };
    ToggleSwitch detectorLinkToggle { "LINK", SixpackColours::ledRed };
    ToggleSwitch audioHpToggle      { "HP",     SixpackColours::ledBlue };
    // Libelles "TUBE"/"FUZZ" (demande explicite) -- idAudioDist2/idAudioDist3
    // et les noms de membre restent inchanges (IDs de parametre APVTS, ne
    // pas casser les sessions/presets existants), seul le texte affiche change.
    ToggleSwitch audioDist2Toggle   { "TUBE", SixpackColours::ledAmber };
    ToggleSwitch audioDist3Toggle   { "FUZZ", SixpackColours::ledAmber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        detectorHpAttachment, detectorBellAttachment, detectorBell2Attachment, detectorLinkAttachment,
        audioHpAttachment, audioDist2Attachment, audioDist3Attachment;

    std::unique_ptr<DetectorFilterGraph> filterGraph;

    KnobControl inputKnob, attackKnob, releaseKnob, outputKnob;

    // Expandeur, a droite du compresseur au-dela de la ligne de separation
    // (retour utilisateur explicite : "dans les bandes avec le gain de
    // place qu'on vient de faire, on va faire apparaitre les expandeurs.
    // pas besoin du reglage release sur les expandeurs. on garde le cote
    // violet des expandeurs") -- RANGE = idInputExpand (c'est LE MEME
    // parametre que la ligne RANGE de CrossoverSpectrumEditor, juste une
    // 2e poignee sur la meme valeur), pas de knob Release expose (le
    // parametre idReleaseExpand reste cable cote DSP avec sa valeur par
    // defaut). Teinte violette via SixpackLookAndFeel::
    // setKnobAccentGradient (voir buildBandPanel()).
    KnobControl expandRangeKnob, expandAttackKnob, expandOutputKnob;

    // Tag "LOW"/"MID"/"HIGH" desormais un LOGO image (LogoLow/Mid/High_png,
    // voir SixpackEditorContent::paint()) -- plus de Label texte.
    juce::Colour accentColour;
};

/**
    SixpackEditorContent
    ------------------------
    TOUT le contenu visuel du plugin (barre de controle, plaque VU/THD, 3
    bandes), a TAILLE LOGIQUE FIXE (1400x750, jamais modifiee). C'est un
    juce::Component ORDINAIRE (pas un AudioProcessorEditor) -- voir
    SixpackAudioProcessorEditor plus bas, qui l'heberge comme UNIQUE
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
class SixpackEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit SixpackEditorContent (SixpackAudioProcessor&);
    ~SixpackEditorContent() override;

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
    static constexpr int baseWidth = 1400, baseHeight = 808;

    // Appele quand l'utilisateur choisit une taille dans OptionsOverlay --
    // branche par SixpackAudioProcessorEditor (le vrai proprietaire du
    // redimensionnement hote, voir plus bas).
    std::function<void (float)> onScaleRequested;

    // Called when the user clicks "License" on the About screen — branched
    // by SixpackAudioProcessorEditor to show its LicenseOverlay (which
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
        std::array<float, (size_t) SixpackAudioProcessor::NumCrossovers> crossoverHz
            { { Crossover::kSplitDefaultHz[0], Crossover::kSplitDefaultHz[1], Crossover::kSplitDefaultHz[2], Crossover::kSplitDefaultHz[3], Crossover::kSplitDefaultHz[4] } };
        std::array<bool, (size_t) SixpackAudioProcessor::NumCrossovers> crossoverEnabled { { true, true, true, true, true } };
        bool linearMode = false; // WIDE (false) / LINEAR (true)
        bool softClipOn = true;  // THD Valve (global, engage par defaut)
        int thdMode = 0;         // 0=Soft 1=Tape 2=Iron 3=Air (voir SoftClipper::SatMode)

        struct Band
        {
            float input = 0.0f;
            int ratioIndex = 3;
            float attack = SixpackAudioProcessor::defaultAttackMs;
            float release = SixpackAudioProcessor::defaultReleaseMs;
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
            float attackExpand = SixpackAudioProcessor::defaultAttackMs;
            float releaseExpand = SixpackAudioProcessor::defaultReleaseMs;
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
        Band bands[(size_t) SixpackAudioProcessor::NumBands];

        juce::String author;
        bool isFactory = false; // les presets usine ne peuvent pas etre supprimes
    };

private:
    void setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                    const juce::String& unitSuffix, int decimalPlaces);
    void rebuildBackgroundCache();
    void refreshThemeReactiveColours();
    void timerCallback() override;

    void buildBandPanel (int band, juce::Colour accent, const juce::String& tag);
    void layoutBandRow (BandPanel& bp, juce::Rectangle<int> area);

    // N'affiche que bandPanels[selectedBand] (masque les 4 autres) --
    // appele au changement de selection (callback CrossoverSpectrumEditor::
    // onBandSelected) et depuis resized(). isMid/audioHp/detectorLink
    // suivent les memes regles de visibilite PAR BANDE que buildBandPanel()
    // leur a donnees a la construction (pas de re-derivation ad-hoc).
    void setBandPanelVisible (int band, bool visible);
    void refreshBandPanelVisibility();

    void wireInputLink();  // lien relatif entre les 3 knobs Input (voir linkInSwitch)
    void wireOutputLink(); // lien relatif entre les 3 knobs Output (voir linkOutSwitch)
    void updateInputLinkVisuals();
    void updateOutputLinkVisuals();

    void showOptionsOverlay();

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
    void copyStateToClipboard();
    void pasteStateFromClipboard();

    // Bouton AI (voir AiLearnAnalyzer.h) : applique le resultat calcule
    // exactement comme un preset (rampParamTo pour les knobs continus,
    // setParamValue pour le ratio discret) -- appele depuis timerCallback()
    // des que processorRef.aiLearnAnalyzer.getState() == Done.
    void applyAiLearnResult (const AiLearnAnalyzer::Result& result);

    SixpackAudioProcessor& processorRef;
    SixpackLookAndFeel lookAndFeel;

    std::array<BandPanel, (size_t) SixpackAudioProcessor::NumBands> bandPanels;

    // Bande actuellement affichee dans le panneau de detail unique (voir
    // BandPanel/setBandPanelVisible()) -- etat purement visuel, PAS un
    // parametre APVTS (pas automatisable, pas sauvegarde en preset/session
    // -- redemarre sur MID a chaque ouverture, comme les onglets d'un menu).
    int selectedBand = (int) SixpackAudioProcessor::Mid;

    // Bandes "liees" en edition (Ctrl/Cmd+clic sur SEL, retour utilisateur :
    // "en appuyant sur control (cmd mac) on peut selectionner plusieurs
    // bandes avec Sel ... je veux le meme systeme que le pairage d'une
    // console") -- transmis a BandMetersRack (setSelectedBand()) qui gere
    // le "pairage" reel des faders Output visibles (voir
    // BandMetersRack::wireOutputSelectionLink()) : bouger le fader de
    // N'IMPORTE QUELLE bande du groupe (selectedBand OU une bande liee)
    // deplace toutes les autres du meme delta, symetrique comme sur une
    // vraie console. Un clic SIMPLE (sans Ctrl) sur SEL vide cette
    // selection et change juste selectedBand.
    std::array<bool, (size_t) SixpackAudioProcessor::NumBands> linkedBands {};

    // --- Barre de controle globale (haut) ---
    PowerRocker powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    // A/B -- pilule encastree, cote actif colore (voir ABSwitch.h et
    // SixpackAudioProcessor::toggleABCompare()/selectABSlot()).
    ABSwitch abButton;

    // Remplace les anciens knobs XLO/XHI (Tripressor) -- spectre + 4
    // lignes de crossover draggables/activables + selection de bande, voir
    // CrossoverSpectrumEditor.h et selectedBand ci-dessous.
    CrossoverSpectrumEditor crossoverSpectrum { processorRef };

    // processorRef est declare (donc initialise) AVANT ce membre -- l'ordre
    // d'initialisation suit l'ordre de DECLARATION dans la classe, pas
    // l'ordre du constructeur, cette reference est donc valide ici (meme
    // pattern que needleVu1/2 et thdHeader plus bas).
    XoverModeButton xoverModeButton { *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idDigitalCrossover) };
    LatencyModeButton latencyModeButton;

    // Bouton AI, juste au-dessus de xoverModeButton (demande explicite) --
    // meme raisonnement d'ordre d'initialisation que ci-dessus.
    AiLearnButton aiLearnButton { processorRef };

    // Style neumorphic "dark mono" unifie avec SOLO/SEL/LEARN/THD (retour
    // utilisateur : "on va mettre les memes boutons pour link in link out"),
    // remplace l'ancien ToggleSwitch (sprite).
    RackPushButton linkInSwitch  { "LINK IN",  SixpackColours::ledGreen };
    RackPushButton linkOutSwitch { "LINK OUT", SixpackColours::ledRed };
    bool linkInGuard = false, linkOutGuard = false;
    std::array<double, (size_t) SixpackAudioProcessor::NumBands> lastInputValue {};
    std::array<double, (size_t) SixpackAudioProcessor::NumBands> lastOutputValue {};

    SixpackLogoButton suppressorLogoButton;
    AboutOverlay aboutOverlay;
    OptionsOverlay optionsOverlay;

    // --- Plaque VU globale (inchangee dans son contenu, juste deplacee
    // plus bas dans resized()) ---
    NeedleVU needleVu1 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex1), NeedleVU::Channel::Left };
    NeedleVU needleVu2 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex2), NeedleVU::Channel::Right };
    NeedleModeSelector needleVu1Modes { "L", processorRef.needleModeIndex1 };
    NeedleModeSelector needleVu2Modes { "R", processorRef.needleModeIndex2 };

    // THD compact, dans la barre de controle (remplace l'ancien rack
    // pleine largeur ThdValveModule -- retour utilisateur : "on va
    // liberer le rack du bas", voir ThdHeaderModule.h).
    ThdHeaderModule thdHeader { *processorRef.apvts.getRawParameterValue (SixpackAudioProcessor::idSoftClip),
                                 processorRef.clipMeterAmount };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> softClipAttachment;
    std::unique_ptr<juce::ParameterAttachment> thdModeAttachment; // voir ThdHeaderModule (TAPE/IRON/AIR/CLIP)

    // Rack du bas -- occupe desormais par un mini vu-metre GR/Expand par
    // bande (retour utilisateur, voir BandMetersRack.h) ; la plaque restait
    // vide depuis le deplacement du module THD dans la barre de controle.
    BandMetersRack bandMetersRack { processorRef };

    juce::Rectangle<float> controlBarBounds; // barre globale (haut)
    juce::Rectangle<float> vuPlateBounds;
    // UNE SEULE zone de detail (plus un tableau par bande) -- voir doc de
    // classe BandPanel/selectedBand ci-dessus.
    juce::Rectangle<float> bandPanelBounds;
    juce::Rectangle<float> crossoverEditorBounds;
    juce::Image backgroundCache;

    // --- Presets rapides : barre dediee en bas de fenetre (Preset / < >
    // nom / Copy / Paste / Save / Corbeille), meme mecanisme que Nuke Mext. ---
    std::vector<PresetData> presets;
    ParamRamper knobRamper; // voir rampParamTo() -- anime les knobs au changement de preset
    int currentPresetIndex = -1;

    juce::Label presetSectionLabel;
    RackButton prevPresetButton { "<" };
    RackButton nextPresetButton { ">" };
    PresetNameButton presetNameButton; // ecran LCD cliquable -- ouvre le navigateur plein ecran (PresetBrowserOverlay)
    juce::Label presetAuthorLabel;     // "Side Key" / nom de l'utilisateur, juste apres le nom
    RackButton copyButton { "COPY", SixpackColours::ledBlue };
    RackButton pasteButton { "PASTE", SixpackColours::ledBlue };
    RackButton saveButton { "SAVE", SixpackColours::ledGreen };
    TrashButton deletePresetButton; // supprime le preset utilisateur courant -- desactivee pour les presets Side Key

    // Plafond de sortie du THD Valve, loge a droite de la barre de presets
    // (retour utilisateur : "je souhaite quand meme pouvoir gerer le niveau
    // de sortie apres le THD ... de -0.5 a -3"). Slider LinearBar compact
    // pour tenir dans la rangee de 26px.
    juce::Label  thdCeilingLabel;
    juce::Slider thdCeilingSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thdCeilingAttachment;

    PresetBrowserOverlay presetBrowser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixpackEditorContent)
};

/**
    SixpackAudioProcessorEditor
    -------------------------------
    Coquille MINCE -- le VRAI AudioProcessorEditor enregistre aupres de
    l'hote. Possede un unique enfant, SixpackEditorContent (taille
    logique FIXE, jamais modifiee), auquel elle applique une
    transformation d'echelle (voir applyUiScale()). C'est CETTE classe --
    et non le contenu -- qui appelle setSize() avec la taille MISE A
    L'ECHELLE : c'est le seul mecanisme que tous les hotes VST3 respectent
    de facon fiable pour redimensionner reellement la fenetre du plugin
    (75/85/100%, voir OptionsOverlay).
*/
class SixpackAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SixpackAudioProcessorEditor (SixpackAudioProcessor& p)
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
        content.setBounds (0, 0, SixpackEditorContent::baseWidth, SixpackEditorContent::baseHeight);
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
        setResizeLimits ((int) (SixpackEditorContent::baseWidth  * 0.75f), (int) (SixpackEditorContent::baseHeight * 0.75f),
                          SixpackEditorContent::baseWidth,                 SixpackEditorContent::baseHeight);

        // Re-applique une seconde fois des que la fenetre a un vrai peer
        // natif -- voir parentHierarchyChanged() ci-dessous (retour
        // utilisateur : "je suis oblige de changer la taille du plugin
        // pour avoir le bon design d'enfoncement" -- CET appel-ci, dans le
        // constructeur, a lieu AVANT que la fenetre ait un peer, et
        // suffit pour la mise en page/taille mais pas pour que le rendu
        // neumorphic (setPaintingIsUnclipped) parte sur de bonnes bases).
        applyUiScale (processorRef.uiScale.load());
    }

    ~SixpackAudioProcessorEditor() override
    {
       #if JUCE_WINDOWS
        unhookTitleBarDoubleClick();
       #endif
    }

    void applyUiScale (float scale)
    {
        processorRef.uiScale.store (scale);
        content.setTransform (juce::AffineTransform::scale (scale));
        setSize (juce::roundToInt (SixpackEditorContent::baseWidth  * scale),
                 juce::roundToInt (SixpackEditorContent::baseHeight * scale));
    }

    // Remplit tout le fond (voir setOpaque(true) au constructeur) -- sinon
    // le rectangle expose par un changement d'echelle resterait blanc
    // jusqu'a ce que `content` (plus petit ou plus grand selon le sens du
    // resize) le recouvre a son tour.
    void paint (juce::Graphics& g) override { g.fillAll (SixpackColours::bg); }

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

        // 7 tentatives basees sur "il faut redeclencher un repaint au bon
        // moment/a la bonne echelle" ont toutes echoue (retour
        // utilisateur repete). Analyse revisee : ce n'est PROBABLEMENT PAS
        // un bug de timing/repaint du tout -- voir SixpackLookAndFeel.cpp,
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
        // + paint() de fond, voir SixpackAudioProcessorEditor::paint()
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
   #endif

private:
    SixpackAudioProcessor& processorRef;

    // Sentinelle : applique le theme sauvegarde aux globales
    // SixpackColours AVANT que `content` (juste apres) ne commence a se
    // construire -- voir SixpackLookAndFeel::applyGlobalTheme() pour le
    // detail du probleme d'ordre d'initialisation que ce membre corrige.
    // L'ordre d'initialisation suit l'ordre de DECLARATION (pas l'ordre
    // dans la liste d'init du constructeur), d'ou ce placement precis juste
    // avant `content`.
    bool themeAppliedBeforeContent = (SixpackLookAndFeel::applyGlobalTheme (processorRef.lightTheme.load() > 0.5f), true);

    SixpackEditorContent content;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixpackAudioProcessorEditor)
};
