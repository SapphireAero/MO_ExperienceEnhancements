#include "GdiText.h"
#include "OrderUtil.h"
#include "BeaconArrows.h"

static int s_ArrowInset = 16;
static int s_ArrowLen = 11;

static int Fav_Min(int a, int b) { return a < b ? a : b; }
static int Fav_Max(int a, int b) { return a > b ? a : b; }

/* 整数平方根 (Newtown 迭代, 无 libm)。 */
static int Fav_ISqrt(int v)
{
    int x, y;
    if (v <= 0) return 0;
    x = v;
    y = x / 2 + x % 2;
    while (y < x) { x = y; y = (x + v / x) >> 1; }
    return x;
}

/* 三角形填充 (三个顶点任意方向, 重心坐标判内外)。 */
static void Fav_TriFill(unsigned char* base, int pitch, int W, int H,
                        int x0, int y0, int x1, int y1, int x2, int y2,
                        unsigned r, unsigned g, unsigned b, unsigned a)
{
    int minx, maxx, miny, maxy, x, y;
    long long w0, w1, w2;

    minx = Fav_Min(x0, Fav_Min(x1, x2));
    maxx = Fav_Max(x0, Fav_Max(x1, x2));
    miny = Fav_Min(y0, Fav_Min(y1, y2));
    maxy = Fav_Max(y0, Fav_Max(y1, y2));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > W - 1) maxx = W - 1;
    if (maxy > H - 1) maxy = H - 1;
    if (minx > maxx || miny > maxy) return;
    if (maxx - minx > 64 || maxy - miny > 64) return;   /* 防脏顶点 */
    for (y = miny; y <= maxy; y++)
        for (x = minx; x <= maxx; x++)
        {
            w0 = (long long)(x1 - x0) * (y - y0) - (long long)(y1 - y0) * (x - x0);
            w1 = (long long)(x2 - x1) * (y - y1) - (long long)(y2 - y1) * (x - x1);
            w2 = (long long)(x0 - x2) * (y - y2) - (long long)(y0 - y2) * (x - x2);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                OU_Put(base, W, H, pitch, NULL, x, y, r, g, b, a);
        }
}

/* 星星: 先画一圈深色描边 (亮地形/雪地上单色星看不清), 再压彩色芯。 */
static void Fav_ArrowAt(unsigned char* base, int pitch, int W, int H,
                        int ex, int ey, int dx, int dy,
                        unsigned r, unsigned g, unsigned b, unsigned a)
{
    int len, ux, uy, px, py, L, hw;
    int tx, ty, bx, by, p1x, p1y, p2x, p2y;
    unsigned oa;

    len = Fav_ISqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    ux = dx * 256 / len;
    uy = dy * 256 / len;
    px = -uy;
    py = ux;
    L = s_ArrowLen;
    hw = L * 5 / 11;
    if (hw < 3) hw = 3;

    /* 尖端略微探出视图边, 视觉上"指向外面" */
    tx = ex + ux * 3 / 256;
    ty = ey + uy * 3 / 256;
    bx = ex - ux * L / 256;
    by = ey - uy * L / 256;
    p1x = bx + px * hw / 256;
    p1y = by + py * hw / 256;
    p2x = bx - px * hw / 256;
    p2y = by - py * hw / 256;
    oa = (205u * a) / 255u;
    /* 描边: 整体往外扩 1px */
    Fav_TriFill(base, pitch, W, H,
                tx + ux / 256, ty + uy / 256,
                p1x + px / 256, p1y + py / 256,
                p2x - px / 256, p2y - py / 256,
                0, 0, 0, oa);
    Fav_TriFill(base, pitch, W, H, tx, ty, p1x, p1y, p2x, p2y, r, g, b, a);
}

static int Fav_CellHidden(int lx, int ly)
{
    void* cell;
    unsigned flags;
    int cx = lx >> 8, cy = ly >> 8;
    if (cx < 0 || cy < 0) return 1;
    cell = OU_CellPtr(cx, cy);
    if (!cell || !IsPlausiblePtr(cell)) return 1;
    flags = *(unsigned*)((char*)cell + OFF_CELL_ALTFLAGS);
    return (flags & CELL_SHROUD_REVEALED) ? 0 : 1;
}

static int Fav_InView(const RectangleStruct* a, int px, int py)
{
    return (px >= a->X && px < a->X + a->Width &&
            py >= a->Y && py < a->Y + a->Height) ? 1 : 0;
}

static int Fav_ArrowAnchor(const RectangleStruct* a, int px, int py,
                           int inset,
                           int* ex, int* ey, int* dx, int* dy)
{
    int acx = a->X + a->Width / 2, acy = a->Y + a->Height / 2;
    int rx = px - acx, ry = py - acy;

    *dx = rx;
    *dy = ry;
    if ((!rx && !ry) || rx < -32000 || rx > 32000 || ry < -32000 || ry > 32000) return 0;
    int ax = (rx < 0) ? -rx : rx;
    int ay = (ry < 0) ? -ry : ry;
    int hw = a->Width / 2 - inset;
    int hh = a->Height / 2 - inset;
    if (hw < 8) hw = 8;
    if (hh < 8) hh = 8;
    if ((long long)ax * hh >= (long long)ay * hw)
    {
        *ex = acx + (rx > 0 ? hw : -hw);
        *ey = acy + (int)((long long)ry * hw / (ax ? ax : 1));
    }
    else
    {
        *ey = acy + (ry > 0 ? hh : -hh);
        *ex = acx + (int)((long long)rx * hh / (ay ? ay : 1));
    }
    return 1;
}

#define BC_HOUSE_MAX    8          /* BeaconManagerClass::Beacons[8][3] */
#define BC_SLOT_MAX     3
#define BC_MAX          (BC_HOUSE_MAX * BC_SLOT_MAX)

typedef unsigned char (__attribute__((thiscall)) *Fn_BcVisible)(void* self);

typedef struct {
    int px, py;         /* 屏幕投影 (离屏) */
    unsigned r, g, b;   /* 该信标所属玩家的颜色 */
} FavBcMark;

static int Fav_BeaconVisible(void* b)
{
    static Fn_BcVisible fn = NULL;
    /* 引擎函数直接解引用本家房指针, 开局前它可能是空。 */
    if (!*(void**)(size_t)ADDR_CURPLAYER) return 0;
    if (!fn) fn = (Fn_BcVisible)(void*)FN_BC_VISIBLE;
    return fn(b) ? 1 : 0;
}

/* 按玩家颜色绘制离屏信标。同方向的多个标记沿边缘向内错开。 */
int BeaconArrows_Draw(void* surf, int w, int h, int pitch)
{
    RectangleStruct a;
    unsigned char* base;
    FavBcMark mk[BC_MAX];
    int mx[BC_MAX], my[BC_MAX];
    int i, n = 0, placed = 0, sep;

    static int enabled = -1;
    if (enabled < 0)
    {
        enabled = GetPrivateProfileIntA("Beacon", "Arrows", 1, g_IniPath) != 0;
        s_ArrowInset = Fav_Max(8, Fav_Min(200, GetPrivateProfileIntA("Beacon", "ArrowInset", 16, g_IniPath)));
        s_ArrowLen = Fav_Max(6, Fav_Min(40, GetPrivateProfileIntA("Beacon", "ArrowSize", 11, g_IniPath)));
    }
    if (!enabled || !OU_IsMP()) return 0;
    if (!g_Calibrated) return 0;
    if (!*(volatile unsigned char*)(size_t)ADDR_SCEN_STARTED) return 0;
    if (!surf || !IsPlausiblePtr(surf)) return 0;
    if (w <= 0 || h <= 0 || pitch < w * 2) return 0;
    a = *(const RectangleStruct*)ADDR_VIEWBOUNDS;
    if (a.X < 0 || a.Y < 0 || a.X >= w || a.Y >= h) return 0;
    a.Width = Fav_Min(a.Width, w - a.X);
    a.Height = Fav_Min(a.Height, h - a.Y);
    if (a.Width < 64 || a.Height < 64) return 0;

    for (i = 0; i < BC_MAX; i++)
    {
        unsigned char* b = *(unsigned char**)((size_t)ADDR_BEACON_MGR + (size_t)i * 4u);
        int px, py, hid, r, g, bl;

        if (!b || !IsPlausiblePtr(b)) continue;
        if (!(b[OFF_BC_BITFIELD] & 1)) continue;        /* 只认已放置的槽 */
        hid = *(int*)(b + OFF_BC_HOUSEID);
        if (!OU_HouseIndexColor(hid, &r, &g, &bl)) continue;
        if (!Fav_BeaconVisible(b)) continue;
        if (Fav_CellHidden(*(int*)b, *(int*)(b + 4))) continue;
        if (!OU_ProjWorld(*(int*)b, *(int*)(b + 4), *(int*)(b + 8), &px, &py)) continue;
        if (Fav_InView(&a, px, py)) continue;          /* 在画面上就有信标本体 */
        mk[n].px = px;
        mk[n].py = py;
        mk[n].r = (unsigned)r;
        mk[n].g = (unsigned)g;
        mk[n].b = (unsigned)bl;
        n++;
    }
    if (n <= 0) return 0;

    base = GtLock(surf);
    if (!base) return 0;
    sep = s_ArrowLen + 6;
    for (i = 0; i < n; i++)
    {
        int layer, ex = 0, ey = 0, dx = 0, dy = 0, ok = 0;

        for (layer = 0; layer <= 8; layer++)
        {
            int inset, k, hit = 0;
            inset = s_ArrowInset + layer * sep;
            if (!Fav_ArrowAnchor(&a, mk[i].px, mk[i].py, inset,
                                 &ex, &ey, &dx, &dy)) break;
            ok = 1;
            for (k = 0; k < placed; k++)
                if (ex >= mx[k] - sep && ex <= mx[k] + sep &&
                    ey >= my[k] - sep && ey <= my[k] + sep) { hit = 1; break; }
            if (!hit) break;
            ok = 0;                                     /* 压在别人身上: 往外一层再试 */
        }
        if (!ok) continue;
        Fav_ArrowAt(base, pitch, w, h, ex, ey, dx, dy, mk[i].r, mk[i].g, mk[i].b, 255);
        mx[placed] = ex;
        my[placed] = ey;
        placed++;
    }
    GtUnlock(surf);
    return 1;
}
