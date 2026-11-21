/* PlacementPreview.cpp - 建筑放置半透明预览 (移植 Phobos PlacementPreview)。
 *
 * 机制 (references/Phobos/src/Ext/BuildingType/Hooks.cpp:68):
 *   引擎 TacticalClass::DrawPlacement 画完绿/红地基格后 (0x6D528A,
 *   6 字节 = mov ecx,[0x880990]) 追加一次静态 SHP 半透明 blit。
 *   原版正常对局只画地基格, 不画建筑影像, 因此这里只是额外补一层预览。
 *
 * 数据全部只读:
 *   - DisplayClass (0x87F7E8) +0x1174/0x1178 = 当前地基中心格/左上偏移
 *   - +0x11A4/+0x11A8 = CurrentBuilding / CurrentBuildingType (本地幽灵对象)
 *   - DisplayClass::GetFloorHeight + TacticalClass::CoordsToClient 只做投影
 *   - BuildingTypeClass vtable 槽 39 = GetImage, 48 = LoadBuildup
 *     (vtable 0x7E4570 实测; LoadBuildup 内部按需加载 Buildup, 不做开局预载)
 *   - 绘制走引擎 CC_Draw_Shape (0x4AED70) 到 DSurface::Temp (0x887314)
 * 不写任何模拟状态; 每个客户端画自己的放置幽灵, 联机安全。
 *
 * 与 Phobos 避让: 每帧检测 Phobos (任一名字含 "phobos" 的模块, 不区分大小写),
 * 已加载则完全不画, 避免双层预览。
 *
 * 配置 [PlacementPreview]: Opacity=0/25/50/75/100 (默认 25, 100 = 不透明), 0 = 关闭。
 */

#include <windows.h>
#include "GdiText.h"
#include "SA_EE.h"
#include "OrderUtil.h"

#define PP_SECTION "PlacementPreview"

/* ---------------- 只读地址 ---------------- */

#define PP_ADDR_DISPLAY          0x87F7E8u
#define PP_OFF_FND_CENTER        0x1174u   /* CellStruct 当前地基中心格 */
#define PP_OFF_FND_TOPLEFT       0x1178u   /* CellStruct 中心到地基左上偏移 */
#define PP_OFF_CUR_BUILDING      0x11A4u   /* BuildingClass* 本地放置幽灵 */
#define PP_OFF_CUR_BUILDING_TYPE 0x11A8u   /* BuildingTypeClass* */

#define PP_ADDR_TACTICAL         0x887324u
#define PP_ADDR_TEMP_SURF        0x887314u /* DSurface::Temp 当前绘制面 */
#define PP_FN_COORDS_TO_CLIENT   0x6D2140u
#define PP_FN_FLOOR_HEIGHT       0x47B3A0u
#define PP_FN_GET_DRAWER         0x705D70u /* TechnoClass::GetDrawer */
#define PP_FN_CC_DRAW_SHAPE      0x4AED70u
#define PP_ADDR_UNITX_PAL        0x87F6B4u /* FileSystem::UNITx_PAL */

/* BuildingTypeClass vtable 槽 (gamemd 0x7E4570 实测):
 *   39 = ObjectTypeClass::GetImage (this+0xA4, 需加载时懒加载)
 *   48 = BuildingTypeClass::LoadBuildup (this+0xE00/0xE5C)
 * 与 Phobos 用虚函数的语义一致; 先用 Buildup, 空则退回 Image 第 0 帧。 */
#define PP_VT_GET_IMAGE    39
#define PP_VT_LOAD_BUILDUP 48

/* BlitterFlags */
#define PP_FLAG_TRANS25   0x2u
#define PP_FLAG_TRANS50   0x4u
#define PP_FLAG_TRANS75   0x6u
#define PP_FLAG_MULTIPASS 0x100u
#define PP_FLAG_CENTERED  0x200u
#define PP_FLAG_NONZERO   0x20000u

/* Phobos 默认偏移 (palette/shape 用默认): 屏幕 X/Y 加 0,-15, Z 抬 1。 */
#define PP_OFFSET_X 0
#define PP_OFFSET_Y (-15)
#define PP_OFFSET_Z 1

/* 缩放档虚影 (自绘缩放) 用的尺寸常量。 */
#define PP_SCR_MAX      640       /* 离屏面单边上限 */

typedef int (__attribute__((thiscall)) *PpC2C)(void* tac,
    const CoordStruct* c, Point2D* out);
typedef int (__attribute__((thiscall)) *PpFloorHeight)(void* cell,
    const Point2D* p);
typedef void* (__attribute__((thiscall)) *PpGetDrawer)(void* building);
typedef void* (__attribute__((thiscall)) *PpTypeFn)(void* type);
/* 与 IconSnap.cpp 的 Fn_DrawShape 同签名: __fastcall ecx=Surface, edx=Palette,
 * 其余按栈传, ret 0x38。 */
typedef void (__fastcall *PpDrawShp)(void* surf, void* pal, void* shp,
    int frame, Point2D* pos, RectangleStruct* bounds, int flags, int remap,
    int zadjust, int zgrad, int bright, int tint, void* zshape, int zframe,
    int xoff, int yoff);

/* ---------------- 配置 ---------------- */

static int g_PpInit = 0;
static int g_PpOpacity = 25;   /* 0 = 关闭; 25/50/75/100 由淡到不透明 */

/* 归一: 只留 0/25/50/75/100 五档 (引擎半透明档位与混合权重都按这五档)。 */
static int PP_NormOpacity(int v)
{
    if (v <= 0) return 0;
    if (v >= 100) return 100;
    if (v >= 75) return 75;
    if (v >= 50) return 50;
    if (v >= 25) return 25;
    return 0;
}

static void PP_LogOpacity(void)
{
    char buf[48];
    unsigned p = 0;

    if (g_PpOpacity <= 0)
    {
        LogLine("placeprev: OFF");
        return;
    }
    p = AppendCStr(buf, p, "placeprev: ON opacity=", sizeof(buf));
    p += UtoA((unsigned)g_PpOpacity, buf + p);
    LogLine(buf);
}

static void PP_EnsureInit(void)
{
    if (g_PpInit) return;
    g_PpInit = 1;
    BuildPaths();
    GtEnsureLog();
    g_PpOpacity = PP_NormOpacity(
        GetPrivateProfileIntA(PP_SECTION, "Opacity", 25, g_IniPath));
    PP_LogOpacity();
}

/* ---------------- 公共虚影绘制 (放置预览 / 展开预览共用) ---------------- */

static int PP_ResolveShape(void* type, void** shpOut, int* frameOut)
{
    void** vt;
    void* shp;
    unsigned fn;
    int frame, frames;

    if (!type || !IsPlausiblePtr(type)) return 0;
    vt = *(void***)type;
    if (!vt || !IsPlausiblePtr(vt)) return 0;

    /* 默认 Buildup 的“最后一非影子帧”: 前半段升起到完成, 后半段是影子。
     * 无 Buildup 时退回建筑 Image 第 0 帧 (与 Phobos 同)。 */
    shp = NULL;
    frame = 0;
    fn = (unsigned)vt[PP_VT_LOAD_BUILDUP];
    if (OU_InExe(fn))
        shp = ((PpTypeFn)(void*)fn)(type);

    if (shp && IsPlausiblePtr(shp))
    {
        frames = (int)*(short*)((char*)shp + 6);   /* SHPStruct::Frames */
        if (frames <= 0) return 0;
        frame = frames / 2 - 1;
        if (frame < 0) frame = 0;
        if (frame >= frames) frame = frames - 1;
    }
    else
    {
        fn = (unsigned)vt[PP_VT_GET_IMAGE];
        if (!OU_InExe(fn)) return 0;
        shp = ((PpTypeFn)(void*)fn)(type);
        if (!shp || !IsPlausiblePtr(shp)) return 0;
        frame = 0;
    }

    *shpOut = shp;
    *frameOut = frame;
    return 1;
}

/* 绘制用调色板: owner 的玩家色/光照; 无 owner 或取失败退回 UNITx_PAL。 */
static void* PP_ResolvePal(void* owner)
{
    void* pal = NULL;
    if (owner && IsPlausiblePtr(owner))
        pal = ((PpGetDrawer)(void*)PP_FN_GET_DRAWER)(owner);
    if (!pal || !IsPlausiblePtr(pal))
        pal = *(void**)(PP_ADDR_UNITX_PAL);
    if (!pal || !IsPlausiblePtr(pal)) return NULL;
    return pal;
}

/* 原尺寸引擎 blit: (cx,cy) = 画布中心, 裁剪框 (0,0,bw,bh)。
 * opacity 100 = 不加任何半透明档位 (不透明)。 */
static void PP_Blit1to1(void* surf, void* shp, int frame, void* pal,
                        int cx, int cy, int opacity, int bw, int bh)
{
    Point2D point;
    RectangleStruct bounds;
    unsigned flags;

    flags = PP_FLAG_CENTERED | PP_FLAG_NONZERO | PP_FLAG_MULTIPASS;
    if (opacity < 100)
    {
        if (opacity >= 75) flags |= PP_FLAG_TRANS25;
        else if (opacity >= 50) flags |= PP_FLAG_TRANS50;
        else flags |= PP_FLAG_TRANS75;
    }

    bounds.X = 0;
    bounds.Y = 0;
    bounds.Width = bw;
    bounds.Height = bh;
    point.X = cx;
    point.Y = cy;
    ((PpDrawShp)(void*)PP_FN_CC_DRAW_SHAPE)(surf, pal, shp, frame, &point,
        &bounds, (int)flags, 0, 0, 0 /* ZGradient::Ground */, 1000, 0,
        NULL, 0, 0, 0);
}

static void PP_DrawGhost1to1(void* type, void* owner, void* surf,
    int x, int y, int opacity)
{
    void* shp;
    void* pal;
    int frame, w, h;

    if (!surf || !IsPlausiblePtr(surf)) return;
    if (opacity <= 0) return;
    if (!PP_ResolveShape(type, &shp, &frame)) return;
    pal = PP_ResolvePal(owner);
    if (!pal) return;

    w = GtSurfGet(surf, GtVT_GET_WIDTH);
    h = GtSurfGet(surf, GtVT_GET_HEIGHT);
    if (w <= 0 || h <= 32) return;
    PP_Blit1to1(surf, shp, frame, pal, x, y, opacity, w, h - 32);  /* 不画进工具栏 */
}

/* ---------------- 绘制 hook ---------------- */

static DWORD PP_DrawBody(void* regs)
{
    void* building;
    void* type;
    void* cell;
    void* tac;
    void* surf;
    int cx, cy, z;
    CellStruct center;
    CellStruct topLeft;
    CoordStruct coords;
    Point2D point;
    Point2D zero;

    (void)regs;
    PP_EnsureInit();
    if (g_PpOpacity <= 0) return 0;

    /* 避让 Phobos: 已加载则完全跳过 (不加载图像, 也不画)。
     * MOE_PhobosLoaded 结果缓存, 每帧调用只读一个 int。 */
    if (MOE_PhobosLoaded()) return 0;

    building = *(void**)(PP_ADDR_DISPLAY + PP_OFF_CUR_BUILDING);
    type = *(void**)(PP_ADDR_DISPLAY + PP_OFF_CUR_BUILDING_TYPE);
    if (!building || !IsPlausiblePtr(building)) return 0;
    if (!type || !IsPlausiblePtr(type)) return 0;
    if (OU_WhatAmI(building) != ABS_BUILDING) return 0;

    center = *(CellStruct*)(PP_ADDR_DISPLAY + PP_OFF_FND_CENTER);
    topLeft = *(CellStruct*)(PP_ADDR_DISPLAY + PP_OFF_FND_TOPLEFT);
    cx = (int)center.X + (int)topLeft.X;
    cy = (int)center.Y + (int)topLeft.Y;
    if (cx < 0 || cy < 0 || cx >= 512 || cy >= 512) return 0;

    cell = OU_CellPtr(cx, cy);
    if (!cell || !IsPlausiblePtr(cell)) return 0;
    zero.X = 0;
    zero.Y = 0;
    z = ((PpFloorHeight)(void*)PP_FN_FLOOR_HEIGHT)(cell, &zero);
    if (z < -8192 || z > 8192) return 0;

    /* CellClass::Cell2Coord: 格中心 + 地面高度; 再加预览偏移。 */
    coords.X = cx * 256 + 128;
    coords.Y = cy * 256 + 128;
    coords.Z = z + PP_OFFSET_Z;

    tac = *(void**)(PP_ADDR_TACTICAL);
    if (!tac || !IsPlausiblePtr(tac)) return 0;
    point.X = 0;
    point.Y = 0;
    ((PpC2C)(void*)PP_FN_COORDS_TO_CLIENT)(tac, &coords, &point);
    point.X += PP_OFFSET_X;
    point.Y += PP_OFFSET_Y;

    surf = *(void**)(PP_ADDR_TEMP_SURF);
    if (!surf || !IsPlausiblePtr(surf)) return 0;
    PP_DrawGhost1to1(type, building, surf, point.X, point.Y, g_PpOpacity);
    return 0;   /* 返回 0 = 继续执行被覆盖的原指令 (mov ecx,[0x880990]) */
}

/* 导出入口 (Syringe 按名挂钩): 只做 Perf 记账。0x6D528A 在地图绘制路径上,
 * 每帧可能被调用多次, 这里用来确认它的固定开销 (Phobos 检测走缓存, 不再
 * 每次进加载器锁)。 */
extern "C" __declspec(dllexport) DWORD __cdecl PlacementPreview_DrawHook(void* regs)
{
    long long tPf = Perf_Now();
    DWORD rc = PP_DrawBody(regs);
    Perf_Add(PF_PLACEPREV, tPf);
    return rc;
}
