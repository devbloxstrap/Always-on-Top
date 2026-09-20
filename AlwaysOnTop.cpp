#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
constexpr wchar_t kWindowClass[] = L"AlwaysOnTopStandaloneWindow";
constexpr wchar_t kBorderClass[] = L"AlwaysOnTopBorderWindow";
constexpr wchar_t kAppName[] = L"Always On Top";
constexpr wchar_t kVersion[] = L"1.0.0";
constexpr wchar_t kMutexName[] = L"Local\\devbloxstrap.AlwaysOnTop";
constexpr wchar_t kRegistryKey[] = L"Software\\devbloxstrap\\AlwaysOnTop";

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT ID_GLOBAL_HOTKEY = 1;
constexpr UINT_PTR TIMER_TRACK = 1;

constexpr UINT ID_TRAY_OPEN = 1000;
constexpr UINT ID_TRAY_ENABLE = 1001;
constexpr UINT ID_TRAY_TOGGLE_ACTIVE = 1002;
constexpr UINT ID_TRAY_CLEAR = 1003;
constexpr UINT ID_TRAY_EXIT = 1099;

constexpr UINT ID_HOTKEY_PRESET_1 = 2001;
constexpr UINT ID_HOTKEY_PRESET_2 = 2002;
constexpr UINT ID_HOTKEY_PRESET_3 = 2003;
constexpr UINT ID_HOTKEY_PRESET_4 = 2004;
constexpr UINT ID_COLOR_BLUE = 2101;
constexpr UINT ID_COLOR_PURPLE = 2102;
constexpr UINT ID_COLOR_GREEN = 2103;
constexpr UINT ID_COLOR_ORANGE = 2104;
constexpr UINT ID_THICKNESS_2 = 2202;
constexpr UINT ID_THICKNESS_3 = 2203;
constexpr UINT ID_THICKNESS_4 = 2204;
constexpr UINT ID_THICKNESS_6 = 2206;

enum class HotkeyPreset : DWORD {
    WinCtrlT = 0,
    CtrlAltT = 1,
    WinShiftT = 2,
    CtrlShiftSpace = 3,
};

enum class UiHit : int {
    None = 0,
    EnableToggle,
    HotkeyButton,
    ColorButton,
    ThicknessButton,
    SoundToggle,
    PinButton,
};

struct PinnedWindow {
    HWND target{};
    HWND borders[4]{};
    bool wasTopmost = false;
};

HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_appIcon = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSection = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontSmall = nullptr;
ULONG_PTR g_gdiplusToken = 0;
UINT g_taskbarCreated = 0;

bool g_enabled = true;
bool g_sound = true;
HotkeyPreset g_hotkeyPreset = HotkeyPreset::WinCtrlT;
bool g_hotkeyRegistered = false;
COLORREF g_borderColor = RGB(0, 120, 212);
int g_borderThickness = 3;
std::vector<PinnedWindow> g_pinned;
HWND g_lastExternalWindow = nullptr;

RECT g_enableRect{};
RECT g_hotkeyRect{};
RECT g_colorRect{};
RECT g_thicknessRect{};
RECT g_soundRect{};
RECT g_pinButtonRect{};
UiHit g_hover = UiHit::None;
bool g_trackingMouse = false;

const COLORREF kBgTop = RGB(242, 247, 252);
const COLORREF kBgBottom = RGB(231, 239, 249);
const COLORREF kCard = RGB(252, 253, 255);
const COLORREF kCardBorder = RGB(205, 216, 229);
const COLORREF kShadow = RGB(208, 218, 230);
const COLORREF kText = RGB(24, 33, 47);
const COLORREF kMuted = RGB(92, 104, 122);
const COLORREF kAccent = RGB(0, 120, 212);
const COLORREF kAccentHover = RGB(0, 104, 184);
const COLORREF kDisabled = RGB(160, 169, 181);

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
    constexpr int bands = 44;
    for (int i = 0; i < bands; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(bands - 1);
        const int rr = static_cast<int>(GetRValue(kBgTop) * (1.0 - t) + GetRValue(kBgBottom) * t);
        const int gg = static_cast<int>(GetGValue(kBgTop) * (1.0 - t) + GetGValue(kBgBottom) * t);
        const int bb = static_cast<int>(GetBValue(kBgTop) * (1.0 - t) + GetBValue(kBgBottom) * t);
        RECT band{0, (h * i) / bands, client.right, (h * (i + 1)) / bands + 1};
        FillSolid(dc, band, RGB(rr, gg, bb));
    }
    DrawEllipseAA(dc, R(client.right - 330, -130, client.right + 120, 260), RGB(226, 241, 255), RGB(226, 241, 255));
    DrawEllipseAA(dc, R(-180, client.bottom - 250, 320, client.bottom + 140), RGB(238, 233, 252), RGB(238, 233, 252));
}

void DrawCard(HDC dc, const RECT& r)
{
    RECT shadow = r;
    OffsetRect(&shadow, 0, 3);
    DrawRounded(dc, shadow, 12.0f, kShadow, kShadow, 0.0f);
    DrawRounded(dc, r, 12.0f, kCard, kCardBorder, 1.0f);
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
    DrawEllipseAA(dc, R(x, r.top + 3, x + d, r.top + 3 + d), RGB(255,255,255), RGB(210,216,224), 1.0f);
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool hovered, bool accent = false)
{
    COLORREF fill = accent ? (hovered ? kAccentHover : kAccent)
                           : (hovered ? RGB(241, 247, 253) : RGB(255, 255, 255));
    COLORREF border = accent ? fill : (hovered ? RGB(125, 166, 205) : RGB(198, 211, 225));
    DrawRounded(dc, r, 8.0f, fill, border, 1.0f);
    DrawTextCrisp(dc, text, r, g_fontBody, accent ? RGB(255,255,255) : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawColorButton(HDC dc, const RECT& r, bool hovered)
{
    DrawButton(dc, r, L"", hovered, false);
    RECT swatch = R(r.left + 14, r.top + 10, r.left + 34, r.bottom - 10);
    DrawRounded(dc, swatch, 5.0f, g_borderColor, g_borderColor, 0.0f);
    DrawTextCrisp(dc, L"Border color", R(r.left + 44, r.top, r.right - 12, r.bottom), g_fontBody, kText);
}

std::wstring HotkeyText(HotkeyPreset p)
{
    switch (p) {
        case HotkeyPreset::WinCtrlT: return L"Win + Ctrl + T";
        case HotkeyPreset::CtrlAltT: return L"Ctrl + Alt + T";
        case HotkeyPreset::WinShiftT: return L"Win + Shift + T";
        case HotkeyPreset::CtrlShiftSpace: return L"Ctrl + Shift + Space";
    }
    return L"Win + Ctrl + T";
}

void HotkeyParts(HotkeyPreset p, UINT& modifiers, UINT& vk)
{
    switch (p) {
        case HotkeyPreset::WinCtrlT:
            modifiers = MOD_WIN | MOD_CONTROL | MOD_NOREPEAT; vk = 'T'; break;
        case HotkeyPreset::CtrlAltT:
            modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT; vk = 'T'; break;
        case HotkeyPreset::WinShiftT:
            modifiers = MOD_WIN | MOD_SHIFT | MOD_NOREPEAT; vk = 'T'; break;
        case HotkeyPreset::CtrlShiftSpace:
            modifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT; vk = VK_SPACE; break;
    }
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

void LoadSettings()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD v = 0;
    if (ReadDword(key, L"Enabled", v)) g_enabled = v != 0;
    if (ReadDword(key, L"Sound", v)) g_sound = v != 0;
    if (ReadDword(key, L"HotkeyPreset", v) && v <= static_cast<DWORD>(HotkeyPreset::CtrlShiftSpace)) {
        g_hotkeyPreset = static_cast<HotkeyPreset>(v);
    }
    if (ReadDword(key, L"BorderColor", v)) g_borderColor = static_cast<COLORREF>(v);
    if (ReadDword(key, L"BorderThickness", v) && (v == 2 || v == 3 || v == 4 || v == 6)) {
        g_borderThickness = static_cast<int>(v);
    }
    RegCloseKey(key);
}

void SaveSettings()
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;
    WriteDword(key, L"Enabled", g_enabled ? 1 : 0);
    WriteDword(key, L"Sound", g_sound ? 1 : 0);
    WriteDword(key, L"HotkeyPreset", static_cast<DWORD>(g_hotkeyPreset));
    WriteDword(key, L"BorderColor", static_cast<DWORD>(g_borderColor));
    WriteDword(key, L"BorderThickness", static_cast<DWORD>(g_borderThickness));
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
    return !hwnd || hwnd == g_hwnd || IsBorderWindow(hwnd);
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
            FillSolid(dc, client, g_borderColor);
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
    if (hwnd) SetLayeredWindowAttributes(hwnd, 0, 230, LWA_ALPHA);
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
    const int width = wr.right - wr.left;
    const int height = wr.bottom - wr.top;
    const RECT edges[4] = {
        R(wr.left, wr.top, wr.right, wr.top + t),
        R(wr.left, wr.bottom - t, wr.right, wr.bottom),
        R(wr.left, wr.top + t, wr.left + t, wr.bottom - t),
        R(wr.right - t, wr.top + t, wr.right, wr.bottom - t)
    };
    (void)width;
    (void)height;
    for (int i = 0; i < 4; ++i) {
        const RECT& e = edges[i];
        SetWindowPos(item.borders[i], HWND_TOPMOST, e.left, e.top, e.right - e.left, e.bottom - e.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(item.borders[i], nullptr, TRUE);
    }
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
            return;
        }
    }
}

void PinWindow(HWND hwnd)
{
    if (!hwnd || IsOwnWindow(hwnd) || !IsWindow(hwnd) || FindPinned(hwnd)) return;
    PinnedWindow item{};
    item.target = hwnd;
    item.wasTopmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE));
    for (HWND& border : item.borders) border = CreateBorderWindow(instance);
    g_pinned.push_back(item);
    PositionBorders(g_pinned.back());
    PlayToggleSound(true);
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
    InvalidateRect(g_hwnd, nullptr, FALSE);
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
    if (!g_hwnd) return false;
    UnregisterHotKey(g_hwnd, ID_GLOBAL_HOTKEY);
    UINT mods = 0, vk = 0;
    HotkeyParts(g_hotkeyPreset, mods, vk);
    g_hotkeyRegistered = RegisterHotKey(g_hwnd, ID_GLOBAL_HOTKEY, mods, vk) != FALSE;
    return g_hotkeyRegistered;
}

bool SetHotkeyPreset(HotkeyPreset preset, bool notifyOnFailure)
{
    HotkeyPreset previous = g_hotkeyPreset;
    g_hotkeyPreset = preset;
    if (RegisterConfiguredHotkey()) {
        SaveSettings();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return true;
    }
    g_hotkeyPreset = previous;
    RegisterConfiguredHotkey();
    if (notifyOnFailure) {
        MessageBoxW(g_hwnd, L"That shortcut is already in use by another application. The previous shortcut was restored.",
                    kAppName, MB_OK | MB_ICONWARNING);
    }
    return false;
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

void ShowHotkeyMenu(HWND hwnd)
{
    POINT pt{g_hotkeyRect.left, g_hotkeyRect.bottom + 4};
    ClientToScreen(hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_hotkeyPreset == HotkeyPreset::WinCtrlT ? MF_CHECKED : 0), ID_HOTKEY_PRESET_1, L"Win + Ctrl + T");
    AppendMenuW(menu, MF_STRING | (g_hotkeyPreset == HotkeyPreset::CtrlAltT ? MF_CHECKED : 0), ID_HOTKEY_PRESET_2, L"Ctrl + Alt + T");
    AppendMenuW(menu, MF_STRING | (g_hotkeyPreset == HotkeyPreset::WinShiftT ? MF_CHECKED : 0), ID_HOTKEY_PRESET_3, L"Win + Shift + T");
    AppendMenuW(menu, MF_STRING | (g_hotkeyPreset == HotkeyPreset::CtrlShiftSpace ? MF_CHECKED : 0), ID_HOTKEY_PRESET_4, L"Ctrl + Shift + Space");
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == ID_HOTKEY_PRESET_1) SetHotkeyPreset(HotkeyPreset::WinCtrlT, true);
    if (cmd == ID_HOTKEY_PRESET_2) SetHotkeyPreset(HotkeyPreset::CtrlAltT, true);
    if (cmd == ID_HOTKEY_PRESET_3) SetHotkeyPreset(HotkeyPreset::WinShiftT, true);
    if (cmd == ID_HOTKEY_PRESET_4) SetHotkeyPreset(HotkeyPreset::CtrlShiftSpace, true);
}

void ShowColorMenu(HWND hwnd)
{
    POINT pt{g_colorRect.left, g_colorRect.bottom + 4};
    ClientToScreen(hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_borderColor == RGB(0,120,212) ? MF_CHECKED : 0), ID_COLOR_BLUE, L"Blue");
    AppendMenuW(menu, MF_STRING | (g_borderColor == RGB(124,58,237) ? MF_CHECKED : 0), ID_COLOR_PURPLE, L"Purple");
    AppendMenuW(menu, MF_STRING | (g_borderColor == RGB(22,163,74) ? MF_CHECKED : 0), ID_COLOR_GREEN, L"Green");
    AppendMenuW(menu, MF_STRING | (g_borderColor == RGB(234,88,12) ? MF_CHECKED : 0), ID_COLOR_ORANGE, L"Orange");
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == ID_COLOR_BLUE) g_borderColor = RGB(0,120,212);
    if (cmd == ID_COLOR_PURPLE) g_borderColor = RGB(124,58,237);
    if (cmd == ID_COLOR_GREEN) g_borderColor = RGB(22,163,74);
    if (cmd == ID_COLOR_ORANGE) g_borderColor = RGB(234,88,12);
    if (cmd) {
        SaveSettings();
        for (auto& p : g_pinned) for (HWND b : p.borders) if (b) InvalidateRect(b, nullptr, TRUE);
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

void ShowThicknessMenu(HWND hwnd)
{
    POINT pt{g_thicknessRect.left, g_thicknessRect.bottom + 4};
    ClientToScreen(hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 2 ? MF_CHECKED : 0), ID_THICKNESS_2, L"2 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 3 ? MF_CHECKED : 0), ID_THICKNESS_3, L"3 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 4 ? MF_CHECKED : 0), ID_THICKNESS_4, L"4 px");
    AppendMenuW(menu, MF_STRING | (g_borderThickness == 6 ? MF_CHECKED : 0), ID_THICKNESS_6, L"6 px");
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == ID_THICKNESS_2) g_borderThickness = 2;
    if (cmd == ID_THICKNESS_3) g_borderThickness = 3;
    if (cmd == ID_THICKNESS_4) g_borderThickness = 4;
    if (cmd == ID_THICKNESS_6) g_borderThickness = 6;
    if (cmd) {
        SaveSettings();
        for (auto& p : g_pinned) PositionBorders(p);
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

UiHit HitTest(const POINT& pt)
{
    if (PtInRect(&g_enableRect, pt)) return UiHit::EnableToggle;
    if (PtInRect(&g_hotkeyRect, pt)) return UiHit::HotkeyButton;
    if (PtInRect(&g_colorRect, pt)) return UiHit::ColorButton;
    if (PtInRect(&g_thicknessRect, pt)) return UiHit::ThicknessButton;
    if (PtInRect(&g_soundRect, pt)) return UiHit::SoundToggle;
    if (PtInRect(&g_pinButtonRect, pt)) return UiHit::PinButton;
    return UiHit::None;
}

void DrawInterface(HWND hwnd, HDC dc)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    DrawSoftBackground(dc, client);

    const int left = 42;
    const int right = client.right - 42;
    DrawTextCrisp(dc, L"Always On Top", R(left, 30, right, 78), g_fontTitle, kText);
    DrawTextCrisp(dc, L"Pin any window above others with a global shortcut.", R(left, 75, right, 103), g_fontBody, kMuted);

    RECT master{left, 122, right, 204};
    DrawCard(dc, master);
    DrawTextCrisp(dc, L"Enable Always On Top", R(left + 22, 132, left + 390, 164), g_fontSection, kText);
    DrawTextCrisp(dc, L"Use the shortcut to pin or unpin the active window.", R(left + 22, 162, left + 520, 190), g_fontSmall, kMuted);
    g_enableRect = R(right - 70, 146, right - 18, 176);
    DrawToggle(dc, g_enableRect, g_enabled, true, g_hover == UiHit::EnableToggle);

    RECT hotkeyCard{left, 220, right, 304};
    DrawCard(dc, hotkeyCard);
    DrawTextCrisp(dc, L"Shortcut", R(left + 22, 232, left + 240, 262), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextCrisp(dc, g_hotkeyRegistered ? L"Global hotkey is active" : L"Shortcut unavailable", R(left + 22, 260, left + 350, 288), g_fontSmall,
                  g_hotkeyRegistered ? kMuted : RGB(185, 55, 55));
    g_hotkeyRect = R(right - 270, 238, right - 18, 282);
    DrawButton(dc, g_hotkeyRect, HotkeyText(g_hotkeyPreset), g_hover == UiHit::HotkeyButton, false);

    RECT appearance{left, 320, right, 420};
    DrawCard(dc, appearance);
    DrawTextCrisp(dc, L"Pinned-window border", R(left + 22, 330, left + 300, 360), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextCrisp(dc, L"A subtle border makes pinned windows easy to identify.", R(left + 22, 358, left + 460, 386), g_fontSmall, g_enabled ? kMuted : kDisabled);
    g_colorRect = R(right - 340, 340, right - 175, 386);
    g_thicknessRect = R(right - 160, 340, right - 18, 386);
    DrawColorButton(dc, g_colorRect, g_hover == UiHit::ColorButton);
    DrawButton(dc, g_thicknessRect, std::to_wstring(g_borderThickness) + L" px", g_hover == UiHit::ThicknessButton, false);

    RECT soundCard{left, 436, right, 504};
    DrawCard(dc, soundCard);
    DrawTextCrisp(dc, L"Sound feedback", R(left + 22, 445, left + 270, 474), g_fontBody, kText);
    DrawTextCrisp(dc, L"Play a short system sound when a window is pinned or unpinned.", R(left + 22, 472, left + 560, 493), g_fontSmall, kMuted);
    g_soundRect = R(right - 70, 455, right - 18, 485);
    DrawToggle(dc, g_soundRect, g_sound, true, g_hover == UiHit::SoundToggle);

    const std::wstring pinText = g_pinned.empty()
        ? L"No windows pinned"
        : std::to_wstring(g_pinned.size()) + (g_pinned.size() == 1 ? L" window pinned" : L" windows pinned");
    DrawTextCrisp(dc, pinText, R(left, 520, left + 300, 552), g_fontSmall, kMuted);

    g_pinButtonRect = R(right - 270, 516, right, 560);
    DrawButton(dc, g_pinButtonRect, L"Pin / unpin active window", g_hover == UiHit::PinButton, true);
}

void ApplyModernWindowVisuals(HWND hwnd)
{
    // Rounded corners on Windows 11; ignored on older versions.
    const DWORD cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, static_cast<DWORD>(33), &cornerPreference, sizeof(cornerPreference));

    // Mica main-window backdrop on supported Windows 11 builds; harmless if unavailable.
    const DWORD backdrop = 2; // DWMSBT_MAINWINDOW
    DwmSetWindowAttribute(hwnd, static_cast<DWORD>(38), &backdrop, sizeof(backdrop));
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
    g_fontTitle = CreateFontW(-34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSection = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBody = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void DeleteFonts()
{
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontSection) DeleteObject(g_fontSection);
    if (g_fontBody) DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
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
            }
            return 0;

        case WM_HOTKEY:
            if (wParam == ID_GLOBAL_HOTKEY && g_enabled) {
                ToggleWindow(ResolveTargetWindow());
                UpdateTray();
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

        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
                if (HitTest(pt) != UiHit::None) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;

        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            switch (HitTest(pt)) {
                case UiHit::EnableToggle:
                    SetEnabled(!g_enabled);
                    UpdateTray();
                    return 0;
                case UiHit::HotkeyButton:
                    ShowHotkeyMenu(hwnd);
                    return 0;
                case UiHit::ColorButton:
                    ShowColorMenu(hwnd);
                    return 0;
                case UiHit::ThicknessButton:
                    ShowThicknessMenu(hwnd);
                    return 0;
                case UiHit::SoundToggle:
                    g_sound = !g_sound;
                    SaveSettings();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case UiHit::PinButton:
                    ToggleWindow(ResolveTargetWindow());
                    UpdateTray();
                    return 0;
                default:
                    break;
            }
            return 0;
        }

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
        return 4;
    }

    g_hwnd = CreateWindowExW(
        0, kWindowClass, L"Always On Top 1.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        DeleteFonts();
        if (g_appIcon) DestroyIcon(g_appIcon);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 5;
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
