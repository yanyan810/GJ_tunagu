#pragma once
#include <xaudio2.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <Windows.h>

//mp4用
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

class AudioSystem {
public:
    using SoundHandle = int;

    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // XAudio2 初期化/終了
    bool Initialize();
    void Finalize();

    // wav 読み込み（RIFF/WAVE のみ想定）
    SoundHandle LoadWave(const std::string& filepath, bool loop = false);
    void Unload(SoundHandle handle);
    void UnloadAll();

    //wav以外も読む
    SoundHandle LoadAudioFile(const std::wstring& filepath, bool loop = false);

    // 再生
    void Play(SoundHandle handle, float volume = 1.0f);
    void StopAll();

    // 再生が終わった Voice を破棄（毎フレーム呼ぶ）
    void Update();

    void Stop(SoundHandle h);
    void Pause(SoundHandle h);
    void Resume(SoundHandle h);

    //void SetBusVolume(AudioBus bus, float volume);

    void FadeIn(SoundHandle h, float sec);
    void FadeOut(SoundHandle h, float sec);


private:
    struct Sound {
        WAVEFORMATEX wfex{};
        std::vector<uint8_t> buffer; // wave data
        bool loop = false;
    };

    struct PlayingVoice {
        SoundHandle handle = 0;
        IXAudio2SourceVoice* voice = nullptr;
        bool paused = false;
    };


    enum class AudioBus {
        BGM,
        SE,
        Voice
    };

private:
    SoundHandle nextHandle_ = 1;

    IXAudio2* xAudio2_ = nullptr;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    std::unordered_map<SoundHandle, Sound> sounds_;
    std::vector<PlayingVoice> playing_;

private:
    static Sound LoadWaveInternal_(const std::string& filepath, bool loop);
    static void DestroyVoice_(IXAudio2SourceVoice*& v);
    static Sound DecodeToPCMWithMF_(const std::wstring& path, bool loop);

    static void DebugMsg(const char* s) { OutputDebugStringA(s); }

};
