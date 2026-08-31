#include "CrashHandler.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <strsafe.h>

#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
    SYSTEMTIME time;
    GetLocalTime(&time);

    CreateDirectory(L"./Dumps", nullptr);

    wchar_t filePath[MAX_PATH]{};
    StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, 0, nullptr);

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = TRUE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
        hFile, MiniDumpNormal, &info, nullptr, nullptr);

    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::Install() {
    SetUnhandledExceptionFilter(ExportDump);
}
