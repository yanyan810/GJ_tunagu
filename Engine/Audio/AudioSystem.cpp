#include "AudioSystem.h"

#include <cassert>
#include <fstream>
#include <cstring>

// ===== RIFF structs (cpp 内に隠す) =====
struct ChunkHeader {
    char id[4];
    uint32_t size;
};

struct RiffHeader {
    ChunkHeader chunk; // "RIFF"
    char type[4];      // "WAVE"
};

struct FormatChunk {
    ChunkHeader chunk; // "fmt "
    WAVEFORMATEX fmt;
};

AudioSystem::~AudioSystem() {
    Finalize();
}

bool AudioSystem::Initialize() {
    if (xAudio2_) {
        return true;
    }

    HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        xAudio2_ = nullptr;
        return false;
    }

    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    if (FAILED(hr)) {
        masterVoice_ = nullptr;
        xAudio2_->Release();
        xAudio2_ = nullptr;
        return false;
    }

     hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));


    return true;
}

void AudioSystem::Finalize() {
    StopAll();
    UnloadAll();

    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }

    MFShutdown();

    if (xAudio2_) {
        xAudio2_->Release();
        xAudio2_ = nullptr;
    }
}

AudioSystem::SoundHandle AudioSystem::LoadWave(const std::string& filepath, bool loop) {
    // 既に同じものを管理したいならキャッシュ設計にするけど、
    // まずは「呼んだら新規 handle」を返すシンプル版
    Sound s = LoadWaveInternal_(filepath, loop);

    const SoundHandle h = nextHandle_++;
    sounds_.emplace(h, std::move(s));
    return h;
}

AudioSystem::SoundHandle AudioSystem::LoadAudioFile(const std::wstring& filepath, bool loop)
{
    Sound s = DecodeToPCMWithMF_(filepath, loop);

    const SoundHandle h = nextHandle_++;
    sounds_.emplace(h, std::move(s));
    return h;
}


void AudioSystem::Unload(SoundHandle handle) {
    auto it = sounds_.find(handle);
    if (it != sounds_.end()) {
        sounds_.erase(it);
    }
}

void AudioSystem::UnloadAll() {
    sounds_.clear();
}

void AudioSystem::Play(SoundHandle handle, float volume) {
    auto it = sounds_.find(handle);
    if (it == sounds_.end()) {
        return;
    }
    if (!xAudio2_) {
        return;
    }

    const Sound& s = it->second;

    IXAudio2SourceVoice* sv = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&sv, &s.wfex);
    if (FAILED(hr) || !sv) {
        return;
    }

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = reinterpret_cast<const BYTE*>(s.buffer.data());
    buf.AudioBytes = static_cast<UINT32>(s.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;

    if (s.loop) {
        buf.LoopBegin = 0;
        buf.LoopLength = 0;           // 全体ループ
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
        buf.Flags = 0;                // ループ中は END_OF_STREAM を付けない
    }

    hr = sv->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        DestroyVoice_(sv);
        return;
    }

    sv->SetVolume(volume);
    hr = sv->Start();
    if (FAILED(hr)) {
        DestroyVoice_(sv);
        return;
    }

    playing_.push_back({ handle, sv, false });
}

void AudioSystem::StopAll() {
    for (auto& p : playing_) {
        if (p.voice) {
            p.voice->Stop(0);
            p.voice->FlushSourceBuffers();
            DestroyVoice_(p.voice);
        }
    }
    playing_.clear();
}

void AudioSystem::Update()
{
    for (size_t i = 0; i < playing_.size();) {
        IXAudio2SourceVoice* v = playing_[i].voice;
        if (!v) {
            playing_.erase(playing_.begin() + i);
            continue;
        }

        // ★ Pause 中は破棄判定しない
        if (playing_[i].paused) {
            ++i;
            continue;
        }

        XAUDIO2_VOICE_STATE st{};
        v->GetState(&st);

        if (st.BuffersQueued == 0) {
            DestroyVoice_(playing_[i].voice);
            playing_.erase(playing_.begin() + i);
            continue;
        }

        ++i;
    }
}


void AudioSystem::DestroyVoice_(IXAudio2SourceVoice*& v) {
    if (v) {
        v->DestroyVoice();
        v = nullptr;
    }
}

AudioSystem::Sound AudioSystem::LoadWaveInternal_(const std::string& filepath, bool loop) {
    std::ifstream file(filepath, std::ios_base::binary);
    assert(file.is_open());

    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

    // RIFF / WAVE チェック
    assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(std::strncmp(riff.type, "WAVE", 4) == 0);

    // fmt チャンク
    ChunkHeader ch{};
    file.read(reinterpret_cast<char*>(&ch), sizeof(ch));
    assert(std::strncmp(ch.id, "fmt ", 4) == 0);

    FormatChunk fmt{};
    fmt.chunk = ch;

    assert(fmt.chunk.size <= sizeof(fmt.fmt));
    file.read(reinterpret_cast<char*>(&fmt.fmt), fmt.chunk.size);

    // data チャンクまで進む（JUNK などをスキップ）
    ChunkHeader data{};
    for (;;) {
        file.read(reinterpret_cast<char*>(&data), sizeof(data));
        assert(file.good());

        if (std::strncmp(data.id, "JUNK", 4) == 0) {
            file.seekg(data.size, std::ios_base::cur);
            continue;
        }
        if (std::strncmp(data.id, "data", 4) == 0) {
            break;
        }

        // 他チャンクは読み飛ばす（拡張にも耐える）
        file.seekg(data.size, std::ios_base::cur);
    }

    Sound out{};
    out.wfex = fmt.fmt;
    out.loop = loop;
    out.buffer.resize(data.size);
    file.read(reinterpret_cast<char*>(out.buffer.data()), data.size);

    return out;
}

AudioSystem::Sound AudioSystem::DecodeToPCMWithMF_(const std::wstring& path, bool loop)
{
    using Microsoft::WRL::ComPtr;

    HRESULT hr;

    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    assert(SUCCEEDED(hr));

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    assert(SUCCEEDED(hr));

    ComPtr<IMFMediaType> outType;
    hr = MFCreateMediaType(&outType);
    assert(SUCCEEDED(hr));
    hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    assert(SUCCEEDED(hr));
    hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    assert(SUCCEEDED(hr));
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, outType.Get());
    assert(SUCCEEDED(hr));

    ComPtr<IMFMediaType> curType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &curType);
    assert(SUCCEEDED(hr));

    UINT32 ch = 0, sr = 0, bps = 0;
    hr = curType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);          assert(SUCCEEDED(hr));
    hr = curType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);   assert(SUCCEEDED(hr));
    hr = curType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bps);     assert(SUCCEEDED(hr));


    std::vector<uint8_t> pcm;
    for (;;) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;

        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIndex, &flags, &ts, &sample);
        assert(SUCCEEDED(hr));

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buf;
        hr = sample->ConvertToContiguousBuffer(&buf); assert(SUCCEEDED(hr));

        BYTE* p = nullptr; DWORD maxLen = 0, curLen = 0;
        hr = buf->Lock(&p, &maxLen, &curLen); assert(SUCCEEDED(hr));

        size_t old = pcm.size();
        pcm.resize(old + curLen);
        std::memcpy(pcm.data() + old, p, curLen);

        buf->Unlock();
    }

    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)ch;
    wf.nSamplesPerSec = sr;
    wf.wBitsPerSample = (WORD)bps;
    wf.nBlockAlign = (WORD)(wf.nChannels * (wf.wBitsPerSample / 8));
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    AudioSystem::Sound out{};
    out.wfex = wf;
    out.buffer = std::move(pcm);
    out.loop = loop;
    return out;
}

void AudioSystem::Stop(SoundHandle handle)
{
    for (size_t i = 0; i < playing_.size(); ) {
        auto& p = playing_[i];
        if (p.handle != handle) {
            ++i;
            continue;
        }

        if (p.voice) {
            p.voice->Stop(0);
            p.voice->FlushSourceBuffers();
            DestroyVoice_(p.voice);
        }
        playing_.erase(playing_.begin() + i);
    }
}

void AudioSystem::Pause(SoundHandle handle)
{
    for (auto& p : playing_) {
        if (p.handle != handle) continue;
        if (!p.voice) continue;
        if (p.paused) continue;

        DebugMsg("[Audio] Pause hit target\n"); // ★ここに移動

        HRESULT hr = p.voice->Stop(0);
        (void)hr;
        p.paused = true;
    }
}


void AudioSystem::Resume(SoundHandle handle)
{
    for (auto& p : playing_) {
        if (p.handle != handle) continue;
        if (!p.voice) continue;
        if (!p.paused) continue;

        DebugMsg("[Audio] Resume hit target\n");

        HRESULT hr = p.voice->Start(0);
        assert(SUCCEEDED(hr));   // ★失敗したらここで止まる
        p.paused = false;
    }
}

