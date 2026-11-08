#pragma once
#include <JuceHeader.h>
#include <functional>

class SevenpackAudioProcessor;

/**
    StandaloneAudioController
    -------------------------
    Gestion de la carte son et du loopback du Standalone (Options > Audio,
    demande explicite : "Il faut une gestion des cartes son, in out et
    loopback sur la version standalone" ; apercu
    claude.ai/artifact/VVMvoxxuZKvomUHspPm9F6). Interface vue par la page
    Options ; l'implementation (StandaloneAudio.cpp) est creee par
    l'application Standalone et confiee au processeur
    (SevenpackAudioProcessor::standaloneAudio). Nulle dans le VST3 : la page
    Audio n'apparait pas.

    Tous les appels : thread de messages.
*/
class StandaloneAudioController
{
public:
    virtual ~StandaloneAudioController() = default;

    // --- Source : 0 = entree de la carte, 1 = lecture de l'ordinateur (loopback) ---
    virtual bool supportsLoopback() const = 0;
    virtual int  getSource() const = 0;
    virtual void setSource (int source) = 0;
    // [0] = sortie par defaut de Windows, puis les peripheriques de lecture.
    virtual juce::StringArray getCaptureNames() = 0;
    virtual int  getCaptureIndex() = 0;
    virtual void setCaptureIndex (int index) = 0;
    // Texte d'etat de la capture ; warning = sortie coupee (risque de larsen).
    virtual juce::String getCaptureStatus (bool& warning) = 0;
    // Risque de larsen (sortie coupee) : texte a afficher, vide sinon.
    //  - loopback : la sortie joue sur le peripherique capture ;
    //  - carte : l'entree est le "Loopback" de la carte qui sort le son.
    virtual juce::String getFeedbackWarning() = 0;

    // --- Carte son ---
    virtual void rescan() = 0; // relit la liste des peripheriques (ouverture de la page)
    virtual juce::StringArray getDriverNames() = 0;
    virtual int  getDriverIndex() = 0;
    virtual void setDriverIndex (int index) = 0;
    // false (ASIO) : un seul peripherique pour l'entree et la sortie.
    virtual bool hasSeparateInputsAndOutputs() = 0;
    virtual juce::StringArray getDeviceNames (bool input) = 0;
    virtual int  getDeviceIndex (bool input) = 0;
    virtual void setDeviceIndex (bool input, int index) = 0;
    virtual juce::StringArray getInputPairNames() = 0;
    virtual int  getInputPairIndex() = 0;
    virtual void setInputPairIndex (int index) = 0;
    virtual bool hasControlPanel() = 0;
    virtual void showControlPanel() = 0;
    virtual juce::Array<double> getSampleRates() = 0;
    virtual double getSampleRate() = 0;
    virtual void setSampleRate (double rate) = 0;
    virtual juce::Array<int> getBufferSizes() = 0;
    virtual int  getBufferSize() = 0;
    virtual void setBufferSize (int samples) = 0;
    virtual double getRoundTripLatencyMs() = 0;
    virtual juce::String getDeviceError() = 0; // vide si tout va bien

    // --- Ecoute ---
    virtual bool getMuteInput() = 0;
    virtual void setMuteInput (bool shouldMute) = 0;
    virtual void playTestTone() = 0;
    // Crete de l'entree (apres choix de la source) depuis le dernier appel, 0..1.
    virtual void getInputLevels (float& left, float& right) = 0;

    // Fin de vie : decroche tout AVANT la destruction du processeur et de la carte.
    virtual void detach() = 0;

    // Appele quand la configuration change (page Options a rafraichir).
    std::function<void()> onChanged;
};

// Cree par l'application Standalone. muteInput = la valeur "Mute input" du
// StandalonePluginHolder ; settings = reglages de l'appli (source, capture).
std::unique_ptr<StandaloneAudioController> createStandaloneAudioController (juce::AudioDeviceManager& deviceManager,
                                                                            juce::Value& muteInput,
                                                                            juce::PropertySet* settings,
                                                                            SevenpackAudioProcessor& processor);
