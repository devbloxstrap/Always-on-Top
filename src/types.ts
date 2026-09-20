export type Settings = {
  enabled: boolean;
  shortcut: string;
  use_system_accent: boolean;
  border_color: string;
  border_opacity: number;
  border_thickness: number;
  default_window_opacity: number;
  sound_enabled: boolean;
  exclusions: string[];
};

export type PinnedWindow = {
  hwnd: number;
  title: string;
  exe: string;
  opacity: number;
};

export type AppSnapshot = {
  settings: Settings;
  pinned: PinnedWindow[];
};

export type PinFeedback = {
  kind: 'pin' | 'unpin' | 'error';
  message: string;
  sound: boolean;
};
