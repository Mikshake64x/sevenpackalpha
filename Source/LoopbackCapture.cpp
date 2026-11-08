#include "LoopbackCapture.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <mmreg.h>
 #include <ksmedia.h>
 #include <initguid.h>                       // definit PKEY_Device_FriendlyName ici (comme Merlin)
 #include <functiondiscoverykeys_devpkey.h>
 #pragma comment(lib, "ole32.lib")

namespace
{
    // Pointeur COM minimal (Release automatique).
    template <class T> struct ComRef
    {
        T* p = nullptr;
        ~ComRef()                       { reset(); }
        void reset()                    { if (p != nullptr) { p->Release(); p = nullptr; } }
        T** put()                       { reset(); return &p; }
        T* operator->() const           { return p; }
        explicit operator bool() const  { return p != nullptr; }
    };

    // COM pour l'appel courant : rien a faire si le thread l'a deja (STA ou MTA).
    struct ScopedCom
    {
        ScopedCom()  { const HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED); owns = SUCCEEDED (hr); }
        ~ScopedCom() { if (owns) CoUninitialize(); }
        bool owns = false;
    };

    juce::String endpointId (IMMDevice* dev)
    {
        LPWSTR id = nullptr;
        if (dev == nullptr || FAILED (dev->GetId (&id)) || id == nullptr)
            return {};
        juce::String s (id);
        CoTaskMemFree (id);
        return s;
    }

    juce::String friendlyName (IMMDevice* dev)
    {
        juce::String name;
        ComRef<IPropertyStore> ps;
        if (dev != nullptr && SUCCEEDED (dev->OpenPropertyStore (STGM_READ, ps.put())))
        {
            PROPVARIANT v;
            PropVariantInit (&v);
            if (SUCCEEDED (ps->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                name = juce::String (v.pwszVal);
            PropVariantClear (&v);
        }
        return name;
    }

    bool makeEnumerator (ComRef<IMMDeviceEnumerator>& en)
    {
        return SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                            __uuidof (IMMDeviceEnumerator), (void**) en.put()));
    }
}

void LoopbackCapture::listPlaybackDevices (juce::StringArray& names, juce::StringArray& ids, juce::String& defaultName)
{
    names.clear();
    ids.clear();
    defaultName.clear();
    ScopedCom com;
    ComRef<IMMDeviceEnumerator> en;
    if (! makeEnumerator (en))
        return;
    {
        ComRef<IMMDevice> def;
        if (SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, def.put())))
            defaultName = friendlyName (def.p);
    }
    ComRef<IMMDeviceCollection> all;
    UINT count = 0;
    if (FAILED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, all.put())) || FAILED (all->GetCount (&count)))
        return;
    for (UINT i = 0; i < count; ++i)
    {
        ComRef<IMMDevice> dev;
        if (FAILED (all->Item (i, dev.put())))
            continue;
        const auto id = endpointId (dev.p);
        if (id.isEmpty())
            continue;
        names.add (friendlyName (dev.p));
        ids.add (id);
    }
}

void LoopbackCapture::setDevice (const juce::String& endpointIdIn)
{
    {
        const juce::ScopedLock sl (nameLock);
        if (wantedId == endpointIdIn)
            return;
        wantedId = endpointIdIn;
    }
    restartRequested = true; // la session en cours se termine et rouvre le bon peripherique
}

void LoopbackCapture::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::highest);
}

void LoopbackCapture::stop()
{
    stopThread (3000);
    capturing = false;
}

void LoopbackCapture::run()
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    while (! threadShouldExit())
        wait (captureSession() ? 20 : 1000); // echec : nouvel essai dans 1 s
    capturing = false;
    CoUninitialize();
}

bool LoopbackCapture::captureSession()
{
    restartRequested = false;
    juce::String wanted;
    {
        const juce::ScopedLock sl (nameLock);
        wanted = wantedId;
    }

    ComRef<IMMDeviceEnumerator> en;
    if (! makeEnumerator (en))
        return false;
    ComRef<IMMDevice> dev;
    if (wanted.isEmpty())
    {
        if (FAILED (en->GetDefaultAudioEndpoint (eRender, eConsole, dev.put())))
            return false;
    }
    else if (FAILED (en->GetDevice (wanted.toWideCharPointer(), dev.put())))
    {
        return false;
    }
    const juce::String devId = endpointId (dev.p);
    {
        const juce::ScopedLock sl (nameLock);
        deviceName = friendlyName (dev.p);
    }

    ComRef<IAudioClient> client;
    if (FAILED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) client.put())))
        return false;
    WAVEFORMATEX* wfx = nullptr;
    if (FAILED (client->GetMixFormat (&wfx)) || wfx == nullptr)
        return false;
    struct FreeFmt { WAVEFORMATEX* w; ~FreeFmt() { CoTaskMemFree (w); } } freeFmt { wfx };

    if (FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000 /* 20 ms */, 0, wfx, nullptr)))
        return false;
    ComRef<IAudioCaptureClient> cap;
    if (FAILED (client->GetService (__uuidof (IAudioCaptureClient), (void**) cap.put())))
        return false;

    // Format du melangeur Windows (le plus souvent flottant 32 bits).
    const int nch = juce::jmax (1, (int) wfx->nChannels);
    const int bits = wfx->wBitsPerSample;
    const double sr = (double) wfx->nSamplesPerSec;
    bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        isFloat = IsEqualGUID (reinterpret_cast<WAVEFORMATEXTENSIBLE*> (wfx)->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
    const int bytesPerSample = bits / 8;
    if ((isFloat && bits != 32) || (! isFloat && bits != 16 && bits != 24 && bits != 32))
        return false;

    if (FAILED (client->Start()))
        return false;
    sampleRate = sr;
    if (onStart)
        onStart (sr);
    capturing = true;

    std::vector<float> bufL ((size_t) kBlock), bufR ((size_t) kBlock);
    const float* chans[2] = { bufL.data(), bufR.data() };
    auto deliver = [&] (int n) { if (onAudio) onAudio (chans, n); };
    auto sampleAt = [&] (const BYTE* frame, int ch) -> float
    {
        const BYTE* s = frame + ch * bytesPerSample;
        if (isFloat)    return *reinterpret_cast<const float*> (s);
        if (bits == 16) return (float) *reinterpret_cast<const juce::int16*> (s) / 32768.0f;
        if (bits == 24)
        {
            const int v = (int) ((juce::uint32) s[0] << 8 | (juce::uint32) s[1] << 16 | (juce::uint32) s[2] << 24) >> 8;
            return (float) v / 8388608.0f;
        }
        return (float) ((double) *reinterpret_cast<const juce::int32*> (s) / 2147483648.0);
    };

    double lastData = juce::Time::getMillisecondCounterHiRes(), lastDevCheck = lastData;
    bool lost = false;
    while (! threadShouldExit() && ! lost && ! restartRequested.load())
    {
        UINT32 packet = 0;
        if (FAILED (cap->GetNextPacketSize (&packet))) { lost = true; break; }
        if (packet == 0)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            // Rien ne joue : silence au rythme reel (la file de l'audio reste alimentee).
            if (now - lastData > 30.0)
            {
                int n = (int) juce::jmin ((now - lastData) * sr * 0.001, sr * 0.1);
                std::fill (bufL.begin(), bufL.end(), 0.0f);
                std::fill (bufR.begin(), bufR.end(), 0.0f);
                while (n > 0) { const int k = juce::jmin (n, kBlock); deliver (k); n -= k; }
                lastData = now;
            }
            // Sortie par defaut changee dans Windows (mode "par defaut") : on rouvre.
            if (wanted.isEmpty() && now - lastDevCheck > 1000.0)
            {
                lastDevCheck = now;
                ComRef<IMMDevice> cur;
                if (FAILED (en->GetDefaultAudioEndpoint (eRender, eConsole, cur.put())) || endpointId (cur.p) != devId)
                    break;
            }
            juce::Thread::sleep (3);
            continue;
        }
        while (packet > 0)
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED (cap->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) { lost = true; break; }
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr;
            const int frameBytes = nch * bytesPerSample;
            for (UINT32 pos = 0; pos < frames;)
            {
                const int k = (int) juce::jmin<UINT32> (frames - pos, (UINT32) kBlock);
                for (int i = 0; i < k; ++i)
                {
                    if (silent) { bufL[(size_t) i] = bufR[(size_t) i] = 0.0f; continue; }
                    const BYTE* fr = data + (size_t) (pos + (UINT32) i) * (size_t) frameBytes;
                    bufL[(size_t) i] = sampleAt (fr, 0);
                    bufR[(size_t) i] = sampleAt (fr, nch > 1 ? 1 : 0); // mono -> L et R
                }
                deliver (k);
                pos += (UINT32) k;
            }
            cap->ReleaseBuffer (frames);
            if (FAILED (cap->GetNextPacketSize (&packet))) { lost = true; break; }
        }
        lastData = juce::Time::getMillisecondCounterHiRes();
    }
    client->Stop();
    capturing = false;
    return true; // nouvelle session aussitot (changement de peripherique / perte)
}

#else // ------------------------------------------------------------ hors Windows

void LoopbackCapture::listPlaybackDevices (juce::StringArray& names, juce::StringArray& ids, juce::String& defaultName)
{
    names.clear();
    ids.clear();
    defaultName.clear();
}

void LoopbackCapture::setDevice (const juce::String& endpointIdIn) { const juce::ScopedLock sl (nameLock); wantedId = endpointIdIn; }
void LoopbackCapture::start() {}
void LoopbackCapture::stop()  { capturing = false; }
void LoopbackCapture::run()   {}
bool LoopbackCapture::captureSession() { return false; }

#endif
