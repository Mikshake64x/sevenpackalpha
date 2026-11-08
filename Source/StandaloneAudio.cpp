#include "StandaloneAudio.h"
#include "PluginProcessor.h"
#include "LoopbackCapture.h"
#include "LoopbackFeed.h"

namespace
{
    int outputFirstChannel (const juce::String& name);

    // Meme materiel en sortie et en capture (risque de larsen) ?
    //  - meme nom de peripherique (Windows Audio / DirectSound) ;
    //  - pilote sans entree/sortie separees (ASIO) : le premier mot
    //    significatif du pilote ("SSL", "Focusrite", "Realtek"...) apparait
    //    dans le nom du peripherique capture.
    bool sameHardware (juce::String out, const juce::String& captured, bool separate)
    {
        out = out.trim();
        if (out.isEmpty() || captured.isEmpty())
            return false;
        if (out.equalsIgnoreCase (captured) || captured.containsIgnoreCase (out))
            return true;
        if (! separate)
        {
            static const char* ignored[] = { "ASIO", "USB", "Driver", "Audio", "Device", "Digital", "The" };
            for (auto tok : juce::StringArray::fromTokens (out, " ()-_/", ""))
            {
                tok = tok.trim();
                if (tok.length() < 3 || tok.containsOnly ("0123456789"))
                    continue;
                bool skip = false;
                for (auto* ig : ignored)
                    skip = skip || tok.equalsIgnoreCase (ig);
                if (! skip) // capture d'une sortie virtuelle (Output 3/4...) : pas la sortie principale 1-2
                    return captured.containsIgnoreCase (tok) && outputFirstChannel (captured) <= 1;
            }
        }
        return false;
    }

    // Partie entre les dernieres parentheses : le materiel sous Windows
    // ("Loopback (SSL 1 USB Audio Device)" -> "SSL 1 USB Audio Device").
    juce::String hardwareTag (const juce::String& name)
    {
        const int close = name.lastIndexOfChar (')');
        if (close <= 0)
            return {};
        int depth = 0;
        for (int i = close; i >= 0; --i)
        {
            if (name[i] == ')') ++depth;
            else if (name[i] == '(' && --depth == 0)
                return name.substring (i + 1, close).trim();
        }
        return {};
    }

    // Nombre qui suit le premier "Loopback" du nom (0 si aucun).
    int loopbackNumber (const juce::String& name)
    {
        const int at = name.indexOfIgnoreCase ("Loopback");
        if (at < 0)
            return 0;
        const auto rest = name.substring (at + 8).trimStart();
        return rest.isNotEmpty() && juce::CharacterFunctions::isDigit (rest[0]) ? rest.getIntValue() : 0;
    }

    // Premiere voie d'une sortie nommee "Output 3/4 ..." (0 si illisible).
    int outputFirstChannel (const juce::String& name)
    {
        const int at = name.indexOfIgnoreCase ("Output");
        if (at < 0)
            return 0;
        const auto rest = name.substring (at + 6).trimStart();
        return rest.isNotEmpty() && juce::CharacterFunctions::isDigit (rest[0]) ? rest.getIntValue() : 0;
    }

    juce::String khz (double rate)
    {
        const double k = rate / 1000.0;
        return juce::String (k, std::abs (k - std::round (k)) < 1.0e-6 ? 0 : 1) + " kHz";
    }
}

class StandaloneAudioService final : public StandaloneAudioController,
                                     private juce::ChangeListener,
                                     private juce::Timer
{
public:
    StandaloneAudioService (juce::AudioDeviceManager& d, juce::Value& muteIn, juce::PropertySet* s, SevenpackAudioProcessor& p)
        : dm (&d), proc (&p), settings (s)
    {
        mute.referTo (muteIn);
        if (settings != nullptr)
        {
            source = juce::jlimit (0, 1, settings->getIntValue ("sevenpackInputSource", 0));
            captureId = settings->getValue ("sevenpackCaptureId");
            // Premier lancement avec cette page : entree NON coupee (le bandeau
            // JUCE "Audio input is muted" a disparu ; "Mute input" est ici).
            if (! settings->containsKey ("sevenpackAudioInit"))
            {
                settings->setValue ("sevenpackAudioInit", true);
                mute.setValue (false);
            }
        }
        capture.onStart = [this] (double rate) { feed.prepareCapture (rate); };
        capture.onAudio = [this] (const float* const* stereo, int n) { feed.push (stereo, n); };
        proc->loopbackFeed.store (&feed);
        dm->addChangeListener (this);
        applySource();
        startTimerHz (1);
    }

    ~StandaloneAudioService() override
    {
        detach();
    }

    void detach() override
    {
        if (detached)
            return;
        detached = true;
        stopTimer();
        dm->removeChangeListener (this);
        proc->loopbackFeed.store (nullptr);
        capture.stop();
        onChanged = nullptr;
    }

    // --- Source ---
    bool supportsLoopback() const override { return LoopbackCapture::isSupported(); }
    int getSource() const override         { return supportsLoopback() ? source : 0; }

    void setSource (int s) override
    {
        source = juce::jlimit (0, 1, s);
        if (settings != nullptr)
            settings->setValue ("sevenpackInputSource", source);
        applySource();
        notify();
    }

    juce::StringArray getCaptureNames() override
    {
        refreshCaptureList();
        juce::StringArray names;
        names.add (defaultName.isNotEmpty() ? "Default output (" + defaultName + ")" : juce::String ("Default output"));
        names.addArray (capNames);
        return names;
    }

    int getCaptureIndex() override
    {
        if (captureId.isEmpty())
            return 0;
        const int i = capIds.indexOf (captureId);
        return i >= 0 ? i + 1 : 0;
    }

    void setCaptureIndex (int index) override
    {
        captureId = (index <= 0 || index > capIds.size()) ? juce::String() : capIds[index - 1];
        if (settings != nullptr)
            settings->setValue ("sevenpackCaptureId", captureId);
        capture.setDevice (captureId);
        checkFeedback();
        notify();
    }

    juce::String getFeedbackWarning() override
    {
        if (! feedback)
            return {};
        if (feed.isActive())
            return "Feedback risk: the output plays on the device being captured, so the processed sound would come back in. "
                   "The output is muted until you pick another capture or another output.";
        return "Feedback risk: the input is the card's loopback, which records the card's own output. "
               "The output is muted until you pick another input or another output.";
    }

    juce::String getCaptureStatus (bool& warning) override
    {
        warning = feedback && feed.isActive();
        if (! feed.isActive())
            return {};
        if (feedback)
            return "Output muted (feedback risk)";
        if (! capture.isCapturing())
            return "Waiting for the device";
        auto* dev = dm->getCurrentAudioDevice();
        return "Capturing  " + khz (capture.getSampleRate()) + juce::String::fromUTF8 (" \xe2\x86\x92 ")
               + (dev != nullptr ? khz (dev->getCurrentSampleRate()) : juce::String ("no card"));
    }

    // --- Carte son ---
    void rescan() override
    {
        if (auto* t = dm->getCurrentDeviceTypeObject())
            t->scanForDevices();
    }

    juce::StringArray getDriverNames() override
    {
        juce::StringArray names;
        for (auto* t : dm->getAvailableDeviceTypes())
            names.add (t->getTypeName());
        return names;
    }

    int getDriverIndex() override { return getDriverNames().indexOf (dm->getCurrentAudioDeviceType()); }

    void setDriverIndex (int index) override
    {
        const auto names = getDriverNames();
        if (! juce::isPositiveAndBelow (index, names.size()) || names[index] == dm->getCurrentAudioDeviceType())
            return;
        dm->setCurrentAudioDeviceType (names[index], true);
        rescan();
        lastError = dm->getCurrentAudioDevice() == nullptr ? juce::String ("The card could not be opened with this driver.") : juce::String();
        afterChange();
    }

    bool hasSeparateInputsAndOutputs() override
    {
        auto* t = dm->getCurrentDeviceTypeObject();
        return t == nullptr || t->hasSeparateInputsAndOutputs();
    }

    juce::StringArray getDeviceNames (bool input) override
    {
        if (auto* t = dm->getCurrentDeviceTypeObject())
            return t->getDeviceNames (input);
        return {};
    }

    int getDeviceIndex (bool input) override
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm->getAudioDeviceSetup (setup);
        return getDeviceNames (input).indexOf (input ? setup.inputDeviceName : setup.outputDeviceName);
    }

    void setDeviceIndex (bool input, int index) override
    {
        const auto names = getDeviceNames (input);
        if (! juce::isPositiveAndBelow (index, names.size()))
            return;
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm->getAudioDeviceSetup (setup);
        if (! hasSeparateInputsAndOutputs())
            setup.inputDeviceName = setup.outputDeviceName = names[index];
        else if (input)
            setup.inputDeviceName = names[index];
        else
            setup.outputDeviceName = names[index];
        setup.useDefaultInputChannels = true;
        apply (setup);
    }

    juce::StringArray getInputPairNames() override
    {
        juce::StringArray pairs;
        if (auto* dev = dm->getCurrentAudioDevice())
        {
            const auto names = dev->getInputChannelNames();
            for (int i = 0; i < names.size(); i += 2)
                pairs.add (i + 1 < names.size() ? names[i] + " + " + names[i + 1] : names[i]);
        }
        return pairs;
    }

    int getInputPairIndex() override
    {
        if (auto* dev = dm->getCurrentAudioDevice())
        {
            const int first = dev->getActiveInputChannels().findNextSetBit (0);
            return first >= 0 ? first / 2 : 0;
        }
        return 0;
    }

    void setInputPairIndex (int index) override
    {
        auto* dev = dm->getCurrentAudioDevice();
        if (dev == nullptr)
            return;
        const int n = dev->getInputChannelNames().size();
        if (index < 0 || index * 2 >= n)
            return;
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm->getAudioDeviceSetup (setup);
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (index * 2, juce::jmin (2, n - index * 2), true);
        apply (setup);
    }

    bool hasControlPanel() override
    {
        auto* dev = dm->getCurrentAudioDevice();
        return dev != nullptr && dev->hasControlPanel();
    }

    void showControlPanel() override
    {
        if (auto* dev = dm->getCurrentAudioDevice())
            if (dev->showControlPanel()) // reglages changes : on rouvre (comme le dialogue JUCE)
            {
                dm->closeAudioDevice();
                dm->restartLastAudioDevice();
                afterChange();
            }
    }

    juce::Array<double> getSampleRates() override
    {
        if (auto* dev = dm->getCurrentAudioDevice())
            return dev->getAvailableSampleRates();
        return {};
    }

    double getSampleRate() override
    {
        auto* dev = dm->getCurrentAudioDevice();
        return dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
    }

    void setSampleRate (double rate) override
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm->getAudioDeviceSetup (setup);
        setup.sampleRate = rate;
        apply (setup);
    }

    juce::Array<int> getBufferSizes() override
    {
        if (auto* dev = dm->getCurrentAudioDevice())
            return dev->getAvailableBufferSizes();
        return {};
    }

    int getBufferSize() override
    {
        auto* dev = dm->getCurrentAudioDevice();
        return dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;
    }

    void setBufferSize (int samples) override
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm->getAudioDeviceSetup (setup);
        setup.bufferSize = samples;
        apply (setup);
    }

    double getRoundTripLatencyMs() override
    {
        auto* dev = dm->getCurrentAudioDevice();
        if (dev == nullptr || dev->getCurrentSampleRate() <= 0.0)
            return 0.0;
        const double sr = dev->getCurrentSampleRate();
        const int bs = dev->getCurrentBufferSizeSamples();
        // Loopback : la file de rattrapage d'horloge (~2048 + 2 blocs) remplace l'entree de la carte.
        const int inSide = feed.isActive() ? 2048 + 2 * bs : dev->getInputLatencyInSamples() + bs;
        const int total = inSide + dev->getOutputLatencyInSamples() + bs + proc->getLatencySamples();
        return 1000.0 * (double) total / sr;
    }

    juce::String getDeviceError() override
    {
        if (dm->getCurrentAudioDevice() == nullptr)
            return lastError.isNotEmpty() ? lastError : juce::String ("No sound card is open.");
        return lastError;
    }

    // --- Ecoute ---
    bool getMuteInput() override            { return (bool) mute.getValue(); }
    void setMuteInput (bool m) override     { mute.setValue (m); notify(); }
    void playTestTone() override            { dm->playTestSound(); }

    void getInputLevels (float& l, float& r) override
    {
        l = proc->inputLevel[0].exchange (0.0f);
        r = proc->inputLevel[1].exchange (0.0f);
    }

private:
    void apply (juce::AudioDeviceManager::AudioDeviceSetup& setup)
    {
        lastError = dm->setAudioDeviceSetup (setup, true);
        afterChange();
    }

    void afterChange()
    {
        checkFeedback();
        notify();
    }

    void notify()
    {
        if (onChanged)
            onChanged();
    }

    void applySource()
    {
        const bool loop = getSource() == 1;
        if (loop)
        {
            refreshCaptureList(); // nom de la sortie par defaut (anti-larsen)
            capture.setDevice (captureId);
            capture.start();
        }
        else
        {
            capture.stop();
        }
        feed.setActive (loop);
        checkFeedback();
    }

    void refreshCaptureList()
    {
        LoopbackCapture::listPlaybackDevices (capNames, capIds, defaultName);
    }

    // Anti-larsen : la sortie joue-t-elle sur le peripherique capture ?
    void checkFeedback()
    {
        bool fb = false;
        if (feed.isActive())
        {
            juce::String captured;
            if (captureId.isEmpty())
                captured = defaultName;
            else if (const int i = capIds.indexOf (captureId); i >= 0)
                captured = capNames[i];
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            dm->getAudioDeviceSetup (setup);
            auto out = setup.outputDeviceName;
            if (out.containsIgnoreCase ("Primary Sound")) // DirectSound : la sortie par defaut de Windows
                out = defaultName;
            fb = sameHardware (out, captured, hasSeparateInputsAndOutputs());
        }
        else if (auto* dev = dm->getCurrentAudioDevice())
        {
            // Carte : entree "Loopback" (SSL...) de la carte qui joue la sortie.
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            dm->getAudioDeviceSetup (setup);
            const auto pairs = getInputPairNames();
            const int pi = getInputPairIndex();
            const bool loopIn = setup.inputDeviceName.containsIgnoreCase ("Loopback")
                                || (juce::isPositiveAndBelow (pi, pairs.size()) && pairs[pi].containsIgnoreCase ("Loopback"));
            if (loopIn)
            {
                // "Loopback N" enregistre la sortie N (Loopback 1 = Output 1/2,
                // Loopback 2 = Output 3/4 virtuelle... comme la SSL 1) : seul
                // le loopback de la sortie utilisee boucle. Numero illisible :
                // on suppose la boucle (prudence).
                const int n = loopbackNumber (juce::isPositiveAndBelow (pi, pairs.size()) && pairs[pi].containsIgnoreCase ("Loopback")
                                                  ? pairs[pi] : setup.inputDeviceName);
                if (! hasSeparateInputsAndOutputs())
                {
                    fb = n <= 1; // ASIO : sortie principale sur les voies 1-2
                }
                else
                {
                    const auto tagIn = hardwareTag (setup.inputDeviceName), tagOut = hardwareTag (setup.outputDeviceName);
                    const int outFirst = outputFirstChannel (setup.outputDeviceName);
                    fb = tagIn.isNotEmpty() && tagIn.equalsIgnoreCase (tagOut)
                         && (n == 0 || outFirst == 0 || 2 * n - 1 == outFirst);
                }
            }
            juce::ignoreUnused (dev);
        }
        feed.setOutputMuted (fb);
        if (fb != feedback)
        {
            feedback = fb;
            notify();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (detached)
            return;
        checkFeedback();
        notify();
    }

    void timerCallback() override
    {
        if (feed.isActive())
            refreshCaptureList(); // la sortie par defaut de Windows peut changer
        checkFeedback();
    }

    juce::AudioDeviceManager* dm;
    SevenpackAudioProcessor* proc;
    juce::PropertySet* settings;
    juce::Value mute;
    LoopbackCapture capture;
    LoopbackFeed feed;
    int source = 0;
    juce::String captureId, defaultName, lastError;
    juce::StringArray capNames, capIds;
    bool feedback = false, detached = false;
};

std::unique_ptr<StandaloneAudioController> createStandaloneAudioController (juce::AudioDeviceManager& deviceManager,
                                                                            juce::Value& muteInput,
                                                                            juce::PropertySet* settings,
                                                                            SevenpackAudioProcessor& processor)
{
    return std::make_unique<StandaloneAudioService> (deviceManager, muteInput, settings, processor);
}
