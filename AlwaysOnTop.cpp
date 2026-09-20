#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <cwctype>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
constexpr wchar_t kWindowClass[] = L"AlwaysOnTopStandaloneWindow";
constexpr wchar_t kBorderClass[] = L"AlwaysOnTopBorderWindow";
constexpr wchar_t kExclusionsClass[] = L"AlwaysOnTopExclusionsWindow";
constexpr wchar_t kAppName[] = L"Always On Top";
constexpr wchar_t kVersion[] = L"1.0.0";
constexpr wchar_t kMutexName[] = L"Local\\devbloxstrap.AlwaysOnTop";
constexpr wchar_t kRegistryKey[] = L"Software\\devbloxstrap\\AlwaysOnTop";

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_HOTKEY_CAPTURE_RESULT = WM_APP + 2;
constexpr UINT ID_GLOBAL_HOTKEY = 1;
constexpr UINT_PTR TIMER_TRACK = 1;

constexpr UINT ID_TRAY_OPEN = 1000;
constexpr UINT ID_TRAY_ENABLE = 1001;
constexpr UINT ID_TRAY_TOGGLE_ACTIVE = 1002;
constexpr UINT ID_TRAY_CLEAR = 1003;
constexpr UINT ID_TRAY_EXIT = 1099;

constexpr UINT ID_THICKNESS_1 = 2201;
constexpr UINT ID_THICKNESS_2 = 2202;
constexpr UINT ID_THICKNESS_3 = 2203;
constexpr UINT ID_THICKNESS_4 = 2204;
constexpr UINT ID_THICKNESS_6 = 2206;
constexpr UINT ID_THICKNESS_8 = 2208;

constexpr UINT IDC_EXCLUSION_EDIT = 3001;
constexpr UINT IDC_EXCLUSION_ADD = 3002;
constexpr UINT IDC_EXCLUSION_REMOVE = 3003;
constexpr UINT IDC_EXCLUSION_ACTIVE = 3004;
constexpr UINT IDC_EXCLUSION_DONE = 3005;
constexpr UINT IDC_EXCLUSION_LIST = 3006;

constexpr UINT MODIFIER_MASK = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

enum class UiHit : int {
    None = 0,
    EnableToggle,
    HotkeyButton,
    AccentToggle,
    ColorButton,
    ThicknessButton,
    OpacitySlider,
    ExclusionsButton,
    SoundToggle,
    PinButton,
};

struct PinnedWindow {
    HWND target{};
    HWND borders[4]{};
    bool wasTopmost = false;
};

HWND g_hwnd = nullptr;
HWND g_exclusionsHwnd = nullptr;
HWND g_exclusionEdit = nullptr;
HWND g_exclusionList = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_appIcon = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSection = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontTiny = nullptr;
ULONG_PTR g_gdiplusToken = 0;
UINT g_taskbarCreated = 0;

bool g_enabled = true;
bool g_sound = true;
bool g_useAccentColor = true;
UINT g_hotkeyModifiers = MOD_WIN | MOD_CONTROL;
UINT g_hotkeyVk = 'T';
bool g_hotkeyRegistered = false;
HHOOK g_hotkeyHook = nullptr;
bool g_capturingHotkey = false;
bool g_capturePosted = false;
COLORREF g_customBorderColor = RGB(0, 120, 212);
int g_borderThickness = 3;
int g_borderOpacity = 88;
std::vector<std::wstring> g_excludedApps;
std::vector<PinnedWindow> g_pinned;
HWND g_lastExternalWindow = nullptr;
std::wstring g_noticeText;
ULONGLONG g_noticeUntil = 0;

RECT g_enableRect{};
RECT g_hotkeyRect{};
RECT g_accentRect{};
RECT g_colorRect{};
RECT g_thicknessRect{};
RECT g_opacityRect{};
RECT g_exclusionsRect{};
RECT g_soundRect{};
RECT g_pinButtonRect{};
UiHit g_hover = UiHit::None;
bool g_trackingMouse = false;
bool g_dragOpacity = false;

const COLORREF kBgTop = RGB(243, 247, 252);
const COLORREF kBgBottom = RGB(232, 240, 249);
const COLORREF kCard = RGB(252, 253, 255);
const COLORREF kCardStrong = RGB(255, 255, 255);
const COLORREF kCardBorder = RGB(207, 218, 231);
const COLORREF kShadow = RGB(211, 221, 232);
const COLORREF kText = RGB(24, 33, 47);
const COLORREF kMuted = RGB(91, 104, 122);
const COLORREF kAccent = RGB(0, 120, 212);
const COLORREF kAccentHover = RGB(0, 104, 184);
const COLORREF kAccentSoft = RGB(229, 242, 253);
const COLORREF kDisabled = RGB(158, 168, 181);
const COLORREF kSuccess = RGB(34, 139, 94);

Gdiplus::Color GpColor(COLORREF c, BYTE alpha = 255)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

RECT R(int l, int t, int r, int b)
{
    RECT rc{l, t, r, b};
    return rc;
}

void AddRoundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, Gdiplus::REAL radius)
{
    radius = std::max<Gdiplus::REAL>(0.0f, std::min(radius, std::min(rect.Width, rect.Height) / 2.0f));
    const Gdiplus::REAL d = radius * 2.0f;
    if (d <= 0.0f) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawRounded(HDC dc, const RECT& r, float radius, COLORREF fill, COLORREF border, float borderWidth = 1.0f)
{
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    Gdiplus::RectF rect(
        static_cast<float>(r.left) + 0.5f,
        static_cast<float>(r.top) + 0.5f,
        static_cast<float>(std::max(1L, r.right - r.left)) - 1.0f,
        static_cast<float>(std::max(1L, r.bottom - r.top)) - 1.0f);
    Gdiplus::GraphicsPath path;
    AddRoundedPath(path, rect, radius);
    Gdiplus::SolidBrush brush(GpColor(fill));
    g.FillPath(&brush, &path);
    if (borderWidth > 0.0f) {
        Gdiplus::Pen pen(GpColor(border), borderWidth);
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        g.DrawPath(&pen, &path);
    }
}

void DrawEllipseAA(HDC dc, const RECT& r, COLORREF fill, COLORREF border, float borderWidth = 0.0f)
{
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::RectF rect(
        static_cast<float>(r.left) + 0.5f,
        static_cast<float>(r.top) + 0.5f,
        static_cast<float>(std::max(1L, r.right - r.left)) - 1.0f,
        static_cast<float>(std::max(1L, r.bottom - r.top)) - 1.0f);
    Gdiplus::SolidBrush brush(GpColor(fill));
    g.FillEllipse(&brush, rect);
    if (borderWidth > 0.0f) {
        Gdiplus::Pen pen(GpColor(border), borderWidth);
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        g.DrawEllipse(&pen, rect);
    }
}

void FillSolid(HDC dc, const RECT& r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void DrawTextCrisp(HDC dc, const std::wstring& text, RECT r, HFONT font, COLORREF color,
                   UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
    const int oldBkMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldText = SetTextColor(dc, color);
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    SetTextColor(dc, oldText);
    SetBkMode(dc, oldBkMode);
}

void DrawSoftBackground(HDC dc, const RECT& client)
{
    const int h = std::max(1L, client.bottom - client.top);
    constexpr int bands = 48;
    for (int i = 0; i < bands; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(bands - 1);
        const int rr = static_cast<int>(GetRValue(kBgTop) * (1.0 - t) + GetRValue(kBgBottom) * t);
        const int gg = static_cast<int>(GetGValue(kBgTop) * (1.0 - t) + GetGValue(kBgBottom) * t);
        const int bb = static_cast<int>(GetBValue(kBgTop) * (1.0 - t) + GetBValue(kBgBottom) * t);
        RECT band{0, (h * i) / bands, client.right, (h * (i + 1)) / bands + 1};
        FillSolid(dc, band, RGB(rr, gg, bb));
    }
    DrawEllipseAA(dc, R(client.right - 340, -160, client.right + 160, 280), RGB(224, 240, 255), RGB(224, 240, 255));
    DrawEllipseAA(dc, R(-210, client.bottom - 260, 330, client.bottom + 150), RGB(239, 234, 252), RGB(239, 234, 252));
}

void DrawCard(HDC dc, const RECT& r, bool strong = false)
{
    RECT shadow = r;
    OffsetRect(&shadow, 0, 3);
    DrawRounded(dc, shadow, 13.0f, kShadow, kShadow, 0.0f);
    DrawRounded(dc, r, 13.0f, strong ? kCardStrong : kCard, kCardBorder, 1.0f);
}

void DrawToggle(HDC dc, const RECT& r, bool on, bool enabled, bool hovered)
{
    const COLORREF fill = !enabled ? RGB(226, 231, 237)
        : on ? (hovered ? kAccentHover : kAccent)
             : (hovered ? RGB(236, 243, 250) : RGB(246, 248, 251));
    const COLORREF border = !enabled ? RGB(204, 212, 221)
        : on ? fill : RGB(151, 163, 177);
    DrawRounded(dc, r, static_cast<float>(r.bottom - r.top) / 2.0f, fill, border, 1.0f);
    const int d = (r.bottom - r.top) - 6;
    const int x = on ? r.right - d - 3 : r.left + 3;
    DrawEllipseAA(dc, R(x, r.top + 3, x + d, r.top + 3 + d), RGB(255, 255, 255), RGB(210, 216, 224), 1.0f);
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool hovered, bool accent = false, bool enabled = true)
{
    COLORREF fill = enabled
        ? (accent ? (hovered ? kAccentHover : kAccent) : (hovered ? RGB(241, 247, 253) : RGB(255, 255, 255)))
        : RGB(242, 245, 248);
    COLORREF border = enabled
        ? (accent ? fill : (hovered ? RGB(125, 166, 205) : RGB(198, 211, 225)))
        : RGB(217, 224, 232);
    DrawRounded(dc, r, 8.0f, fill, border, 1.0f);
    DrawTextCrisp(dc, text, r, g_fontBody,
                  enabled ? (accent ? RGB(255, 255, 255) : kText) : kDisabled,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawBadge(HDC dc, const RECT& r, const std::wstring& text, COLORREF fill, COLORREF color)
{
    DrawRounded(dc, r, static_cast<float>(r.bottom - r.top) / 2.0f, fill, fill, 0.0f);
    DrawTextCrisp(dc, text, r, g_fontTiny, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawSlider(HDC dc, const RECT& r, int value, bool hovered)
{
    const int minValue = 20;
    const int maxValue = 100;
    const int clamped = std::clamp(value, minValue, maxValue);
    const int cy = (r.top + r.bottom) / 2;
    RECT track{r.left + 4, cy - 2, r.right - 4, cy + 2};
    DrawRounded(dc, track, 2.0f, RGB(218, 225, 234), RGB(218, 225, 234), 0.0f);
    const int x = track.left + MulDiv(clamped - minValue, track.right - track.left, maxValue - minValue);
    RECT active{track.left, track.top, x, track.bottom};
    if (active.right > active.left) DrawRounded(dc, active, 2.0f, kAccent, kAccent, 0.0f);
    RECT knob{x - 7, cy - 7, x + 7, cy + 7};
    DrawEllipseAA(dc, knob, hovered ? RGB(245, 250, 255) : RGB(255, 255, 255), kAccent, 2.0f);
}

std::wstring Trim(std::wstring value)
{
    auto notSpace = [](wchar_t c) { return !iswspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring NormalizeExeName(std::wstring value)
{
    value = Trim(value);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    const size_t slash = value.find_last_of(L"\\/");
    if (slash != std::wstring::npos) value = value.substr(slash + 1);
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    if (!value.empty() && value.find(L'.') == std::wstring::npos) value += L".exe";
    return value;
}

std::wstring GetWindowProcessName(HWND hwnd)
{
    if (!hwnd) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"";
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        result.assign(path, size);
        result = NormalizeExeName(result);
    }
    CloseHandle(process);
    return result;
}

bool IsExcludedName(const std::wstring& name)
{
    const std::wstring normalized = NormalizeExeName(name);
    if (normalized.empty()) return false;
    return std::any_of(g_excludedApps.begin(), g_excludedApps.end(), [&](const std::wstring& item) {
        return _wcsicmp(item.c_str(), normalized.c_str()) == 0;
    });
}

bool IsWindowExcluded(HWND hwnd)
{
    return IsExcludedName(GetWindowProcessName(hwnd));
}

COLORREF GetWindowsAccentColor()
{
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    }
    return kAccent;
}

COLORREF EffectiveBorderColor()
{
    return g_useAccentColor ? GetWindowsAccentColor() : g_customBorderColor;
}

BYTE BorderAlpha()
{
    return static_cast<BYTE>(std::clamp(MulDiv(g_borderOpacity, 255, 100), 1, 255));
}

std::wstring KeyName(UINT vk)
{
    switch (vk) {
        case VK_SPACE: return L"Space";
        case VK_RETURN: return L"Enter";
        case VK_TAB: return L"Tab";
        case VK_BACK: return L"Backspace";
        case VK_DELETE: return L"Delete";
        case VK_INSERT: return L"Insert";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"Page Up";
        case VK_NEXT: return L"Page Down";
        case VK_LEFT: return L"Left";
        case VK_RIGHT: return L"Right";
        case VK_UP: return L"Up";
        case VK_DOWN: return L"Down";
        case VK_ESCAPE: return L"Esc";
    }
    wchar_t name[128]{};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan << 16);
    if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE || vk == VK_NUMLOCK) {
        lParam |= 1 << 24;
    }
    if (GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) > 0) return name;
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    return L"Key " + std::to_wstring(vk);
}

std::wstring HotkeyText(UINT modifiers, UINT vk)
{
    std::wstring text;
    auto append = [&](const wchar_t* part) {
        if (!text.empty()) text += L" + ";
        text += part;
    };
    if (modifiers & MOD_WIN) append(L"Win");
    if (modifiers & MOD_CONTROL) append(L"Ctrl");
    if (modifiers & MOD_ALT) append(L"Alt");
    if (modifiers & MOD_SHIFT) append(L"Shift");
    if (vk) {
        if (!text.empty()) text += L" + ";
        text += KeyName(vk);
    }
    return text.empty() ? L"Not set" : text;
}

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value)
{
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void ReadExcludedApps(HKEY key)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, L"ExcludedApps", nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || size < sizeof(wchar_t)) return;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(key, L"ExcludedApps", nullptr, nullptr,
                         reinterpret_cast<BYTE*>(buffer.data()), &size) != ERROR_SUCCESS) return;
    const wchar_t* p = buffer.data();
    while (*p) {
        std::wstring item = NormalizeExeName(p);
        if (!item.empty() && !IsExcludedName(item)) g_excludedApps.push_back(item);
        p += wcslen(p) + 1;
    }
}

void WriteExcludedApps(HKEY key)
{
    size_t chars = 1;
    for (const auto& item : g_excludedApps) chars += item.size() + 1;
    std::vector<wchar_t> buffer(chars, L'\0');
    wchar_t* p = buffer.data();
    for (const auto& item : g_excludedApps) {
        memcpy(p, item.c_str(), item.size() * sizeof(wchar_t));
        p += item.size() + 1;
    }
    RegSetValueExW(key, L"ExcludedApps", 0, REG_MULTI_SZ,
                   reinterpret_cast<const BYTE*>(buffer.data()),
                   static_cast<DWORD>(buffer.size() * sizeof(wchar_t)));
}

void LoadSettings()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD v = 0;
    if (ReadDword(key, L"Enabled", v)) g_enabled = v != 0;
    if (ReadDword(key, L"Sound", v)) g_sound = v != 0;
    if (ReadDword(key, L"UseAccentColor", v)) g_useAccentColor = v != 0;
    if (ReadDword(key, L"HotkeyModifiers", v)) g_hotkeyModifiers = v & MODIFIER_MASK;
    if (ReadDword(key, L"HotkeyVk", v) && v > 0 && v <= 0xFF) g_hotkeyVk = v;
    if (ReadDword(key, L"BorderColor", v)) g_customBorderColor = static_cast<COLORREF>(v);
    if (ReadDword(key, L"BorderThickness", v) && (v == 1 || v == 2 || v == 3 || v == 4 || v == 6 || v == 8)) {
        g_borderThickness = static_cast<int>(v);
    }
    if (ReadDword(key, L"BorderOpacity", v)) g_borderOpacity = std::clamp(static_cast<int>(v), 20, 100);
    ReadExcludedApps(key);
    RegCloseKey(key);
}

void SaveSettings()
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;
    WriteDword(key, L"Enabled", g_enabled ? 1 : 0);
    WriteDword(key, L"Sound", g_sound ? 1 : 0);
    WriteDword(key, L"UseAccentColor", g_useAccentColor ? 1 : 0);
    WriteDword(key, L"HotkeyModifiers", g_hotkeyModifiers & MODIFIER_MASK);
    WriteDword(key, L"HotkeyVk", g_hotkeyVk);
    WriteDword(key, L"BorderColor", static_cast<DWORD>(g_customBorderColor));
    WriteDword(key, L"BorderThickness", static_cast<DWORD>(g_borderThickness));
    WriteDword(key, L"BorderOpacity", static_cast<DWORD>(g_borderOpacity));
    WriteExcludedApps(key);
    RegCloseKey(key);
}

bool IsBorderWindow(HWND hwnd)
{
    if (!hwnd) return false;
    wchar_t name[128]{};
    GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
    return wcscmp(name, kBorderClass) == 0;
}

bool IsOwnWindow(HWND hwnd)
{
    return !hwnd || hwnd == g_hwnd || hwnd == g_exclusionsHwnd || IsBorderWindow(hwnd);
}

PinnedWindow* FindPinned(HWND hwnd)
{
    for (auto& item : g_pinned) {
        if (item.target == hwnd) return &item;
    }
    return nullptr;
}

LRESULT CALLBACK BorderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillSolid(dc, client, EffectiveBorderColor());
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateBorderWindow(HINSTANCE instance)
{
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kBorderClass, L"", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (hwnd) SetLayeredWindowAttributes(hwnd, 0, BorderAlpha(), LWA_ALPHA);
    return hwnd;
}

bool GetVisibleWindowRect(HWND hwnd, RECT& rect)
{
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(hwnd, &rect)) return false;
    }
    return rect.right > rect.left && rect.bottom > rect.top;
}

void HideBorders(PinnedWindow& item)
{
    for (HWND border : item.borders) if (border) ShowWindow(border, SW_HIDE);
}

void RefreshBorderVisuals()
{
    for (auto& item : g_pinned) {
        for (HWND border : item.borders) {
            if (!border) continue;
            SetLayeredWindowAttributes(border, 0, BorderAlpha(), LWA_ALPHA);
            InvalidateRect(border, nullptr, TRUE);
        }
    }
}

void PositionBorders(PinnedWindow& item)
{
    if (!IsWindow(item.target) || !IsWindowVisible(item.target) || IsIconic(item.target)) {
        HideBorders(item);
        return;
    }
    RECT wr{};
    if (!GetVisibleWindowRect(item.target, wr)) {
        HideBorders(item);
        return;
    }
    const int t = g_borderThickness;
    const RECT edges[4] = {
        R(wr.left, wr.top, wr.right, wr.top + t),
        R(wr.left, wr.bottom - t, wr.right, wr.bottom),
        R(wr.left, wr.top + t, wr.left + t, wr.bottom - t),
        R(wr.right - t, wr.top + t, wr.right, wr.bottom - t)
    };
    for (int i = 0; i < 4; ++i) {
        const RECT& e = edges[i];
        SetWindowPos(item.borders[i], HWND_TOPMOST, e.left, e.top, e.right - e.left, e.bottom - e.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void SetNotice(const std::wstring& text, DWORD durationMs = 3200)
{
    g_noticeText = text;
    g_noticeUntil = GetTickCount64() + durationMs;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void PlayToggleSound(bool pinning)
{
    if (!g_sound) return;
    MessageBeep(pinning ? MB_OK : MB_ICONINFORMATION);
}

void UnpinAt(size_t index)
{
    if (index >= g_pinned.size()) return;
    PinnedWindow item = g_pinned[index];
    if (IsWindow(item.target) && !item.wasTopmost) {
        SetWindowPos(item.target, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    for (HWND border : item.borders) if (border) DestroyWindow(border);
    g_pinned.erase(g_pinned.begin() + static_cast<std::ptrdiff_t>(index));
}

void UnpinWindow(HWND hwnd)
{
    for (size_t i = 0; i < g_pinned.size(); ++i) {
        if (g_pinned[i].target == hwnd) {
            UnpinAt(i);
            PlayToggleSound(false);
            SetNotice(L"Window unpinned");
            return;
        }
    }
}

void PinWindow(HWND hwnd)
{
    if (!hwnd || IsOwnWindow(hwnd) || !IsWindow(hwnd) || FindPinned(hwnd)) return;
    const std::wstring exe = GetWindowProcessName(hwnd);
    if (!exe.empty() && IsExcludedName(exe)) {
        MessageBeep(MB_ICONWARNING);
        SetNotice(L"Skipped excluded app: " + exe);
        return;
    }

    PinnedWindow item{};
    item.target = hwnd;
    item.wasTopmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
    for (HWND& border : item.borders) border = CreateBorderWindow(instance);
    g_pinned.push_back(item);
    PositionBorders(g_pinned.back());
    PlayToggleSound(true);
    SetNotice(L"Window pinned above others");
}

void ToggleWindow(HWND hwnd)
{
    if (!g_enabled || !hwnd || IsOwnWindow(hwnd)) return;
    if (FindPinned(hwnd)) UnpinWindow(hwnd);
    else PinWindow(hwnd);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ClearPinned()
{
    while (!g_pinned.empty()) UnpinAt(g_pinned.size() - 1);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void CleanupInvalidPinned()
{
    for (size_t i = g_pinned.size(); i-- > 0;) {
        if (!IsWindow(g_pinned[i].target)) {
            for (HWND border : g_pinned[i].borders) if (border) DestroyWindow(border);
            g_pinned.erase(g_pinned.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void UpdateExternalForeground()
{
    HWND fg = GetForegroundWindow();
    if (fg && !IsOwnWindow(fg)) g_lastExternalWindow = fg;
}

HWND ResolveTargetWindow()
{
    HWND fg = GetForegroundWindow();
    if (fg && !IsOwnWindow(fg)) return fg;
    if (g_lastExternalWindow && IsWindow(g_lastExternalWindow) && !IsOwnWindow(g_lastExternalWindow)) return g_lastExternalWindow;
    return nullptr;
}

bool RegisterConfiguredHotkey()
{
    if (!g_hwnd || !g_hotkeyVk) return false;
    UnregisterHotKey(g_hwnd, ID_GLOBAL_HOTKEY);
    g_hotkeyRegistered = RegisterHotKey(g_hwnd, ID_GLOBAL_HOTKEY,
                                         (g_hotkeyModifiers & MODIFIER_MASK) | MOD_NOREPEAT,
                                         g_hotkeyVk) != FALSE;
    return g_hotkeyRegistered;
}

void StopHotkeyCapture(bool restoreRegistration)
{
    if (g_hotkeyHook) {
        UnhookWindowsHookEx(g_hotkeyHook);
        g_hotkeyHook = nullptr;
    }
    g_capturingHotkey = false;
    g_capturePosted = false;
    if (restoreRegistration) RegisterConfiguredHotkey();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK HotkeyCaptureProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_capturingHotkey) {
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const KBDLLHOOKSTRUCT* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (down && key && !g_capturePosted) {
            const UINT vk = key->vkCode;
            const bool modifierOnly = vk == VK_LWIN || vk == VK_RWIN || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                                      vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
            if (vk == VK_ESCAPE) {
                g_capturePosted = true;
                PostMessageW(g_hwnd, WM_HOTKEY_CAPTURE_RESULT, 0, 0);
            } else if (!modifierOnly) {
                UINT mods = 0;
                if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
                g_capturePosted = true;
                PostMessageW(g_hwnd, WM_HOTKEY_CAPTURE_RESULT, mods & MODIFIER_MASK, vk);
            }
        }
        return 1;
    }
    return CallNextHookEx(g_hotkeyHook, code, wParam, lParam);
}

void StartHotkeyCapture()
{
    if (g_capturingHotkey) return;
    UnregisterHotKey(g_hwnd, ID_GLOBAL_HOTKEY);
    g_hotkeyRegistered = false;
    g_capturingHotkey = true;
    g_capturePosted = false;
    g_hotkeyHook = SetWindowsHookExW(WH_KEYBOARD_LL, HotkeyCaptureProc, GetModuleHandleW(nullptr), 0);
    if (!g_hotkeyHook) {
        g_capturingHotkey = false;
        RegisterConfiguredHotkey();
        MessageBoxW(g_hwnd, L"The shortcut recorder could not be started.", kAppName, MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ApplyCapturedHotkey(UINT modifiers, UINT vk)
{
    const UINT oldModifiers = g_hotkeyModifiers;
    const UINT oldVk = g_hotkeyVk;
    StopHotkeyCapture(false);

    if (!vk) {
        RegisterConfiguredHotkey();
        SetNotice(L"Shortcut change cancelled");
        return;
    }

    g_hotkeyModifiers = modifiers & MODIFIER_MASK;
    g_hotkeyVk = vk;
    if (RegisterConfiguredHotkey()) {
        SaveSettings();
        SetNotice(L"Shortcut updated to " + HotkeyText(g_hotkeyModifiers, g_hotkeyVk));
    } else {
        g_hotkeyModifiers = oldModifiers;
        g_hotkeyVk = oldVk;
        RegisterConfiguredHotkey();
        MessageBoxW(g_hwnd,
                    L"That shortcut is already in use by Windows or another application. The previous shortcut was restored.",
                    kAppName, MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void SetEnabled(bool enabled)
{
    if (g_enabled == enabled) return;
    g_enabled = enabled;
    if (!enabled) ClearPinned();
    SaveSettings();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

std::wstring TrayTip()
{
    std::wstring tip = L"Always On Top - ";
    tip += g_enabled ? L"On" : L"Off";
    if (!g_pinned.empty()) tip += L" - " + std::to_wstring(g_pinned.size()) + L" pinned";
    return tip;
}

void UpdateTray()
{
    if (!g_hwnd) return;
    std::wstring tip = TrayTip();
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void AddTrayIcon()
{
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    std::wstring tip = TrayTip();
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowMainWindow()
{
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open Always On Top");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_TRAY_ENABLE, L"Enabled");
    AppendMenuW(menu, MF_STRING | (!g_enabled ? MF_GRAYED : 0), ID_TRAY_TOGGLE_ACTIVE, L"Pin / unpin active window");
    AppendMenuW(menu, MF_STRING | (g_pinned.empty() ? MF_GRAYED : 0), ID_TRAY_CLEAR, L"Clear all pinned windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

void ShowThicknessMenu(HWND hwnd)
{
    POINT pt{g_thicknessRect.left, g_thicknessRect.bottom + 4};
    ClientToScreen(hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 1 ? MF_CHECKED : 0), ID_THICKNESS_1, L"1 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 2 ? MF_CHECKED : 0), ID_THICKNESS_2, L"2 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 3 ? MF_CHECKED : 0), ID_THICKNESS_3, L"3 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 4 ? MF_CHECKED : 0), ID_THICKNESS_4, L"4 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 6 ? MF_CHECKED : 0), ID_THICKNESS_6, L"6 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 8 ? MF_CHECKED : 0), ID_THICKNESS_8, L"8 px");
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == ID_THICKNESS_1) g_borderThickness = 1;
    if (cmd == ID_THICKNESS_2) g_borderThickness = 2;
    if (cmd == ID_THICKNESS_3) g_borderThickness = 3;
    if (cmd == ID_THICKNESS_4) g_borderThickness = 4;
    if (cmd == ID_THICKNESS_6) g_borderThickness = 6;
    if (cmd == ID_THICKNESS_8) g_borderThickness = 8;
    if (cmd) {
        SaveSettings();
        for (auto& p : g_pinned) PositionBorders(p);
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

void ChooseCustomBorderColor(HWND hwnd)
{
    COLORREF custom[16]{};
    CHOOSECOLORW choose{};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = hwnd;
    choose.rgbResult = g_customBorderColor;
    choose.lpCustColors = custom;
    choose.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&choose)) {
        g_customBorderColor = choose.rgbResult;
        g_useAccentColor = false;
        SaveSettings();
        RefreshBorderVisuals();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

void RefreshExclusionListControl()
{
    if (!g_exclusionList) return;
    SendMessageW(g_exclusionList, LB_RESETCONTENT, 0, 0);
    for (const auto& item : g_excludedApps) {
        SendMessageW(g_exclusionList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
}

void AddExclusion(const std::wstring& raw)
{
    std::wstring value = NormalizeExeName(raw);
    if (value.empty() || IsExcludedName(value)) return;
    g_excludedApps.push_back(value);
    std::sort(g_excludedApps.begin(), g_excludedApps.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    SaveSettings();
    RefreshExclusionListControl();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void RemoveSelectedExclusion()
{
    if (!g_exclusionList) return;
    const LRESULT index = SendMessageW(g_exclusionList, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR || static_cast<size_t>(index) >= g_excludedApps.size()) return;
    g_excludedApps.erase(g_excludedApps.begin() + index);
    SaveSettings();
    RefreshExclusionListControl();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ApplyModernWindowVisuals(HWND hwnd)
{
    const DWORD cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, static_cast<DWORD>(33), &cornerPreference, sizeof(cornerPreference));
    const DWORD backdrop = 2; // DWMSBT_MAINWINDOW
    DwmSetWindowAttribute(hwnd, static_cast<DWORD>(38), &backdrop, sizeof(backdrop));
}

LRESULT CALLBACK ExclusionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            CreateWindowExW(0, L"STATIC", L"Apps in this list will be ignored when you use the pin shortcut.",
                            WS_CHILD | WS_VISIBLE, 24, 20, 490, 22, hwnd, nullptr, nullptr, nullptr);
            g_exclusionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                               24, 54, 330, 32, hwnd,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_EDIT)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Add",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 364, 54, 70, 32, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_ADD)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Add active app",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 442, 54, 116, 32, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_ACTIVE)), nullptr, nullptr);
            g_exclusionList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL,
                                               24, 100, 534, 220, hwnd,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_LIST)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Remove selected",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24, 334, 132, 34, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_REMOVE)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Done",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 470, 334, 88, 34, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXCLUSION_DONE)), nullptr, nullptr);

            EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);
                SetWindowTheme(child, L"Explorer", nullptr);
                return TRUE;
            }, 0);
            RefreshExclusionListControl();
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_EXCLUSION_ADD: {
                    wchar_t value[512]{};
                    GetWindowTextW(g_exclusionEdit, value, static_cast<int>(std::size(value)));
                    AddExclusion(value);
                    SetWindowTextW(g_exclusionEdit, L"");
                    return 0;
                }
                case IDC_EXCLUSION_ACTIVE: {
                    HWND target = ResolveTargetWindow();
                    std::wstring name = GetWindowProcessName(target);
                    if (name.empty()) {
                        MessageBoxW(hwnd, L"Could not identify the active application's executable name.", kAppName, MB_OK | MB_ICONINFORMATION);
                    } else {
                        AddExclusion(name);
                    }
                    return 0;
                }
                case IDC_EXCLUSION_REMOVE:
                    RemoveSelectedExclusion();
                    return 0;
                case IDC_EXCLUSION_DONE:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_EXCLUSION_LIST:
                    if (HIWORD(wParam) == LBN_DBLCLK) RemoveSelectedExclusion();
                    return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_exclusionsHwnd = nullptr;
            g_exclusionEdit = nullptr;
            g_exclusionList = nullptr;
            EnableWindow(g_hwnd, TRUE);
            ShowWindow(g_hwnd, SW_SHOW);
            SetForegroundWindow(g_hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowExclusionsWindow()
{
    if (g_exclusionsHwnd && IsWindow(g_exclusionsHwnd)) {
        SetForegroundWindow(g_exclusionsHwnd);
        return;
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
    RECT parent{};
    GetWindowRect(g_hwnd, &parent);
    const int width = 600;
    const int height = 430;
    const int x = parent.left + ((parent.right - parent.left) - width) / 2;
    const int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
    g_exclusionsHwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kExclusionsClass,
        L"Excluded apps",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        g_hwnd, nullptr, instance, nullptr);
    if (g_exclusionsHwnd) {
        ApplyModernWindowVisuals(g_exclusionsHwnd);
        EnableWindow(g_hwnd, FALSE);
        ShowWindow(g_exclusionsHwnd, SW_SHOW);
        UpdateWindow(g_exclusionsHwnd);
    }
}

UiHit HitTest(const POINT& pt)
{
    if (PtInRect(&g_enableRect, pt)) return UiHit::EnableToggle;
    if (PtInRect(&g_hotkeyRect, pt)) return UiHit::HotkeyButton;
    if (PtInRect(&g_accentRect, pt)) return UiHit::AccentToggle;
    if (PtInRect(&g_colorRect, pt)) return UiHit::ColorButton;
    if (PtInRect(&g_thicknessRect, pt)) return UiHit::ThicknessButton;
    if (PtInRect(&g_opacityRect, pt)) return UiHit::OpacitySlider;
    if (PtInRect(&g_exclusionsRect, pt)) return UiHit::ExclusionsButton;
    if (PtInRect(&g_soundRect, pt)) return UiHit::SoundToggle;
    if (PtInRect(&g_pinButtonRect, pt)) return UiHit::PinButton;
    return UiHit::None;
}

void UpdateOpacityFromPoint(int x, bool save)
{
    const int left = g_opacityRect.left + 4;
    const int right = g_opacityRect.right - 4;
    if (right <= left) return;
    const int clampedX = std::clamp(x, left, right);
    g_borderOpacity = 20 + MulDiv(clampedX - left, 80, right - left);
    g_borderOpacity = std::clamp(g_borderOpacity, 20, 100);
    RefreshBorderVisuals();
    if (save) SaveSettings();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void DrawInterface(HWND hwnd, HDC dc)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    DrawSoftBackground(dc, client);

    const int left = 38;
    const int right = client.right - 38;

    if (g_appIcon) DrawIconEx(dc, left, 28, g_appIcon, 32, 32, 0, nullptr, DI_NORMAL);
    DrawTextCrisp(dc, L"Always On Top", R(left + 44, 22, right - 100, 61), g_fontTitle, kText);
    DrawTextCrisp(dc, L"Keep important windows visible without installing the full PowerToys suite.",
                  R(left + 44, 59, right - 100, 88), g_fontSmall, kMuted);
    DrawBadge(dc, R(right - 76, 30, right, 56), L"v1.0.0", RGB(236, 242, 249), kMuted);

    RECT master{left, 102, right, 180};
    DrawCard(dc, master, true);
    DrawTextCrisp(dc, L"Always On Top", R(left + 22, 110, left + 320, 140), g_fontSection, kText);
    std::wstring masterSub = g_enabled ? L"Ready — use your shortcut on any active window" : L"Disabled — pinned windows are cleared";
    DrawTextCrisp(dc, masterSub, R(left + 22, 140, left + 520, 168), g_fontSmall, g_enabled ? kMuted : kDisabled);
    const std::wstring pinCount = std::to_wstring(g_pinned.size()) + (g_pinned.size() == 1 ? L" pinned" : L" pinned");
    DrawBadge(dc, R(right - 174, 126, right - 88, 154), pinCount,
              g_pinned.empty() ? RGB(239, 243, 248) : RGB(229, 246, 238),
              g_pinned.empty() ? kMuted : kSuccess);
    g_enableRect = R(right - 66, 126, right - 14, 156);
    DrawToggle(dc, g_enableRect, g_enabled, true, g_hover == UiHit::EnableToggle);

    RECT shortcut{left, 196, right, 282};
    DrawCard(dc, shortcut);
    DrawTextCrisp(dc, L"Shortcut", R(left + 22, 206, left + 240, 235), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextCrisp(dc,
                  g_capturingHotkey ? L"Press your new shortcut now — Esc cancels" :
                  (g_hotkeyRegistered ? L"Click the shortcut to record any key combination" : L"Shortcut unavailable — choose another combination"),
                  R(left + 22, 235, left + 510, 265), g_fontSmall,
                  g_hotkeyRegistered || g_capturingHotkey ? kMuted : RGB(184, 55, 55));
    g_hotkeyRect = R(right - 315, 216, right - 18, 262);
    if (g_capturingHotkey) {
        DrawRounded(dc, g_hotkeyRect, 8.0f, kAccentSoft, kAccent, 1.5f);
        DrawTextCrisp(dc, L"Press shortcut…", g_hotkeyRect, g_fontBody, kAccent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        DrawButton(dc, g_hotkeyRect, HotkeyText(g_hotkeyModifiers, g_hotkeyVk), g_hover == UiHit::HotkeyButton, false, g_enabled);
    }

    RECT appearance{left, 298, right, 452};
    DrawCard(dc, appearance);
    DrawTextCrisp(dc, L"Border appearance", R(left + 22, 306, left + 250, 336), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextCrisp(dc, L"Make pinned windows easy to spot without a heavy visual outline.",
                  R(left + 22, 333, left + 520, 359), g_fontSmall, g_enabled ? kMuted : kDisabled);

    DrawTextCrisp(dc, L"Windows accent color", R(left + 22, 370, left + 230, 398), g_fontSmall, g_enabled ? kText : kDisabled);
    g_accentRect = R(left + 230, 370, left + 280, 398);
    DrawToggle(dc, g_accentRect, g_useAccentColor, g_enabled, g_hover == UiHit::AccentToggle);

    g_colorRect = R(left + 300, 364, left + 475, 405);
    RECT colorSwatch{g_colorRect.left + 12, g_colorRect.top + 10, g_colorRect.left + 34, g_colorRect.bottom - 10};
    DrawButton(dc, g_colorRect, L"", g_hover == UiHit::ColorButton, false, g_enabled && !g_useAccentColor);
    DrawRounded(dc, colorSwatch, 5.0f, EffectiveBorderColor(), EffectiveBorderColor(), 0.0f);
    DrawTextCrisp(dc, g_useAccentColor ? L"Using accent" : L"Custom color",
                  R(g_colorRect.left + 44, g_colorRect.top, g_colorRect.right - 8, g_colorRect.bottom),
                  g_fontSmall, g_enabled ? (g_useAccentColor ? kMuted : kText) : kDisabled);

    DrawTextCrisp(dc, L"Thickness", R(right - 300, 370, right - 220, 398), g_fontSmall, g_enabled ? kText : kDisabled);
    g_thicknessRect = R(right - 215, 364, right - 120, 405);
    DrawButton(dc, g_thicknessRect, std::to_wstring(g_borderThickness) + L" px", g_hover == UiHit::ThicknessButton, false, g_enabled);

    DrawTextCrisp(dc, L"Opacity", R(left + 22, 414, left + 90, 442), g_fontSmall, g_enabled ? kText : kDisabled);
    g_opacityRect = R(left + 92, 412, right - 90, 444);
    DrawSlider(dc, g_opacityRect, g_borderOpacity, g_hover == UiHit::OpacitySlider || g_dragOpacity);
    DrawTextCrisp(dc, std::to_wstring(g_borderOpacity) + L"%", R(right - 82, 412, right - 18, 444), g_fontSmall,
                  g_enabled ? kText : kDisabled, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int gap = 14;
    const int half = (right - left - gap) / 2;
    RECT exclusions{left, 468, left + half, 544};
    RECT sound{left + half + gap, 468, right, 544};
    DrawCard(dc, exclusions);
    DrawCard(dc, sound);

    DrawTextCrisp(dc, L"Excluded apps", R(exclusions.left + 20, 476, exclusions.right - 140, 506), g_fontBody, kText);
    std::wstring excludeSub = g_excludedApps.empty() ? L"No apps excluded" : std::to_wstring(g_excludedApps.size()) + L" app" + (g_excludedApps.size() == 1 ? L" excluded" : L"s excluded");
    DrawTextCrisp(dc, excludeSub, R(exclusions.left + 20, 505, exclusions.right - 140, 532), g_fontSmall, kMuted);
    g_exclusionsRect = R(exclusions.right - 128, 486, exclusions.right - 16, 526);
    DrawButton(dc, g_exclusionsRect, L"Manage", g_hover == UiHit::ExclusionsButton);

    DrawTextCrisp(dc, L"Sound feedback", R(sound.left + 20, 476, sound.right - 90, 506), g_fontBody, kText);
    DrawTextCrisp(dc, L"Play a sound when pinning or unpinning", R(sound.left + 20, 505, sound.right - 90, 532), g_fontSmall, kMuted);
    g_soundRect = R(sound.right - 66, 491, sound.right - 14, 521);
    DrawToggle(dc, g_soundRect, g_sound, true, g_hover == UiHit::SoundToggle);

    const bool noticeVisible = !g_noticeText.empty() && GetTickCount64() < g_noticeUntil;
    std::wstring footer = noticeVisible ? g_noticeText :
        (g_pinned.empty() ? L"No windows are currently pinned" :
         std::to_wstring(g_pinned.size()) + (g_pinned.size() == 1 ? L" window is pinned" : L" windows are pinned"));
    DrawTextCrisp(dc, footer, R(left, 558, right - 300, 600), g_fontSmall, noticeVisible ? kAccent : kMuted);
    g_pinButtonRect = R(right - 285, 556, right, 600);
    DrawButton(dc, g_pinButtonRect, L"Pin / unpin active window", g_hover == UiHit::PinButton, true, g_enabled);
}

void EnableBestDpiAwareness()
{
    using Fn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn && fn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;
    }
    SetProcessDPIAware();
}

void CreateFonts()
{
    g_fontTitle = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSection = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBody = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontTiny = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void DeleteFonts()
{
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontSection) DeleteObject(g_fontSection);
    if (g_fontBody) DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    if (g_fontTiny) DeleteObject(g_fontTiny);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, TIMER_TRACK, 100, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_TRACK) {
                UpdateExternalForeground();
                CleanupInvalidPinned();
                for (auto& item : g_pinned) PositionBorders(item);
                if (!g_noticeText.empty() && g_noticeUntil != 0 && GetTickCount64() >= g_noticeUntil) {
                    g_noticeText.clear();
                    g_noticeUntil = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_HOTKEY:
            if (wParam == ID_GLOBAL_HOTKEY && g_enabled && !g_capturingHotkey) {
                ToggleWindow(ResolveTargetWindow());
                UpdateTray();
            }
            return 0;

        case WM_HOTKEY_CAPTURE_RESULT:
            ApplyCapturedHotkey(static_cast<UINT>(wParam), static_cast<UINT>(lParam));
            return 0;

        case WM_DWMCOLORIZATIONCOLORCHANGED:
            if (g_useAccentColor) {
                RefreshBorderVisuals();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ old = SelectObject(mem, bitmap);
            DrawInterface(hwnd, mem);
            BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bitmap);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (HitTest(pt) == UiHit::OpacitySlider && g_enabled) {
                g_dragOpacity = true;
                SetCapture(hwnd);
                UpdateOpacityFromPoint(pt.x, false);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_dragOpacity) {
                UpdateOpacityFromPoint(pt.x, false);
                return 0;
            }
            UiHit hit = HitTest(pt);
            if (hit != g_hover) {
                g_hover = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (!g_trackingMouse) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                g_trackingMouse = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            if (g_hover != UiHit::None) {
                g_hover = UiHit::None;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                UiHit hit = HitTest(pt);
                if (hit != UiHit::None) {
                    SetCursor(LoadCursorW(nullptr, hit == UiHit::OpacitySlider ? IDC_SIZEWE : IDC_HAND));
                    return TRUE;
                }
            }
            break;

        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_dragOpacity) {
                g_dragOpacity = false;
                ReleaseCapture();
                UpdateOpacityFromPoint(pt.x, true);
                return 0;
            }
            switch (HitTest(pt)) {
                case UiHit::EnableToggle:
                    SetEnabled(!g_enabled);
                    UpdateTray();
                    return 0;
                case UiHit::HotkeyButton:
                    if (g_enabled) StartHotkeyCapture();
                    return 0;
                case UiHit::AccentToggle:
                    if (g_enabled) {
                        g_useAccentColor = !g_useAccentColor;
                        SaveSettings();
                        RefreshBorderVisuals();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                case UiHit::ColorButton:
                    if (g_enabled && !g_useAccentColor) ChooseCustomBorderColor(hwnd);
                    return 0;
                case UiHit::ThicknessButton:
                    if (g_enabled) ShowThicknessMenu(hwnd);
                    return 0;
                case UiHit::ExclusionsButton:
                    ShowExclusionsWindow();
                    return 0;
                case UiHit::SoundToggle:
                    g_sound = !g_sound;
                    SaveSettings();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case UiHit::PinButton:
                    if (g_enabled) {
                        ToggleWindow(ResolveTargetWindow());
                        UpdateTray();
                    }
                    return 0;
                default:
                    break;
            }
            return 0;
        }

        case WM_CAPTURECHANGED:
            if (g_dragOpacity) {
                g_dragOpacity = false;
                SaveSettings();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) ShowMainWindow();
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_OPEN:
                    ShowMainWindow();
                    break;
                case ID_TRAY_ENABLE:
                    SetEnabled(!g_enabled);
                    UpdateTray();
                    break;
                case ID_TRAY_TOGGLE_ACTIVE:
                    ToggleWindow(ResolveTargetWindow());
                    UpdateTray();
                    break;
                case ID_TRAY_CLEAR:
                    ClearPinned();
                    UpdateTray();
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            StopHotkeyCapture(false);
            KillTimer(hwnd, TIMER_TRACK);
            UnregisterHotKey(hwnd, ID_GLOBAL_HOTKEY);
            ClearPinned();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    EnableBestDpiAwareness();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    LoadSettings();

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    WNDCLASSEXW borderClass{};
    borderClass.cbSize = sizeof(borderClass);
    borderClass.lpfnWndProc = BorderProc;
    borderClass.hInstance = hInstance;
    borderClass.lpszClassName = kBorderClass;
    borderClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&borderClass)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }

    g_appIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    CreateFonts();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW exclusionClass{};
    exclusionClass.cbSize = sizeof(exclusionClass);
    exclusionClass.lpfnWndProc = ExclusionsProc;
    exclusionClass.hInstance = hInstance;
    exclusionClass.lpszClassName = kExclusionsClass;
    exclusionClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    exclusionClass.hIcon = g_appIcon;
    exclusionClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&exclusionClass)) {
        DeleteFonts();
        if (g_appIcon) DestroyIcon(g_appIcon);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 4;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc)) {
        DeleteFonts();
        if (g_appIcon) DestroyIcon(g_appIcon);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 5;
    }

    g_hwnd = CreateWindowExW(
        0, kWindowClass, L"Always On Top 1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 660,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        DeleteFonts();
        if (g_appIcon) DestroyIcon(g_appIcon);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 6;
    }

    ApplyModernWindowVisuals(g_hwnd);
    RegisterConfiguredHotkey();
    AddTrayIcon();
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteFonts();
    if (g_appIcon) DestroyIcon(g_appIcon);
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
