import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

/** A setting value, in the three shapes melee.toml and the UI both hold. */
export type Value = boolean | number | string;

export type Choice = { value: number; label: string };

/** One control on the Mods page, declared by the mod itself in mod.toml. */
export type Setting = {
  key: string;
  kind: "bool" | "int" | "float" | "enum" | "string";
  label?: string | null;
  help?: string | null;
  default?: number | null;
  default_str?: string | null;
  min?: number | null;
  max?: number | null;
  step?: number | null;
  choices: Choice[];
};

export type ModInfo = {
  id: string;
  name: string;
  version: string;
  priority: number;
  dir: string;
  module: string | null;
  settings: Setting[];
};

export type Snapshot = {
  config_path: string;
  config_text: string;
  values: Record<string, Value>;
  port_path: string;
  port_found: boolean;
  disc_path: string;
  disc_found: boolean;
  mods_dir: string;
  mods: ModInfo[];
  profile: string;
  crashes: number;
};

export type Binding = {
  device: "keyboard" | "gamepad";
  player: number;
  action: string;
  value: string;
};

export type Controls = {
  bindings: Binding[];
  deadzone: number;
  problems: string[];
};

export type ReleaseInfo = {
  tag: string;
  asset: string;
  url: string;
  size: number;
};

export type CrashReport = {
  id: string;
  when: string;
  reason: string;
  exit: string;
  seed: string | null;
  profile: string;
  log: string[];
  config: string;
};

export const api = {
  snapshot: () => invoke<Snapshot>("snapshot"),
  save: (values: Record<string, Value>, removed: string[] = []) =>
    invoke<void>("save_settings", { values, removed }),
  setPortPath: (path: string) => invoke<void>("set_port_path", { path }),
  setProfile: (profile: string) => invoke<void>("set_profile", { profile }),
  envFor: (key: string) => invoke<string | null>("env_for", { key }),
  start: (profile: string, seed?: string) =>
    invoke<void>("start", { profile, seed: seed ?? null }),
  stop: () => invoke<void>("stop"),
  controlBindings: () => invoke<Controls>("control_bindings"),
  latestRelease: () => invoke<ReleaseInfo>("latest_release"),
  downloadRelease: (info: ReleaseInfo) =>
    invoke<string>("download_release", {
      infoUrl: info.url,
      asset: info.asset,
      tag: info.tag,
      size: info.size,
    }),
  crashes: () => invoke<CrashReport[]>("crashes"),
  forgetCrash: (id: string) => invoke<void>("forget_crash", { id }),
};

export const onPortLine = (fn: (line: string) => void) =>
  listen<{ line: string }>("port:line", (e) => fn(e.payload.line));

export const onPortExit = (
  fn: (e: { code: string; crashed: boolean; crash_id: string | null }) => void,
) => listen<{ code: string; crashed: boolean; crash_id: string | null }>(
  "port:exit",
  (e) => fn(e.payload),
);
