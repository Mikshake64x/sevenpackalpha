#pragma once

#include <JuceHeader.h>
#include "BuildLabel.h"

#include <deque>

// Talks to the Cloudflare Worker license server (see D:/vst/LicenseServer).
// Activation and the weekly re-validation both run off the message thread,
// so this never blocks the audio thread or the UI.
//
// Plantage a la fermeture sur Mac (retour de Sunny : "j'ai une erreur quand
// je ferme le logiciel") : les requetes partaient dans des threads detaches
// (juce::Thread::launch ([this] ...)) jamais arretes. Une requete encore en
// cours a la fermeture continuait sur un objet detruit, puis postait sa
// reponse (callAsync) alors que la boucle de messages n'existait plus -- sur
// Mac, ce message vise un delegue d'application deja libere. Desormais : UN
// thread de travail possede par ce gestionnaire, arrete (et la requete en
// cours annulee) dans le destructeur ; les reponses ne touchent l'objet que
// s'il existe encore (WeakReference).
class LicenseManager : private juce::Timer,
                       private juce::Thread
{
public:
    // betaEnded (v2.22.6) : cle beta dont la date de fin est passee (voir
    // expiresAt plus bas et D:/vst/LicenseServer/migration-010-license-expiry.sql).
    enum class Status { unlicensed, activating, licensed, invalid, offlineGrace, trial, trialExpired, betaEnded };

    explicit LicenseManager (const juce::String& productId)
        : juce::Thread ("License requests"), product (productId)
    {
        weakThis = this;
        machineId = juce::SystemStats::getUniqueDeviceID();
        loadStoredLicense();

        if (storedKey.isNotEmpty())
            beginActivation (storedKey, {}, {}, {}, true);
        else if (trialStart != juce::Time()) // demo was already started on a previous launch
            recomputeTrialStatus();
        // else: never activated, never started the demo — stays Status::unlicensed,
        // the activation window shows both "Activate" and "Start 14-Day Demo".

        startTimer (checkIntervalMs);
    }

    ~LicenseManager() override
    {
        stopTimer();
        signalThreadShouldExit();
        {
            const juce::ScopedLock sl (streamLock);
            if (currentStream != nullptr)
                currentStream->cancel(); // debloque une requete en cours
        }
        notify();
        stopThread (5000);
    }

    // Called from the UI when the user types a key into the activation dialog.
    // firstName/lastName/email are optional — pass empty strings to skip them.
    void activate (const juce::String& licenseKey, const juce::String& firstName = {},
                   const juce::String& lastName = {}, const juce::String& email = {})
    {
        beginActivation (licenseKey.trim(), firstName.trim(), lastName.trim(), email.trim());
    }

    // "Join the beta program" (ecran de licence, v2.23.2) : nom + e-mail ->
    // /v1/beta-join rend une cle beta pour CETTE machine, qui expire a la fin
    // du programme (20/12/2026 cote serveur). Ensuite elle vit comme une cle
    // tapee (controles, date de fin hors ligne). En cas d'echec, le statut ne
    // change pas (une demo en cours continue) : seul getBetaJoinError() le dit.
    void joinBeta (const juce::String& firstName, const juce::String& lastName, const juce::String& email)
    {
        if (betaJoining)
            return;
        betaJoining = true;
        betaJoinError = {};
        request ("/v1/beta-join",
                 { { "machine_id", machineId }, { "product_id", product },
                   { "machine_name", juce::SystemStats::getComputerName() },
                   { "plugin_version", sevenpackVersionLabel() },
                   { "customer_name", (firstName.trim() + " " + lastName.trim()).trim() },
                   { "email", email.trim() } },
                 [this] (const ApiResult& result)
                 {
                     betaJoining = false;
                     const auto key = result.json.getProperty ("license_key", "").toString();
                     if (result.ok && key.isNotEmpty())
                     {
                         if (key != storedKey)
                             setExpiry ({});
                         storedKey = key;
                         handleLicenseResult (result, false); // licensed + date de fin + sauvegarde
                     }
                     else
                     {
                         betaJoinError = result.error.isNotEmpty() ? result.error : juce::String ("unknown");
                     }
                 });
    }

    bool isJoiningBeta() const noexcept { return betaJoining; }
    juce::String getBetaJoinError() const { return betaJoinError; }
    void clearBetaJoinError() { betaJoinError = {}; }

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

    // Une cle beta s'arrete a sa date PILE, meme hors ligne et sans attendre le
    // controle quotidien : betaEnded est calcule ici a chaque lecture.
    Status getStatus() const
    {
        const auto s = status.load();
        if ((s == Status::licensed || s == Status::offlineGrace) && betaHasEnded())
            return Status::betaEnded;
        return s;
    }
    juce::String getLastError() const { return lastError; }

    // Convenience for the processor: true if the plugin should run unrestricted.
    bool isUsable() const
    {
        auto s = getStatus();
        return s == Status::licensed || s == Status::offlineGrace || s == Status::trial;
    }

    // Cle beta (date de fin posee dans /admin) ou licence definitive.
    bool isBetaLicense() const { return expiresAt.isNotEmpty(); }

    // Jour de fin de la beta, en anglais ("15 January 2030", ou "15 Jan 2030"
    // avec shortMonth). C'est le jour UTC choisi dans /admin : la cle s'arrete
    // a 23:59 UTC ce jour-la. Vide pour une licence definitive.
    juce::String getBetaEndDateText (bool shortMonth = false) const
    {
        if (expiresAt.length() < 10)
            return {};
        static const char* const months[] = { "January", "February", "March", "April", "May", "June", "July",
                                              "August", "September", "October", "November", "December" };
        const int y = expiresAt.substring (0, 4).getIntValue();
        const int mo = expiresAt.substring (5, 7).getIntValue();
        const int d = expiresAt.substring (8, 10).getIntValue();
        if (mo < 1 || mo > 12)
            return expiresAt.substring (0, 10);
        const juce::String month (months[mo - 1]);
        return juce::String (d) + " " + (shortMonth ? month.substring (0, 3) : month) + " " + juce::String (y);
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

    // Reponse du serveur (json : corps complet, pour expires_at).
    struct ApiResult { bool ok = false; bool networkError = false; juce::String error; juce::var json; };

    juce::String product;
    juce::String machineId;
    juce::String storedKey;
    std::atomic<Status> status { Status::unlicensed };
    juce::String lastError;
    juce::Time lastSuccessfulCheck;
    juce::Time trialStart;

    // ---- Cle beta a date de fin (v2.22.6) ----
    // expiresAt = chaine du serveur, UTC "AAAA-MM-JJ HH:MM:SS" (format D1),
    // vide = licence definitive. Recue a l'activation et a chaque controle (le
    // serveur peut la prolonger), gardee dans license.json pour s'arreter a la
    // date meme hors ligne. Sevenpack n'a pas de certificat signe : effacer la
    // date du fichier ne tient que jusqu'au prochain controle en ligne, qui la
    // renvoie (ou repond "expired"), et 14 jours au plus hors ligne.
    // expiresAtMs : 0 = pas de date, -1 = date illisible (traitee comme passee),
    // sinon l'instant de fin ; atomique car lu par getStatus().
    juce::String expiresAt;
    std::atomic<juce::int64> expiresAtMs { 0 };

    // "Join the beta program" en cours / derniere erreur (voir joinBeta()).
    bool betaJoining = false;
    juce::String betaJoinError;

    static juce::int64 parseServerUtcMs (const juce::String& s)
    {
        if (s.length() < 19 || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':')
            return 0;
        const int y = s.substring (0, 4).getIntValue(), mo = s.substring (5, 7).getIntValue(),
                  d = s.substring (8, 10).getIntValue(), h = s.substring (11, 13).getIntValue(),
                  mi = s.substring (14, 16).getIntValue(), se = s.substring (17, 19).getIntValue();
        if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59)
            return 0;
        return juce::Time (y, mo - 1, d, h, mi, se, 0, false).toMilliseconds(); // false = UTC
    }

    void setExpiry (const juce::String& s)
    {
        expiresAt = s.trim();
        if (expiresAt.isEmpty())
            expiresAtMs = 0;
        else
        {
            const auto ms = parseServerUtcMs (expiresAt);
            expiresAtMs = ms > 0 ? ms : -1;
        }
    }

    bool betaHasEnded() const
    {
        const auto ms = expiresAtMs.load();
        return ms != 0 && (ms < 0 || juce::Time::currentTimeMillis() >= ms);
    }

    // /v1/activate et /v1/validate renvoient toujours expires_at depuis la
    // migration-010 (null = definitive). Un serveur plus ancien ne l'envoie
    // pas : on garde alors la date connue.
    void takeExpiryFrom (const juce::var& json)
    {
        if (auto* o = json.getDynamicObject(); o != nullptr && o->hasProperty ("expires_at"))
        {
            const auto v = o->getProperty ("expires_at");
            setExpiry (v.isString() ? v.toString() : juce::String());
        }
    }

    static juce::File getLicenseFile()
    {
       #ifdef SEVENPACK_LICENSE_FILE_FOR_TESTS
        // Bancs de test hors plugin seulement (scratchpad/harness/license_test.cpp) :
        // jamais defini dans le projet, la vraie licence n'est pas touchee.
        return juce::File (SEVENPACK_LICENSE_FILE_FOR_TESTS);
       #else
        return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
            .getChildFile ("YourCompany").getChildFile ("SevenpackAlpha").getChildFile ("license.json");
       #endif
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

        setExpiry (json.getProperty ("expires_at", "").toString());
    }

    // Assumes trialStart is already set (see startDemo()) — just recomputes
    // trial/trialExpired from it and pings the server for /admin visibility.
    void recomputeTrialStatus()
    {
        status = (getTrialDaysLeft() > 0) ? Status::trial : Status::trialExpired;

        // Also lets /admin see trial installs — and if an admin revoked this
        // one, the server says so here and we block immediately rather than
        // waiting for the local 14-day countdown to run out.
        request ("/v1/trial-ping",
                 { { "machine_id", machineId }, { "product_id", product },
                   { "machine_name", juce::SystemStats::getComputerName() },
                   { "plugin_version", sevenpackVersionLabel() } },
                 [this] (const ApiResult& result)
                 {
                     // Only an explicit "revoked" from the server ends the trial early —
                     // any other server hiccup (500, unmapped route, etc.) must never be
                     // treated as a revocation, or a server-side bug silently locks out demos.
                     if (result.error == "revoked")
                         status = Status::trialExpired;
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
        obj->setProperty ("expires_at", expiresAt);
        file.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    // Reponse commune a /v1/activate et /v1/validate. allowOfflineGrace :
    // controle d'une cle DEJA validee (lancement, controle quotidien) -> sans
    // reseau, on tolere 14 jours depuis le dernier controle reussi. Une cle
    // tapee a l'instant n'a pas cette tolerance.
    void handleLicenseResult (const ApiResult& result, bool allowOfflineGrace)
    {
        if (result.ok)
        {
            takeExpiryFrom (result.json);
            status = Status::licensed; // getStatus() passe a betaEnded si la date est passee
            lastSuccessfulCheck = juce::Time::getCurrentTime();
            saveStoredLicense();
        }
        else if (result.error == "expired")
        {
            // Cle beta finie cote serveur : on garde la date pour l'afficher et
            // pour rester bloque hors ligne aux lancements suivants.
            takeExpiryFrom (result.json);
            status = Status::betaEnded;
            lastError = result.error;
            saveStoredLicense();
        }
        else if (result.networkError && allowOfflineGrace && lastSuccessfulCheck != juce::Time())
        {
            // Server unreachable: keep working for a grace period rather than
            // punishing users who are offline (studio without internet, etc).
            // v2.22.6 : vaut aussi au LANCEMENT -- avant, un lancement hors ligne
            // affichait "Invalid license: network_error" au bout de 8 s.
            auto daysSinceCheck = (juce::Time::getCurrentTime() - lastSuccessfulCheck).inDays();
            status = (daysSinceCheck <= offlineGraceDays) ? Status::offlineGrace : Status::invalid;
            lastError = result.error;
        }
        else
        {
            // Server explicitly said no (revoked / machine removed), or a new
            // key could not reach the server.
            status = Status::invalid;
            lastError = result.error;
        }
    }

    void beginActivation (const juce::String& key, const juce::String& firstName = {},
                          const juce::String& lastName = {}, const juce::String& email = {},
                          bool isStoredKeyCheck = false)
    {
        if (key.isEmpty())
            return;

        if (key != storedKey)
            setExpiry ({}); // nouvelle cle : la date de l'ancienne ne la concerne pas

        storedKey = key;
        status = Status::activating;

        request ("/v1/activate",
                 { { "license_key", key }, { "machine_id", machineId },
                   { "machine_name", juce::SystemStats::getComputerName() },
                   { "product_id", product },
                   { "plugin_version", sevenpackVersionLabel() },
                   { "first_name", firstName }, { "last_name", lastName }, { "email", email } },
                 [this, isStoredKeyCheck] (const ApiResult& result)
                 {
                     handleLicenseResult (result, isStoredKeyCheck);
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

        request ("/v1/validate",
                 { { "license_key", storedKey }, { "machine_id", machineId },
                   { "plugin_version", sevenpackVersionLabel() } },
                 [this] (const ApiResult& result)
                 {
                     handleLicenseResult (result, true);
                 });
    }

    using Fields = std::map<juce::String, juce::String>;

    struct Job
    {
        juce::String path;
        Fields fields;
        std::function<void (const ApiResult&)> onResult; // thread de messages, objet vivant
    };

    // Met une requete en file ; onResult est appele sur le thread de messages,
    // seulement si ce gestionnaire existe encore.
    void request (const juce::String& path, Fields fields, std::function<void (const ApiResult&)> onResult)
    {
        {
            const juce::ScopedLock sl (jobLock);
            jobs.push_back ({ path, std::move (fields), std::move (onResult) });
        }
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock sl (jobLock);
                if (! jobs.empty())
                {
                    job = std::move (jobs.front());
                    jobs.pop_front();
                }
            }
            if (job.path.isEmpty())
            {
                wait (-1);
                continue;
            }

            const auto result = postJson (job.path, job.fields);
            if (threadShouldExit())
                break; // fermeture : on ne repond plus

            juce::MessageManager::callAsync ([weak = weakThis, done = std::move (job.onResult), result]
            {
                if (weak.get() != nullptr && done)
                    done (result);
            });
        }
    }

    ApiResult postJson (const juce::String& path, const Fields& fields)
    {
        auto obj = new juce::DynamicObject();
        for (auto& f : fields)
            obj->setProperty (f.first, f.second);
        juce::String body = juce::JSON::toString (juce::var (obj), true);

        juce::URL url (juce::String (licenseServerBaseUrl) + path);
        url = url.withPOSTData (body);

        // WebInputStream plutot que createInputStream : annulable depuis le
        // destructeur (fermeture pendant une requete).
        juce::WebInputStream stream (url, true);
        stream.withExtraHeaders ("Content-Type: application/json")
              .withConnectionTimeout (8000);
        {
            const juce::ScopedLock sl (streamLock);
            if (threadShouldExit())
                return { false, true, "network_error" };
            currentStream = &stream;
        }
        const bool connected = stream.connect (nullptr);
        const int statusCode = stream.getStatusCode();
        const auto response = connected && ! threadShouldExit() ? stream.readEntireStreamAsString() : juce::String();
        {
            const juce::ScopedLock sl (streamLock);
            currentStream = nullptr;
        }

        if (! connected || statusCode == 0)
            return { false, true, "network_error" };

        auto json = juce::JSON::parse (response);
        ApiResult result;
        result.ok = statusCode >= 200 && statusCode < 300
                    && static_cast<bool> (json.getProperty ("ok", false));
        result.error = json.getProperty ("error", "").toString();
        result.json = json;
        return result;
    }

    juce::CriticalSection jobLock, streamLock;
    std::deque<Job> jobs;
    juce::WebInputStream* currentStream = nullptr;
    juce::WeakReference<LicenseManager> weakThis;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseManager)

    // Replace with your deployed Worker URL (see D:/vst/LicenseServer/README.md).
    static constexpr const char* licenseServerBaseUrl = "https://license.sidekey.fr";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
