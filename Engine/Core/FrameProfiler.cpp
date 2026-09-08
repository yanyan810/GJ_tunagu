#include "FrameProfiler.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

FrameProfiler& FrameProfiler::Get() {
    static FrameProfiler profiler;
    return profiler;
}

FrameProfiler::CpuScope::CpuScope(FrameProfiler& owner, const char* name) {
    if (!owner.IsCapturing()) return;
    slot_ = owner.FindSample(owner.cpu_, owner.cpuCount_, name);
    if (slot_ < 0) return;
    owner_ = &owner;
    start_ = Clock::now();
}

FrameProfiler::CpuScope::~CpuScope() {
    if (!owner_ || !owner_->IsCapturing()) return;
    auto& sample = owner_->cpu_[static_cast<size_t>(slot_)];
    sample.milliseconds += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    ++sample.calls;
    sample.available = true;
}

FrameProfiler::GpuScope::GpuScope(FrameProfiler& owner, ID3D12GraphicsCommandList* list, const char* name)
    : owner_(&owner), list_(list), token_(owner.BeginGpu(list, name)) {}

FrameProfiler::GpuScope::~GpuScope() { owner_->EndGpu(list_, token_); }

int FrameProfiler::FindSample(std::array<Sample, kMaxSamples>& samples, uint32_t& count, const char* name) {
    if (!name || !*name) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (std::strncmp(samples[i].name.data(), name, samples[i].name.size() - 1) == 0) return static_cast<int>(i);
    }
    if (count == kMaxSamples) { overflow_ = true; return -1; }
    const uint32_t index = count++;
    strncpy_s(samples[index].name.data(), samples[index].name.size(), name, _TRUNCATE);
    return static_cast<int>(index);
}

void FrameProfiler::InitializeGpu(ID3D12Device* device, ID3D12CommandQueue* queue) {
    ShutdownGpu();
    if (!device || !queue || FAILED(queue->GetTimestampFrequency(&timestampFrequency_)) || timestampFrequency_ == 0) return;
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = kMaxGpuSpans * 2;
    if (FAILED(device->CreateQueryHeap(&query, IID_PPV_ARGS(&queryHeap_)))) { ShutdownGpu(); return; }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = static_cast<UINT64>(query.Count) * sizeof(uint64_t);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)))) { ShutdownGpu(); return; }
    const D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(buffer.Width) };
    if (FAILED(readback_->Map(0, &readRange, reinterpret_cast<void**>(&timestamps_)))) { ShutdownGpu(); return; }
    queryHeap_->SetName(L"FrameProfiler timestamps");
    readback_->SetName(L"FrameProfiler completed-frame readback");
}

void FrameProfiler::ShutdownGpu() {
    if (timestamps_ && readback_) {
        const D3D12_RANGE noWrites{ 0, 0 };
        readback_->Unmap(0, &noWrites);
    }
    timestamps_ = nullptr;
    timestampFrequency_ = 0;
    readback_.Reset();
    queryHeap_.Reset();
    frameActive_ = false;
    gpuResolved_ = false;
}

void FrameProfiler::BeginFrame() {
    frameStart_ = Clock::now();
    frameActive_ = true;
    captureThisFrame_ = forcedCapture_ || mode_ == Mode::Details;
    gpuSpanCount_ = 0;
    totalGpuToken_ = -1;
    gpuResolved_ = gpuValid_ = overflow_ = false;
    for (auto* samples : { &cpu_, &gpu_ }) {
        for (auto& sample : *samples) {
            sample.milliseconds = 0.0;
            sample.calls = 0;
            sample.available = false;
        }
    }
    for (auto& counter : counters_) counter.value = 0;
}

void FrameProfiler::PublishSamples(const std::array<Sample, kMaxSamples>& working,
    uint32_t count, std::array<Sample, kMaxSamples>& published) {
    for (uint32_t i = 0; i < count; ++i) {
        const auto& next = working[i];
        const double average = next.available
            ? (published[i].available ? published[i].averageMs * 0.9 + next.milliseconds * 0.1 : next.milliseconds)
            : 0.0;
        published[i] = next;
        published[i].averageMs = average;
    }
}

void FrameProfiler::EndFrame() {
    if (!frameActive_) return;
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - frameStart_).count();
    ++snapshot_.frameIndex;
    snapshot_.frameMs = ms;
    snapshot_.averageFrameMs = snapshot_.frameIndex == 1 ? ms : snapshot_.averageFrameMs * 0.9 + ms * 0.1;
    snapshot_.fps = snapshot_.averageFrameMs > 0.0 ? 1000.0 / snapshot_.averageFrameMs : 0.0;
    snapshot_.capturing = captureThisFrame_;
    snapshot_.gpuSupported = timestamps_ && timestampFrequency_ != 0;
    snapshot_.gpuValid = gpuValid_;
    snapshot_.overflow = overflow_;
    snapshot_.cpuCount = cpuCount_;
    snapshot_.gpuCount = gpuCount_;
    snapshot_.counterCount = counterCount_;
    PublishSamples(cpu_, cpuCount_, snapshot_.cpu);
    PublishSamples(gpu_, gpuCount_, snapshot_.gpu);
    snapshot_.counters = counters_;
    frameActive_ = false;
}

void FrameProfiler::AddCounter(const char* name, uint64_t value) {
    if (!IsCapturing() || !name || !*name) return;
    for (uint32_t i = 0; i < counterCount_; ++i) {
        if (std::strncmp(counters_[i].name.data(), name, counters_[i].name.size() - 1) == 0) {
            counters_[i].value += value;
            return;
        }
    }
    if (counterCount_ == kMaxCounters) { overflow_ = true; return; }
    auto& counter = counters_[counterCount_++];
    strncpy_s(counter.name.data(), counter.name.size(), name, _TRUNCATE);
    counter.value = value;
}

int FrameProfiler::BeginGpu(ID3D12GraphicsCommandList* list, const char* name) {
    if (!IsCapturing() || !list || !queryHeap_ || gpuResolved_) return -1;
    if (gpuSpanCount_ == kMaxGpuSpans) { overflow_ = true; return -1; }
    const int slot = FindSample(gpu_, gpuCount_, name);
    if (slot < 0) return -1;
    const uint32_t token = gpuSpanCount_++;
    gpuSpans_[token] = { slot, false, true };
    list->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, token * 2);
    return static_cast<int>(token);
}

void FrameProfiler::EndGpu(ID3D12GraphicsCommandList* list, int token) {
    if (!IsCapturing() || !list || token < 0 || gpuResolved_ || static_cast<uint32_t>(token) >= gpuSpanCount_) return;
    auto& span = gpuSpans_[static_cast<size_t>(token)];
    if (span.ended) return;
    list->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(token) * 2 + 1);
    span.ended = true;
}

void FrameProfiler::BeginGpuFrame(ID3D12GraphicsCommandList* firstList) {
    totalGpuToken_ = BeginGpu(firstList, "GPU total");
}

void FrameProfiler::ResolveGpuFrame(ID3D12GraphicsCommandList* lastList) {
    if (!IsCapturing() || !queryHeap_ || !lastList || gpuResolved_ || gpuSpanCount_ == 0) return;
    EndGpu(lastList, totalGpuToken_);
    // A misplaced caller scope must not resolve an unwritten timestamp. Mark it
    // unavailable and finish its query here instead of issuing an invalid read.
    for (uint32_t i = 0; i < gpuSpanCount_; ++i) {
        if (!gpuSpans_[i].ended) {
            gpuSpans_[i].valid = false;
            EndGpu(lastList, static_cast<int>(i));
        }
    }
    lastList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
        gpuSpanCount_ * 2, readback_.Get(), 0);
    gpuResolved_ = true;
}

void FrameProfiler::ReadGpuAfterFence() {
    if (!gpuResolved_ || !timestamps_ || timestampFrequency_ == 0) return;
    for (uint32_t i = 0; i < gpuSpanCount_; ++i) {
        const auto& span = gpuSpans_[i];
        const uint64_t start = timestamps_[i * 2], end = timestamps_[i * 2 + 1];
        if (!span.valid || !span.ended || end < start) continue;
        const double ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(timestampFrequency_);
        if (!std::isfinite(ms) || ms > 60000.0) continue;
        auto& sample = gpu_[static_cast<size_t>(span.slot)];
        sample.milliseconds += ms;
        sample.available = true;
        ++sample.calls;
        if (static_cast<int>(i) == totalGpuToken_) gpuValid_ = true;
    }
    gpuResolved_ = false;
}

void FrameProfiler::CycleMode() {
    mode_ = mode_ == Mode::Off ? Mode::Fps : mode_ == Mode::Fps ? Mode::Details : Mode::Off;
}

void FrameProfiler::InitializeUi([[maybe_unused]] HWND window, DirectXCommon* dx, SrvManager* srv) {
    if (uiReady_ || !dx || !srv) return;
    srv_ = srv;
#ifdef USE_IMGUI
    // Development already owns the context. Do not change its docking or layout.
    uiReady_ = ImGui::GetCurrentContext() != nullptr;
#else
    // Release has no editor UI. This context renders only a passive F8 overlay.
    if (ImGui::GetCurrentContext()) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ownsUi_ = true;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    // Off mode has no ImGui frames. This passive overlay must not accumulate
    // Win32 input events while hidden; F8 is handled by the game's Input class.
    io.SetAppAcceptingEvents(false);
    ImFontConfig font{};
    font.SizePixels = 17.0f;
    io.Fonts->AddFontDefault(&font);
    if (!ImGui_ImplWin32_Init(window)) { ShutdownUi(); return; }
    ImGui_ImplDX12_InitInfo info{};
    info.Device = dx->GetDevice();
    info.CommandQueue = dx->GetCommandQueue();
    info.NumFramesInFlight = 2;
    info.RTVFormat = dx->GetRTVFormat();
    info.SrvDescriptorHeap = srv->GetDescriptorHeap();
    info.UserData = srv;
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* descriptors = static_cast<SrvManager*>(init->UserData);
        const uint32_t index = descriptors->Allocate();
        *cpu = descriptors->GetCPUDescriptionHandle(index);
        *gpu = descriptors->GetGPUDescriptionHandle(index);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
    if (!ImGui_ImplDX12_Init(&info)) { ShutdownUi(); return; }
    uiReady_ = true;
#endif
}

void FrameProfiler::ShutdownUi() {
    if (ownsUi_ && ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX12_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    uiReady_ = ownsUi_ = false;
    srv_ = nullptr;
}

void FrameProfiler::DrawOverlay(ID3D12GraphicsCommandList* list) {
    if (mode_ == Mode::Off || !uiReady_ || !ImGui::GetCurrentContext()) return;
    if (ownsUi_) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetForegroundDrawList(viewport);
    const float width = std::min(mode_ == Mode::Details ? 604.0f : 225.0f, std::max(180.0f, viewport->WorkSize.x - 24.0f));
    const float x = std::max(viewport->WorkPos.x + 8.0f, viewport->WorkPos.x + viewport->WorkSize.x - width - 12.0f);
    const float y = viewport->WorkPos.y + 12.0f;
    const float line = 21.0f;
    const float height = mode_ == Mode::Details ? std::min(650.0f, viewport->WorkSize.y - 24.0f) : 61.0f;
    draw->PushClipRect(viewport->WorkPos, { viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y }, true);
    draw->AddRectFilled({ x, y }, { x + width, y + height }, IM_COL32(7, 17, 27, 235), 9.0f);
    draw->AddRect({ x, y }, { x + width, y + height }, IM_COL32(97, 203, 225, 130), 9.0f);
    auto text = [&](float tx, float ty, ImU32 color, const char* value) { draw->AddText({ tx, ty }, color, value); };
    constexpr ImU32 white = IM_COL32(226, 239, 245, 255), muted = IM_COL32(159, 179, 190, 255);
    constexpr ImU32 cyan = IM_COL32(100, 230, 245, 255), amber = IM_COL32(255, 196, 104, 255);
    char buffer[200];
    std::snprintf(buffer, sizeof(buffer), "%.1f FPS    %.2f ms", snapshot_.fps, snapshot_.averageFrameMs);
    text(x + 12, y + 8, snapshot_.averageFrameMs > 18.0 ? amber : cyan, buffer);
    text(x + 12, y + 33, muted, mode_ == Mode::Details ? "F8: Hide  |  Times: rolling average (ms)" : "F8: CPU / GPU details");
    if (mode_ == Mode::Details) {
        text(x + 12, y + 59, white, "CPU: work and waits");
        const float columnWidth = (width - 36.0f) * 0.5f;
        const float secondColumn = x + 24.0f + columnWidth;
        text(secondColumn, y + 59, white, "GPU: execution");
        const uint32_t shownCounters = std::min(snapshot_.counterCount, 6u);
        const float counterHeight = static_cast<float>((shownCounters + 1u) / 2u) * line;
        const float noteY = y + height - 82.0f - counterHeight;
        const int maxRows = std::max(1, static_cast<int>((noteY - y - 110.0f) / line));
        auto column = [&](const std::array<Sample, kMaxSamples>& samples, uint32_t count, float left) {
            std::array<uint32_t, kMaxSamples> order{};
            uint32_t available = 0;
            for (uint32_t i = 0; i < count; ++i) if (samples[i].available) order[available++] = i;
            std::sort(order.begin(), order.begin() + available, [&](uint32_t a, uint32_t b) { return samples[a].averageMs > samples[b].averageMs; });
            const uint32_t shown = std::min(available, static_cast<uint32_t>(maxRows));
            for (uint32_t row = 0; row < shown; ++row) {
                const auto& sample = samples[order[row]];
                const float py = y + 86 + static_cast<float>(row) * line;
                const float bar = static_cast<float>(std::clamp(sample.averageMs / 16.667, 0.0, 1.0)) * columnWidth;
                draw->AddRectFilled({ left, py }, { left + bar, py + 19 }, IM_COL32(55, 114, 146, 55), 2.0f);
                std::snprintf(buffer, sizeof(buffer), "%.24s", sample.name.data());
                draw->PushClipRect({ left, py }, { left + std::max(20.0f, columnWidth - 62.0f), py + line }, true);
                text(left, py, white, buffer);
                draw->PopClipRect();
                std::snprintf(buffer, sizeof(buffer), "%6.2f", sample.averageMs);
                const float tx = left + columnWidth - ImGui::CalcTextSize(buffer).x;
                text(tx, py, sample.averageMs > 8.0 ? amber : cyan, buffer);
            }
            if (available > shown) {
                std::snprintf(buffer, sizeof(buffer), "+ %u smaller scopes (snapshot API)", available - shown);
                text(left, y + 86 + static_cast<float>(shown) * line, muted, buffer);
            }
        };
        column(snapshot_.cpu, snapshot_.cpuCount, x + 12);
        column(snapshot_.gpu, snapshot_.gpuCount, secondColumn);
        if (!snapshot_.gpuValid) text(secondColumn, y + 85, amber, snapshot_.gpuSupported ? "Waiting for a complete GPU frame" : "GPU timestamps unavailable");
        for (uint32_t i = 0; i < shownCounters; ++i) {
            const auto& counter = snapshot_.counters[i];
            const float left = i % 2u == 0 ? x + 12.0f : secondColumn;
            const float py = noteY + static_cast<float>(i / 2u) * line;
            std::snprintf(buffer, sizeof(buffer), "%.26s: %llu", counter.name.data(), static_cast<unsigned long long>(counter.value));
            draw->PushClipRect({ left, py }, { left + columnWidth, py + line }, true);
            text(left, py, white, buffer);
            draw->PopClipRect();
        }
        text(x + 12, noteY + counterHeight + 5, muted, "CPU and GPU overlap; do not add them together.");
        text(x + 12, noteY + counterHeight + 26, muted, "Nested scopes include children. Present includes VSync.");
        text(x + 12, noteY + counterHeight + 47, muted, "60 Hz sleep is intentional; waiting is not GPU execution.");
        if (snapshot_.overflow) text(x + 12, y + height - 19, amber, "Scope capacity reached; some measurements omitted.");
    }
    draw->PopClipRect();
    if (ownsUi_) {
        ImGui::Render();
        ID3D12DescriptorHeap* heaps[] = { srv_->GetDescriptorHeap() };
        list->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
    }
}
