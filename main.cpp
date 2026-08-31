#include <Windows.h>
#include "GameApp.h"
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    D3DResourceLeakChecker leakCheck;
    CrashHandler::Install();

    GameApp app;
    return app.Run();
}
