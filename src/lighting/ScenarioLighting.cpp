/* 新开局、读档与引擎改变光照后，刷新场景的地形光照和调色板。
 * [Environment] LightMix 控制调色比例。
 */

#include <windows.h>
#include "ScenarioLighting.h"
#include "EngineAddr.h"
#include "GdiText.h"
#include "LightConvFix.h"
#include "EnvIni.h"

enum { P_AMBIENT, P_LEVEL, P_GROUND, P_RED, P_GREEN, P_BLUE, LIGHT_PARAM_N };
constexpr int LIGHT_MIX_MAX = 100;

/* 每个参数的引擎字段偏移 (读值用 AmbientOriginal, 它与 Current 始终同步)。 */
static const int s_ParamOff[LIGHT_PARAM_N] = {
    OFF_SCEN_AMBIENT_ORIG, OFF_SCEN_LEVEL, OFF_SCEN_GROUND,
    OFF_SCEN_TINT_R, OFF_SCEN_TINT_G, OFF_SCEN_TINT_B
};

static int s_Wrote[LIGHT_PARAM_N];  /* 上次写进引擎的值 */
static int s_Mix = 0;               /* 调色比例 0..LIGHT_MIX_MAX, 长期设置
                                     * (落盘 [Environment] LightMix, 越局保留) */
static int s_Tinted = 0;            /* 上一轮染过单位/建筑没有 (决定要不要清洗) */
static int s_WroteOk = 0;           /* s_Wrote 是否有效 */
static int s_Pending = 0;           /* 场景重开了, 等一下再刷新光照 */
static int s_PendWait = 0;          /* pending 已经等了多少帧 */

typedef void (__attribute__((fastcall)) *FnRecalcLighting)(int r, int g, int b,
                                                          int tint);

#define LP_SECTION ENV_SECTION

/* Mix 从 ini 懒读入 (与 OreLight 同一套延迟初始化: g_IniPath 在日志初始化时
 * 才可用)。Poll 每帧进来, 首次后只剩一个分支。 */
static int s_Init;

static void LP_EnsureInit(void)
{
    int mix;

    if (s_Init) return;
    s_Init = 1;
    mix = GetPrivateProfileIntA(LP_SECTION, ENV_KEY_LIGHTMIX, 0, g_IniPath);
    if (mix < 0) mix = 0;
    if (mix > LIGHT_MIX_MAX) mix = LIGHT_MIX_MAX;
    s_Mix = mix;
}

static unsigned char* Light_Scen(void)
{
    return *(unsigned char**)(size_t)ADDR_SCENARIO;
}

static int Light_InGame(void)
{
    return *(volatile unsigned char*)(size_t)ADDR_SCEN_STARTED ? 1 : 0;
}

static int Light_Read(int off)
{
    unsigned char* s = Light_Scen();

    if (!s) return 0;
    return *(volatile int*)(s + off);
}

static void Light_Write(int off, int v)
{
    unsigned char* s = Light_Scen();

    if (!s) return;
    *(volatile int*)(s + off) = v;
}

static void Light_ReadAll(int* v)
{
    int i;

    for (i = 0; i < LIGHT_PARAM_N; i++)
        v[i] = Light_Read(s_ParamOff[i]);
}

/* 环境光要写三处: Current==Target 引擎才不做渐变 (立刻生效), 而 UpdateLighting
 * 的普通分支每次都把 Target 复位成 Original, 所以 Original 也得跟着改。
 * 等级/地面各只有一格 (建 convert 时读)。 */
static void Light_WriteAll(const int* v)
{
    Light_Write(OFF_SCEN_AMBIENT_ORIG, v[P_AMBIENT]);
    Light_Write(OFF_SCEN_AMBIENT_CUR,  v[P_AMBIENT]);
    Light_Write(OFF_SCEN_AMBIENT_TGT,  v[P_AMBIENT]);
    Light_Write(OFF_SCEN_TINT_R, v[P_RED]);
    Light_Write(OFF_SCEN_TINT_G, v[P_GREEN]);
    Light_Write(OFF_SCEN_TINT_B, v[P_BLUE]);
    Light_Write(OFF_SCEN_LEVEL,  v[P_LEVEL]);
    Light_Write(OFF_SCEN_GROUND, v[P_GROUND]);
}

static int Light_Same(const int* a, const int* b)
{
    int i;

    for (i = 0; i < LIGHT_PARAM_N; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* 让新光照上屏。渲染方式由调色比例决定, 按引擎字段重算:
 *
 *   100 = 触发光照: 引擎触发器 (SetTintRed/Green/Blue) 那条
 *         RecalcLighting(tint×10, …, 0), 整池覆写 —— 每格的点光源色被抹平只剩
 *         亮度, 单位/建筑一起被染, 与触发色逐位一致。
 *   0   = 默认光照: 逐格重建 convert (与建图同路, 点光源色都在), 单位/建筑不染色。
 *   中间 = 逐格重建 + 按比例给玩家色 / 单位 / 建筑上色。
 *
 * 先染色再重建: 逐格重建会盖掉刚染的地形, 顺序不能反。比例为 0 且上一轮染过时
 * 要走一次 1000,1000,1000 的清洗, 把池 [0] 默认件写回原色 —— 整池覆写会染脏它,
 * 而 InitLightConvert 的快速路径 (0x544E94) 不校验它的 Color1。
 * 收尾的 RecalcLighting(-1,-1,-1,0) 只重算渐变 + 触发重画, 不改色。 */
static void Light_Refresh(const int* v)
{
    if (!Light_Scen()) return;

    if (s_Mix >= LIGHT_MIX_MAX)
    {
        s_Tinted = 1;
        ((FnRecalcLighting)(size_t)FN_RECALC_LIGHTING)(
            v[P_RED] * 10, v[P_GREEN] * 10, v[P_BLUE] * 10, 0);
        return;
    }

    if (s_Mix > 0 || s_Tinted)
    {
        LightConv_TriggerTint(v[P_RED] * 10, v[P_GREEN] * 10, v[P_BLUE] * 10,
                              s_Mix);
        s_Tinted = (s_Mix > 0);
    }
    LightConv_RebuildCells();
    ((FnRecalcLighting)(size_t)FN_RECALC_LIGHTING)(-1, -1, -1, 0);
}

/* 日志片段 " amb=.. tint=r,g,b" (应用与场景重开两处共用)。 */
static unsigned Light_AppendVals(char* msg, unsigned p, unsigned cap, const int* v)
{
    p = AppendCStr(msg, p, " amb=", cap);
    p = AppendInt(msg, p, cap, v[P_AMBIENT]);
    p = AppendCStr(msg, p, " tint=", cap);
    p = AppendInt(msg, p, cap, v[P_RED]);
    p = AppendCh(msg, p, ',', cap);
    p = AppendInt(msg, p, cap, v[P_GREEN]);
    p = AppendCh(msg, p, ',', cap);
    p = AppendInt(msg, p, cap, v[P_BLUE]);
    return p;
}

static void Light_Apply(const int* v)
{
    int i;
    char msg[128];
    unsigned p;

    if (s_WroteOk && !Light_Same(v, s_Wrote))
    {
        p = AppendCStr(msg, 0, "lighting: apply", sizeof(msg));
        p = Light_AppendVals(msg, p, sizeof(msg), v);
        p = AppendCStr(msg, p, " lv=", sizeof(msg));
        p = AppendInt(msg, p, sizeof(msg), v[P_LEVEL]);
        p = AppendCStr(msg, p, " gr=", sizeof(msg));
        p = AppendInt(msg, p, sizeof(msg), v[P_GROUND]);
        LogLine(msg);
    }

    Light_WriteAll(v);
    for (i = 0; i < LIGHT_PARAM_N; i++) s_Wrote[i] = v[i];
    s_WroteOk = 1;
    Light_Refresh(v);
}

/* 场景重开 (新开/读档/换图) 后按"引擎此刻给的值"重算一遍光照。
 *
 * 要解决的问题: 读档后地形是按"重建那一刻的光照"建的, 而 LoadGame (0x67E440)
 * 不调 RecalcLighting —— 字段是对的, 画面却停在重建时的亮度。
 *
 * 触发: 两个场景就绪钩子, 即引擎把"对局中" (0xA8ED5C) 置 1 的全部两条指令
 * (新开局 0x683E75 / 读档 0x67E6C4, 两条路径互不经过) 置 pending。
 *
 * 等什么: 过了载入闸、格子数组回来了才动手; 但**最多等 240 帧** —— 任何一个闸门
 * 卡住都会让 Poll 从此不干活, 到点就无视闸门硬刷一次。多刷一次只是白干一次活,
 * 不刷就是永久错。 */
void Lighting_Poll(void)
{
    int started = Light_InGame();
    int v[LIGHT_PARAM_N];
    int waited;
    char msg[192];
    unsigned p;

    LP_EnsureInit();
    /* 关闭: 自动链路全停, 引擎原版怎么做就怎么做。 */
    if (!LightFix_Enabled()) return;

    if (s_Pending)
    {
        s_PendWait++;
        if (s_PendWait < 2) return;       /* 让引擎把这一帧的收尾走完再动手 */
        if ((!started || *(volatile int*)(size_t)ADDR_LOAD_PHASE ||
             !LightConv_WorldReady()) && s_PendWait < 240)
            return;                       /* 载入中/世界没搭好: 继续等 */
        waited = s_PendWait;
        s_Pending = 0;
        s_PendWait = 0;
        /* 场景对象/格子数组都还没有: 没东西可刷, 等下一次场景就绪 (主菜单就是
         * 这个状态)。超时只可能出现在"世界始终没准备好"这种情形。 */
        if (!Light_Scen() || !LightConv_WorldReady()) return;

        Light_ReadAll(v);
        Light_Apply(v);

        p = AppendCStr(msg, 0, "lighting: scene reopen", sizeof(msg));
        p = Light_AppendVals(msg, p, sizeof(msg), v);
        p = AppendCStr(msg, p, " -> engine", sizeof(msg));
        p = AppendCStr(msg, p, " n=", sizeof(msg));
        p = AppendInt(msg, p, sizeof(msg), waited);
        LogLine(msg);
        return;
    }

    if (!started) return;                 /* 不在对局里不动手 */
    if (*(volatile int*)(size_t)ADDR_LOAD_PHASE) return;   /* 还在载入 */

    Light_ReadAll(v);
    if (s_WroteOk && !Light_Same(v, s_Wrote))
        Light_Apply(v);
}

/* 开局和读档都由 Lighting_Poll 在载入结束后刷新。 */
void Lighting_OnScenarioReady(void)
{
    s_Tinted = 0;              /* 新场景的 convert 池由引擎重建, 上一局的染色作废 */
    if (!LightFix_Enabled()) return;
    s_Pending = 1;
    s_PendWait = 0;
}
