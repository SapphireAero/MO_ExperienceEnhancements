#include <windows.h>
#include "EngineAddr.h"
#include "GdiText.h"
#include "OrderUtil.h"
#include "Observer.h"
#include "BeaconReplace.h"

/* ---------------- 状态 ---------------- */

static int g_BsInit = 0;
static int g_BsSwapCfg = 1;     /* [Beacon] AutoReplace */
static int g_BsNoteOn = 0;      /* 0x430CD4 记序号就绪 */
static int g_BsSwapOn = 0;      /* 两处判满替换就绪且允许 */

/* 本家三个信标槽的放置序号。0 表示未知，优先替换。 */
#define BS_SLOT_MAX 3
static unsigned g_BcTs[BS_SLOT_MAX];
static unsigned g_BcSeq;

static int Bc_PickOldest(void);
static void Bc_NoteSlot(int house, int slot);

/* 壳的跳转目标 (裸钩里只许用 "m" 约束, 不能用寄存器)。 */
static void* g_BsTailGo     = (void*)ADDR_BC_PLACETAIL_GO;
static void* g_BsTailNext   = (void*)ADDR_BC_PLACETAIL_NEXT;
static void* g_BsFullAlloc  = (void*)ADDR_BC_FULLCHK_NEXT;
static void* g_BsFullGiveUp = (void*)ADDR_BC_FULLCHK_GIVEUP;
static void* g_BcPickPtr    = (void*)Bc_PickOldest;
static void* g_BcNotePtr    = (void*)Bc_NoteSlot;

/* ---------------- 五个壳 ----------------
 * 关闭分支重放原指令。辅助函数只破坏落点前会被重载的 caller-saved 寄存器,
 * house、manager 等仍活跃的 callee-saved 寄存器由调用约定保存。 */

/* 放置尾部: ebx 为 houseId, [esp+0x1F4] 为刚写入的槽号。
 * 记录本机信标槽的序号，再交回原生提示段。 */
__attribute__((naked)) void BeaconReplace_TailEntry(void)
{
    __asm__ volatile (
        "cmpl $0, %0\n\t"
        "je 1f\n\t"
        "movl 0x1f4(%%esp), %%eax\n\t"
        "pushl %%eax\n\t"
        "pushl %%ebx\n\t"
        "call *%3\n\t"
        "addl $8, %%esp\n\t"
        "jmp *%1\n\t"
        "1:\n\t"
        "movl 0xA8B238, %%eax\n\t"
        "jmp *%2\n\t"
        :
        : "m" (g_BsNoteOn), "m" (g_BsTailGo), "m" (g_BsTailNext), "m" (g_BcNotePtr)
        : "cc", "eax", "ecx", "edx");
}

/* 0x430F4A (CanPlaceBeacon 的"满"返回): 打开时满 3 也返回 1。 */
__attribute__((naked)) void BeaconReplace_CanFullEntry(void)
{
    __asm__ volatile (
        "cmpl $0, %0\n\t"
        "je 1f\n\t"
        "movb $1, %%al\n\t"
        "ret $4\n\t"
        "1:\n\t"
        "xorb %%al, %%al\n\t"
        "ret $4\n\t"
        :
        : "m" (g_BsSwapOn)
        : "cc");
}

/* 仅本地未指定槽号 (-1) 时挑最旧槽; 远端指定槽号不得重选。
 * 写回 [esp+0x1F4] 后复用引擎覆盖分支, 广播携带同一槽号。 */
__attribute__((naked)) void BeaconReplace_FullEntry(void)
{
    __asm__ volatile (
        "cmpl $0, %0\n\t"
        "je 1f\n\t"
        "cmpl $-1, 0x1f4(%%esp)\n\t"
        "jne 2f\n\t"
        "call *%1\n\t"
        "cmpl $-1, %%eax\n\t"
        "je 3f\n\t"
        "movl %%eax, 0x1f4(%%esp)\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "cmpl $-1, 0x1f4(%%esp)\n\t"
        "je 3f\n\t"
        "2:\n\t"
        "jmp *%2\n\t"
        "3:\n\t"
        "jmp *%3\n\t"
        :
        : "m" (g_BsSwapOn), "m" (g_BcPickPtr), "m" (g_BsFullAlloc),
          "m" (g_BsFullGiveUp)
        : "cc", "eax", "ecx", "edx");
}

/* 三处补丁成组启用，保留原生联机广播。 */
static const unsigned char kBcMovSess[5] = { 0xA1, 0x38, 0xB2, 0xA8, 0x00 };
static const unsigned char kBcCanFull[5] = { 0x32, 0xC0, 0xC2, 0x04, 0x00 };
static const unsigned char kBcFullChk[8] = { 0x83, 0xBC, 0x24, 0xF4, 0x01, 0x00, 0x00, 0xFF };


typedef struct
{
    unsigned addr;
    const unsigned char* want;
    int len;
    void* entry;
    const char* tag;
    int state;                  /* 0=没装 1=已装 2=放弃 */
} BS_Site;

static BS_Site g_BsSites[] =
{
    { ADDR_BC_PLACETAIL, kBcMovSess, 5, (void*)BeaconReplace_TailEntry,    "placetail", 0 },
    { ADDR_BC_CANFULL,   kBcCanFull, 5, (void*)BeaconReplace_CanFullEntry, "canfull",   0 },
    { ADDR_BC_FULLCHK,   kBcFullChk, 8, (void*)BeaconReplace_FullEntry,    "swapfull",  0 },
};
#define BS_NSITE ((int)(sizeof(g_BsSites) / sizeof(g_BsSites[0])))

/* ---------------- 日志 ---------------- */

static void BS_Log(const char* tag, const char* msg)
{
    char buf[64];
    unsigned n = AppendCStr(buf, 0, "beaconsp: ", sizeof(buf));
    AppendCStr(buf, n, tag ? tag : "?", sizeof(buf));
    OU_LogTag(buf, msg);
}

/* 每次自动替换记一行, 反馈问题时直接看槽号与序号。 */
static void Bc_LogSwap(int house, int slot, unsigned ts)
{
    char buf[80];
    unsigned n = AppendCStr(buf, 0, "beaconsp: swap h=", sizeof(buf));
    n = OU_AppendU(buf, n, (unsigned)house, sizeof(buf));
    n = AppendCStr(buf, n, " slot=", sizeof(buf));
    n = OU_AppendU(buf, n, (unsigned)slot, sizeof(buf));
    n = AppendCStr(buf, n, " seq=", sizeof(buf));
    n = OU_AppendU(buf, n, ts, sizeof(buf));
    LogLine(buf);
}

/* Slot IDs are preserved on the wire. Only choose among our own three slots. */
static int Bc_PickOldest(void)
{
    void* cur = *(void**)ADDR_CURPLAYER;
    if (!cur) return -1;
    const int house = *(int*)((char*)cur + OFF_HOUSE_INDEX);
    if (house < 0 || house >= 8) return -1;
    auto** slot = (void**)ADDR_BEACON_MGR + house * BS_SLOT_MAX;
    int pick = -1;
    for (int i = 0; i < BS_SLOT_MAX; i++)
        if (slot[i] && (pick < 0 || g_BcTs[i] < g_BcTs[pick])) pick = i;
    if (pick >= 0) Bc_LogSwap(house, pick, g_BcTs[pick]);
    return pick;
}

static void Bc_NoteSlot(int house, int slot)
{
    void* cur = *(void**)ADDR_CURPLAYER;
    if (!cur || slot < 0 || slot >= BS_SLOT_MAX) return;
    if (house != *(int*)((char*)cur + OFF_HOUSE_INDEX)) return;
    g_BcTs[slot] = ++g_BcSeq;
}

/* ---------------- 装/卸 (幂等) ---------------- */

static void BS_Install(BS_Site* s)
{
    unsigned char* p;
    DWORD old = 0;
    int i;
    if (!OU_InExe(s->addr) || *(int*)ADDR_FRAME < 60) return;
    p = (unsigned char*)s->addr;
    if (p[0] == 0xE9)
    {
        /* 仅接管自身跳转, 不覆盖其它补丁。 */
        if ((void*)(p + 5 + *(int*)(p + 1)) == s->entry) { s->state = 1; return; }
        if (s->state != 2)
        {
            s->state = 2;
            BS_Log(s->tag, " taken by another patch - OFF");
        }
        return;
    }
    if (s->state == 2) return;                   /* 已放弃: 不与别的补丁抢同一个站点 */
    for (i = 0; i < s->len; i++) if (p[i] != s->want[i]) break;
    if (i < s->len)
    {
        s->state = 2;
        BS_Log(s->tag, " prologue mismatch - OFF");
        return;
    }
    if (!VirtualProtect(p, s->len, PAGE_EXECUTE_READWRITE, &old))
    {
        s->state = 2;
        BS_Log(s->tag, " VirtualProtect fail - OFF");
        return;
    }
    p[0] = 0xE9;
    *(int*)(p + 1) = (int)(long)s->entry - (int)(long)(p + 5);
    for (i = 5; i < s->len; i++) p[i] = 0x90;
    VirtualProtect(p, s->len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, s->len);
    s->state = 1;
    BS_Log(s->tag, " hook ON");
}

static void BS_Uninstall(BS_Site* s)
{
    unsigned char* p;
    DWORD old = 0;
    int i;
    if (s->state != 1 || !OU_InExe(s->addr)) return;
    p = (unsigned char*)s->addr;
    if (p[0] != 0xE9 || (void*)(p + 5 + *(int*)(p + 1)) != s->entry)
    {
        s->state = 0;                            /* 已被别人改回, 不再管 */
        return;
    }
    if (!VirtualProtect(p, s->len, PAGE_EXECUTE_READWRITE, &old)) return;
    for (i = 0; i < s->len; i++) p[i] = s->want[i];
    VirtualProtect(p, s->len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, s->len);
    s->state = 0;
    BS_Log(s->tag, " restored (vanilla)");
}

/* 装/卸一组, 返回 1 = 该组已全装好。 */
static int BS_Group(int want)
{
    int i, ok = want;
    for (i = 0; want && i < BS_NSITE; i++)
        if (g_BsSites[i].state == 2) want = ok = 0;
    for (i = 0; i < BS_NSITE; i++)
    {
        if (want) BS_Install(&g_BsSites[i]);
        else BS_Uninstall(&g_BsSites[i]);
    }
    for (i = 0; ok && i < BS_NSITE; i++)
        if (g_BsSites[i].state != 1) ok = 0;
    if (want && !ok)
        for (i = 0; i < BS_NSITE; i++)
            BS_Uninstall(&g_BsSites[i]);
    return ok;
}

void BeaconReplace_Reset(void)
{
    g_BsSwapOn = g_BsNoteOn = 0;
    g_BcSeq = 0;
    for (unsigned& ts : g_BcTs) ts = 0;
}

void BeaconReplace_Poll(void)
{
    if (!g_BsInit)
    {
        g_BsInit = 1;
        g_BsSwapCfg = GetPrivateProfileIntA("Beacon", "AutoReplace", 1, g_IniPath) != 0;
    }
    const bool want = g_BsSwapCfg && OU_IsMP() &&
        *(volatile unsigned char*)ADDR_SCEN_STARTED &&
        !Observer_LocalIsSpectator() && *(int*)ADDR_FRAME >= 60 &&
        *(unsigned*)ADDR_BC_ART && *(unsigned*)ADDR_BC_RADARART &&
        *(unsigned*)(ADDR_BEACON_MGR + OFF_BC_PERIOD);
    if (!want) BeaconReplace_Reset();
    const int ready = BS_Group(want);
    g_BsNoteOn = g_BsSwapOn = ready;
}
