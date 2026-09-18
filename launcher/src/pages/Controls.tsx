import { useCallback, useEffect, useState } from "react";
import type { Store } from "../App";
import { api, type Controls as ControlsData } from "../lib/api";
import { prettyKeys, sdlKeyName } from "../lib/keys";
import ControllerMap from "../components/ControllerMap";
import {
  Button,
  Card,
  Field,
  PageHead,
  Select,
  Slider,
} from "../components/controls";

/* Grouped the way the controller is, not the way the port stores them: twenty
 * ungrouped rows is a list to scroll, four short groups is a controller to
 * read. */
const GROUPS: { title: string; actions: { key: string; label: string }[] }[] = [
  {
    title: "Control stick",
    actions: [
      { key: "stick_up", label: "Up" },
      { key: "stick_down", label: "Down" },
      { key: "stick_left", label: "Left" },
      { key: "stick_right", label: "Right" },
    ],
  },
  {
    title: "C-stick",
    actions: [
      { key: "cstick_up", label: "Up" },
      { key: "cstick_down", label: "Down" },
      { key: "cstick_left", label: "Left" },
      { key: "cstick_right", label: "Right" },
    ],
  },
  {
    title: "Buttons",
    actions: [
      { key: "a", label: "A" },
      { key: "b", label: "B" },
      { key: "x", label: "X" },
      { key: "y", label: "Y" },
      { key: "z", label: "Z" },
      { key: "l", label: "L" },
      { key: "r", label: "R" },
      { key: "start", label: "Start" },
    ],
  },
  {
    title: "D-pad",
    actions: [
      { key: "dpad_up", label: "Up" },
      { key: "dpad_down", label: "Down" },
      { key: "dpad_left", label: "Left" },
      { key: "dpad_right", label: "Right" },
    ],
  },
];

const ACTIONS = GROUPS.flatMap((g) => g.actions);

/* SDL's own button names, which is what the port parses. Live capture is not
 * used for pads: the browser's Gamepad API is not dependable inside a webview,
 * and a picker that silently fails to see your controller is worse than a
 * list. */
const PAD_BUTTONS = [
  { value: "", label: "Unbound" },
  { value: "a", label: "A (south)" },
  { value: "b", label: "B (east)" },
  { value: "x", label: "X (west)" },
  { value: "y", label: "Y (north)" },
  { value: "leftshoulder", label: "Left shoulder" },
  { value: "rightshoulder", label: "Right shoulder" },
  { value: "leftstick", label: "Left stick click" },
  { value: "rightstick", label: "Right stick click" },
  { value: "start", label: "Start" },
  { value: "back", label: "Back" },
  { value: "guide", label: "Guide" },
  { value: "dpup", label: "D-pad up" },
  { value: "dpdown", label: "D-pad down" },
  { value: "dpleft", label: "D-pad left" },
  { value: "dpright", label: "D-pad right" },
];

type Tab = "p1" | "p2" | "pad";

export default function Controls({ store }: { store: Store }) {
  const [tab, setTab] = useState<Tab>("p1");
  const [data, setData] = useState<ControlsData | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [capturing, setCapturing] = useState<string | null>(null);

  const load = useCallback(() => {
    api
      .controlBindings()
      .then((c) => {
        setData(c);
        setError(null);
      })
      .catch((e) => setError(String(e)));
  }, []);

  useEffect(load, [load]);

  /* Capture swallows the key so a rebind cannot also trigger the launcher --
   * binding Tab would otherwise move focus out of the row you are editing. */
  useEffect(() => {
    if (!capturing) return;
    const onKey = (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (e.code === "Escape") {
        setCapturing(null);
        return;
      }
      const name = sdlKeyName(e.code);
      if (!name) {
        setError(`${e.code} has no SDL name; pick another key`);
        setCapturing(null);
        return;
      }
      const player = tab === "p2" ? "p2" : "p1";
      store.set(`controls.${player}.keyboard.${capturing}`, name);
      setError(null);
      setCapturing(null);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [capturing, tab, store]);

  /* What the config says if it has been edited this session, else what the
   * port reported. The config wins because it is the unsaved edit. */
  const valueFor = (action: string) => {
    const key =
      tab === "pad"
        ? `controls.p1.gamepad.${action}`
        : `controls.${tab}.keyboard.${action}`;
    const pending = store.values[key];
    if (typeof pending === "string") return pending;
    const device = tab === "pad" ? "gamepad" : "keyboard";
    const player = tab === "p2" ? 2 : 1;
    return (
      data?.bindings.find(
        (b) => b.device === device && b.player === player && b.action === action,
      )?.value ?? ""
    );
  };

  const deadzone = (() => {
    const pending = store.values["controls.deadzone"];
    if (typeof pending === "number") return pending;
    return data?.deadzone ?? 4000;
  })();

  /* Removes the keys rather than blanking them: the port falls back to its
   * built-in defaults only when a variable is absent, so writing "" here
   * would unbind every control while the button claimed to restore them. */
  const resetAll = () => {
    for (const a of ACTIONS) {
      store.remove(`controls.p1.keyboard.${a.key}`);
      store.remove(`controls.p2.keyboard.${a.key}`);
      store.remove(`controls.p1.gamepad.${a.key}`);
    }
    store.remove("controls.deadzone");
  };

  const TABS: { id: Tab; label: string }[] = [
    { id: "p1", label: "Keyboard · P1" },
    { id: "p2", label: "Keyboard · P2" },
    { id: "pad", label: "Gamepad" },
  ];

  return (
    <div className="space-y-6">
      <PageHead
        eyebrow="Controls"
        title={TABS.find((t) => t.id === tab)?.label ?? "Controls"}
        action={<Button onClick={load}>Reload</Button>}
      />

      <div className="flex gap-2">
        {TABS.map((t) => (
          <button
            key={t.id}
            type="button"
            onClick={() => {
              setTab(t.id);
              setCapturing(null);
            }}
            className={`border px-4 py-2 text-micro uppercase transition-colors ${
              tab === t.id
                ? "on-violet border-mu-violet bg-mu-violet text-mu-black"
                : "border-white/15 text-mu-dim hover:border-white/40 hover:text-mu-white"
            }`}
          >
            {t.label}
          </button>
        ))}
      </div>

      {error ? (
        <Card className="p-4">
          <p className="text-[0.85rem] text-mu-dim">{error}</p>
        </Card>
      ) : null}

      {data?.problems.length ? (
        <Card className="p-4">
          {data.problems.map((p, i) => (
            <p key={i} className="font-mono text-[0.78rem] text-mu-violet">
              {p}
            </p>
          ))}
        </Card>
      ) : null}

      <div className="flex justify-center py-2">
        <ControllerMap
          selected={capturing}
          interactive={tab !== "pad"}
          bound={(action) => valueFor(action).length > 0}
          onPick={(action) =>
            setCapturing((c) => (c === action ? null : action))
          }
        />
      </div>

      {GROUPS.map((group) => (
        <section key={group.title}>
          <div className="border-b border-white/10 pb-2 text-eyebrow uppercase text-mu-dim">
            {group.title}
          </div>
          {group.actions.map((a) => {
            const value = valueFor(a.key);
            const active = capturing === a.key;
            const configKey =
              tab === "pad"
                ? `controls.p1.gamepad.${a.key}`
                : `controls.${tab}.keyboard.${a.key}`;
            return (
              <div
                key={a.key}
                className="flex items-center justify-between gap-6 border-b border-white/5 py-2.5"
              >
                <span className="text-[0.9rem] font-semibold">{a.label}</span>
                <div className="flex items-center gap-2">
                  {tab === "pad" ? (
                    <Select
                      value={value}
                      options={PAD_BUTTONS}
                      onChange={(v) => store.set(configKey, v)}
                    />
                  ) : (
                    <>
                      <button
                        type="button"
                        onClick={() => setCapturing(active ? null : a.key)}
                        className={`min-w-56 border px-3 py-1.5 text-left font-mono text-[0.8rem] transition-colors ${
                          active
                            ? "animate-attention motion-reduce:animate-none border-mu-violet bg-mu-card text-mu-violet"
                            : "border-white/15 bg-mu-card text-mu-white hover:border-white/40"
                        }`}
                      >
                        {active
                          ? "Press a key…  Esc cancels"
                          : prettyKeys(value)}
                      </button>
                      <button
                        type="button"
                        title="Unbind"
                        aria-label={`Unbind ${a.label}`}
                        onClick={() => store.set(configKey, "")}
                        className="border border-white/10 px-2.5 py-1.5 text-[0.8rem] text-mu-dim transition-colors hover:border-white/40 hover:text-mu-white"
                      >
                        ✕
                      </button>
                    </>
                  )}
                </div>
              </div>
            );
          })}
        </section>
      ))}

      {tab === "pad" ? (
        <section>
          <Field
            label="Stick deadzone"
            help="How far a stick must move before the port listens. Raise it for a worn controller that drifts."
            env="MELEE_CONTROLS_DEADZONE"
          >
            <Slider
              value={deadzone}
              min={0}
              max={16000}
              step={250}
              onChange={(v) => store.set("controls.deadzone", v)}
            />
          </Field>
          <p className="pt-3 text-[0.8rem] text-mu-dim">
            L and R always answer to their analog triggers as well.
          </p>
        </section>
      ) : null}

      <div className="flex gap-3 pb-4">
        <Button variant="danger" onClick={resetAll}>
          Reset all to defaults
        </Button>
      </div>
    </div>
  );
}
