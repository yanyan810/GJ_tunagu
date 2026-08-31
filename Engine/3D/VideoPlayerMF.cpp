#include "VideoPlayerMF.h"
#include <cassert>
#include <d3dx12.h>
#include "SrvManager.h"

#include <Windows.h>

#include <format>

static inline void HR_(HRESULT hr, const char* expr, const char* file, int line)
{
	if (FAILED(hr)) {
		char buf[512];
		sprintf_s(buf, "[VideoPlayerMF] HR FAILED: 0x%08X at %s(%d) : %s\n",
			(unsigned)hr, file, line, expr);
		OutputDebugStringA(buf);
		assert(false);
	}
}
#define HR(x) HR_((x), #x, __FILE__, __LINE__)

static void LogVideoTypes(IMFSourceReader* reader)
{
	OutputDebugStringA("==== [VideoPlayerMF] NativeMediaTypes ====\n");

	for (DWORD i = 0;; ++i) {
		Microsoft::WRL::ComPtr<IMFMediaType> mt;
		HRESULT hr = reader->GetNativeMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			i,
			&mt
		);
		if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS)) break;
		if (FAILED(hr)) break;

		GUID sub = {};
		UINT32 w = 0, h = 0;
		mt->GetGUID(MF_MT_SUBTYPE, &sub);
		MFGetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, &w, &h);

		LPOLESTR s = nullptr;
		StringFromCLSID(sub, &s);
		wchar_t buf[256];
		swprintf_s(buf, L"  [%u] subtype=%s size=%ux%u\n", i, s, w, h);
		OutputDebugStringW(buf);
		CoTaskMemFree(s);
	}

	OutputDebugStringA("========================================\n");
}


static std::wstring Utf8ToWide(const std::string& s) {
	if (s.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	if (len <= 0) return {};

	// len は終端 \0 を含む必要文字数
	std::wstring ws(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);

	// 終端の \0 を削る（std::wstring の長さを適正化）
	if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
	return ws;
}


bool VideoPlayerMF::Open(const std::string& pathUtf8, bool loop) {
	return Open(Utf8ToWide(pathUtf8), loop);
}



bool VideoPlayerMF::CreateDxResources(ID3D12Device* device, SrvManager* srv)
{
	if (!device || !srv) return false;
	if (width_ == 0 || height_ == 0) return false;

	const DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

	// ---- Texture (Default heap) ----
	CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width_, height_, 1, 1);
	CD3DX12_HEAP_PROPERTIES heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	HR(device->CreateCommittedResource(
		&heapDefault,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&videoTex_)
	));

	texInPSR_ = false;

	// ---- Upload buffer ----
	const UINT64 uploadSize = GetRequiredIntermediateSize(videoTex_.Get(), 0, 1);

	CD3DX12_HEAP_PROPERTIES heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

	HR(device->CreateCommittedResource(
		&heapUpload,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuf_)
	));

	// ---- SRV ----
	srvIndex_ = srv->Allocate();
	srv->CreateSRVTexture2D(srvIndex_, videoTex_.Get(), fmt, 1);
	srvGpu_ = srv->GetGPUDescriptionHandle(srvIndex_);

	gpuInitialized_ = true;
	return true;
}

void VideoPlayerMF::UploadToGpu(ID3D12GraphicsCommandList* cmd)
{
	if (!gpuInitialized_ || !cmd) return;

	if (!hasNewFrame_) {
		return;
	}

	if (!videoTex_ || !uploadBuf_) return;

	if (texInPSR_) {
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			videoTex_.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		cmd->ResourceBarrier(1, &barrier);
		texInPSR_ = false;
	}

	const int32_t strideAbs = (stride_ < 0) ? -stride_ : stride_;

	if (frame_.empty() || frame_.data() == nullptr) {
		OutputDebugStringA("[VideoPlayerMF] frame_ is empty\n");
		return;
	}
	if (width_ == 0 || height_ == 0 || strideAbs == 0) {
		OutputDebugStringA("[VideoPlayerMF] invalid w/h/stride\n");
		return;
	}

	// RGB32前提：最低でもこのサイズが必要
	const size_t need = (size_t)strideAbs * (size_t)height_;
	if (frame_.size() < need) {
		OutputDebugStringA("[VideoPlayerMF] frame_ too small\n");
		return;
	}

	D3D12_SUBRESOURCE_DATA sub{};
	sub.pData = frame_.data();
	sub.RowPitch = strideAbs;
	sub.SlicePitch = (LONG_PTR)strideAbs * (LONG_PTR)height_;


	UpdateSubresources(cmd, videoTex_.Get(), uploadBuf_.Get(), 0, 0, 1, &sub);

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			videoTex_.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		cmd->ResourceBarrier(1, &barrier);
	}
	texInPSR_ = true;

	hasNewFrame_ = false;
}

void VideoPlayerMF::EndFrame(ID3D12GraphicsCommandList* cmd)
{
	/* if (!gpuInitialized_ || !cmd) return;
	 if (!videoTex_) return;

	 if (texInPSR_) {
		 auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			 videoTex_.Get(),
			 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			 D3D12_RESOURCE_STATE_COPY_DEST
		 );
		 cmd->ResourceBarrier(1, &barrier);
		 texInPSR_ = false;
	 }*/
}

// 追加：Media Foundation 初期化（初回だけ）
static void EnsureMfStartupOnce()
{
	static bool s_started = false;
	if (!s_started) {
		HR(MFStartup(MF_VERSION));
		s_started = true;
	}
}

bool VideoPlayerMF::Open(const std::wstring& path, bool loop)
{
	DWORD attr = GetFileAttributesW(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
		OutputDebugStringA("[VideoPlayerMF] File not found.\n");
		return false;
	}


	Close(); // 既存を閉じる

	EnsureMfStartupOnce();

	loop_ = loop;
	duration100ns_ = 0;
	lastTs100ns_ = 0;
	hasNewFrame_ = false;

	// ソースリーダー作成（★VideoProcessing ON）
	Microsoft::WRL::ComPtr<IMFAttributes> attrs;
	HR(MFCreateAttributes(&attrs, 2));

	// 色変換/スケール等を許可（★これが超重要）
	HR(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));

	// 任意：HW変換許可（環境による）
	HR(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));

	HR(MFCreateSourceReaderFromURL(path.c_str(), attrs.Get(), &reader_));
	LogVideoTypes(reader_.Get());

	EnumerateAudioStreams_();

	// トラック0を初期選択（存在するなら）
	if (!audioStreamIds_.empty()) {
		audioTrackIndex_ = 0;
		audioStreamId_ = audioStreamIds_[0];
	}

	auto TrySetSubtype = [&](const GUID& subtype) -> HRESULT {
		Microsoft::WRL::ComPtr<IMFMediaType> outType;
		HR(MFCreateMediaType(&outType));
		HR(outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
		HR(outType->SetGUID(MF_MT_SUBTYPE, subtype));
		return reader_->SetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outType.Get());
		};

	HRESULT hrSet = TrySetSubtype(MFVideoFormat_ARGB32);
	if (FAILED(hrSet)) {
		hrSet = TrySetSubtype(MFVideoFormat_RGB32);
	}
	if (FAILED(hrSet)) {
		char buf[256];
		sprintf_s(buf, "[VideoPlayerMF] SetCurrentMediaType failed: 0x%08X (native)\n", (unsigned)hrSet);
		OutputDebugStringA(buf);
	}


	// まず全部OFFにして、必要なものだけON（安定）
	HR(reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE));

	// Video は必ずON
	HR(reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));

	// Audio は「存在するなら」ON
	const bool hasAudio = !audioStreamIds_.empty();
	if (hasAudio) {
		audioTrackIndex_ = 0;                 // ★ 0 が正しい（UIで +1 表示）
		audioStreamId_ = audioStreamIds_[0];

		HR(reader_->SetStreamSelection(audioStreamId_, TRUE));
	}
	else {
		OutputDebugStringA("[VideoPlayerMF] No audio stream found. audio disabled.\n");
		audioStreamId_ = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM; // ダミー値（使わない）
	}


	if (hasAudio) {

		// 出力音声形式を PCM に
		Microsoft::WRL::ComPtr<IMFMediaType> outAudio;
		HR(MFCreateMediaType(&outAudio));
		HR(outAudio->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		HR(outAudio->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
		HR(reader_->SetCurrentMediaType(audioStreamId_, nullptr, outAudio.Get()));

		Microsoft::WRL::ComPtr<IMFMediaType> curAudio;
		HR(reader_->GetCurrentMediaType(audioStreamId_, &curAudio));

		WAVEFORMATEX* wfx = nullptr;
		UINT32 wfxSize = 0;
		HR(MFCreateWaveFormatExFromMFMediaType(curAudio.Get(), &wfx, &wfxSize, 0));

		// XAudio2 init
		{
			if (source_) { source_->DestroyVoice(); source_ = nullptr; }
			if (master_) { master_->DestroyVoice(); master_ = nullptr; }
			xa_.Reset();

			HR(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR));
			HR(xa_->CreateMasteringVoice(&master_));
			HR(xa_->CreateSourceVoice(&source_, wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &voiceCb_));
			HR(source_->Start(0));
			source_->SetVolume(volume_);
		}

		CoTaskMemFree(wfx);

	}
	else {
		// 音声無し：XAudio2作らない（または既存を止める）
		if (source_) { source_->DestroyVoice(); source_ = nullptr; }
		if (master_) { master_->DestroyVoice(); master_ = nullptr; }
		xa_.Reset();
	}


	// 実際に設定されたメディアタイプから width/height を取得
	Microsoft::WRL::ComPtr<IMFMediaType> curType;
	HR(reader_->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &curType));

	GUID sub = {};
	HRESULT hrSub = curType->GetGUID(MF_MT_SUBTYPE, &sub);

	wchar_t gbuf[64]{};
	if (SUCCEEDED(hrSub)) {
		StringFromGUID2(sub, gbuf, _countof(gbuf));
		OutputDebugStringW((std::wstring(L"[VideoPlayerMF] Current subtype = ") + gbuf + L"\n").c_str());
	}
	else {
		wchar_t msg[256]{};
		swprintf_s(msg, L"[VideoPlayerMF] Current subtype NOT FOUND. hr=0x%08X\n", (unsigned)hrSub);
		OutputDebugStringW(msg);
	}



	UINT32 w = 0, h = 0;
	HR(MFGetAttributeSize(curType.Get(), MF_MT_FRAME_SIZE, &w, &h));
	width_ = w;
	height_ = h;

	// ストライド取得（無ければ width*4）
	stride_ = (int32_t)MFGetAttributeUINT32(
		curType.Get(),
		MF_MT_DEFAULT_STRIDE,
		width_ * 4
	);

	// duration（取れれば）
	PROPVARIANT var{};
	PropVariantInit(&var);
	if (SUCCEEDED(reader_->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))) {
		if (var.vt == VT_UI8) duration100ns_ = (LONGLONG)var.uhVal.QuadPart;
		else if (var.vt == VT_I8) duration100ns_ = (LONGLONG)var.hVal.QuadPart;
	}
	PropVariantClear(&var);

	// フレームバッファ確保
	const int32_t strideAbs = (stride_ < 0) ? -stride_ : stride_;
	frame_.resize((size_t)strideAbs * (size_t)height_);


	return true;
}

void VideoPlayerMF::Close()
{
	if (source_) {
		source_->Stop(0);
		source_->FlushSourceBuffers();
	}

	if (reader_) {
		reader_->Flush((DWORD)MF_SOURCE_READER_ALL_STREAMS);
	}

	reader_.Reset();

	width_ = height_ = 0;
	stride_ = 0;

	frame_.clear();
	lastTs100ns_ = 0;
	duration100ns_ = 0;
	hasNewFrame_ = false;

	// DX12 resources は必要に応じて解放
	videoTex_.Reset();
	uploadBuf_.Reset();
	srvIndex_ = 0;
	srvGpu_ = {};
	gpuInitialized_ = false;
	texInPSR_ = false;
	if (source_) { source_->DestroyVoice(); source_ = nullptr; }
	if (master_) { master_->DestroyVoice(); master_ = nullptr; }
	xa_.Reset();
	audioEos_ = false;

}

bool VideoPlayerMF::ReadNextFrame()
{
	if (!reader_) return false;

	// ★1回呼ばれたら、videoが取れるまで最大N回読み進める
	//   (audioが続くことがあるため)
	const int kMaxReadsPerCall = 32;

	for (int n = 0; n < kMaxReadsPerCall; ++n) {

		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG ts = 0;
		Microsoft::WRL::ComPtr<IMFSample> sample;

		HRESULT hr = reader_->ReadSample(
			(DWORD)MF_SOURCE_READER_ANY_STREAM,
			0,
			&streamIndex,
			&flags,
			&ts,
			&sample
		);

		if (FAILED(hr)) {
			char msg[256]{};
			sprintf_s(msg, "[VideoPlayerMF] ReadSample failed hr=0x%08X\n", (unsigned)hr);
			OutputDebugStringA(msg);
			return false;
		}

		// EOS
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			if (loop_) {
				PROPVARIANT pv{};
				PropVariantInit(&pv);
				pv.vt = VT_I8;
				pv.hVal.QuadPart = 0;
				HR(reader_->SetCurrentPosition(GUID_NULL, pv));
				PropVariantClear(&pv);

				// ループ開始時はストリームをフラッシュ
				reader_->Flush((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM);
				reader_->Flush((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM);

				audioEos_ = false;
				continue; // ★読み直す
			}
			return false;
		}

		if (!sample) {
			// データなしでも続行
			continue;
		}

		// =========================
		// Audio
		// =========================
		if (streamIndex == (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM) {

			if (!source_) continue; // 音声未初期化なら読み捨て

			Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
			HR(sample->ConvertToContiguousBuffer(&buf));

			BYTE* data = nullptr;
			DWORD maxLen = 0;
			DWORD curLen = 0;
			HR(buf->Lock(&data, &maxLen, &curLen));

			if (curLen > 0) {
				// ★再生完了まで保持が必要なのでheap確保
				uint8_t* heap = new uint8_t[curLen];
				memcpy(heap, data, curLen);

				XAUDIO2_BUFFER xb{};
				xb.AudioBytes = curLen;
				xb.pAudioData = heap;
				xb.pContext = heap; // OnBufferEndでdelete[]する

				// キューが詰まり過ぎると遅延するので、適度に制限する（任意）
				XAUDIO2_VOICE_STATE st{};
				source_->GetState(&st);
				if (st.BuffersQueued < 8) {
					HR(source_->SubmitSourceBuffer(&xb));
				}
				else {
					delete[] heap; // 送らないなら解放
				}
			}

			buf->Unlock();

			// ★audioだけだったら、videoを探して読み進める
			continue;
		}

		// =========================
		// Video
		// =========================
		if (streamIndex == (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM) {

			// 現在の出力subtype確認（RGB32/ARGB32以外はスキップ）
			Microsoft::WRL::ComPtr<IMFMediaType> curType;
			HR(reader_->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &curType));

			GUID sub{};
			HRESULT hrSub = curType->GetGUID(MF_MT_SUBTYPE, &sub);
			if (FAILED(hrSub)) {
				char msg[256]{};
				sprintf_s(msg, "[VideoPlayerMF] GetGUID(MF_MT_SUBTYPE) failed hr=0x%08X\n", (unsigned)hrSub);
				OutputDebugStringA(msg);
				continue;
			}

			const bool isRGB32 = (sub == MFVideoFormat_RGB32) || (sub == MFVideoFormat_ARGB32);
			if (!isRGB32) {
				OutputDebugStringA("[VideoPlayerMF] video subtype is not RGB32/ARGB32 -> skip\n");
				continue;
			}

			Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
			HR(sample->ConvertToContiguousBuffer(&buf));

			BYTE* data = nullptr;
			DWORD maxLen = 0;
			DWORD curLen = 0;
			HR(buf->Lock(&data, &maxLen, &curLen));

			const int32_t strideAbs = (stride_ < 0) ? -stride_ : stride_;
			const size_t need = (size_t)strideAbs * (size_t)height_;

			if (need > 0 && (size_t)curLen >= need) {

				if (frame_.size() < need) frame_.resize(need);
				memcpy(frame_.data(), data, need);

				// RGB32 はαが未定義になりがちなので埋める
				for (size_t y = 0; y < (size_t)height_; ++y) {
					uint8_t* row = frame_.data() + y * (size_t)strideAbs;
					for (size_t x = 0; x < (size_t)width_; ++x) {
						row[x * 4 + 3] = 0xFF;
					}
				}

				lastTs100ns_ = ts;
				hasNewFrame_ = true;

				buf->Unlock();
				return true; // ★videoを取れたのでここで終了
			}

			buf->Unlock();
			continue;
		}

		// その他のストリームは無視
		continue;
	}

	// この呼び出しでは video が来なかった
	// audio は積めてる可能性があるので true 返して継続
	return true;
}

void VideoPlayerMF::PumpAudio()
{
	if (!reader_ || !source_) return;

	// キューが溜まってるなら読まない（これで軽くなる）
	XAUDIO2_VOICE_STATE st{};
	source_->GetState(&st);
	if (st.BuffersQueued >= 6) return; // 6くらいで十分

	// 足りない分だけ読む
	for (int i = 0; i < 4; ++i) {

		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG ts = 0;
		Microsoft::WRL::ComPtr<IMFSample> sample;

		HRESULT hr = reader_->ReadSample(
			audioStreamId_,
			0,
			&streamIndex,
			&flags,
			&ts,
			&sample
		);

		if (FAILED(hr)) return;

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			if (loop_) {
				// 音はループ開始でFlushしておけばOK（映像側で巻き戻すならここは何もしなくてもOK）
				return;
			}
			return;
		}

		if (!sample) continue;

		Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
		if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;

		BYTE* data = nullptr;
		DWORD maxLen = 0, curLen = 0;
		if (FAILED(buf->Lock(&data, &maxLen, &curLen))) continue;

		if (curLen > 0) {
			uint8_t* heap = new uint8_t[curLen];
			memcpy(heap, data, curLen);

			XAUDIO2_BUFFER xb{};
			xb.AudioBytes = curLen;
			xb.pAudioData = heap;
			xb.pContext = heap; // OnBufferEndでdelete[]する

			// Submit
			if (SUCCEEDED(source_->SubmitSourceBuffer(&xb))) {
				// OK
			}
			else {
				delete[] heap;
			}
		}

		buf->Unlock();

		// もう十分溜まったら抜ける
		source_->GetState(&st);
		if (st.BuffersQueued >= 6) break;
	}
}

bool VideoPlayerMF::ReadNextVideoFrame()
{
	if (!reader_) return false;

	DWORD streamIndex = 0;
	DWORD flags = 0;
	LONGLONG ts = 0;
	Microsoft::WRL::ComPtr<IMFSample> sample;

	HRESULT hr = reader_->ReadSample(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0,
		&streamIndex,
		&flags,
		&ts,
		&sample
	);

	if (FAILED(hr)) return false;

	if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
		if (loop_) {
			PROPVARIANT pv{};
			PropVariantInit(&pv);
			pv.vt = VT_I8;
			pv.hVal.QuadPart = 0;
			HR(reader_->SetCurrentPosition(GUID_NULL, pv));
			PropVariantClear(&pv);

			// ループ開始時は両方Flush（重要）
			reader_->Flush((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM);
			reader_->Flush((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM);

			audioEos_ = false;

			lastTs100ns_ = 0;      // ★追加
			hasNewFrame_ = false;  // 任意

			return true; // 次フレーム以降また読ませる
		}
		return false;
	}

	if (!sample) return true;

	Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
	HR(sample->ConvertToContiguousBuffer(&buf));

	BYTE* data = nullptr;
	DWORD maxLen = 0, curLen = 0;
	HR(buf->Lock(&data, &maxLen, &curLen));

	const int32_t strideAbs = (stride_ < 0) ? -stride_ : stride_;
	const size_t need = (size_t)strideAbs * (size_t)height_;

	if (need > 0 && (size_t)curLen >= need) {
		if (frame_.size() < need) frame_.resize(need);
		memcpy(frame_.data(), data, need);

		// RGB32のα埋め
		for (size_t y = 0; y < (size_t)height_; ++y) {
			uint8_t* row = frame_.data() + y * (size_t)strideAbs;
			for (size_t x = 0; x < (size_t)width_; ++x) {
				row[x * 4 + 3] = 0xFF;
			}
		}

		lastTs100ns_ = ts;
		hasNewFrame_ = true;
	}

	buf->Unlock();
	return true;
}

void VideoPlayerMF::SetVolume(float volume)
{
	// 安全にクランプ（0〜2くらいで）
	if (volume < 0.0f) volume = 0.0f;
	if (volume > 2.0f) volume = 2.0f;

	volume_ = volume;

	if (source_) {
		source_->SetVolume(volume_);
	}
}

void VideoPlayerMF::EnumerateAudioStreams_()
{
	audioStreamIds_.clear();
	if (!reader_) return;

	for (DWORD s = 0; ; ++s) {
		Microsoft::WRL::ComPtr<IMFMediaType> mt;
		HRESULT hr = reader_->GetNativeMediaType(s, 0, &mt);

		if (hr == MF_E_INVALIDSTREAMNUMBER) break;
		if (FAILED(hr) || !mt) continue;

		GUID major{};
		if (SUCCEEDED(mt->GetGUID(MF_MT_MAJOR_TYPE, &major))) {
			if (major == MFMediaType_Audio) {
				audioStreamIds_.push_back(s);
			}
		}
	}

	char buf[128]{};
	sprintf_s(buf, "[VideoPlayerMF] audio tracks = %d\n", (int)audioStreamIds_.size());
	OutputDebugStringA(buf);
}

bool VideoPlayerMF::SetupAudioForStream_(DWORD streamId)
{
	if (!reader_) return false;

	// PCM に
	Microsoft::WRL::ComPtr<IMFMediaType> outAudio;
	HR(MFCreateMediaType(&outAudio));
	HR(outAudio->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HR(outAudio->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
	HR(reader_->SetCurrentMediaType(streamId, nullptr, outAudio.Get()));

	Microsoft::WRL::ComPtr<IMFMediaType> curAudio;
	HR(reader_->GetCurrentMediaType(streamId, &curAudio));

	WAVEFORMATEX* wfx = nullptr;
	UINT32 wfxSize = 0;
	HR(MFCreateWaveFormatExFromMFMediaType(curAudio.Get(), &wfx, &wfxSize, 0));

	// XAudio2 rebuild（フォーマット違い対策）
	if (source_) { source_->DestroyVoice(); source_ = nullptr; }
	if (master_) { master_->DestroyVoice(); master_ = nullptr; }
	xa_.Reset();

	HR(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR));
	HR(xa_->CreateMasteringVoice(&master_));
	HR(xa_->CreateSourceVoice(&source_, wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &voiceCb_));
	HR(source_->Start(0));
	source_->SetVolume(volume_);

	CoTaskMemFree(wfx);
	return true;
}

bool VideoPlayerMF::SetAudioTrack(int trackIndex)
{
	if (!reader_) return false;
	if (trackIndex < 0 || trackIndex >= (int)audioStreamIds_.size()) return false;

	DWORD newStreamId = audioStreamIds_[trackIndex];
	if (newStreamId == audioStreamId_) return true;

	// いったん停止＆キュークリア（音混ざり防止）
	if (source_) {
		source_->Stop();
		source_->FlushSourceBuffers();
	}

	// すべての音声ストリームを無効 → 選択だけ有効
	for (DWORD id : audioStreamIds_) {
		reader_->SetStreamSelection(id, FALSE);
	}
	HR(reader_->SetStreamSelection(newStreamId, TRUE));

	// reader 側のそのストリームを flush
	reader_->Flush(newStreamId);

	// XAudio2側を作り直す（フォーマット違いの可能性）
	if (!SetupAudioForStream_(newStreamId)) return false;

	audioTrackIndex_ = trackIndex;
	audioStreamId_ = newStreamId;

	return true;
}
