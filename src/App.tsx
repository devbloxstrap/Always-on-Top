import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import { getCurrentWindow } from '@tauri-apps/api/window';
import {
  Ban,
  BellRing,
  ChevronRight,
  CircleDot,
  Eye,
  Gauge,
  Minus,
  Pin,
  PinOff,
  Plus,
  RotateCcw,
  Settings2,
  Sparkles,
  Volume2,
  VolumeX,
  X,
} from 'lucide-react';
import type { AppSnapshot, PinFeedback, Settings } from './types';

const EMPTY: AppSnapshot = {
  settings: {
    enabled: true,
    shortcut: 'super+ctrl+t',
    use_system_accent: true,
    border_color: '#0078d4',
    border_opacity: 82,
    border_thickness: 3,
    default_window_opacity: 100,
    sound_enabled: true,
    exclusions: [],
  },
  pinned: [],
};

const windowRef = getCurrentWindow();

function humanizeShortcut(value: string) {
  return value
    .split('+')
    .map((part) => {
      const p = part.toLowerCase();
      if (p === 'super' || p === 'meta') return 'Win';
      if (p === 'ctrl' || p === 'control') return 'Ctrl';
      if (p === 'alt') return 'Alt';
      if (p === 'shift') return 'Shift';
      if (p === 'space') return 'Space';
      return part.length === 1 ? part.toUpperCase() : part;
    })
    .join(' + ');
}

function buildShortcut(e: KeyboardEvent): string | null {
  const parts: string[] = [];
  if (e.metaKey) parts.push('super');
  if (e.ctrlKey) parts.push('ctrl');
  if (e.altKey) parts.push('alt');
  if (e.shiftKey) parts.push('shift');

  const ignored = new Set(['Meta', 'Control', 'Alt', 'Shift']);
  if (ignored.has(e.key)) return null;

  let key = e.key;
  if (key === ' ') key = 'space';
  else if (key.length === 1) key = key.toLowerCase();
  else key = key.toLowerCase();

  parts.push(key);
  return parts.join('+');
}

function useDebouncedAction<T>(delay = 120) {
  const timer = useRef<number | null>(null);
  return useCallback((value: T, fn: (v: T) => void) => {
    if (timer.current) window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => fn(value), delay);
  }, [delay]);
}

function Toggle({ checked, onChange, label }: { checked: boolean; onChange: (v: boolean) => void; label: string }) {
  return (
    <button className={`switch ${checked ? 'on' : ''}`} onClick={() => onChange(!checked)} aria-label={label} aria-pressed={checked}>
      <span />
    </button>
  );
}

function Slider({ value, min, max, step = 1, onChange, unit = '%' }: {
  value: number; min: number; max: number; step?: number; onChange: (v: number) => void; unit?: string;
}) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <div className="slider-row">
      <input
        className="range"
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        style={{ '--range-progress': `${pct}%` } as React.CSSProperties}
      />
      <div className="value-chip">{value}{unit}</div>
    </div>
  );
}

export default function App() {
  const [snapshot, setSnapshot] = useState<AppSnapshot>(EMPTY);
  const [loading, setLoading] = useState(true);
  const [recordingShortcut, setRecordingShortcut] = useState(false);
  const [draftExclusion, setDraftExclusion] = useState('');
  const [toast, setToast] = useState<string>('');
  const debounce = useDebouncedAction<number>(120);

  const refresh = useCallback(async () => {
    try {
      const data = await invoke<AppSnapshot>('get_state');
      setSnapshot(data);
    } finally {
      setLoading(false);
    }
  }, []);

  const updateSetting = useCallback(async <K extends keyof Settings>(key: K, value: Settings[K]) => {
    setSnapshot((prev) => ({ ...prev, settings: { ...prev.settings, [key]: value } }));
    await invoke('update_setting', { key, value });
  }, []);

  const showToast = useCallback((message: string) => {
    setToast(message);
    window.setTimeout(() => setToast(''), 2600);
  }, []);

  const playFeedback = useCallback((kind: 'pin' | 'unpin') => {
    const audio = new Audio(kind === 'pin' ? '/sounds/pin.wav' : '/sounds/unpin.wav');
    audio.volume = 0.55;
    void audio.play().catch(() => undefined);
  }, []);

  useEffect(() => {
    void refresh();
    const unlistenState = listen<AppSnapshot>('state-changed', (event) => setSnapshot(event.payload));
    const unlistenFeedback = listen<PinFeedback>('pin-feedback', (event) => {
      const payload = event.payload;
      showToast(payload.message);
      if (payload.sound && (payload.kind === 'pin' || payload.kind === 'unpin')) playFeedback(payload.kind);
    });
    const poll = window.setInterval(() => void refresh(), 1800);
    return () => {
      window.clearInterval(poll);
      void unlistenState.then((f) => f());
      void unlistenFeedback.then((f) => f());
    };
  }, [playFeedback, refresh, showToast]);

  useEffect(() => {
    if (!recordingShortcut) return;
    const onKey = async (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (e.key === 'Escape') {
        setRecordingShortcut(false);
        return;
      }
      const shortcut = buildShortcut(e);
      if (!shortcut || !shortcut.includes('+')) return;
      try {
        await invoke('set_shortcut', { shortcut });
        setRecordingShortcut(false);
        await refresh();
        showToast(`Shortcut changed to ${humanizeShortcut(shortcut)}`);
      } catch (err) {
        showToast(String(err));
      }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, [recordingShortcut, refresh, showToast]);

  const pinnedCountText = useMemo(() => {
    const n = snapshot.pinned.length;
    return `${n} pinned window${n === 1 ? '' : 's'}`;
  }, [snapshot.pinned.length]);

  const addExclusion = async (exe?: string) => {
    const value = (exe ?? draftExclusion).trim();
    if (!value) return;
    await invoke('add_exclusion', { exe: value });
    setDraftExclusion('');
    await refresh();
  };

  const addActiveExclusion = async () => {
    try {
      const exe = await invoke<string>('add_active_exclusion');
      showToast(`${exe} added to exclusions`);
      await refresh();
    } catch (err) {
      showToast(String(err));
    }
  };

  const pinActive = async () => {
    try {
      await invoke('toggle_active_from_ui');
    } catch (err) {
      showToast(String(err));
    }
  };

  const previewSound = () => playFeedback('pin');

  if (loading) {
    return <div className="loading-shell"><div className="spinner" /></div>;
  }

  return (
    <div className="app-shell">
      <header className="titlebar" data-tauri-drag-region>
        <div className="brand" data-tauri-drag-region>
          <div className="brand-mark"><Pin size={17} strokeWidth={2.4} /></div>
          <span data-tauri-drag-region>Always On Top</span>
          <span className="version">v1.0</span>
        </div>
        <div className="window-actions">
          <button onClick={() => void windowRef.minimize()} aria-label="Minimize"><Minus size={15} /></button>
          <button className="close" onClick={() => void windowRef.close()} aria-label="Close"><X size={16} /></button>
        </div>
      </header>

      <main className="content">
        <section className="hero">
          <div>
            <div className="eyebrow"><Sparkles size={14} /> Native pinning, polished control</div>
            <h1>Keep the right window in sight.</h1>
            <p>Pin any app above the rest, tune its transparency, and keep distractions out of the way.</p>
          </div>
          <div className="hero-actions">
            <button className="primary" onClick={pinActive}><Pin size={17} /> Pin last active window</button>
            <div className="status-pill"><CircleDot size={14} /> {pinnedCountText}</div>
          </div>
        </section>

        <section className="top-grid">
          <article className="glass-card master-card">
            <div className="card-icon"><Pin size={20} /></div>
            <div className="card-copy">
              <h2>Always On Top</h2>
              <p>Global pinning runs quietly in the tray.</p>
            </div>
            <Toggle checked={snapshot.settings.enabled} onChange={(v) => void updateSetting('enabled', v)} label="Enable Always On Top" />
          </article>

          <article className="glass-card shortcut-card">
            <div className="section-heading compact">
              <div><span className="kicker">Shortcut</span><strong>{recordingShortcut ? 'Press your shortcut…' : humanizeShortcut(snapshot.settings.shortcut)}</strong></div>
              <button className={`soft-button ${recordingShortcut ? 'recording' : ''}`} onClick={() => setRecordingShortcut((v) => !v)}>
                {recordingShortcut ? 'Esc to cancel' : 'Change'}
              </button>
            </div>
          </article>
        </section>

        <section className="glass-card pinned-card">
          <div className="section-heading">
            <div>
              <span className="kicker">Pinned windows</span>
              <h2>Live control</h2>
            </div>
            <button className="ghost-button" onClick={() => void invoke('unpin_all')} disabled={snapshot.pinned.length === 0}>
              <PinOff size={16} /> Unpin all
            </button>
          </div>

          {snapshot.pinned.length === 0 ? (
            <div className="empty-state">
              <div className="empty-icon"><Pin size={25} /></div>
              <div><strong>No windows pinned yet</strong><span>Focus a window and press {humanizeShortcut(snapshot.settings.shortcut)}.</span></div>
            </div>
          ) : (
            <div className="pinned-list">
              {snapshot.pinned.map((item) => (
                <div className="pinned-row" key={item.hwnd}>
                  <div className="app-dot">{item.exe.slice(0, 1).toUpperCase()}</div>
                  <div className="pinned-meta">
                    <strong>{item.title || item.exe}</strong>
                    <span>{item.exe}</span>
                  </div>
                  <div className="opacity-control">
                    <Eye size={15} />
                    <input
                      className="range compact-range"
                      type="range"
                      min={30}
                      max={100}
                      value={item.opacity}
                      style={{ '--range-progress': `${((item.opacity - 30) / 70) * 100}%` } as React.CSSProperties}
                      onChange={(e) => {
                        const value = Number(e.target.value);
                        setSnapshot((prev) => ({ ...prev, pinned: prev.pinned.map((p) => p.hwnd === item.hwnd ? { ...p, opacity: value } : p) }));
                        debounce(value, (v) => void invoke('set_pinned_opacity', { hwnd: item.hwnd, opacity: v }));
                      }}
                    />
                    <span>{item.opacity}%</span>
                  </div>
                  <button className="icon-button danger-soft" onClick={() => void invoke('unpin_window', { hwnd: item.hwnd })} aria-label={`Unpin ${item.title}`}>
                    <X size={16} />
                  </button>
                </div>
              ))}
            </div>
          )}
        </section>

        <section className="settings-grid">
          <article className="glass-card settings-card">
            <div className="section-heading">
              <div><span className="kicker">Appearance</span><h2>Border & transparency</h2></div>
              <Gauge size={20} className="heading-icon" />
            </div>

            <div className="setting-row">
              <div><strong>Windows accent color</strong><span>Follow your system accent automatically</span></div>
              <Toggle checked={snapshot.settings.use_system_accent} onChange={(v) => void updateSetting('use_system_accent', v)} label="Use Windows accent color" />
            </div>

            {!snapshot.settings.use_system_accent && (
              <div className="setting-row color-row">
                <div><strong>Custom border color</strong><span>Pick a clean highlight for pinned windows</span></div>
                <label className="color-picker">
                  <input type="color" value={snapshot.settings.border_color} onChange={(e) => void updateSetting('border_color', e.target.value)} />
                  <span style={{ background: snapshot.settings.border_color }} />
                  {snapshot.settings.border_color.toUpperCase()}
                </label>
              </div>
            )}

            <div className="setting-stack">
              <div className="setting-label"><div><strong>Border opacity</strong><span>Control only the pin outline</span></div></div>
              <Slider value={snapshot.settings.border_opacity} min={0} max={100} onChange={(v) => {
                setSnapshot((prev) => ({ ...prev, settings: { ...prev.settings, border_opacity: v } }));
                debounce(v, (n) => void invoke('update_setting', { key: 'border_opacity', value: n }));
              }} />
            </div>

            <div className="setting-stack">
              <div className="setting-label"><div><strong>New pinned window opacity</strong><span>Default transparency for future pins</span></div></div>
              <Slider value={snapshot.settings.default_window_opacity} min={30} max={100} onChange={(v) => {
                setSnapshot((prev) => ({ ...prev, settings: { ...prev.settings, default_window_opacity: v } }));
                debounce(v, (n) => void invoke('update_setting', { key: 'default_window_opacity', value: n }));
              }} />
            </div>

            <div className="setting-stack">
              <div className="setting-label"><div><strong>Border thickness</strong><span>Subtle or clearly visible outline</span></div></div>
              <Slider value={snapshot.settings.border_thickness} min={1} max={8} unit=" px" onChange={(v) => {
                setSnapshot((prev) => ({ ...prev, settings: { ...prev.settings, border_thickness: v } }));
                debounce(v, (n) => void invoke('update_setting', { key: 'border_thickness', value: n }));
              }} />
            </div>
          </article>

          <article className="glass-card settings-card">
            <div className="section-heading">
              <div><span className="kicker">Behavior</span><h2>Feedback & exclusions</h2></div>
              <Settings2 size={20} className="heading-icon" />
            </div>

            <div className="setting-row">
              <div className="sound-copy">
                <div className="mini-icon">{snapshot.settings.sound_enabled ? <Volume2 size={17} /> : <VolumeX size={17} />}</div>
                <div><strong>Pin sound</strong><span>Short custom chime when a window is pinned</span></div>
              </div>
              <div className="inline-actions">
                <button className="soft-button" onClick={previewSound}><BellRing size={15} /> Preview</button>
                <Toggle checked={snapshot.settings.sound_enabled} onChange={(v) => void updateSetting('sound_enabled', v)} label="Pin sound" />
              </div>
            </div>

            <div className="divider" />

            <div className="exclusion-header">
              <div><strong>Excluded apps</strong><span>These apps will never be pinned.</span></div>
              <button className="soft-button" onClick={addActiveExclusion}><Plus size={15} /> Active app</button>
            </div>

            <div className="exclusion-input">
              <input value={draftExclusion} onChange={(e) => setDraftExclusion(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && void addExclusion()} placeholder="example.exe" />
              <button onClick={() => void addExclusion()} aria-label="Add exclusion"><Plus size={17} /></button>
            </div>

            <div className="chip-list">
              {snapshot.settings.exclusions.length === 0 ? (
                <div className="empty-inline"><Ban size={15} /> No excluded apps</div>
              ) : snapshot.settings.exclusions.map((exe) => (
                <button className="app-chip" key={exe} onClick={() => void invoke('remove_exclusion', { exe }).then(refresh)} title="Remove exclusion">
                  {exe}<X size={13} />
                </button>
              ))}
            </div>
          </article>
        </section>

        <footer>
          <div><span className="live-dot" /> Running in the notification area</div>
          <button className="text-button" onClick={() => void invoke('reset_settings').then(refresh)}><RotateCcw size={14} /> Reset settings</button>
        </footer>
      </main>

      {toast && <div className="toast"><ChevronRight size={15} />{toast}</div>}
    </div>
  );
}
