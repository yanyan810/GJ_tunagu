#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

// Main-thread CPU scopes and direct-queue GPU timestamps. The renderer already
// waits once per frame: readback uses that completed fence, never another wait.
class FrameProfiler {
public:
    static constexpr uint32_t kMaxSamples = 64;
    static constexpr uint32_t kMaxCounters = 24;
    enum class Mode { Off, Fps, Details };
    struct Sample {
        std::array<char, 64> name{};
        double milliseconds = 0.0;
        double averageMs = 0.0;
        uint32_t calls = 0;
        bool available = false;
    };
    struct Counter {
        std::array<char, 64> name{};
        uint64_t value = 0;
    };
    struct Snapshot {
        uint64_t frameIndex = 0;
        double frameMs = 0.0;
        double averageFrameMs = 0.0;
        double fps = 0.0;
        bool capturing = false;
        bool gpuSupported = false;
        bool gpuValid = false;
        bool overflow = false;
        uint32_t cpuCount = 0, gpuCount = 0, counterCount = 0;
        std::array<Sample, kMaxSamples> cpu{}, gpu{};
        std::array<Counter, kMaxCounters> counters{};
    };
    using Clock = std::chrono::steady_clock;
    class CpuScope {
    public:
        CpuScope(FrameProfiler& owner, const char* name);
        ~CpuScope();
        CpuScope(const CpuScope&) = delete;
        CpuScope& operator=(const CpuScope&) = delete;
    private:
        FrameProfiler* owner_ = nullptr;
        int slot_ = -1;
        Clock::time_point start_{};
    };
    class GpuScope {
    public:
        GpuScope(FrameProfiler& owner, ID3D12GraphicsCommandList* list, const char* name);
        ~GpuScope();
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;
    private:
        FrameProfiler* owner_ = nullptr;
        ID3D12GraphicsCommandList* list_ = nullptr;
        int token_ = -1;
    };

    static FrameProfiler& Get();
    void InitializeGpu(ID3D12Device* device, ID3D12CommandQueue* queue);
    void ShutdownGpu();
    void InitializeUi(HWND window, DirectXCommon* dx, SrvManager* srv);
    void ShutdownUi();
    void DrawOverlay(ID3D12GraphicsCommandList* list);
    void CycleMode();
    void SetMode(Mode mode) { mode_ = mode; }
    Mode GetMode() const { return mode_; }
    // Allows headless measurement without drawing an overlay or simulating keys.
    void SetCaptureEnabled(bool enabled) { forcedCapture_ = enabled; }
    bool IsCapturing() const { return frameActive_ && captureThisFrame_; }
    void BeginFrame();
    void EndFrame();
    void BeginGpuFrame(ID3D12GraphicsCommandList* firstList);
    void ResolveGpuFrame(ID3D12GraphicsCommandList* lastList);
    // Call only after the submitted frame's existing fence has completed.
    void ReadGpuAfterFence();
    [[nodiscard]] CpuScope ScopeCpu(const char* name) { return CpuScope(*this, name); }
    [[nodiscard]] GpuScope ScopeGpu(ID3D12GraphicsCommandList* list, const char* name) {
        return GpuScope(*this, list, name);
    }
    void AddCounter(const char* name, uint64_t value);
    const Snapshot& GetSnapshot() const { return snapshot_; }

private:
    static constexpr uint32_t kMaxGpuSpans = 512;
    struct GpuSpan { int slot = -1; bool ended = false; bool valid = true; };
    int FindSample(std::array<Sample, kMaxSamples>& samples, uint32_t& count, const char* name);
    int BeginGpu(ID3D12GraphicsCommandList* list, const char* name);
    void EndGpu(ID3D12GraphicsCommandList* list, int token);
    static void PublishSamples(const std::array<Sample, kMaxSamples>& working,
        uint32_t count, std::array<Sample, kMaxSamples>& published);

    Snapshot snapshot_{};
    std::array<Sample, kMaxSamples> cpu_{}, gpu_{};
    std::array<Counter, kMaxCounters> counters_{};
    std::array<GpuSpan, kMaxGpuSpans> gpuSpans_{};
    uint32_t cpuCount_ = 0, gpuCount_ = 0, counterCount_ = 0, gpuSpanCount_ = 0;
    bool frameActive_ = false, captureThisFrame_ = false, forcedCapture_ = false;
    bool gpuResolved_ = false, gpuValid_ = false, overflow_ = false;
    int totalGpuToken_ = -1;
    Mode mode_ = Mode::Off;
    Clock::time_point frameStart_{};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    uint64_t* timestamps_ = nullptr;
    uint64_t timestampFrequency_ = 0;
    bool ownsUi_ = false, uiReady_ = false;
    SrvManager* srv_ = nullptr;
};
