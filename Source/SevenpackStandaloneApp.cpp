/*
    SevenpackStandaloneApp
    --------------------
    Application standalone PERSONNALISEE (activee par le define projet
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1, voir Sevenpack.jucer).

    Raison d'etre : le routage multi-sorties par bande (voir idBandRouteOut /
    mainOutPairIndex / bandOutPairIndex dans PluginProcessor). L'app
    standalone livree par JUCE :
      - n'ouvre la carte son qu'avec le bus de sortie PRINCIPAL (2 canaux) ;
      - plafonne les cases "sorties actives" de la fenetre Audio Settings a
        la largeur de ce bus principal.
    Ici on passe un channelConfiguration explicite { 2 in, 16 out } au
    StandalonePluginHolder (ouverture de l'interface avec 16 sorties), et on
    force en permanence TOUS les canaux de sortie de l'interface a l'etat
    actif -- l'utilisateur choisit ensuite, PAR BANDE, la paire de sortie
    physique dans l'overlay Options du plugin.

    On pousse aussi les NOMS des canaux de sortie de l'interface courante
    vers le processeur (a chaque (re)configuration du peripherique) pour que
    ces menus listent les vraies sorties de la carte.

    Le corps n'est compile QUE quand JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
    est defini. Le define est global au projet, donc ce fichier est aussi
    compile dans la SharedCode des autres formats (VST3/AU/Unity) : il n'y
    definit que la fonction libre juce_CreateApplication(), jamais
    referencee la-bas, donc code mort inoffensif. Seule la cible Standalone
    (qui compile le glue JUCE juce_audio_plugin_client_Standalone.cpp, lequel
    declare `extern juce_CreateApplication()` et fournit le point d'entree)
    la lie reellement.
*/

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include "JuceHeader.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"

namespace juce
{

class SevenpackStandaloneApp final : public JUCEApplication,
                                   private ChangeListener
{
public:
    SevenpackStandaloneApp()
    {
        PropertiesFile::Options options;
        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    void initialise (const String&) override
    {
        mainWindow.reset (createWindow());

        if (mainWindow != nullptr)
        {
            mainWindow->setVisible (true);
            mainWindow->getDeviceManager().addChangeListener (this);
            syncDeviceOutputs();
        }
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->getDeviceManager().removeChangeListener (this);

        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    StandaloneFilterWindow* createWindow()
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse; // aucun ecran disponible
            return nullptr;
        }

        // 2 canaux de mix + 2 par bande (7 bandes) = 16 sorties. Force
        // l'ouverture de la carte son avec 16 canaux (voir en-tete).
        // Construit champ par champ : PluginInOuts est un agregat { short,
        // short } et l'init par accolades imbriquees dans un Array<> ne
        // compile pas de facon portable ici.
        StandalonePluginHolder::PluginInOuts io;
        io.numIns  = (short) 2;
        io.numOuts = (short) (2 + 2 * 7);

        Array<StandalonePluginHolder::PluginInOuts> channelConfig;
        channelConfig.add (io);

        return new StandaloneFilterWindow (getApplicationName(),
                                           LookAndFeel::getDefaultLookAndFeel()
                                               .findColour (ResizableWindow::backgroundColourId),
                                           appProperties.getUserSettings(),
                                           false,      // ne pas prendre possession de settings
                                           String{},   // pas de peripherique par defaut impose
                                           nullptr,    // pas d'options audio pre-remplies
                                           channelConfig);
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        syncDeviceOutputs();
    }

    // (1) force TOUTES les sorties de l'interface a l'etat actif -- la
    //     fenetre Audio Settings de JUCE ne montre que 2 cases (largeur du
    //     bus principal), donc sans ca les sorties 3+ ne recevraient rien /
    //     seraient re-desactivees des que l'utilisateur ouvre le dialogue.
    // (2) pousse les noms des canaux de sortie vers le processeur pour les
    //     menus de l'overlay Options.
    void syncDeviceOutputs()
    {
        if (mainWindow == nullptr || reentrantGuard)
            return;

        auto& dm = mainWindow->getDeviceManager();
        auto* dev = dm.getCurrentAudioDevice();

        StringArray outNames;
        if (dev != nullptr)
        {
            outNames = dev->getOutputChannelNames();

            AudioDeviceManager::AudioDeviceSetup setup;
            dm.getAudioDeviceSetup (setup);

            BigInteger wanted;
            wanted.setRange (0, outNames.size(), true);

            if (setup.useDefaultOutputChannels || setup.outputChannels != wanted)
            {
                setup.useDefaultOutputChannels = false;
                setup.outputChannels = wanted;

                const ScopedValueSetter<bool> svs (reentrantGuard, true);
                dm.setAudioDeviceSetup (setup, true);
            }
        }

        if (auto* proc = dynamic_cast<SevenpackAudioProcessor*> (mainWindow->getAudioProcessor()))
            proc->setStandaloneOutputChannelNames (outNames);
    }

    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
    bool reentrantGuard = false;
};

} // namespace juce

// Fournie a la place du StandaloneFilterApp par defaut de JUCE (le glue
// standalone fait `extern juce::JUCEApplicationBase* juce_CreateApplication();`
// et appelle cette fonction depuis son point d'entree genere).
juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication()   { return new juce::SevenpackStandaloneApp(); }

#endif
