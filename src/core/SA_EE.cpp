#include <windows.h>
#include <tlhelp32.h>
#include "SA_EE.h"
#include "Version.h"
#include "GdiText.h"
#include "Patch.h"
#include "AirWay.h"
#include "OrderThrottle.h"
#include "WaypointCap.h"
#include "MegaListCap.h"
#include "SelPoolCap.h"
#include "BuildQueue.h"
#include "RepairAll.h"
#include "PlaceScatter.h"
#include "TreeChop.h"
#include "SmartArea.h"
#include "Aggressive.h"
#include "ScenarioLighting.h"
#include "FrameCap.h"
#include "DisplayFix.h"
#include "PowerDisplay.h"
#include "ChatEdit.h"
#include "WeaponRange.h"
#include "SelectedInfo.h"
#include "Observer.h"
#include "BeaconArrows.h"
#include "BeaconReplace.h"

static int g_HostChecked = 0;
static int g_HostOk = 0;

static int MOE_HostIsGame(void)
{
    /* 名字 + 长度 (不用 strlen 习语: -nostdlib 下 GCC 会把它变成 strlen 调用)。 */
    static const struct { const char* name; DWORD len; } kWant[] =
    {
        { "gamemd.exe",       10 },
        { "gamemd-spawn.exe", 16 },
    };
    char pb[260];
    DWORD n, i, base, blen, k, w;
    if (g_HostChecked) return g_HostOk;
    g_HostChecked = 1;
    g_HostOk = 0;
    n = GetModuleFileNameA(NULL, pb, (DWORD)sizeof(pb));
    if (n == 0 || n >= (DWORD)sizeof(pb)) return 0;
    pb[n] = 0;
    i = n;
    while (i > 0 && pb[i - 1] != '\\' && pb[i - 1] != '/' && pb[i - 1] != ':') i--;
    base = i;
    blen = n - base;
    for (w = 0; w < (DWORD)(sizeof(kWant) / sizeof(kWant[0])); w++)
    {
        if (blen != kWant[w].len) continue;
        for (k = 0; k < kWant[w].len; k++)
        {
            char a = pb[base + k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (a != kWant[w].name[k]) break;
        }
        if (k == kWant[w].len) { g_HostOk = 1; return 1; }
    }
    return 0;
}

/* host 一行 (start 之前打, 双进程各留一行, 下次直接按 pid 区分):
 * SA_EE: host Syringe.exe pid=2668 not gamemd, skipped
 * SA_EE: host gamemd.exe pid=12976 (只记文件名, 路径太长不记)。 */
static void MOE_LogHost(void)
{
    char pb[260], buf[512], b1[16];
    unsigned p = 0, i;
    DWORD n = GetModuleFileNameA(NULL, pb, (DWORD)sizeof(pb));
    if (n == 0 || n >= (DWORD)sizeof(pb)) pb[0] = 0;
    else pb[n] = 0;
    i = n;
    while (i > 0 && pb[i - 1] != '\\' && pb[i - 1] != '/' && pb[i - 1] != ':') i--;
    p = AppendCStr(buf, p, "SA_EE: host ", sizeof(buf));
    p = AppendCStr(buf, p, pb + i, sizeof(buf));
    p = AppendCStr(buf, p, " pid=", sizeof(buf));
    UtoA((unsigned)GetCurrentProcessId(), b1);
    p = AppendCStr(buf, p, b1, sizeof(buf));
    if (!MOE_HostIsGame())
        p = AppendCStr(buf, p, " not gamemd, skipped", sizeof(buf));
    LogLine(buf);
}

/* 日志打开 + 启动轮转 ([Log] MaxKB, 默认 500, <=0 不限)。
 * 幂等, 每进程只执行一次。超限先截断 (占着也能清), 不行再删, 都不行就续写。 */
#define MOE_LOG_MAXKB_DEF 500
#define MOE_LOG_MAXKB_MAX 1048576u

void GtEnsureLog(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    int maxKB, rotated = 0;
    unsigned limit;
    if (g_Log) return;
    BuildPaths();
    maxKB = GetPrivateProfileIntA("Log", "MaxKB", MOE_LOG_MAXKB_DEF, g_IniPath);
    if (maxKB <= 0) limit = 0;
    else
    {
        if ((unsigned)maxKB > MOE_LOG_MAXKB_MAX) maxKB = (int)MOE_LOG_MAXKB_MAX;
        limit = (unsigned)maxKB * 1024u;
    }
    if (limit > 0 &&
        GetFileAttributesExA(g_LogPath, GetFileExInfoStandard, &fa) &&
        !(fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        (fa.nFileSizeHigh > 0 || fa.nFileSizeLow > limit))
    {
        HANDLE h = CreateFileA(g_LogPath, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); rotated = 1; }
        else if (DeleteFileA(g_LogPath)) rotated = 1;
    }
    g_Log = CreateFileA(g_LogPath, FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_Log == INVALID_HANDLE_VALUE) g_Log = NULL;
    if (rotated && g_Log)
    {
        char rb[64], n1[16];
        unsigned q = AppendCStr(rb, 0, "log: cleared (exceeded ", sizeof(rb));
        UtoA((unsigned)maxKB, n1);
        q = AppendCStr(rb, q, n1, sizeof(rb));
        AppendCStr(rb, q, "KB)", sizeof(rb));
        LogLine(rb);
    }
}


int MOE_PhobosLoaded(void)
{
    // The first callers run in the game after Syringe loads the extensions.
    static int found = -1;
    if (found >= 0) return found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
    {
        found = 0;
        static const wchar_t name[] = L"phobos";
        do
        {
            for (const wchar_t* p = entry.szModule; *p && !found; ++p)
            {
                unsigned i = 0;
                while (name[i] && p[i])
                {
                    wchar_t ch = p[i];
                    if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
                    if (ch != name[i]) break;
                    ++i;
                }
                if (!name[i]) found = 1;
            }
        } while (!found && Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found > 0;
}

static void PollSelectedFeatures()
{
    static int active = 0;
    static int lastFrame = -1;
    static unsigned lastMs = 0;
    if (active) return;
    const int frame = *(volatile int*)(size_t)ADDR_FRAME_NO;
    const unsigned now = GetTickCount();
    if (lastFrame == frame && now - lastMs < 16u) return;
    lastFrame = frame;
    lastMs = now;
    active = 1;
    ChatEdit_Install();
    Observer_Poll();
    BeaconReplace_Poll();
    AirWay_Poll();
    BuildQueue_Poll();
    TreeChop_Poll();
    SmartArea_Poll();
    OrderThrottle_Poll();
    WpCap_Poll();
    MlCap_Poll();
    SelPool_Poll();
    RepairAll_Poll();
    PlaceScatter_Poll();
    Aggressive_Poll();
    Lighting_Poll();
    active = 0;
}

static void* const g_Poll = (void*)PollSelectedFeatures;
static void* g_PrepareTramp = NULL;

__attribute__((naked)) static void PrepareEntry()
{
    __asm__ volatile (
        "pushal\n\t"
        "pushfl\n\t"
        "call *%0\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *%1\n\t"
        :
        : "m" (g_Poll), "m" (g_PrepareTramp)
        : "memory", "cc");
}

static void InstallPrepare()
{
    const unsigned at = 0x4F42F0u;
    static const unsigned char expected[] = {0xA1, 0x24, 0x73, 0x88, 0x00};
    if (!PatchSame((const void*)at, expected, sizeof(expected)))
    {
        LogLine("selected: prepare bytes mismatch");
        return;
    }
    g_PrepareTramp = PatchTrampAt(at, sizeof(expected), at + sizeof(expected));
    if (g_PrepareTramp && PatchAt(at, (void*)PrepareEntry, sizeof(expected)))
        LogLine("selected: prepare hook ON");
    else
        LogLine("selected: prepare hook OFF");
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_MessageDrawHook(void*)
{
    void* surf = *(void**)ADDR_SURF_COMPOSITE;
    if (!surf || !IsPlausiblePtr(surf)) return 0;
    if (!g_Calibrated) Calibrate();
    const int w = GtSurfGet(surf, GtVT_GET_WIDTH);
    const int h = GtSurfGet(surf, GtVT_GET_HEIGHT);
    const int pitch = GtSurfGet(surf, GtVT_GET_PITCH);
    if (w <= 0 || h <= 0 || pitch <= 0) return 0;
    GtSetGameSize(w, h);
    SmartArea_Draw(surf, w, h, pitch);
    TreeChop_Draw(surf, w, h, pitch);
    Aggressive_Draw(surf, w, h, pitch);
    if (pitch >= w * 2 && *(volatile unsigned char*)ADDR_SCEN_STARTED)
    {
        if (!*(volatile unsigned char*)(ADDR_MSGLIST + OFF_MSGLIST_EDIT))
            WeaponRange_Draw(surf, w, h, pitch);
        BeaconArrows_Draw(surf, w, h, pitch);
        SelectedInfo_Draw(surf, w, h, pitch);
    }
    Perf_Frame();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_MsgListEntry(void* regs)
{
    void* self = regs ? *(void**)((char*)regs + REG_OFF_ECX) : NULL;
    if (self && IsPlausiblePtr(self) && *(void**)self) return 0;
    return Selected_MessageDrawHook(NULL);
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_MainGate(void*)
{
    FrameCap_Gate();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_ScenarioReady(void*)
{
    Lighting_OnScenarioReady();
    BeaconReplace_Reset();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_PowerSidebar(void*)
{
    PowerDisplay_DrawSb();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl Selected_EscOnShow(void* regs)
{
    if (regs)
    {
        HWND dlg = *(HWND*)((char*)regs + REG_OFF_ESI);
        if (dlg) FrameCap_SetActiveDlg(dlg);
    }
    return 0;
}

extern "C" __declspec(dllexport) BOOL __stdcall DllMain(HINSTANCE dll, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(dll);
    GtEnsureLog();
    MOE_LogHost();
    if (!MOE_HostIsGame()) return TRUE;
    LogLine("SA_EE " MOE_VERSION " start");
    Perf_EnsureInit();
    FrameCap_EnsureInit();
    AirWay_Install();
    InstallPrepare();
    InstallSmallMapCenter();
    return TRUE;
}
