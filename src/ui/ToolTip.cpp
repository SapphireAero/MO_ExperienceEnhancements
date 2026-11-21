#include <windows.h>
#include "GdiText.h"
#include "SA_EE.h"

static void TT_AlignSlaveCopy(void* surf, unsigned esp)
{
    void* comp = *(void**)ADDR_SURF_COMPOSITE;
    int* pr;
    int* pt;
    int cw, d;
    if (!comp || !IsPlausiblePtr(comp) || surf != comp) return;
    cw = GtSurfGet(comp, GtVT_GET_WIDTH);
    pr = (int*)(esp + 0x34u);   /* X,Y,W,H */
    if (cw <= 0 || !IsPlausiblePtr(pr) || pr[2] <= 0 || pr[2] > 2048) return;
    if (pr[0] + pr[2] <= cw) return;
    pt = (int*)(esp + 0x24u);   /* L,T,R,B */
    d = (cw - pr[2]) - pr[0];
    pr[0] += d;
    if (IsPlausiblePtr(pt))
    {
        pt[0] += d;
        pt[2] += d;
    }
}

extern "C" __declspec(dllexport) DWORD __cdecl ToolTip_Clamp(void* regs)
{
    unsigned esp;
    void* surf;
    void* comp;
    int* pr;
    int* pt;
    int minX, maxX, dx, w;
    static int enabled = -1;
    if (enabled < 0)
        enabled = GetPrivateProfileIntA("DisplayFix", "TooltipClamp", 1, g_IniPath) != 0;
    if (!regs || !enabled) return 0;
    esp = *(unsigned*)((char*)regs + 0x14u);
    surf = *(void**)((char*)regs + REG_OFF_ESI);
    if (!esp || !surf) return 0;
    if (MOE_PhobosLoaded())
    {
        TT_AlignSlaveCopy(surf, esp);
        return 0;
    }
    comp = *(void**)ADDR_SURF_COMPOSITE;
    if (!comp || !IsPlausiblePtr(comp) || surf != comp) return 0;
    pr = (int*)(esp + 0x34u);   /* X,Y,W,H */
    pt = (int*)(esp + 0x24u);   /* L,T,R,B */
    if (!IsPlausiblePtr(pr) || !IsPlausiblePtr(pt)) return 0;
    w = pr[2];
    if (w <= 0 || w > 2048) return 0;
    if (!IsPlausiblePtr((void*)ADDR_VIEWBOUNDS)) return 0;
    minX = *(int*)ADDR_VIEWBOUNDS;
    maxX = minX + *(int*)(ADDR_VIEWBOUNDS + 8u);
    if (maxX <= minX + 16 || maxX - minX < w + 16) return 0;
    dx = 0;
    if (pr[0] + w > maxX) dx = maxX - (pr[0] + w);
    if (pr[0] + dx < minX) dx = minX - pr[0];
    if (!dx) return 0;
    pr[0] += dx;
    pt[0] += dx;
    pt[2] += dx;
    return 0;
}
