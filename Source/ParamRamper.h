#pragma once
#include <JuceHeader.h>

/**
    ParamRamper
    -----------
    Anime en douceur la transition d'un ou plusieurs parametres continus
    (knobs Input/Attack/Release/Output, Crossover Lo/Hi) vers une nouvelle
    valeur -- utilise au chargement d'un preset pour que les knobs
    TOURNENT visiblement au lieu de teleporter instantanement. Plusieurs
    parametres peuvent rampe en meme temps (un seul Timer partage, pas un
    par parametre).

    setValueNotifyingHost() est appele a chaque frame le temps de la
    rampe (~260ms, ease-out cubique) -- acceptable ici puisque c'est un
    evenement ponctuel (changement de preset), pas de l'automation continue.
*/
class ParamRamper : private juce::Timer
{
public:
    void rampTo (juce::RangedAudioParameter* param, float targetNormalised, int durationMs = 260)
    {
        if (param == nullptr)
            return;

        const double now = juce::Time::getMillisecondCounterHiRes();

        for (auto& e : entries)
        {
            if (e.param == param)
            {
                e.from = param->getValue();
                e.to = targetNormalised;
                e.startMs = now;
                e.durationMs = (double) durationMs;
                return;
            }
        }

        entries.push_back ({ param, param->getValue(), targetNormalised, now, (double) durationMs });
        if (! isTimerRunning())
            startTimerHz (60);
    }

private:
    struct Entry
    {
        juce::RangedAudioParameter* param;
        float from, to;
        double startMs, durationMs;
    };
    std::vector<Entry> entries;

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        for (int i = (int) entries.size() - 1; i >= 0; --i)
        {
            auto& e = entries[(size_t) i];
            const float t = (float) juce::jlimit (0.0, 1.0, (now - e.startMs) / e.durationMs);
            const float eased = 1.0f - std::pow (1.0f - t, 3.0f); // ease-out cubique
            e.param->setValueNotifyingHost (e.from + (e.to - e.from) * eased);

            if (t >= 1.0f)
                entries.erase (entries.begin() + i);
        }

        if (entries.empty())
            stopTimer();
    }
};
