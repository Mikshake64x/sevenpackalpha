#pragma once
#include <JuceHeader.h>

namespace SevenpackColours
{
    // Chassis rack anthracite/brosse, esthetique "unite hardware montee en
    // baie" -- remplace l'ancien theme "labo cyberpunk" de Nuke Mk1.
    //
    // MUTABLES (inline, pas const) -- retour utilisateur explicite : "je
    // souhaite faire un design plus clair qu'on peut changer dans
    // l'engrenage" -- ce sont les SEULES teintes qui different entre le
    // theme SOMBRE (par defaut) et le theme CLAIR "metal brosse argente"
    // (voir applyDarkTheme()/applyLightTheme() plus bas et
    // SevenpackLookAndFeel::setLightTheme()). Les couleurs de DIODE/LCD
    // ci-dessous restent volontairement CONST (jamais retheme -- un
    // afficheur/meter reste sombre et vif quelle que soit la couleur du
    // chassis, comme sur du vrai hardware -- voir preview validee).
    inline juce::Colour bg          { 0xff121417 };
    inline juce::Colour panel       { 0xff1a1d21 };
    inline juce::Colour panelLight  { 0xff21252b };
    inline juce::Colour gridLine    { 0xff0a0b0d };
    inline juce::Colour textDim     { 0xffbcc3cc };
    inline juce::Colour textBright  { 0xffffffff };

    const juce::Colour ledOff      { 0xff20232a };
    const juce::Colour ledBlue     { 0xff2fb6ff };
    const juce::Colour ledAmber    { 0xffffb000 };
    const juce::Colour ledRed      { 0xffff3b30 };

    // Degrade de la rangee de GR LEDs (vert -> jaune -> ambre -> rouge),
    // comme sur le hardware de reference.
    const juce::Colour ledCyan     { 0xff00e5ff };
    const juce::Colour ledGreen    { 0xff39ff6a };
    const juce::Colour ledYellow   { 0xfff5e642 };
    const juce::Colour ledOrange   { 0xffff8c1a };

    // Degrade DEDIE a la rangee Expand du GRMeter (demande explicite : "un
    // systeme de couleur pour les diodes de l'expander mais pas les memes
    // que celle du gain reduction") -- famille froide bleu/cyan/violet,
    // delibrement distincte du vert->jaune->orange->rouge de la reduction.
    const juce::Colour ledPurple   { 0xffb84dff };

    // Accent de la 7e bande "Air" (au-dessus de High) -- continue le
    // degrade spectral cyan->vert->ambre->orange->violet->rouge en
    // poursuivant la roue chromatique au-dela du rouge, plutot que de
    // reutiliser une teinte deja prise par une bande voisine.
    const juce::Colour ledPink     { 0xffff4da6 };

    const juce::Colour lcdBg       { 0xff0a1620 };
    const juce::Colour lcdText     { 0xff3fd0ff };

    // FIXES (jamais retheme) -- retour utilisateur explicite : "il faut
    // ecrire en blanc sur les boutons push noire". De nombreux "chips"
    // (ToggleSwitch, RatioSelector, RackButton, boutons d'OptionsOverlay,
    // CrossoverSpectrumEditor, DetectorFilterGraph...) gardent un fond
    // FIXE fonce quel que soit le theme (comme les meters/LCD) -- leur
    // texte doit donc rester fixe clair lui aussi, plutot que suivre
    // textBright/textDim (qui s'assombrissent en theme clair pour rester
    // lisibles sur le CHASSIS clair, un contexte different).
    const juce::Colour chipTextBright { 0xffffffff };
    const juce::Colour chipTextDim    { 0xffbcc3cc };

    // (Palette "knob cream" retiree -- le knob est desormais un anneau
    // metal brosse, voir drawRotarySlider() dans le .cpp)

    // Chassis METAL BROSSE ARGENTE, gris moyen (preview "palette B"
    // validee par l'utilisateur) -- degrade de fond utilise par
    // SevenpackLookAndFeel::renderMetalPanel() et la plaque par-bande de
    // BandMetersRack.
    inline juce::Colour lightPanelGradTop    { 0xff9c9d9f };
    inline juce::Colour lightPanelGradMid    { 0xff868789 };
    inline juce::Colour lightPanelGradBottom { 0xff6a6c6e };

    inline void applyDarkTheme() noexcept
    {
        bg = juce::Colour (0xff121417);
        panel = juce::Colour (0xff1a1d21);
        panelLight = juce::Colour (0xff21252b);
        gridLine = juce::Colour (0xff0a0b0d);
        textDim = juce::Colour (0xffbcc3cc);
        textBright = juce::Colour (0xffffffff);
    }

    inline void applyLightTheme() noexcept
    {
        bg = juce::Colour (0xff9c9d9f);
        panel = juce::Colour (0xff86878a);
        panelLight = juce::Colour (0xffaaacae);
        gridLine = juce::Colour (0xff3a3c3e);
        textDim = juce::Colour (0xff2c2d2e);
        textBright = juce::Colour (0xff1c1d1e);
    }

    // Theme retro "MS-DOS / CGA" (3e theme, demande explicite : "vieille
    // machine qui tourne en MSDOS. avec un design vieille OS", palette
    // validee sur preview Artifact -- "CGA 16 couleurs"). Fond quasi noir
    // plat (pas de degrade brosse), cyan vif pour les traits/labels
    // secondaires, blanc pour le texte principal -- les diodes/LEDs
    // (SevenpackColours::ledXxx, jamais rethemees) restent EN L'ETAT et
    // fournissent deja la palette multicolore CGA (vert/jaune/orange/
    // rouge, cyan/bleu/violet) sans rien changer ici. Les primitives de
    // rendu "chrome" (renderMetalPanel, drawRotarySlider, etc., voir le
    // .cpp) ont chacune leur PROPRE branche plate des qu'active -- ce
    // bloc ne fait que fournir les teintes de base.
    inline void applyDosTheme() noexcept
    {
        bg = juce::Colour (0xff000000);
        panel = juce::Colour (0xff000000);
        panelLight = juce::Colour (0xff0a0a0a);
        gridLine = juce::Colour (0xff55ffff);
        textDim = juce::Colour (0xff55ffff);
        textBright = juce::Colour (0xffffffff);
    }
}

/**
    SevenpackLookAndFeel
    ----------------------
    Habillage "unite hardware rack" pour Suppressor (clone d'apparence d'un
    compresseur feedback classique de type 1176/Distressor) :
    - knobs ivoire/creme avec echelle imprimee 0-10 et grip crenele (au lieu
      de l'ancien capot blanc + anneau de LEDs de Nuke Mk1) ;
    - reste de la boite a outils graphique (panneaux verre/brosses, vis de
      coin, degrade des LEDs, dot glossy) conserve tel quel pour les
      composants reutilises (GRMeter, ToggleSwitch, AboutOverlay).
*/
class SevenpackLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SevenpackLookAndFeel();

    // Etat POWER centralise : chaque composant (knobs, ToggleSwitch,
    // RatioSelector, DetectorFilterGraph...) l'interroge dans son paint()
    // et se rend lui-meme "eteint" (comme debranche), au lieu d'un grand
    // rectangle noir plaque par-dessus tout. Mis a jour par l'editeur.
    void setPowered (bool p) noexcept
    {
        // Retour utilisateur : "quand on appuie sur off du header que les
        // parametre qui s'eteignet clignotte comme un neon a l'extinction
        // seulement" -- le flicker ne se declenche QUE sur la transition
        // ON->OFF (jamais au rallumage, voir getPowerTransitionMix() qui
        // renvoie 1.0 direct des que powered redevient true).
        if (powered && ! p)
            powerOffFlickerStartMs = juce::Time::getMillisecondCounter();
        powered = p;
    }
    bool isPowered() const noexcept { return powered; }

    // Melange 1.0 (pleine luminosite) -> 0.0 (etat eteint au repos) pendant
    // la bascule ON->OFF -- 1.0 tant que powered==true (aucun effet au
    // rallumage), puis un bref tremblement irregulier ("vieux neon", voir
    // doc de setPowered()) suivi d'un fondu doux vers 0.0 une fois
    // powered==false. Chaque appelant combine ceci avec SA PROPRE valeur
    // de repos "eteint" : finalBrightness = restValue + (1-restValue) *
    // getPowerTransitionMix() -- restValue = 0.0 pour un meter qui
    // s'eteint completement, 0.12 pour l'aiguille NeedleVU qui reste
    // visible en sourdine, etc.
    //
    // Deterministe (cosinus amorti, PAS de generateur aleatoire) : tous les
    // composants qui lisent cette fonction au meme instant voient
    // exactement la meme valeur, donc restent synchronises entre eux malgre
    // leurs Timer independants. jitterAt(0) == 1.0 : raccord visuel continu
    // avec la pleine luminosite juste avant la coupure (pas de saut brusque
    // a mi-luminosite des l'instant du clic).
    float getPowerTransitionMix() const noexcept
    {
        if (powered)
            return 1.0f;

        constexpr double jitterMs = 200.0, fadeMs = 220.0;
        const double elapsed = (double) (juce::Time::getMillisecondCounter() - powerOffFlickerStartMs);
        if (elapsed >= jitterMs + fadeMs)
            return 0.0f;

        const auto jitterAt = [jitterMs] (double t)
        {
            const double envelope = juce::jlimit (0.0, 1.0, 1.0 - (t / jitterMs) * 0.5);
            return juce::jlimit (0.0, 1.0, 0.5 + 0.5 * std::cos (t * 0.085) * envelope);
        };

        if (elapsed < jitterMs)
            return (float) jitterAt (elapsed);

        // Fondu : part de la derniere valeur de tremblement vers 0, lineaire.
        const double fadeProgress = (elapsed - jitterMs) / fadeMs;
        return (float) (jitterAt (jitterMs) * (1.0 - fadeProgress));
    }

    // Theme SOMBRE (par defaut)/CLAIR -- retour utilisateur explicite : "je
    // souhaite faire un design plus clair qu'on peut changer dans
    // l'engrenage". Met a jour les teintes MUTABLES de SevenpackColours
    // (bg/panel/panelLight/gridLine/textDim/textBright -- voir applyDarkTheme()/
    // applyLightTheme()) et repousse les juce::Colour IDs fixes par le
    // constructeur (Slider/Label) vers l'instance courante -- l'appelant
    // (SevenpackEditorContent) doit ensuite invalider son cache de fond et
    // repaint() pour que le nouveau theme s'affiche (voir
    // rebuildBackgroundCache()).
    // static (pas besoin d'instance) : n'agit QUE sur l'etat GLOBAL/STATIQUE
    // (SevenpackColours::bg/panel/.../textBright/textDim + lightThemeActive)
    // -- separee de setLightTheme() ci-dessous pour pouvoir etre appelee
    // AVANT qu'aucune instance de SevenpackLookAndFeel (ni aucun autre
    // composant) n'existe. Necessaire car de nombreux composants
    // (KnobControl, FaderControl...) sont des MEMBRES DIRECTS de
    // SevenpackEditorContent/BandMetersRack et capturent SevenpackColours
    // via setColour() DANS LEUR PROPRE CONSTRUCTEUR -- execute pendant la
    // phase d'initialisation des membres, donc potentiellement AVANT que le
    // corps d'AUCUN constructeur n'ait tourne. Voir le membre sentinelle
    // juste avant `content` dans PluginEditor.h (retour utilisateur : "att,
    // rel, range, wet, mono... sont noires" en theme clair -- ces composants
    // capturaient encore la valeur SOMBRE par defaut, le theme etant applique
    // trop tard).
    static void applyGlobalTheme (bool shouldBeLight) noexcept
    {
        applyGlobalTheme (shouldBeLight ? 1 : 0);
    }

    // Point d'entree CANONIQUE (3 themes) : 0=Sombre, 1=Clair, 2=DOS retro
    // (voir SevenpackColours::applyDosTheme()). Meme raisonnement "static,
    // pas besoin d'instance" que ci-dessus.
    static void applyGlobalTheme (int themeIndex) noexcept
    {
        themeIndex = juce::jlimit (0, 2, themeIndex);
        lightThemeActive = (themeIndex == 1);
        dosThemeActive   = (themeIndex == 2);

        if (dosThemeActive)        SevenpackColours::applyDosTheme();
        else if (lightThemeActive) SevenpackColours::applyLightTheme();
        else                       SevenpackColours::applyDarkTheme();
    }

    // Theme SOMBRE (par defaut)/CLAIR -- retour utilisateur explicite : "je
    // souhaite faire un design plus clair qu'on peut changer dans
    // l'engrenage". Version INSTANCE : fait tout ce que fait
    // applyGlobalTheme() ci-dessus, PLUS repousse les juce::Colour IDs fixes
    // par le constructeur (Slider/Label) vers l'instance courante --
    // l'appelant (SevenpackEditorContent) doit ensuite invalider son cache
    // de fond et repaint() pour que le nouveau theme s'affiche (voir
    // rebuildBackgroundCache()).
    void setLightTheme (bool shouldBeLight)
    {
        setUiTheme (shouldBeLight ? 1 : 0);
    }

    // Version instance du point d'entree canonique (voir applyGlobalTheme(int)
    // ci-dessus) -- 0=Sombre, 1=Clair, 2=DOS retro.
    void setUiTheme (int themeIndex)
    {
        applyGlobalTheme (themeIndex);
        refreshFixedColours();
    }

    static bool isLightThemeStatic() noexcept { return lightThemeActive; }
    bool isLightTheme() const noexcept { return lightThemeActive; }

    // Theme DOS retro actif -- voir applyGlobalTheme(int)/SevenpackColours::
    // applyDosTheme(). Verifie AVANT isLightThemeStatic() par les
    // primitives de rendu (voir .cpp) : DOS a toujours priorite, jamais
    // combine avec le rendu clair/sombre.
    static bool isDosThemeStatic() noexcept { return dosThemeActive; }

    static int getUiThemeIndex() noexcept
    {
        return dosThemeActive ? 2 : (lightThemeActive ? 1 : 0);
    }

    // Bleu de TEXTE (titres de section, liens -- AboutOverlay/OptionsOverlay)
    // -- contrairement aux LEDs/diodes fixes de SevenpackColours (jamais
    // rethemees, un afficheur reste sombre quel que soit le chassis), un
    // libelle ecrit DIRECTEMENT sur le panneau doit rester lisible une fois
    // ce panneau devenu gris clair (retour utilisateur : "le bleu cyan sur
    // gris c'est pas visible" en theme clair) -- variante nettement plus
    // foncee dans ce cas, identique a SevenpackColours::ledBlue en sombre.
    static juce::Colour textAccentBlue() noexcept
    {
        if (isDosThemeStatic())   return juce::Colour (0xffff55ff); // magenta CGA, pop sur fond noir
        return isLightThemeStatic() ? juce::Colour (0xff0d6a9e) : SevenpackColours::ledBlue;
    }

    // Knob "anneau metal" : chiffres 0-10 imprimes sur l'anneau brosse
    // qui tourne AVEC le knob (espacement regulier + subdivisions par
    // demi-unite + repere plus grand au 5), double bezel noir en creux,
    // capuchon brosse central avec reflet glossy mobile, et un repere
    // fixe imprime sur le panneau juste au-dessus pour lire la valeur.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    // Teinte OPTIONNELLE et tres legere de la bague noire du knob (voir
    // drawRotarySlider() dans le .cpp) -- utilisee UNIQUEMENT sur les
    // knobs Xover Low/High pour marquer visuellement qu'ils sont "entre 2
    // bandes". PAS un degrade spatial fixe : la teinte resultante est un
    // MELANGE de colourAtMin/colourAtMax dont le dosage suit la POSITION
    // reelle du knob dans sa plage (sliderPos, deja fourni a chaque appel
    // de drawRotarySlider -- pas besoin d'ecouter les changements de
    // valeur) -- proche du minimum = colourAtMin domine, proche du maximum
    // = colourAtMax domine. Design/taille des knobs par bande inchanges
    // (propriete absente = comportement d'origine).
    static void setKnobAccentGradient (juce::Slider& slider, juce::Colour colourAtMin, juce::Colour colourAtMax)
    {
        slider.getProperties().set ("knobAccentAtMin", colourAtMin.toString());
        slider.getProperties().set ("knobAccentAtMax", colourAtMax.toString());
    }

    // Retire un accent pose par setKnobAccentGradient() -- retour au
    // degrade gris neutre d'origine. Utilise quand une bande repasse en
    // Comp (demande explicite : "quand on passe en expand ... il faut
    // changer la couleur des knob en violet degrade" -- implique l'inverse
    // au retour sur Comp, sinon le violet resterait colle sur le meme
    // Slider physique, seulement reattache a un autre parametre).
    static void clearKnobAccentGradient (juce::Slider& slider)
    {
        slider.getProperties().remove ("knobAccentAtMin");
        slider.getProperties().remove ("knobAccentAtMax");
    }

    // Texte lisible en toute circonstance, reutilisable par tout
    // g.drawFittedText() direct (pas seulement les juce::Label, voir
    // drawLabel() plus bas qui s'appuie dessus). Le font DOIT deja etre
    // pose sur `g` par l'appelant (meme convention que g.drawFittedText
    // direct).
    //
    // Historique : une 1ere version ajoutait un liseret clair decale d'1px
    // en dessous ("effet grave") en theme clair -- retour utilisateur
    // explicite ENSUITE : "dry wet, mono, att rel out. rien n'est lisible"
    // -- a cette taille de police (10-11px), le double-trace (fond clair +
    // texte fonce a 1px) donnait un flou/dedoublement illisible plutot
    // qu'un relief. Retire : en theme clair on force juste l'opacite
    // PLEINE (les appelants passent souvent une couleur avec alpha reduit,
    // pense pour du texte clair sur fond sombre -- ce meme alpha reduit
    // un texte FONCE sur fond clair bien plus severement visuellement) --
    // contraste net, sans artefact. Le theme sombre garde le comportement
    // (et l'alpha) d'origine, inchange.
    static void drawEngravedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds,
                                   juce::Colour colour, juce::Justification justification = juce::Justification::centred,
                                   int maximumNumberOfLines = 1)
    {
        if (isLightThemeStatic() || isDosThemeStatic())
            colour = colour.withAlpha (1.0f);
        g.setColour (colour);
        g.drawFittedText (text, bounds, justification, maximumNumberOfLines);
    }

    juce::Font getLabelFont (juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    static void renderAnthraciteBackground (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Plaque metal brossee unifiee (remplace le fond anthracite "labo" --
    // un seul plan, pas de plaques separees par zone).
    static void renderMetalPanel (juce::Graphics& g, juce::Rectangle<float> bounds);

    // Fine ligne verticale claire pour separer 2 zones fonctionnelles --
    // remplace les contours de groupe/racetrack : juste des traits, pas
    // de boites, comme sur l'image de reference.
    static void renderDividerLine (juce::Graphics& g, float x, float top, float bottom);

    // Variante "design" (halo lumineux discret) de renderDividerLine --
    // retour utilisateur explicite : "il faut separer proprement le
    // compresseur et l'expandeur avec une ligne blanche design" (preview
    // "style A" valide) -- reservee aux NOUVELLES separations (Comp/Expand
    // dans BandPanel, nouveaux faders dans BandMetersRack), la ligne plate
    // existante ailleurs reste inchangee.
    static void renderGlowDividerLine (juce::Graphics& g, float x, float top, float bottom);

    static void renderGlassPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

    // Plaque plastique blanc brillant (effet 3D) -- utilisee pour la
    // plaque THD Valve, en rupture avec le reste du chassis metal sombre.
    static void renderGlossyWhitePanel (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

    // Fin contour arrondi pour delimiter un groupe de fonctions (GAIN
    // REDUCTION, RATIO, DETECTOR/AUDIO...), sans plaque/bezel -- juste un
    // trait clair, comme sur l'image de reference. labelText optionnel,
    // affiche centre sur le bord superieur (style "racetrack").
    static void renderGroupOutline (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                                     const juce::String& labelText = {});
    static void renderGlossySheen (juce::Graphics& g, juce::Rectangle<float> bounds);
    static void renderBrushedMetal (juce::Graphics& g, juce::Rectangle<float> bounds);

    static void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius);
    static void renderCornerScrews (juce::Graphics& g, juce::Rectangle<float> bounds, float inset, float screwRadius);

    // Anneau "socle encastre" derriere un knob -- effet extrusion (le knob
    // parait visse dans un decroche du chassis plutot que pose a plat).
    static void renderKnobSocket (juce::Graphics& g, juce::Point<float> centre, float radius);

    static juce::Colour ledGradientColour (float t);

    // Degrade de la rangee Expand du GRMeter (demande explicite : "un
    // systeme de couleur pour les diodes de l'expander mais pas les memes
    // que celle du gain reduction") -- bleu -> cyan -> violet, t = 0..1.
    static juce::Colour ledGradientColourExpand (float t);

    // Degrade des diodes du RatioSelector : cyan (ratio le plus doux,
    // 2:1) -> vert -> jaune -> orange -> rouge (20:1), t = 0..1.
    static juce::Colour ratioLedColour (float t);
    static float meterDisplayRatio (float db, float minDb, float maxDb);
    static void drawGlossyDot (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, bool lit);

    // Bouton "neumorphic dark mono" -- retour utilisateur (RackPushButton :
    // SOLO/SEL) : "j'aime bien dark mono", puis reutilise tel quel pour
    // AiLearnButton ("il faut le meme type de bouton que solo et sel").
    // Meme teinte que la plaque environnante, aucune bordure : souleve
    // (halo clair haut-gauche/sombre bas-droite) au repos, creuse (2
    // degrades radiaux opposes simulant une ombre interne) quand lit ou
    // pendant un appui (isDown). Factorise ici plutot que duplique dans
    // les 2 boutons -- meme rendu, seule la source de "lit" differe
    // (toggle/selection pour SOLO/SEL, clignotement pendant l'analyse pour
    // Learn).
    //
    // Base TOUJOURS neutre, jamais teintee par bande (retour utilisateur,
    // avec captures comparant plusieurs bandes cote a cote : "je veux que
    // sur chaque bande, chaque bouton dans le header soit identique a
    // l'enfoncement") -- plusieurs tentatives passees de teinter la base
    // avec la couleur DE LA BANDE (voir historique git) donnaient un
    // rendu different d'une bande a l'autre selon la teinte de l'accent
    // -- retire au profit d'un rendu strictement identique partout.
    //
    // haloMargin : retour utilisateur avec reference visuelle (capture
    // CSS "C -- extreme") -- le halo doit deborder LARGEMENT hors du
    // bouton (pas un simple degrade interne). Ca ne peut exister que si
    // le relief a un vrai espace REEL pour deborder -- haloMargin (voir
    // les appelants : `bounds` doit deja inclure cette marge en plus de
    // la taille visuelle voulue, via une expansion faite au niveau de la
    // mise en page, PAS ici) delimite jusqu'ou le halo peut deborder
    // SANS jamais sortir des bounds du composant -- donc plus besoin de
    // setPaintingIsUnclipped() (source du bug de coherence rencontre
    // avec l'ancienne technique, qui laissait deborder hors du
    // composant lui-meme).
    static void paintNeumorphicButton (juce::Graphics& g, juce::Rectangle<float> bounds, bool lit, bool isDown,
                                        juce::Colour accent, const juce::String& text, float textAlphaMultiplier = 1.0f,
                                        float haloMargin = 2.0f);

    // Reconstruit la signature "Mikshake" (encre `ink`) a partir du PNG
    // maitre haute-resolution embarque (BinaryData::signature_mikshake_master_png,
    // 1536x1024, deja anti-aliase nativement), recadree sur la bbox utile de
    // l'encre puis reduite via un pipeline de qualite (alpha = couverture
    // d'encre reelle + resampler JUCE area-averaging par paliers x2) jusqu'a
    // une largeur d'affichage `targetW` (hauteur deduite du ratio). Usage
    // partage entre le chrome principal (PluginEditor.cpp) et l'ecran About
    // (AboutOverlay.h) -- retour utilisateur : l'ancien pipeline maison
    // "min-pooling + seuil binaire" produisait une "soupe de pixels"
    // illisible a ces tailles ; voir commentaire d'appel pour le detail.
    static juce::Image buildSignatureImage (juce::Colour ink, int targetW);

private:
    void refreshFixedColours();

    bool powered = true;
    juce::uint32 powerOffFlickerStartMs = 0;
    // static : le theme est un choix GLOBAL du plugin (voir OptionsOverlay),
    // pas par instance -- une seule instance de SevenpackLookAndFeel existe
    // de toute facon (SevenpackAudioProcessorEditor::lookAndFeel), mais
    // isLightThemeStatic() doit rester accessible sans instance (utilise
    // par des methodes static comme renderMetalPanel()).
    static inline bool lightThemeActive = false;
    // Theme DOS retro -- mutuellement exclusif avec lightThemeActive (voir
    // applyGlobalTheme(int), seul point d'entree qui touche les 2 flags).
    static inline bool dosThemeActive = false;
};
