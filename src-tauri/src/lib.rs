use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    fs,
    path::PathBuf,
    sync::{mpsc, Arc, Mutex},
    thread,
    time::Duration,
};
use tauri::{
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, Emitter, Manager, State, WindowEvent,
};
use tauri_plugin_global_shortcut::{GlobalShortcutExt, ShortcutState};

#[cfg(target_os = "windows")]
mod win;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
struct Settings {
    enabled: bool,
    shortcut: String,
    use_system_accent: bool,
    border_color: String,
    border_opacity: u8,
    border_thickness: u8,
    default_window_opacity: u8,
    sound_enabled: bool,
    exclusions: Vec<String>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            enabled: true,
            shortcut: "super+ctrl+t".into(),
            use_system_accent: true,
            border_color: "#0078D4".into(),
            border_opacity: 82,
            border_thickness: 3,
            default_window_opacity: 100,
            sound_enabled: true,
            exclusions: vec![],
        }
    }
}

#[derive(Debug, Clone, Serialize)]
struct PinnedWindowView {
    hwnd: isize,
    title: String,
    exe: String,
    opacity: u8,
}

#[derive(Debug, Clone, Serialize)]
struct AppSnapshot {
    settings: Settings,
    pinned: Vec<PinnedWindowView>,
}

#[derive(Debug, Clone, Serialize)]
struct PinFeedback {
    kind: String,
    message: String,
    sound: bool,
}

#[cfg(target_os = "windows")]
#[derive(Debug, Clone)]
struct PinnedWindow {
    hwnd: isize,
    title: String,
    exe: String,
    opacity: u8,
    original_exstyle: isize,
    original_alpha: u8,
    original_layered: bool,
    was_topmost: bool,
}

#[cfg(not(target_os = "windows"))]
#[derive(Debug, Clone)]
struct PinnedWindow {
    hwnd: isize,
    title: String,
    exe: String,
    opacity: u8,
}

#[derive(Clone)]
struct BorderController {
    tx: mpsc::Sender<BorderCommand>,
}

#[derive(Debug, Clone)]
enum BorderCommand {
    Pin { hwnd: isize },
    Unpin { hwnd: isize },
    Style { color: u32, alpha: u8, thickness: i32 },
    Shutdown,
}

struct SharedState {
    settings: Mutex<Settings>,
    pinned: Mutex<HashMap<isize, PinnedWindow>>,
    last_external_hwnd: Mutex<isize>,
    settings_path: PathBuf,
    border: BorderController,
}

type Shared = Arc<SharedState>;

fn load_settings(path: &PathBuf) -> Settings {
    fs::read_to_string(path)
        .ok()
        .and_then(|raw| serde_json::from_str::<Settings>(&raw).ok())
        .unwrap_or_default()
}

fn save_settings(shared: &Shared) -> Result<(), String> {
    let settings = shared.settings.lock().map_err(|_| "Settings lock failed")?.clone();
    if let Some(parent) = shared.settings_path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let raw = serde_json::to_string_pretty(&settings).map_err(|e| e.to_string())?;
    fs::write(&shared.settings_path, raw).map_err(|e| e.to_string())
}

fn parse_hex_color(hex: &str) -> u32 {
    let h = hex.trim().trim_start_matches('#');
    if h.len() != 6 {
        return 0x00D47800; // COLORREF for #0078D4 (BGR)
    }
    let rgb = u32::from_str_radix(h, 16).unwrap_or(0x0078D4);
    let r = (rgb >> 16) & 0xFF;
    let g = (rgb >> 8) & 0xFF;
    let b = rgb & 0xFF;
    r | (g << 8) | (b << 16)
}

#[cfg(target_os = "windows")]
fn effective_border_color(settings: &Settings) -> u32 {
    if settings.use_system_accent {
        win::system_accent_color().unwrap_or_else(|| parse_hex_color(&settings.border_color))
    } else {
        parse_hex_color(&settings.border_color)
    }
}

#[cfg(not(target_os = "windows"))]
fn effective_border_color(settings: &Settings) -> u32 {
    parse_hex_color(&settings.border_color)
}

fn push_border_style(shared: &Shared) {
    let settings = match shared.settings.lock() {
        Ok(v) => v.clone(),
        Err(_) => return,
    };
    let alpha = ((settings.border_opacity as u16 * 255) / 100) as u8;
    let _ = shared.border.tx.send(BorderCommand::Style {
        color: effective_border_color(&settings),
        alpha,
        thickness: settings.border_thickness.clamp(1, 8) as i32,
    });
}

fn snapshot(shared: &Shared) -> AppSnapshot {
    #[cfg(target_os = "windows")]
    prune_dead_windows(shared);

    let settings = shared.settings.lock().map(|v| v.clone()).unwrap_or_default();
    let mut pinned = shared
        .pinned
        .lock()
        .map(|map| {
            map.values()
                .map(|p| PinnedWindowView {
                    hwnd: p.hwnd,
                    title: p.title.clone(),
                    exe: p.exe.clone(),
                    opacity: p.opacity,
                })
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    pinned.sort_by(|a, b| a.exe.to_lowercase().cmp(&b.exe.to_lowercase()));
    AppSnapshot { settings, pinned }
}

fn emit_state(app: &AppHandle, shared: &Shared) {
    let _ = app.emit("state-changed", snapshot(shared));
}

fn emit_feedback(app: &AppHandle, shared: &Shared, kind: &str, message: impl Into<String>) {
    let sound = shared.settings.lock().map(|s| s.sound_enabled).unwrap_or(false);
    let _ = app.emit(
        "pin-feedback",
        PinFeedback {
            kind: kind.to_string(),
            message: message.into(),
            sound,
        },
    );
}

#[cfg(target_os = "windows")]
fn prune_dead_windows(shared: &Shared) {
    let dead = {
        let pinned = match shared.pinned.lock() {
            Ok(v) => v,
            Err(_) => return,
        };
        pinned
            .keys()
            .copied()
            .filter(|hwnd| !win::is_window(*hwnd))
            .collect::<Vec<_>>()
    };
    if dead.is_empty() {
        return;
    }
    if let Ok(mut pinned) = shared.pinned.lock() {
        for hwnd in dead {
            pinned.remove(&hwnd);
            let _ = shared.border.tx.send(BorderCommand::Unpin { hwnd });
        }
    }
}

#[cfg(target_os = "windows")]
fn target_from_ui(shared: &Shared) -> Result<isize, String> {
    let hwnd = *shared.last_external_hwnd.lock().map_err(|_| "Window tracker unavailable")?;
    if hwnd == 0 || !win::is_window(hwnd) {
        Err("Switch to the window you want to pin, then return here and try again.".into())
    } else {
        Ok(hwnd)
    }
}

#[cfg(target_os = "windows")]
fn toggle_target(app: &AppHandle, shared: &Shared, hwnd: isize) -> Result<(), String> {
    if hwnd == 0 || !win::is_window(hwnd) {
        return Err("No valid window selected.".into());
    }

    if shared.pinned.lock().map_err(|_| "Pinned windows lock failed")?.contains_key(&hwnd) {
        unpin_internal(app, shared, hwnd)?;
        return Ok(());
    }

    let settings = shared.settings.lock().map_err(|_| "Settings lock failed")?.clone();
    if !settings.enabled {
        return Err("Always On Top is currently disabled.".into());
    }

    let info = win::window_info(hwnd).ok_or_else(|| "Could not inspect the selected window.".to_string())?;
    if info.pid == std::process::id() {
        return Err("Always On Top cannot pin its own settings window.".into());
    }

    let excluded = settings
        .exclusions
        .iter()
        .any(|x| x.eq_ignore_ascii_case(&info.exe));
    if excluded {
        emit_feedback(app, shared, "error", format!("{} is excluded from pinning.", info.exe));
        return Err(format!("{} is excluded from pinning.", info.exe));
    }

    let original = win::capture_window_style(hwnd)?;
    win::set_topmost(hwnd, true)?;
    let opacity = settings.default_window_opacity.clamp(30, 100);
    win::set_window_opacity(hwnd, opacity, original.original_layered)?;

    let item = PinnedWindow {
        hwnd,
        title: info.title,
        exe: info.exe,
        opacity,
        original_exstyle: original.exstyle,
        original_alpha: original.alpha,
        original_layered: original.original_layered,
        was_topmost: original.was_topmost,
    };
    shared.pinned.lock().map_err(|_| "Pinned windows lock failed")?.insert(hwnd, item.clone());
    let _ = shared.border.tx.send(BorderCommand::Pin { hwnd });
    emit_state(app, shared);
    emit_feedback(app, shared, "pin", format!("Pinned {}", item.title_or_exe()));
    Ok(())
}

#[cfg(target_os = "windows")]
impl PinnedWindow {
    fn title_or_exe(&self) -> String {
        if self.title.trim().is_empty() { self.exe.clone() } else { self.title.clone() }
    }
}

#[cfg(target_os = "windows")]
fn unpin_internal(app: &AppHandle, shared: &Shared, hwnd: isize) -> Result<(), String> {
    let item = shared
        .pinned
        .lock()
        .map_err(|_| "Pinned windows lock failed")?
        .remove(&hwnd)
        .ok_or_else(|| "That window is not pinned.".to_string())?;

    if win::is_window(hwnd) {
        win::restore_window_style(
            hwnd,
            item.original_exstyle,
            item.original_layered,
            item.original_alpha,
            item.was_topmost,
        )?;
    }
    let _ = shared.border.tx.send(BorderCommand::Unpin { hwnd });
    emit_state(app, shared);
    emit_feedback(app, shared, "unpin", format!("Unpinned {}", item.title_or_exe()));
    Ok(())
}

#[cfg(target_os = "windows")]
fn unpin_all_internal(app: &AppHandle, shared: &Shared, feedback: bool) {
    let ids = shared
        .pinned
        .lock()
        .map(|p| p.keys().copied().collect::<Vec<_>>())
        .unwrap_or_default();
    for hwnd in ids {
        let _ = unpin_internal(app, shared, hwnd);
    }
    if feedback && shared.pinned.lock().map(|p| p.is_empty()).unwrap_or(false) {
        emit_feedback(app, shared, "unpin", "All pinned windows cleared");
    }
}

#[cfg(target_os = "windows")]
fn register_shortcut(app: &AppHandle, _shared: &Shared, shortcut: &str) -> Result<(), String> {
    app.global_shortcut()
        .on_shortcut(shortcut, move |app, _shortcut, event| {
            if event.state != ShortcutState::Pressed {
                return;
            }
            let shared = app.state::<Shared>();
            let enabled = shared.settings.lock().map(|s| s.enabled).unwrap_or(false);
            if !enabled {
                return;
            }
            let hwnd = win::foreground_window();
            if hwnd == 0 {
                emit_feedback(app, &shared, "error", "No active window to pin");
                return;
            }
            if let Err(err) = toggle_target(app, &shared, hwnd) {
                emit_feedback(app, &shared, "error", err);
            }
        })
        .map_err(|e| format!("Could not register shortcut: {e}"))
}

#[cfg(not(target_os = "windows"))]
fn register_shortcut(_app: &AppHandle, _shared: &Shared, _shortcut: &str) -> Result<(), String> {
    Ok(())
}

#[tauri::command]
fn get_state(shared: State<'_, Shared>) -> AppSnapshot {
    snapshot(&shared)
}

#[tauri::command]
fn set_shortcut(app: AppHandle, shared: State<'_, Shared>, shortcut: String) -> Result<(), String> {
    let normalized = shortcut.trim().to_lowercase();
    if normalized.is_empty() || !normalized.contains('+') {
        return Err("Use at least one modifier plus a key.".into());
    }

    #[cfg(target_os = "windows")]
    {
        let old = shared.settings.lock().map_err(|_| "Settings lock failed")?.shortcut.clone();
        app.global_shortcut().unregister(old.as_str()).ok();
        if let Err(err) = register_shortcut(&app, &shared, &normalized) {
            let _ = register_shortcut(&app, &shared, &old);
            return Err(err);
        }
    }

    shared.settings.lock().map_err(|_| "Settings lock failed")?.shortcut = normalized;
    save_settings(&shared)?;
    emit_state(&app, &shared);
    Ok(())
}

#[tauri::command]
fn update_setting(app: AppHandle, shared: State<'_, Shared>, key: String, value: serde_json::Value) -> Result<(), String> {
    {
        let mut s = shared.settings.lock().map_err(|_| "Settings lock failed")?;
        match key.as_str() {
            "enabled" => s.enabled = value.as_bool().ok_or("Invalid enabled value")?,
            "use_system_accent" => s.use_system_accent = value.as_bool().ok_or("Invalid accent value")?,
            "border_color" => {
                let color = value.as_str().ok_or("Invalid color")?;
                if color.len() != 7 || !color.starts_with('#') {
                    return Err("Use a hex color such as #0078D4.".into());
                }
                s.border_color = color.to_string();
            }
            "border_opacity" => s.border_opacity = value.as_u64().ok_or("Invalid border opacity")?.clamp(0, 100) as u8,
            "border_thickness" => s.border_thickness = value.as_u64().ok_or("Invalid border thickness")?.clamp(1, 8) as u8,
            "default_window_opacity" => s.default_window_opacity = value.as_u64().ok_or("Invalid window opacity")?.clamp(30, 100) as u8,
            "sound_enabled" => s.sound_enabled = value.as_bool().ok_or("Invalid sound value")?,
            _ => return Err("Unknown setting".into()),
        }
    }
    save_settings(&shared)?;
    if matches!(key.as_str(), "use_system_accent" | "border_color" | "border_opacity" | "border_thickness") {
        push_border_style(&shared);
    }
    emit_state(&app, &shared);
    Ok(())
}

#[tauri::command]
fn toggle_active_from_ui(app: AppHandle, shared: State<'_, Shared>) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        let hwnd = target_from_ui(&shared)?;
        return toggle_target(&app, &shared, hwnd);
    }
    #[cfg(not(target_os = "windows"))]
    Err("Always On Top currently supports Windows only.".into())
}

#[tauri::command]
fn set_pinned_opacity(app: AppHandle, shared: State<'_, Shared>, hwnd: isize, opacity: u8) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        let opacity = opacity.clamp(30, 100);
        let original_layered = {
            let mut map = shared.pinned.lock().map_err(|_| "Pinned windows lock failed")?;
            let item = map.get_mut(&hwnd).ok_or("Pinned window not found")?;
            item.opacity = opacity;
            item.original_layered
        };
        win::set_window_opacity(hwnd, opacity, original_layered)?;
        emit_state(&app, &shared);
        return Ok(());
    }
    #[cfg(not(target_os = "windows"))]
    Err("Windows only".into())
}

#[tauri::command]
fn unpin_window(app: AppHandle, shared: State<'_, Shared>, hwnd: isize) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        return unpin_internal(&app, &shared, hwnd);
    }
    #[cfg(not(target_os = "windows"))]
    Err("Windows only".into())
}

#[tauri::command]
fn unpin_all(app: AppHandle, shared: State<'_, Shared>) {
    #[cfg(target_os = "windows")]
    unpin_all_internal(&app, &shared, true);
}

#[tauri::command]
fn add_exclusion(app: AppHandle, shared: State<'_, Shared>, exe: String) -> Result<(), String> {
    let normalized = exe.trim().to_lowercase();
    if normalized.is_empty() {
        return Err("Enter an executable name such as chrome.exe.".into());
    }
    {
        let mut s = shared.settings.lock().map_err(|_| "Settings lock failed")?;
        if !s.exclusions.iter().any(|x| x.eq_ignore_ascii_case(&normalized)) {
            s.exclusions.push(normalized.clone());
            s.exclusions.sort();
        }
    }

    #[cfg(target_os = "windows")]
    {
        let matching = shared
            .pinned
            .lock()
            .map_err(|_| "Pinned windows lock failed")?
            .values()
            .filter(|p| p.exe.eq_ignore_ascii_case(&normalized))
            .map(|p| p.hwnd)
            .collect::<Vec<_>>();
        for hwnd in matching {
            let _ = unpin_internal(&app, &shared, hwnd);
        }
    }

    save_settings(&shared)?;
    emit_state(&app, &shared);
    Ok(())
}

#[tauri::command]
fn remove_exclusion(app: AppHandle, shared: State<'_, Shared>, exe: String) -> Result<(), String> {
    shared
        .settings
        .lock()
        .map_err(|_| "Settings lock failed")?
        .exclusions
        .retain(|x| !x.eq_ignore_ascii_case(exe.trim()));
    save_settings(&shared)?;
    emit_state(&app, &shared);
    Ok(())
}

#[tauri::command]
fn add_active_exclusion(app: AppHandle, shared: State<'_, Shared>) -> Result<String, String> {
    #[cfg(target_os = "windows")]
    {
        let hwnd = target_from_ui(&shared)?;
        let info = win::window_info(hwnd).ok_or("Could not identify the last active app")?;
        let exe = info.exe.to_lowercase();
        add_exclusion(app, shared, exe.clone())?;
        return Ok(exe);
    }
    #[cfg(not(target_os = "windows"))]
    Err("Windows only".into())
}

#[tauri::command]
fn reset_settings(app: AppHandle, shared: State<'_, Shared>) -> Result<(), String> {
    let old_shortcut = shared.settings.lock().map_err(|_| "Settings lock failed")?.shortcut.clone();
    let defaults = Settings::default();
    #[cfg(target_os = "windows")]
    {
        app.global_shortcut().unregister(old_shortcut.as_str()).ok();
        register_shortcut(&app, &shared, &defaults.shortcut)?;
    }
    *shared.settings.lock().map_err(|_| "Settings lock failed")? = defaults;
    save_settings(&shared)?;
    push_border_style(&shared);
    emit_state(&app, &shared);
    Ok(())
}

fn setup_tracker(shared: Shared) {
    #[cfg(target_os = "windows")]
    thread::spawn(move || loop {
        let hwnd = win::foreground_window();
        if hwnd != 0 {
            if let Some(info) = win::window_info(hwnd) {
                if info.pid != std::process::id() {
                    if let Ok(mut slot) = shared.last_external_hwnd.lock() {
                        *slot = hwnd;
                    }
                }
            }
        }
        thread::sleep(Duration::from_millis(180));
    });
}

fn setup_tray(app: &mut tauri::App, shared: Shared) -> tauri::Result<()> {
    let show = MenuItem::with_id(app, "show", "Open Always On Top", true, None::<&str>)?;
    let toggle = MenuItem::with_id(app, "toggle", "Pin / unpin last active window", true, None::<&str>)?;
    let clear = MenuItem::with_id(app, "clear", "Unpin all", true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&show, &toggle, &clear, &quit])?;

    let icon = app.default_window_icon().cloned();
    let mut builder = TrayIconBuilder::with_id("main")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .tooltip("Always On Top");
    if let Some(icon) = icon {
        builder = builder.icon(icon);
    }

    builder
        .on_menu_event(move |app, event| match event.id.as_ref() {
            "show" => {
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.show();
                    let _ = window.set_focus();
                }
            }
            "toggle" => {
                #[cfg(target_os = "windows")]
                {
                    let hwnd = *shared.last_external_hwnd.lock().unwrap_or_else(|e| e.into_inner());
                    if hwnd != 0 {
                        if let Err(err) = toggle_target(app, &shared, hwnd) {
                            emit_feedback(app, &shared, "error", err);
                        }
                    }
                }
            }
            "clear" => {
                #[cfg(target_os = "windows")]
                unpin_all_internal(app, &shared, true);
            }
            "quit" => {
                #[cfg(target_os = "windows")]
                unpin_all_internal(app, &shared, false);
                let _ = shared.border.tx.send(BorderCommand::Shutdown);
                app.exit(0);
            }
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = event
            {
                let app = tray.app_handle();
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.show();
                    let _ = window.set_focus();
                }
            }
        })
        .build(app)?;
    Ok(())
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_global_shortcut::Builder::new().build())
        .invoke_handler(tauri::generate_handler![
            get_state,
            set_shortcut,
            update_setting,
            toggle_active_from_ui,
            set_pinned_opacity,
            unpin_window,
            unpin_all,
            add_exclusion,
            remove_exclusion,
            add_active_exclusion,
            reset_settings
        ])
        .setup(|app| {
            let settings_dir = app.path().app_config_dir()?;
            let settings_path = settings_dir.join("settings.json");
            let settings = load_settings(&settings_path);

            let border = {
                #[cfg(target_os = "windows")]
                { win::spawn_border_thread() }
                #[cfg(not(target_os = "windows"))]
                {
                    let (tx, _rx) = mpsc::channel();
                    BorderController { tx }
                }
            };

            let shared = Arc::new(SharedState {
                settings: Mutex::new(settings.clone()),
                pinned: Mutex::new(HashMap::new()),
                last_external_hwnd: Mutex::new(0),
                settings_path,
                border,
            });
            app.manage(shared.clone());
            push_border_style(&shared);
            setup_tracker(shared.clone());
            setup_tray(app, shared.clone())?;

            #[cfg(target_os = "windows")]
            {
                register_shortcut(app.handle(), &shared, &settings.shortcut)
                    .map_err(|e| tauri::Error::Anyhow(anyhow::anyhow!(e)))?;
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window_vibrancy::apply_mica(&window, None);
                }
            }

            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                if window.label() == "main" {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running Always On Top");
}
