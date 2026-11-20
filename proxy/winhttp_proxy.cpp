// winhttp.dll proxy for Ori and the Will of the Wisps VR.
// UnityPlayer.dll imports winhttp.dll, so Windows loads this file from the game folder first.
// Every export jumps to the real System32 winhttp.dll (thunks.asm). Then:
//  1. chain-loads winhttp_chain.dll if present (another mod's winhttp.dll renamed, for example the randomizer's),
//  2. loads OriWotWVR\OriWotWVRNative.dll on a new thread (never LoadLibrary inside DllMain's loader lock).
// Idea from the WotW randomizer's InjectProxy (github.com/ori-community/wotw-rando-client, MIT); written fresh.
#include <windows.h>
#include <string>

extern "C" UINT64 g_real[];
static const char* kNames[] = {
#include "names.inc"
};

static std::wstring GameDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

static void Log(const char* msg) {
    std::wstring path = GameDir() + L"OriWotWVR_proxy.log";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t; GetLocalTime(&t);
    char line[600];
    int n = wsprintfA(line, "%02d:%02d:%02d.%03d %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
    DWORD w; WriteFile(h, line, n, &w, nullptr);
    CloseHandle(h);
}

static DWORD WINAPI LoadThread(void*) {
    std::wstring chain = GameDir() + L"winhttp_chain.dll";
    if (GetFileAttributesW(chain.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HMODULE c = LoadLibraryW(chain.c_str());
        Log(c ? "PROXY chain-loaded winhttp_chain.dll" : "PROXY winhttp_chain.dll found but failed to load");
    } else {
        Log("PROXY no winhttp_chain.dll (fine)");
    }
    std::wstring dir = GameDir() + L"OriWotWVR\\";
    SetDllDirectoryW(dir.c_str());
    HMODULE m = LoadLibraryW((dir + L"OriWotWVRNative.dll").c_str());
    char msg[200];
    wsprintfA(msg, m ? "PROXY loaded OriWotWVRNative.dll" : "PROXY OriWotWVRNative.dll failed to load, error %lu", GetLastError());
    Log(msg);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        HMODULE real = LoadLibraryW((std::wstring(sys) + L"\\winhttp.dll").c_str());
        int missing = 0;
        for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
            g_real[i] = real ? (UINT64)GetProcAddress(real, kNames[i]) : 0;
            if (!g_real[i]) missing++;
        }
        char msg[160];
        wsprintfA(msg, "PROXY winhttp proxy attached, real winhttp %s, %d of %d exports missing",
                  real ? "loaded" : "NOT loaded", missing, (int)(sizeof(kNames) / sizeof(kNames[0])));
        Log(msg);
        CloseHandle(CreateThread(nullptr, 0, LoadThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
