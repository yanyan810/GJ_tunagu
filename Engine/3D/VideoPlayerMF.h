#pragma once
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <cstdint>
#include <xaudio2.h> 

#include <d3d12.h>
#include <dxgi1_6.h>
#include <mferror.h>

struct XAudio2VoiceCallback : public IXAudio2VoiceCallback {
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}

    // ★再生し終わったバッファを解放
    void STDMETHODCALLTYPE OnBufferEnd(void* pContext) override {
        auto* p = reinterpret_cast<uint8_t*>(pContext);
        delete[] p;
    }

    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};


class SrvManager;

class VideoPlayerMF {
public:

    bool Open(const std::wstring& path, bool loop);
    bool Open(const std::string& pathUtf8, bool loop);

    void Close();

    bool ReadNextFrame();

    void PumpAudio();         // ★追加
    bool ReadNextVideoFrame(); // ★追加（映像専用）

    bool CreateDxResources(ID3D12Device* device, SrvManager* srv);

    // Upload（このフレームで描画する前に呼ぶ）
    void UploadToGpu(ID3D12GraphicsCommandList* cmd);

    // 描画が終わった後に呼ぶ（次のUploadのために戻す）
    void EndFrame(ID3D12GraphicsCommandList* cmd);

    // SRV を使って描画したい時用
    uint32_t SrvIndex() const { return srvIndex_; }
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu() const { return srvGpu_; }

    uint32_t Width()  const { return width_; }
    uint32_t Height() const { return height_; }
    int32_t  Stride() const { return stride_; }
    const uint8_t* FrameBGRA() const { return frame_.data(); }

    LONGLONG LastTimestamp100ns() const { return lastTs100ns_; }

    bool IsReady() const { return gpuInitialized_ && (videoTex_ != nullptr) && (srvGpu_.ptr != 0); }

    void SetVolume(float volume);   // 0.0f〜1.0f（2.0fとかも一応可）
    float GetVolume() const { return volume_; }

    int  GetAudioTrackCount() const { return (int)audioStreamIds_.size(); }
    int  GetAudioTrackIndex() const { return audioTrackIndex_; }
    bool SetAudioTrack(int trackIndex); // 0..count-1

    double GetTimeSec() const { return (double)lastTs100ns_ / 10000000.0; }       // 現在秒
    double GetDurationSec() const { return (double)duration100ns_ / 10000000.0; } // 総秒(0なら未取得)
    LONGLONG GetTime100ns() const { return lastTs100ns_; }
    bool HasDuration() const { return duration100ns_ > 0; }

private:


    void EnumerateAudioStreams_();
    bool SetupAudioForStream_(DWORD streamId);

private:

    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    uint32_t width_ = 0, height_ = 0;
    int32_t  stride_ = 0;

    bool loop_ = false;
    LONGLONG duration100ns_ = 0;

    std::vector<uint8_t> frame_;
    LONGLONG lastTs100ns_ = 0;
    bool hasNewFrame_ = false;

    // DX12
    Microsoft::WRL::ComPtr<ID3D12Resource> videoTex_;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuf_;
    uint32_t srvIndex_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu_{};

    bool gpuInitialized_ = false;
    bool texInPSR_ = false; // 今 PSR 状態か？（状態管理）

    Microsoft::WRL::ComPtr<IXAudio2> xa_;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* source_ = nullptr;

    XAudio2VoiceCallback voiceCb_{};
    bool audioEos_ = false;

    float volume_ = 1.0f;

    std::vector<DWORD> audioStreamIds_;
    DWORD audioStreamId_ = MF_SOURCE_READER_FIRST_AUDIO_STREAM; // 現在使用中の実ストリームID
    int   audioTrackIndex_ = 1;


};
