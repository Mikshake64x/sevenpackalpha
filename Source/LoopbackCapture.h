#pragma once
#include <JuceHeader.h>
#include <functional>
#include <atomic>

/**
    LoopbackCapture
    ---------------
    Standalone, Options > Audio > Source "Computer playback" (demande
    explicite : "Il faut une gestion des cartes son, in out et loopback sur
    la version standalone"). Capture WASAPI "loopback" de ce que Windows joue
    sur un peripherique de lecture : celui choisi (identifiant de point de
    terminaison) ou la sortie par defaut (identifiant vide, suit les
    changements de sortie par defaut). Repris de SystemLoopbackCapture de
    Merlin By Mikshake.

    Thread dedie : ouvre le peripherique en mode partage loopback, convertit
    en flottant stereo et livre des blocs via onAudio. Quand rien ne joue,
    Windows ne livre rien : on envoie alors du silence au rythme reel. Si le
    peripherique disparait, la capture se rouvre toute seule.

    Hors Windows : rien (isSupported() = false) -- sur Mac, passer par un
    peripherique virtuel (BlackHole...) choisi comme entree de la carte.
*/
class LoopbackCapture : private juce::Thread
{
public:
    LoopbackCapture() : juce::Thread ("Sevenpack loopback") {}
    ~LoopbackCapture() override { stop(); }

    static constexpr bool isSupported()
    {
       #if JUCE_WINDOWS
        return true;
       #else
        return false;
       #endif
    }

    // Peripheriques de lecture actifs (noms lisibles + identifiants) et nom de
    // la sortie par defaut. Thread de messages.
    static void listPlaybackDevices (juce::StringArray& names, juce::StringArray& ids, juce::String& defaultName);

    // Identifiant vide = sortie par defaut de Windows. Relance la capture si
    // elle tourne.
    void setDevice (const juce::String& endpointId);

    void start();
    void stop();
    bool isRunning() const                 { return isThreadRunning(); }
    bool isCapturing() const               { return capturing.load(); }
    juce::String getDeviceName() const     { const juce::ScopedLock sl (nameLock); return deviceName; }
    double getSampleRate() const           { return sampleRate.load(); }

    // Appeles depuis le thread de capture.
    std::function<void (double sampleRate)>                   onStart;
    std::function<void (const float* const* stereo, int n)>  onAudio;

    static constexpr int kBlock = 1024;

private:
    void run() override;
    bool captureSession(); // une session ; false = echec (nouvel essai plus tard)

    juce::CriticalSection nameLock;
    juce::String deviceName, wantedId;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<bool> capturing { false }, restartRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCapture)
};
