#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SuppressorLookAndFeel.h"
#include "GRMeter.h"
#include "RatioSelector.h"
#include "PowerRocker.h"
#include "ToggleSwitch.h"
#include "DetectorFilterGraph.h"
#include "NeedleVU.h"
#include "NeedleModeSelector.h"
#include "ThdValveModule.h"
#include "SuppressorLogoButton.h"
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
    Une rangee de compression complete pour UNE bande (Low, Mid ou High) --
    exactement le contenu de l'ancienne plaque principale du Suppressor
    mono-bande (GR meter/Ratio, Detector, Audio, Input/Attack/Release/
    Output, Optical/British, Sidechain), MOINS le Power rocker (devenu
    global, voir barre de controle). Les 3 instances sont TOUJOURS visibles
    simultanement, empilees Low/Mid/High -- design M76/Mext.
*/
struct BandPanel
{
    ToggleSwitch opticalButton { "OPTICAL", SuppressorColours::ledAmber };
    ToggleSwitch britishButton { "OVERKILL", SuppressorColours::ledRed };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> opticalAttachment, britishAttachment;

    // Above/Below/Both (demande explicite, "dans le meme esprit que Ableton"
    // + "il faut que les deux puissent jouer en meme temps ... troisieme
    // fois avec ecrit both") -- a gauche d'Optical, cycle ABOVE -> BELOW ->
    // BOTH -> ABOVE... au clic. C'est un choix a 3 valeurs (pas un simple
    // bool), donc un ParameterAttachment manuel comme ratioSelector -- pas
    // de ButtonAttachment (reserve aux bool). Voir SuppressorEditorContent::
    // buildBandPanel().
    ToggleSwitch aboveBelowButton { "COMP", SuppressorColours::ledBlue };
    std::unique_ptr<juce::ParameterAttachment> aboveBelowAttachment;

    ToggleSwitch sidechainToggle { "SC", SuppressorColours::ledCyan };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    // Solo -- a gauche de la rangee, sous le tag LOW/MID/HIGH (voir
    // SoloSwitch.h). Exclut les 2 autres bandes du mix tant qu'actif.
    std::unique_ptr<SoloSwitch> soloSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;

    std::unique_ptr<GRMeter> grMeter;

    RatioSelector ratioSelector;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    ToggleSwitch detectorHpToggle   { "HP",   SuppressorColours::ledGreen };
    ToggleSwitch detectorBellToggle { "BELL", SuppressorColours::ledYellow };
    // BAND MID UNIQUEMENT (remplace detectorHpToggle, cache pour cette
    // bande -- deja bornee par le crossover, un HP y est redondant) : 2e
    // point Bell independant sur le detecteur, voir DetectorFilterGraph.
    ToggleSwitch detectorBell2Toggle { "BELL 2", SuppressorColours::ledBlue };
    ToggleSwitch detectorLinkToggle { "LINK", SuppressorColours::ledRed };
    ToggleSwitch audioHpToggle      { "HP",     SuppressorColours::ledBlue };
    // Libelles "TUBE"/"FUZZ" (demande explicite) -- idAudioDist2/idAudioDist3
    // et les noms de membre restent inchanges (IDs de parametre APVTS, ne
    // pas casser les sessions/presets existants), seul le texte affiche change.
    ToggleSwitch audioDist2Toggle   { "TUBE", SuppressorColours::ledAmber };
    ToggleSwitch audioDist3Toggle   { "FUZZ", SuppressorColours::ledAmber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        detectorHpAttachment, detectorBellAttachment, detectorBell2Attachment, detectorLinkAttachment,
        audioHpAttachment, audioDist2Attachment, audioDist3Attachment;

    std::unique_ptr<DetectorFilterGraph> filterGraph;

    KnobControl inputKnob, attackKnob, releaseKnob, outputKnob;

    // Faders Mix (Dry/Wet) et Width, a droite du dernier knob (Output),
    // au-dela de la ligne blanche -- voir PluginProcessor.h (idBandMix /
    // idBandWidth) et FaderControl.h.
    FaderControl mixFader, widthFader;

    // Tag "LOW"/"MID"/"HIGH" desormais un LOGO image (LogoLow/Mid/High_png,
    // voir SuppressorEditorContent::paint()) -- plus de Label texte.
    juce::Colour accentColour;
};

/**
    SuppressorEditorContent
    ------------------------
    TOUT le contenu visuel du plugin (barre de controle, plaque VU/THD, 3
    bandes), a TAILLE LOGIQUE FIXE (1400x750, jamais modifiee). C'est un
    juce::Component ORDINAIRE (pas un AudioProcessorEditor) -- voir
    SuppressorAudioProcessorEditor plus bas, qui l'heberge comme UNIQUE
    enfant avec une transformation d'echelle (setTransform). Cette
    separation est necessaire car un AudioProcessorEditor qui possede son
    propre "peer" (fenetre native de l'hote) ignore/ne propage pas
    correctement setTransform pour le redimensionnement -- seul un enfant
    JUCE ordinaire (non-peer) peut etre mis a l'echelle de facon fiable, le
    parent (l'editeur) annonce alors sa VRAIE taille (mise a l'echelle) a
    l'hote via setSize(), ce que tous les DAW respectent.

    POWER : plus d'overlay rectangulaire -- chaque composant (knobs,
    ToggleSwitch, RatioSelector, DetectorFilterGraph, NeedleVU,
    ThdValveModule) interroge lookAndFeel.isPowered() dans son propre
    paint() et se rend lui-meme "eteint", comme une machine debranchee.
    Voir timerCallback().
*/
class SuppressorEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit SuppressorEditorContent (SuppressorAudioProcessor&);
    ~SuppressorEditorContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // +40 par rapport a l'original (750) pour loger la barre de presets
    // (34px) en bas de fenetre, voir resized(). Version PRO -- pas de
    // bande "support" defilante (voir SupportTickerBar.h dans la copie
    // Free), donc pas d'espace supplementaire reserve.
    static constexpr int baseWidth = 1400, baseHeight = 790;

    // Appele quand l'utilisateur choisit une taille dans OptionsOverlay --
    // branche par SuppressorAudioProcessorEditor (le vrai proprietaire du
    // redimensionnement hote, voir plus bas).
    std::function<void (float)> onScaleRequested;

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
        float crossoverLow = 121.5f, crossoverHigh = 2521.0f;
        bool linearMode = false; // WIDE (false) / LINEAR (true)
        bool softClipOn = true;  // THD Valve (global, engage par defaut)
        int thdMode = 0;         // 0=Soft 1=Tape 2=Iron 3=Air (voir SoftClipper::SatMode)

        struct Band
        {
            float input = 0.0f;
            int ratioIndex = 3;
            float attack = SuppressorAudioProcessor::defaultAttackMs;
            float release = SuppressorAudioProcessor::defaultReleaseMs;
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
            float attackExpand = SuppressorAudioProcessor::defaultAttackMs;
            float releaseExpand = SuppressorAudioProcessor::defaultReleaseMs;
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
        Band bands[3];

        juce::String author;
        bool isFactory = false; // les presets usine ne peuvent pas etre supprimes
    };

private:
    void setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                    const juce::String& unitSuffix, int decimalPlaces);
    // accentBelowNeutral optionnel (demande explicite : "le fader spread
    // est completement orange ... pas hyper visible si on est cote mono et
    // cote stereo") -- laisser par defaut (transparent) = fader a 1 seule
    // couleur, comme avant (utilise par Mix). Le passer explicitement
    // donne un fader a 2 tons, cote mono / cote spread (utilise par Width).
    void setupFader (FaderControl& fader, const juce::String& paramID, double neutralValue,
                      const juce::String& topText, const juce::String& bottomText, juce::Colour accent,
                      juce::Colour accentBelowNeutral = juce::Colour());
    void rebuildBackgroundCache();
    void timerCallback() override;

    void buildBandPanel (int band, juce::Colour accent, const juce::String& tag);
    void layoutBandRow (BandPanel& bp, juce::Rectangle<int> area);

    // Comp et Expand partagent la meme bande mais des reglages Input/
    // Ratio/Attack/Release/Output INDEPENDANTS (demande explicite) --
    // reattache les 4 knobs + le RatioSelector de cette bande au jeu de
    // parametres correspondant. mode : 0=Comp, 1=Expand, 2=Both (Both
    // reste sur les parametres COMPRESSION, demande explicite).
    void rebindBandKnobsForMode (int band, int mode);

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

    SuppressorAudioProcessor& processorRef;
    SuppressorLookAndFeel lookAndFeel;

    std::array<BandPanel, (size_t) SuppressorAudioProcessor::NumBands> bandPanels;

    // --- Barre de controle globale (haut) ---
    PowerRocker powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    // BYPASS -- meme design que les boutons Detector (ToggleSwitch), sous
    // le logo/wordmark. MOMENTANE : pas de ButtonAttachment (ce n'est pas
    // un parametre APVTS persistant) -- pilote directement
    // processorRef.bypassHeld via onStateChange (voir constructeur), reste
    // actif tant que le bouton est maintenu enfonce.
    ToggleSwitch bypassButton { "BYPASS", SuppressorColours::ledRed };

    // A/B -- pilule encastree, cote actif colore (voir ABSwitch.h et
    // SuppressorAudioProcessor::toggleABCompare()/selectABSlot()).
    ABSwitch abButton;

    KnobControl crossoverLowKnob, crossoverHighKnob;
    // processorRef est declare (donc initialise) AVANT ce membre -- l'ordre
    // d'initialisation suit l'ordre de DECLARATION dans la classe, pas
    // l'ordre du constructeur, cette reference est donc valide ici (meme
    // pattern que needleVu1/2 et thdValve plus bas).
    XoverModeButton xoverModeButton { *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idDigitalCrossover) };
    LatencyModeButton latencyModeButton;

    // Bouton AI, juste au-dessus de xoverModeButton (demande explicite) --
    // meme raisonnement d'ordre d'initialisation que ci-dessus.
    AiLearnButton aiLearnButton { processorRef };

    ToggleSwitch linkInSwitch  { "LINK IN",  SuppressorColours::ledGreen };
    ToggleSwitch linkOutSwitch { "LINK OUT", SuppressorColours::ledRed };
    bool linkInGuard = false, linkOutGuard = false;
    std::array<double, (size_t) SuppressorAudioProcessor::NumBands> lastInputValue {};
    std::array<double, (size_t) SuppressorAudioProcessor::NumBands> lastOutputValue {};

    SuppressorLogoButton suppressorLogoButton;
    AboutOverlay aboutOverlay;
    OptionsOverlay optionsOverlay;

    // --- Plaque VU/THD globale (inchangee dans son contenu, juste
    // deplacee plus bas dans resized()) ---
    NeedleVU needleVu1 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex1), NeedleVU::Channel::Left };
    NeedleVU needleVu2 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex2), NeedleVU::Channel::Right };
    NeedleModeSelector needleVu1Modes { "L", processorRef.needleModeIndex1 };
    NeedleModeSelector needleVu2Modes { "R", processorRef.needleModeIndex2 };

    ThdValveModule thdValve { processorRef, processorRef.clipMeterAmountLow, processorRef.clipMeterAmountMid, processorRef.clipMeterAmountHigh,
                              *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idSoftClip) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> softClipAttachment;
    std::unique_ptr<juce::ParameterAttachment> thdModeAttachment; // voir ThdValveModule (TAPE/IRON/AIR/CLIP)

    juce::Rectangle<float> controlBarBounds; // barre globale (haut)
    juce::Rectangle<float> vuPlateBounds;    // plaque VU/THD globale
    std::array<juce::Rectangle<float>, (size_t) SuppressorAudioProcessor::NumBands> bandPanelBounds;
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
    RackButton copyButton { "COPY", SuppressorColours::ledBlue };
    RackButton pasteButton { "PASTE", SuppressorColours::ledBlue };
    RackButton saveButton { "SAVE", SuppressorColours::ledGreen };
    TrashButton deletePresetButton; // supprime le preset utilisateur courant -- desactivee pour les presets Side Key

    PresetBrowserOverlay presetBrowser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuppressorEditorContent)
};

/**
    SuppressorAudioProcessorEditor
    -------------------------------
    Coquille MINCE -- le VRAI AudioProcessorEditor enregistre aupres de
    l'hote. Possede un unique enfant, SuppressorEditorContent (taille
    logique FIXE, jamais modifiee), auquel elle applique une
    transformation d'echelle (voir applyUiScale()). C'est CETTE classe --
    et non le contenu -- qui appelle setSize() avec la taille MISE A
    L'ECHELLE : c'est le seul mecanisme que tous les hotes VST3 respectent
    de facon fiable pour redimensionner reellement la fenetre du plugin
    (75/85/100%, voir OptionsOverlay).
*/
class SuppressorAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SuppressorAudioProcessorEditor (SuppressorAudioProcessor& p)
        : juce::AudioProcessorEditor (&p), processorRef (p), content (p)
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
        content.setBounds (0, 0, SuppressorEditorContent::baseWidth, SuppressorEditorContent::baseHeight);
        content.onScaleRequested = [this] (float scale) { applyUiScale (scale); };

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
        setResizeLimits ((int) (SuppressorEditorContent::baseWidth  * 0.75f), (int) (SuppressorEditorContent::baseHeight * 0.75f),
                          SuppressorEditorContent::baseWidth,                 SuppressorEditorContent::baseHeight);

        applyUiScale (processorRef.uiScale.load());
    }

   #if JUCE_WINDOWS
    ~SuppressorAudioProcessorEditor() override { unhookTitleBarDoubleClick(); }
   #endif

    void applyUiScale (float scale)
    {
        processorRef.uiScale.store (scale);
        content.setTransform (juce::AffineTransform::scale (scale));
        setSize (juce::roundToInt (SuppressorEditorContent::baseWidth  * scale),
                 juce::roundToInt (SuppressorEditorContent::baseHeight * scale));
    }

    // Remplit tout le fond (voir setOpaque(true) au constructeur) -- sinon
    // le rectangle expose par un changement d'echelle resterait blanc
    // jusqu'a ce que `content` (plus petit ou plus grand selon le sens du
    // resize) le recouvre a son tour.
    void paint (juce::Graphics& g) override { g.fillAll (SuppressorColours::bg); }

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
        // + paint() de fond, voir SuppressorAudioProcessorEditor::paint()
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
    SuppressorAudioProcessor& processorRef;
    SuppressorEditorContent content;

   #if JUCE_WINDOWS
    HWND titleBarHwnd = nullptr;
    HWND ownHwndHooked = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuppressorAudioProcessorEditor)
};
