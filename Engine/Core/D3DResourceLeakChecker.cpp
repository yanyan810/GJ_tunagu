#include "D3DResourceLeakChecker.h"

#ifdef _DEBUG
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")   // ← これ重要
#endif

D3DResourceLeakChecker::~D3DResourceLeakChecker() {
	#ifdef _DEBUG
			//リソースチェック
			Microsoft::WRL::ComPtr <IDXGIDebug1> debug = nullptr;
			//hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug));
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
				debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
				debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
				debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	
			}
	#endif
}
