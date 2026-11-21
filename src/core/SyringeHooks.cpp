/* Syringe reads the hooks from this DLL's .syhks00 section. */
#include <windows.h>
#include "Version.h"

struct SyHookDecl
{
    unsigned int addr;
    unsigned int size;
    const char* name;
    unsigned int pad;
};

#define SYHOOK(a, s, n) { (a), (s), (n), 0u }

static SyHookDecl g_syHooks[] __attribute__((section(".syhks00"), used, aligned(16))) =
{
    SYHOOK(0x5D49B4u, 5u, "ChatEdit_HideNative"),
    SYHOOK(0x55E477u, 5u, "ChatEdit_ChatSp1"),
    SYHOOK(0x55E62Fu, 6u, "ChatEdit_ChatSp2"),
    SYHOOK(0x55E693u, 6u, "ChatEdit_ChatSp3"),
    SYHOOK(0x55E746u, 5u, "ChatEdit_ChatSp4"),
    SYHOOK(0x6A70E0u, 6u, "Selected_PowerSidebar"),
    SYHOOK(0x5D49A0u, 6u, "Selected_MsgListEntry"),
    SYHOOK(0x5D4A94u, 7u, "Selected_MessageDrawHook"),
    SYHOOK(0x732A85u, 7u, "TypeSelect_SetCtx"),
    SYHOOK(0x732B28u, 6u, "TypeSelect_SetCtx"),
    SYHOOK(0x465D40u, 6u, "TypeSelect_BuildingGate"),
    SYHOOK(0x732C20u, 8u, "TypeSelect_Clear"),
    SYHOOK(0x6AB689u, 5u, "BQ_QueueClick"),
    SYHOOK(0x6AB0C1u, 6u, "BQ_PausePeel"),
    SYHOOK(0x6AB197u, 8u, "BQ_CancelPeel"),
    SYHOOK(0x6AAE44u, 6u, "BQ_DarkDispatchPeel"),
    SYHOOK(0x6AB6CEu, 10u, "BQ_FreeShift"),
    SYHOOK(0x6A9C86u, 5u, "BQ_ShowCount"),
    SYHOOK(0x6A9C6Cu, 6u, "BQ_DrawNullFac"),
    SYHOOK(0x478FDCu, 5u, "ToolTip_Clamp"),
    SYHOOK(0x536FA0u, 7u, "RepairAll_HotKey"),
    SYHOOK(0x6A78D9u, 6u, "RepairAll_Button"),
    SYHOOK(0x55D360u, 5u, "Selected_MainGate"),
    SYHOOK(0x4D77BDu, 6u, "NoApproach_Hook"),
    SYHOOK(0x4F1140u, 5u, "Selected_EscOnShow"),
    SYHOOK(0x6D528Au, 6u, "PlacementPreview_DrawHook"),
    SYHOOK(0x47F939u, 6u, "OreLight_Slope"),
    SYHOOK(0x47FA3Du, 5u, "OreLight_Flat"),
    SYHOOK(0x53ADDBu, 5u, "LightConv_Recalc"),
    SYHOOK(0x55B4C6u, 5u, "LightConv_Step"),
    SYHOOK(0x67E6C4u, 6u, "Selected_ScenarioReady"), // LoadGame sets ScenarioStarted.
    SYHOOK(0x683E75u, 7u, "Selected_ScenarioReady"), // A new scenario sets it as well.
};

struct SyringeHandshakeInfo
{
    int cbSize;
    int num_hooks;
    unsigned int checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned int exeCRC;
    int cchMessage;
    char* Message;
};

extern "C" __declspec(dllexport) HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* pInfo)
{
    /* "SA_EE " + 版本 + " (embedded hooks)" 手工拼接 (无 CRT)。 */
    static const char kHead[] = "SA_EE ";
    static const char kTail[] = " (embedded hooks, no .inj needed)";
    const char* ver = MOE_VERSION;
    int i = 0, k;
    if (!pInfo || pInfo->cbSize < (int)sizeof(SyringeHandshakeInfo))
        return E_FAIL;
    if (pInfo->Message && pInfo->cchMessage > 0)
    {
        for (k = 0; kHead[k] && i < pInfo->cchMessage - 1; k++)
            pInfo->Message[i++] = kHead[k];
        for (k = 0; ver[k] && i < pInfo->cchMessage - 1; k++)
            pInfo->Message[i++] = ver[k];
        for (k = 0; kTail[k] && i < pInfo->cchMessage - 1; k++)
            pInfo->Message[i++] = kTail[k];
        pInfo->Message[i] = '\0';
    }
    return S_OK;
}
