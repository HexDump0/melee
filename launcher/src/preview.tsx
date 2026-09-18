/* A design harness for the launcher's UI.
 *
 * Tauri's `invoke` goes through `window.__TAURI_INTERNALS__`, so stubbing that
 * runs the real App against fabricated data in an ordinary browser. That makes
 * the layout reviewable -- and screenshottable -- without building the shell or
 * opening a window on someone's desktop.
 *
 * Dev only: `vite build` has index.html as its entry, so nothing here ships.
 * Open http://localhost:5183/preview.html with `npm run dev`. */
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "./styles.css";

const SNAPSHOT = {
  config_path: "/home/you/.config/melee/melee.toml",
  config_text: "",
  values: {
    disc: "/home/you/games/melee.iso",
    "cinematic.enabled": true,
    "cinematic.threshold": 0.9,
    "cinematic.bloom": 0.2,
    "cinematic.wide": 0.1,
    "cinematic.rim": 0.35,
    "cinematic.sharpen": 0.35,
    "cinematic.exposure": 0.85,
    "cinematic.sat": 1.1,
    "cinematic.vignette": 0.26,
    "mods.unbound.widescreen": 1,
    "mods.unbound.aspect_correct": false,
    "mods.unbound.credits_overlay": false,
  },
  port_path: "/home/you/projects/melee/build/native/melee",
  port_found: true,
  disc_path: "/home/you/games/melee.iso",
  disc_found: true,
  mods_dir: "/home/you/projects/melee/mods",
  mods: [
    {
      id: "unbound",
      name: "Melee Unbound",
      version: "1.0.0",
      priority: 100,
      dir: "/home/you/projects/melee/mods/unbound",
      module: "unbound.wasm",
      settings: [
        {
          key: "widescreen",
          kind: "enum",
          label: "Widescreen",
          help: "Everywhere widens menus too, which shows gaps at the edges.",
          default: 1,
          choices: [
            { value: 0, label: "Off — 4:3" },
            { value: 1, label: "Gameplay only" },
            { value: 2, label: "Everywhere" },
          ],
        },
        {
          key: "aspect_correct",
          kind: "bool",
          label: "Correct the aspect",
          help: "Retail is ~9.5% wider than neutral. A departure, not a fix.",
          default: 0,
          choices: [],
        },
        {
          key: "credits_overlay",
          kind: "bool",
          label: "Credits overlay",
          help: "Show the mod's credit line over the engine's credits.",
          default: 0,
          choices: [],
        },
      ],
    },
  ],
  profile: "play",
  crashes: 2,
};

const CRASHES = [
  {
    id: String(Math.floor(Date.now() / 1000) - 400),
    when: "",
    reason: 'in "/src/melee/ft/ftdata.c" on line 1745.',
    exit: "signal",
    seed: "0x6adb28af",
    profile: "play",
    log: [
      "[mod] loaded unbound (Melee Unbound 1.0.0) priority 100",
      "[rng] seed=0x6adb28af (MELEE_RNG_SEED=0x6adb28af replays this run)",
      "[ftdata] figatree over: kind=4 i=124 count=479",
      '*** PANIC:  in "/src/melee/ft/ftdata.c" on line 1745.',
      "[boot]   #4 ftData_80085A14+0x1d0",
      "[boot]   #5 Fighter_Create+0xb3",
    ],
    config: "disc = \"/home/you/games/melee.iso\"\n",
  },
  {
    id: String(Math.floor(Date.now() / 1000) - 90000),
    when: "",
    reason: "SIGSEGV at 0x4",
    exit: "signal",
    seed: null,
    profile: "record",
    log: ["[boot] controlled stop: SIGSEGV at 0x4"],
    config: "",
  },
];

const CONTROLS = {
  deadzone: 4000,
  problems: [] as string[],
  bindings: [
    ["stick_up", "Up"], ["stick_down", "Down"],
    ["stick_left", "Left"], ["stick_right", "Right"],
    ["a", "Z"], ["b", "X"], ["x", "C"], ["y", "V"],
    ["z", "Q"], ["l", "A"], ["r", "S"],
    ["start", "Return,Keypad Enter"],
  ].map(([action, value]) => ({
    device: "keyboard" as const,
    player: 1,
    action,
    value,
  })),
};

const RESPONSES: Record<string, unknown> = {
  control_bindings: CONTROLS,
  snapshot: SNAPSHOT,
  crashes: CRASHES,
  save_settings: null,
  set_port_path: null,
  set_profile: null,
  env_for: "MELEE_EXAMPLE",
  start: null,
  stop: null,
  forget_crash: null,
};

(window as unknown as Record<string, unknown>).__TAURI_INTERNALS__ = {
  invoke: (cmd: string) => Promise.resolve(RESPONSES[cmd] ?? null),
  transformCallback: (cb: unknown) => cb,
};

const params = new URLSearchParams(location.search);
if (params.get("nodisc") !== null) {
  SNAPSHOT.disc_found = false;
  SNAPSHOT.disc_path = "";
  SNAPSHOT.values.disc = "";
}

const page = (params.get("page") ?? "play") as
  | "play"
  | "mods"
  | "graphics"
  | "controls"
  | "crashes";

const { default: App } = await import("./App");

function Harness() {
  return <App initialPage={page} />;
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <Harness />
  </StrictMode>,
);
