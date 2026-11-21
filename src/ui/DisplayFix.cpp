#include "GdiText.h"
#include "Patch.h"
#include "DisplayFix.h"

static int MapPixelRect(int* l, int* t, int* w, int* h)
{
    int bx, gx, gy, cw, ch;
    if (!l || !t || !w || !h) return 0;
    bx = *(volatile int*)(size_t)0x87F8DCu;
    gx = *(volatile int*)(size_t)0x87F8E4u;
    gy = *(volatile int*)(size_t)0x87F8E8u;
    cw = *(volatile int*)(size_t)0x87F8ECu;
    ch = *(volatile int*)(size_t)0x87F8F0u;
    if (cw <= 0 || ch <= 0 || cw > 8192 || ch > 8192) return 0;
    if (gx < -4096 || gx > 4096 || gy < -4096 || gy > 4096) return 0;
    *l = 30 * (2 * gx - bx);
    *t = 15 * (bx + 2 * gy - 5);
    *w = 60 * cw;
    *h = 30 * ch + 135;
    return 1;
}

using ClampFn = unsigned char (__thiscall *)(void*, Point2D*);
static ClampFn g_ClampOriginal = NULL;

static unsigned char __thiscall ClampSmallMap(void* self, Point2D* center)
{
    int left, top, width, height;
    if (!center || !MapPixelRect(&left, &top, &width, &height))
        return g_ClampOriginal(self, center);
    const auto* view = (const volatile RectangleStruct*)ADDR_VIEWBOUNDS;
    const int viewW = view->Width;
    const int viewH = view->Height;
    if (viewW <= 16 || viewH <= 16 || (width >= viewW && height >= viewH))
        return g_ClampOriginal(self, center);

    const Point2D requested = *center;
    g_ClampOriginal(self, center);
    if (width < viewW) center->X = left + width / 2;
    if (height < viewH) center->Y = top + height / 2;
    return center->X != requested.X || center->Y != requested.Y;
}

void InstallSmallMapCenter()
{
    if (!GetPrivateProfileIntA("DisplayFix", "SmallMapCenter", 1, g_IniPath)) return;
    const unsigned at = 0x6D8640u;
    static const unsigned char expected[] = {0xA1, 0xE4, 0xF8, 0x87, 0x00};
    if (!PatchSame((const void*)at, expected, sizeof(expected)))
    {
        LogLine("selected: small map center bytes mismatch");
        return;
    }
    g_ClampOriginal = (ClampFn)PatchTrampAt(at, sizeof(expected), at + sizeof(expected));
    if (g_ClampOriginal && PatchAt(at, (void*)ClampSmallMap, sizeof(expected)))
        LogLine("selected: small map center hook ON");
    else
        LogLine("selected: small map center hook OFF");
}
