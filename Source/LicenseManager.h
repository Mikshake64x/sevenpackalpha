#pragma once

#include <JuceHeader.h>

// Talks to the Cloudflare Worker license server (see D:/vst/LicenseServer).
// Activation and the weekly re-validation both run off the message thread
// via juce::Thread::launch, so this never blocks the audio thread or the UI.
class LicenseManager : private juce::Timer
{
public:
    enum class Status { unlicensed, activating, licensed, invalid, offlineGrace, trial, trialExpired };

    explicit LicenseManager (const juce::String& productId)
        : product (productId)
    {
        machineId = juce::SystemStats::getUniqueDeviceID();
        loadStoredLicense();

        if (storedKey.isNotEmpty())
            beginActivation (storedKey);
        else if (trialStart != juce::Time()) // demo was already started on a previous launch
            recomputeTrialStatus();
        // else: never activated, never started the demo — stays Status::unlicensed,
        // the activation window shows both "Activate" and "Start 14-Day Demo".

        startTimer (checkIntervalMs);
    }

    ~LicenseManager() override { stopTimer(); }

    // Called from the UI when the user types a key into the activation dialog.
    // firstName/lastName/email are optional — pass empty strings to skip them.
    void activate (const juce::String& licenseKey, const juce::String& firstName = {},
                   const juce::String& lastName = {}, const juce::String& email = {})
    {
        beginActivation (licenseKey.trim(), firstName.trim(), lastName.trim(), email.trim());
    }

    // Called from the UI when the user clicks "Start 14-Day Demo". No-op if the
    // demo was already started before (trialStart is set once and never reset).
    void startDemo()
    {
        if (trialStart == juce::Time())
        {
            trialStart = juce::Time::getCurrentTime();
            saveStoredLicense();
        }

        recomputeTrialStatus();
    }

    // The activated license key, empty if not licensed. Used by the About
    // screen footer to show either this or the demo days remaining.
    juce::String getLicenseKey() const { return storedKey; }

    // Whether "Start 14-Day Demo" should still be offered — independent of
    // getStatus(), which can be Status::invalid after a failed activation
    // attempt even though the demo itself was never started.
    bool canStartDemo() const { return trialStart == juce::Time(); }

    Status getStatus() const { return status.load(); }
    juce::String getLastError() const { return lastError; }

    // Convenience for the processor: true if the plugin should run unrestricted.
    bool isUsable() const
    {
        auto s = status.load();
        return s == Status::licensed || s == Status::offlineGrace || s == Status::trial;
    }

    // Days left in the trial, only meaningful while getStatus() == Status::trial.
    int getTrialDaysLeft() const
    {
        auto elapsedDays = (juce::Time::getCurrentTime() - trialStart).inDays();
        return juce::jmax (0, trialDays - (int) elapsedDays);
    }

private:
    static constexpr int checkIntervalMs = 24 * 60 * 60 * 1000; // check once a day, server enforces weekly grace
    static constexpr int offlineGraceDays = 14;
    static constexpr int trialDays = 14;

    juce::String product;
    juce::String machineId;
    juce::String storedKey;
    std::atomic<Status> status { Status::unlicensed };
    juce::String lastError;
    juce::Time lastSuccessfulCheck;
    juce::Time trialStart;

    static juce::File getLicenseFile()
    {
        return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
            .getChildFile ("YourCompany").getChildFile ("SevenpackAlpha").getChildFile ("license.json");
    }

    void loadStoredLicense()
    {
        auto file = getLicenseFile();
        if (! file.existsAsFile())
            return;

        auto json = juce::JSON::parse (file);
        storedKey = json.getProperty ("license_key", "").toString();
        auto lastCheckMs = static_cast<juce::int64> (json.getProperty ("last_check_ms", 0));
        if (lastCheckMs > 0)
            lastSuccessfulCheck = juce::Time (lastCheckMs);

        auto trialStartMs = static_cast<juce::int64> (json.getProperty ("trial_start_ms", 0));
        if (trialStartMs > 0)
            trialStart = juce::Time (trialStartMs);
    }

    // Assumes trialStart is already set (see startDemo()) — just recomputes
    // trial/trialExpired from it and pings the server for /admin visibility.
    void recomputeTrialStatus()
    {
        status = (getTrialDaysLeft() > 0) ? Status::trial : Status::trialExpired;

        // Also lets /admin see trial installs — and if an admin revoked this
        // one, the server says so here and we block immediately rather than
        // waiting for the local 14-day countdown to run out.
        juce::Thread::launch ([this]
        {
            auto result = postJson ("/v1/trial-ping",
                { { "machine_id", machineId }, { "product_id", product },
                  { "machine_name", juce::SystemStats::getComputerName() },
                  { "plugin_version", ProjectInfo::versionString } });

            // Only an explicit "revoked" from the server ends the trial early —
            // any other server hiccup (500, unmapped route, etc.) must never be
            // treated as a revocation, or a server-side bug silently locks out demos.
            if (result.error == "revoked")
            {
                juce::MessageManager::callAsync ([this]
                {
                    status = Status::trialExpired;
                });
            }
        });
    }

    void saveStoredLicense()
    {
        auto file = getLicenseFile();
        file.getParentDirectory().createDirectory();

        auto obj = new juce::DynamicObject();
        obj->setProperty ("license_key", storedKey);
        obj->setProperty ("last_check_ms", lastSuccessfulCheck.toMilliseconds());
        obj->setProperty ("trial_start_ms", trialStart.toMilliseconds());
        file.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    void beginActivation (const juce::String& key, const juce::String& firstName = {},
                          const juce::String& lastName = {}, const juce::String& email = {})
    {
        if (key.isEmpty())
            return;

        storedKey = key;
        status = Status::activating;

        juce::Thread::launch ([this, key, firstName, lastName, email]
        {
            auto result = postJson ("/v1/activate",
                { { "license_key", key }, { "machine_id", machineId },
                  { "machine_name", juce::SystemStats::getComputerName() },
                  { "product_id", product },
                  { "plugin_version", ProjectInfo::versionString },
                  { "first_name", firstName }, { "last_name", lastName }, { "email", email } });

            juce::MessageManager::callAsync ([this, key, result]
            {
                if (result.ok)
                {
                    status = Status::licensed;
                    lastSuccessfulCheck = juce::Time::getCurrentTime();
                    saveStoredLicense();
                }
                else
                {
                    status = Status::invalid;
                    lastError = result.error;
                }
            });
        });
    }

    void timerCallback() override
    {
        if (storedKey.isEmpty())
        {
            if (trialStart != juce::Time())
                recomputeTrialStatus(); // catches trial expiry during a long-running DAW session
            return;
        }

        if (status.load() == Status::activating)
            return;

        juce::Thread::launch ([this]
        {
            auto result = postJson ("/v1/validate",
                { { "license_key", storedKey }, { "machine_id", machineId },
                  { "plugin_version", ProjectInfo::versionString } });

            juce::MessageManager::callAsync ([this, result]
            {
                if (result.ok)
                {
                    status = Status::licensed;
                    lastSuccessfulCheck = juce::Time::getCurrentTime();
                    saveStoredLicense();
                }
                else if (result.networkError)
                {
                    // Server unreachable: keep working for a grace period rather than
                    // punishing users who are offline (studio without internet, etc).
                    auto daysSinceCheck = (juce::Time::getCurrentTime() - lastSuccessfulCheck).inDays();
                    status = (daysSinceCheck <= offlineGraceDays) ? Status::offlineGrace : Status::invalid;
                }
                else
                {
                    // Server explicitly said no (revoked / machine removed).
                    status = Status::invalid;
                    lastError = result.error;
                }
            });
        });
    }

    struct ApiResult { bool ok = false; bool networkError = false; juce::String error; };

    ApiResult postJson (const juce::String& path, const std::map<juce::String, juce::String>& fields)
    {
        auto obj = new juce::DynamicObject();
        for (auto& f : fields)
            obj->setProperty (f.first, f.second);
        juce::String body = juce::JSON::toString (juce::var (obj), true);

        juce::URL url (juce::String (licenseServerBaseUrl) + path);
        url = url.withPOSTData (body);

        int statusCode = 0;
        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders ("Content-Type: application/json")
                .withConnectionTimeoutMs (8000)
                .withStatusCode (&statusCode));

        if (stream == nullptr)
            return { false, true, "network_error" };

        auto response = stream->readEntireStreamAsString();
        auto json = juce::JSON::parse (response);

        ApiResult result;
        result.ok = statusCode >= 200 && statusCode < 300
                    && static_cast<bool> (json.getProperty ("ok", false));
        result.error = json.getProperty ("error", "").toString();
        return result;
    }

    // Replace with your deployed Worker URL (see D:/vst/LicenseServer/README.md).
    static constexpr const char* licenseServerBaseUrl = "https://license.sidekey.fr";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
