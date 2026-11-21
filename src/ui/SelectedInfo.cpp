#include "GdiText.h"
#include "Observer.h"
#include "OrderUtil.h"
#include "SelectedInfo.h"

namespace {
constexpr int kMaxUnits = 6;
constexpr int kMaxRows = 9;
constexpr int kTextCap = 100;
constexpr char kSection[] = "UnitPanel";
int s_enabled = -1;
int s_fontPx;
int s_margin;
wchar_t s_face[FONT_FACE_MAX];

struct Rows {
    wchar_t text[kMaxRows][kTextCap];
    int count;
};

void AddRow(Rows& rows, const char* text)
{
    if (rows.count == kMaxRows) return;
    auto* out = rows.text[rows.count++];
    int i = 0;
    for (; i + 1 < kTextCap && text[i]; ++i) out[i] = (unsigned char)text[i];
    out[i] = 0;
}

const char* UnitType(void* object, int kind)
{
    int offset;
    switch (kind) {
    case ABS_UNIT: offset = OFF_UNIT_TYPE; break;
    case ABS_AIRCRAFT: offset = OFF_AIRCRAFT_TYPE; break;
    case ABS_INFANTRY: offset = OFF_INFANTRY_TYPE; break;
    case ABS_BUILDING: offset = OFF_BLD_TYPE; break;
    default: return nullptr;
    }
    const auto* slot = (const char*)object + offset;
    if (MemReadableBytes(slot, sizeof(void*)) < sizeof(void*)) return nullptr;
    const auto* type = *(const char* const*)slot;
    return IsPlausiblePtr(type) ? type : nullptr;
}

unsigned AppendTypeId(char* row, unsigned pos, const char* type)
{
    const auto* id = type + OFF_TYPE_ID;
    const unsigned limit = MemReadableBytes(id, 23);
    for (unsigned i = 0; i < limit && id[i]; ++i)
    {
        const auto ch = (unsigned char)id[i];
        if (ch < 32 || ch > 126) break;
        pos = AppendCh(row, pos, (char)ch, kTextCap);
    }
    return pos;
}

void AddPassengers(Rows& rows, const char* unit, int kind)
{
    const bool occupiedBuilding = kind == ABS_BUILDING && *(const int*)(unit + OFF_BLD_OCC_COUNT) > 0;
    const int count = *(const int*)(unit + (occupiedBuilding ? OFF_BLD_OCC_COUNT : OFF_TECH_PASSNUM));
    if (count <= 0 || count > 64) return;
    char line[kTextCap];
    AppendInt(line, AppendCStr(line, 0, "Passengers: ", sizeof(line)), sizeof(line), count);
    AddRow(rows, line);
    void* const* occupants = occupiedBuilding ? *(void* const**)(unit + OFF_BLD_OCC_ITEMS) : nullptr;
    void* passenger = occupiedBuilding ? nullptr : *(void* const*)(unit + OFF_TECH_PASSFIRST);
    if (occupiedBuilding && MemReadableBytes(occupants, count * sizeof(void*)) < count * sizeof(void*)) return;
    for (int i = 0; i < count && rows.count + 1 < kMaxRows; ++i)
    {
        void* object = occupiedBuilding ? occupants[i] : passenger;
        if (!IsPlausiblePtr(object)) break;
        const char* type = UnitType(object, OU_WhatAmI(object));
        if (type)
        {
            const auto pos = AppendCStr(line, 0, "  ", sizeof(line));
            AppendTypeId(line, pos, type);
            AddRow(rows, line);
        }
        if (!occupiedBuilding)
        {
            const auto* next = (const char*)object + OFF_NEXT_OBJ;
            if (MemReadableBytes(next, sizeof(void*)) < sizeof(void*)) break;
            passenger = *(void* const*)next;
        }
    }
    if (count > rows.count - 2) AddRow(rows, "  ...");
}

Rows ReadSelection()
{
    Rows rows = {};
    void* player = *(void**)ADDR_CURPLAYER;
    const bool observer = Observer_LocalIsSpectator() != 0;
    const int count = *(int*)ADDR_SELOBJ_COUNT;
    auto** items = *(void***)ADDR_SELOBJ_ITEMS;
    if (!player || !IsPlausiblePtr(items) || count <= 0 || count > 8192) return rows;
    const unsigned maxBytes = count * sizeof(void*);
    const int readable = (int)(MemReadableBytes(items, maxBytes) / sizeof(void*));
    bool more = false;
    for (int i = 0; i < readable; ++i)
    {
        const auto* object = (const char*)items[i];
        if (!IsPlausiblePtr(object)) continue;
        if (MemReadableBytes(object, OFF_OWNER + sizeof(void*)) < OFF_OWNER + sizeof(void*)) continue;
        if (!observer && *(void* const*)(object + OFF_OWNER) != player) continue;
        if (rows.count == kMaxUnits)
        {
            more = true;
            break;
        }
        const int kind = OU_WhatAmI(items[i]);
        const char* type = UnitType(items[i], kind);
        if (!type) continue;
        if (kind != ABS_BUILDING && MemReadableBytes(object, OFF_TECH_AMMO + sizeof(int)) < OFF_TECH_AMMO + sizeof(int)) continue;
        if (MemReadableBytes(type, OFF_TT_AMMO + sizeof(int)) < OFF_TT_AMMO + sizeof(int)) continue;
        char line[kTextCap] = {};
        unsigned pos = AppendTypeId(line, 0, type);
        pos = AppendCStr(line, pos, "  HP ", sizeof(line));
        pos = AppendInt(line, pos, sizeof(line), *(const int*)(object + OFF_HEALTH));
        pos = AppendCh(line, pos, '/', sizeof(line));
        pos = AppendInt(line, pos, sizeof(line), *(const int*)(type + OFF_TYPE_STRENGTH));
        const float veterancy = *(const float*)(object + OFF_TECH_VETERANCY);
        if (veterancy >= 1.0f)
            pos = AppendCStr(line, pos, veterancy >= 2.0f ? "  Elite" : "  Veteran", sizeof(line));
        const int maxAmmo = *(const int*)(type + OFF_TT_AMMO);
        if (maxAmmo > 0 && kind != ABS_BUILDING)
        {
            pos = AppendCStr(line, pos, "  Ammo ", sizeof(line));
            pos = AppendInt(line, pos, sizeof(line), *(const int*)(object + OFF_TECH_AMMO));
            pos = AppendCh(line, pos, '/', sizeof(line));
            AppendInt(line, pos, sizeof(line), maxAmmo);
        }
        AddRow(rows, line);
        if (count == 1 && MemReadableBytes(object, OFF_BLD_OCC_COUNT + sizeof(int)) >= OFF_BLD_OCC_COUNT + sizeof(int))
            AddPassengers(rows, object, kind);
    }
    if (more) AddRow(rows, "...");
    return rows;
}
}

void SelectedInfo_Draw(void* surface, int width, int height, int pitch)
{
    if (s_enabled < 0)
    {
        s_enabled = GetPrivateProfileIntA(kSection, "Enable", 1, g_IniPath) != 0;
        s_fontPx = GetPrivateProfileIntA(kSection, "FontPx", 13, g_IniPath);
        if (s_fontPx < 8) s_fontPx = 8;
        if (s_fontPx > 24) s_fontPx = 24;
        s_margin = GetPrivateProfileIntA(kSection, "BottomMargin", 35, g_IniPath);
        if (s_margin < 0) s_margin = 0;
        if (s_margin > 240) s_margin = 240;
        char face[64];
        OU_ReadString(kSection, "FontFace", "msyh.ttc", face, sizeof(face));
        ResolveFontFace(face, "unitinfo", s_face, L"Microsoft YaHei");
    }
    if (!s_enabled || !g_Calibrated) return;
    if (*(volatile unsigned char*)(ADDR_MSGLIST + OFF_MSGLIST_EDIT)) return;
    const Rows rows = ReadSelection();
    if (!rows.count || !GdiEnsure(s_fontPx, 0, 12, s_face)) return;

    const int lineH = g_G->adv;
    if (lineH <= 0 || lineH > 64) return;
    constexpr int x = 6, pad = 6;
    const int bottom = height - s_margin;
    const int top = bottom - rows.count * lineH - pad * 2;
    if (top < 0 || width < 100) return;
    int fitted[kMaxRows];
    int panelW = 0;
    for (int i = 0; i < rows.count; ++i)
    {
        fitted[i] = CharsFit(rows.text[i], lstrlenW(rows.text[i]), width - x - pad * 2 - 2);
        const int w = fitted[i] > 0 ? RenderLine(rows.text[i], fitted[i]) : 0;
        if (w > panelW) panelW = w;
    }
    if (!panelW) return;
    panelW += pad * 2 + 2;
    auto* base = GtLock(surface);
    if (!base) return;
    for (int y = top; y < bottom; ++y)
    {
        auto* row = (unsigned short*)(base + y * pitch);
        for (int j = x; j < x + panelW && j < width; ++j)
            row[j] = Blend16(row[j], 15, 18, 22, 220);
    }
    for (int i = 0; i < rows.count; ++i)
    {
        if (fitted[i] <= 0) continue;
        const int w = RenderLine(rows.text[i], fitted[i]);
        if (w > 0)
            BlitCoverage(base, pitch, width, height, x + pad, top + pad + i * lineH,
                         w + 2, lineH, 235, 242, 249, 100);
    }
    GtUnlock(surface);
}
