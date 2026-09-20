use super::{BorderCommand, BorderController};
use std::{
    collections::HashMap,
    ffi::c_void,
    mem::{size_of, zeroed},
    ptr::{null, null_mut},
    sync::mpsc,
    thread,
    time::Duration,
};
use windows_sys::Win32::{
    Foundation::{CloseHandle, HWND, LPARAM, LRESULT, RECT, WPARAM},
    Graphics::{
        Dwm::{DwmGetColorizationColor, DwmGetWindowAttribute, DWMWA_EXTENDED_FRAME_BOUNDS},
        Gdi::{BeginPaint, CreateSolidBrush, DeleteObject, EndPaint, FillRect, InvalidateRect, PAINTSTRUCT},
    },
    System::{
        LibraryLoader::GetModuleHandleW,
        Threading::{OpenProcess, QueryFullProcessImageNameW, PROCESS_QUERY_LIMITED_INFORMATION},
    },
    UI::WindowsAndMessaging::{
        CreateWindowExW, DefWindowProcW, DestroyWindow, GetClientRect, GetForegroundWindow,
        GetLayeredWindowAttributes, GetWindowLongPtrW, GetWindowRect, GetWindowTextLengthW,
        GetWindowTextW, GetWindowThreadProcessId, IsIconic, IsWindow, IsWindowVisible,
        LoadCursorW, PeekMessageW, RegisterClassW, SetLayeredWindowAttributes, SetWindowLongPtrW,
        SetWindowPos, ShowWindow, TranslateMessage, DispatchMessageW, CS_HREDRAW, CS_VREDRAW,
        GWL_EXSTYLE, GWLP_USERDATA, HTTRANSPARENT, HWND_NOTOPMOST, HWND_TOPMOST, IDC_ARROW,
        LWA_ALPHA, MSG, PM_REMOVE, SW_HIDE, SW_SHOWNOACTIVATE, SWP_NOACTIVATE, SWP_NOMOVE,
        SWP_NOSIZE, SWP_NOOWNERZORDER, WM_ERASEBKGND, WM_NCHITTEST, WM_PAINT, WNDCLASSW,
        WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_EX_TRANSPARENT,
        WS_POPUP,
    },
};

#[derive(Debug, Clone)]
pub struct WindowInfo {
    pub title: String,
    pub exe: String,
    pub pid: u32,
}

#[derive(Debug, Clone, Copy)]
pub struct OriginalStyle {
    pub exstyle: isize,
    pub alpha: u8,
    pub original_layered: bool,
    pub was_topmost: bool,
}

#[inline]
fn to_hwnd(raw: isize) -> HWND {
    raw as HWND
}

pub fn foreground_window() -> isize {
    unsafe { GetForegroundWindow() as isize }
}

pub fn is_window(hwnd: isize) -> bool {
    hwnd != 0 && unsafe { IsWindow(to_hwnd(hwnd)) != 0 }
}

fn wide_to_string(buf: &[u16]) -> String {
    let end = buf.iter().position(|c| *c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end])
}

pub fn window_info(hwnd: isize) -> Option<WindowInfo> {
    if !is_window(hwnd) {
        return None;
    }
    unsafe {
        let h = to_hwnd(hwnd);
        let len = GetWindowTextLengthW(h);
        let mut title = vec![0u16; (len.max(0) as usize) + 2];
        if len > 0 {
            GetWindowTextW(h, title.as_mut_ptr(), title.len() as i32);
        }

        let mut pid = 0u32;
        GetWindowThreadProcessId(h, &mut pid);
        if pid == 0 {
            return None;
        }

        let process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
        let exe = if !process.is_null() {
            let mut path = vec![0u16; 32768];
            let mut size = path.len() as u32;
            let ok = QueryFullProcessImageNameW(process, 0, path.as_mut_ptr(), &mut size);
            CloseHandle(process);
            if ok != 0 {
                let full = wide_to_string(&path[..size as usize]);
                full.rsplit(['\\', '/']).next().unwrap_or(&full).to_string()
            } else {
                format!("process-{pid}.exe")
            }
        } else {
            format!("process-{pid}.exe")
        };

        Some(WindowInfo {
            title: wide_to_string(&title),
            exe,
            pid,
        })
    }
}

pub fn capture_window_style(hwnd: isize) -> Result<OriginalStyle, String> {
    unsafe {
        let h = to_hwnd(hwnd);
        let exstyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
        let original_layered = ((exstyle as u32) & WS_EX_LAYERED) != 0;
        let was_topmost = ((exstyle as u32) & WS_EX_TOPMOST) != 0;
        let mut alpha = 255u8;
        if original_layered {
            let mut color_key = 0u32;
            let mut flags = 0u32;
            if GetLayeredWindowAttributes(h, &mut color_key, &mut alpha, &mut flags) == 0 {
                alpha = 255;
            }
        }
        Ok(OriginalStyle {
            exstyle,
            alpha,
            original_layered,
            was_topmost,
        })
    }
}

pub fn set_topmost(hwnd: isize, topmost: bool) -> Result<(), String> {
    unsafe {
        let after = if topmost { HWND_TOPMOST } else { HWND_NOTOPMOST };
        let ok = SetWindowPos(
            to_hwnd(hwnd),
            after,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER,
        );
        if ok == 0 {
            Err("Windows could not change the window's topmost state.".into())
        } else {
            Ok(())
        }
    }
}

pub fn set_window_opacity(hwnd: isize, opacity: u8, original_layered: bool) -> Result<(), String> {
    unsafe {
        let h = to_hwnd(hwnd);
        let exstyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
        let opacity = opacity.clamp(30, 100);
        if opacity >= 100 && !original_layered {
            SetWindowLongPtrW(h, GWL_EXSTYLE, exstyle & !(WS_EX_LAYERED as isize));
            return Ok(());
        }
        SetWindowLongPtrW(h, GWL_EXSTYLE, exstyle | WS_EX_LAYERED as isize);
        let alpha = ((opacity as u16 * 255) / 100) as u8;
        if SetLayeredWindowAttributes(h, 0, alpha, LWA_ALPHA) == 0 {
            return Err("Windows could not apply opacity to this window.".into());
        }
        Ok(())
    }
}

pub fn restore_window_style(
    hwnd: isize,
    original_exstyle: isize,
    original_layered: bool,
    original_alpha: u8,
    was_topmost: bool,
) -> Result<(), String> {
    unsafe {
        let h = to_hwnd(hwnd);
        SetWindowLongPtrW(h, GWL_EXSTYLE, original_exstyle);
        if original_layered {
            let _ = SetLayeredWindowAttributes(h, 0, original_alpha, LWA_ALPHA);
        }
    }
    set_topmost(hwnd, was_topmost)
}

pub fn system_accent_color() -> Option<u32> {
    unsafe {
        let mut argb = 0u32;
        let mut opaque = 0i32;
        if DwmGetColorizationColor(&mut argb, &mut opaque) < 0 {
            return None;
        }
        let r = (argb >> 16) & 0xFF;
        let g = (argb >> 8) & 0xFF;
        let b = argb & 0xFF;
        Some(r | (g << 8) | (b << 16))
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

unsafe extern "system" fn border_wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_NCHITTEST => HTTRANSPARENT as LRESULT,
        WM_ERASEBKGND => 1,
        WM_PAINT => {
            let mut ps: PAINTSTRUCT = zeroed();
            let hdc = BeginPaint(hwnd, &mut ps);
            let color = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as u32;
            let brush = CreateSolidBrush(color);
            let mut rc: RECT = zeroed();
            GetClientRect(hwnd, &mut rc);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush as *mut c_void);
            EndPaint(hwnd, &ps);
            0
        }
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
    }
}

#[derive(Debug)]
struct BorderWindows {
    hwnds: [HWND; 4],
}

impl BorderWindows {
    unsafe fn destroy(self) {
        for hwnd in self.hwnds {
            if !hwnd.is_null() {
                DestroyWindow(hwnd);
            }
        }
    }
}

unsafe fn create_border_windows(class_name: *const u16, color: u32, alpha: u8) -> Option<BorderWindows> {
    let mut hwnds: [HWND; 4] = [null_mut(); 4];
    for slot in &mut hwnds {
        let hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST,
            class_name,
            class_name,
            WS_POPUP,
            0,
            0,
            1,
            1,
            null_mut(),
            null_mut(),
            GetModuleHandleW(null()),
            null_mut(),
        );
        if hwnd.is_null() {
            for created in hwnds {
                if !created.is_null() {
                    DestroyWindow(created);
                }
            }
            return None;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, color as isize);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        *slot = hwnd;
    }
    Some(BorderWindows { hwnds })
}

unsafe fn target_rect(hwnd: HWND) -> Option<RECT> {
    let mut rect: RECT = zeroed();
    let hr = DwmGetWindowAttribute(
        hwnd,
        DWMWA_EXTENDED_FRAME_BOUNDS as u32,
        &mut rect as *mut RECT as *mut c_void,
        size_of::<RECT>() as u32,
    );
    if hr >= 0 || GetWindowRect(hwnd, &mut rect) != 0 {
        Some(rect)
    } else {
        None
    }
}

unsafe fn position_borders(target: HWND, borders: &BorderWindows, thickness: i32) {
    if IsWindow(target) == 0 || IsWindowVisible(target) == 0 || IsIconic(target) != 0 {
        for hwnd in borders.hwnds {
            ShowWindow(hwnd, SW_HIDE);
        }
        return;
    }
    let Some(rc) = target_rect(target) else { return; };
    let t = thickness.max(1);
    let w = (rc.right - rc.left).max(1);
    let h = (rc.bottom - rc.top).max(1);
    let rects = [
        (rc.left - t, rc.top - t, w + t * 2, t),
        (rc.left - t, rc.bottom, w + t * 2, t),
        (rc.left - t, rc.top, t, h),
        (rc.right, rc.top, t, h),
    ];
    for (hwnd, (x, y, width, height)) in borders.hwnds.iter().copied().zip(rects) {
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
}

pub fn spawn_border_thread() -> BorderController {
    let (tx, rx) = mpsc::channel::<BorderCommand>();
    thread::spawn(move || unsafe {
        let class_name = wide("DevbloxstrapAlwaysOnTopBorder");
        let hinstance = GetModuleHandleW(null());
        let wc = WNDCLASSW {
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(border_wnd_proc),
            hInstance: hinstance,
            hCursor: LoadCursorW(null_mut(), IDC_ARROW),
            lpszClassName: class_name.as_ptr(),
            ..zeroed()
        };
        RegisterClassW(&wc);

        let mut color = 0x00D47800u32;
        let mut alpha = 210u8;
        let mut thickness = 3i32;
        let mut targets: HashMap<isize, BorderWindows> = HashMap::new();
        let mut running = true;

        while running {
            while let Ok(command) = rx.try_recv() {
                match command {
                    BorderCommand::Pin { hwnd } => {
                        if !targets.contains_key(&hwnd) && IsWindow(to_hwnd(hwnd)) != 0 {
                            if let Some(borders) = create_border_windows(class_name.as_ptr(), color, alpha) {
                                targets.insert(hwnd, borders);
                            }
                        }
                    }
                    BorderCommand::Unpin { hwnd } => {
                        if let Some(borders) = targets.remove(&hwnd) {
                            borders.destroy();
                        }
                    }
                    BorderCommand::Style { color: c, alpha: a, thickness: t } => {
                        color = c;
                        alpha = a;
                        thickness = t.max(1);
                        for borders in targets.values() {
                            for hwnd in borders.hwnds {
                                SetWindowLongPtrW(hwnd, GWLP_USERDATA, color as isize);
                                SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
                                InvalidateRect(hwnd, null(), 1);
                            }
                        }
                    }
                    BorderCommand::Shutdown => running = false,
                }
            }

            let dead = targets
                .keys()
                .copied()
                .filter(|raw| IsWindow(to_hwnd(*raw)) == 0)
                .collect::<Vec<_>>();
            for raw in dead {
                if let Some(borders) = targets.remove(&raw) {
                    borders.destroy();
                }
            }

            for (raw, borders) in &targets {
                position_borders(to_hwnd(*raw), borders, thickness);
            }

            let mut msg: MSG = zeroed();
            while PeekMessageW(&mut msg, null_mut(), 0, 0, PM_REMOVE) != 0 {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            thread::sleep(Duration::from_millis(33));
        }

        for (_, borders) in targets.drain() {
            borders.destroy();
        }
    });
    BorderController { tx }
}
