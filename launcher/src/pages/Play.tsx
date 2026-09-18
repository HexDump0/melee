import { useEffect, useRef, useState } from "react";
import { open } from "@tauri-apps/plugin-dialog";
import type { Store } from "../App";
import { api } from "../lib/api";
import { Button, Card, Field, PageHead, Select, StatusDot, TextInput } from "../components/controls";

const PROFILES = [
  {
    id: "play",
    label: "Play",
    blurb: "The game, with your settings exactly as saved.",
  },
  {
    id: "record",
    label: "Record",
    blurb: "Cinematic on, a level 9 CPU match, no memory card. For trailers.",
  },
  {
    id: "debug",
    label: "Debug",
    blurb: "Triage output and a full config trace on stderr.",
  },
];

export default function Play({
  store,
  log,
  running,
  onStart,
  onStop,
}: {
  store: Store;
  log: string[];
  running: boolean;
  onStart: (profile: string, seed?: string) => void;
  onStop: () => void;
}) {
  const snap = store.snap;
  const [profile, setProfile] = useState(snap?.profile ?? "play");
  const [follow, setFollow] = useState(true);
  const logRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (snap?.profile) setProfile(snap.profile);
  }, [snap?.profile]);

  useEffect(() => {
    if (follow && logRef.current) {
      logRef.current.scrollTop = logRef.current.scrollHeight;
    }
  }, [log, follow]);

  const pickDisc = async () => {
    const picked = await open({
      multiple: false,
      directory: false,
      filters: [{ name: "GameCube disc", extensions: ["iso", "gcm", "usd", "img"] }],
    });
    if (typeof picked === "string") store.set("disc", picked);
  };

  const pickPort = async () => {
    const picked = await open({ multiple: false, directory: false });
    if (typeof picked === "string") {
      await api.setPortPath(picked);
      await store.reload();
    }
  };

  const ready = !!snap?.disc_found && !!snap?.port_found;
  const chosen = PROFILES.find((p) => p.id === profile) ?? PROFILES[0];

  return (
    <div className="space-y-8">
      <PageHead
        eyebrow="Melee Unbound"
        title="Ready when you are"
        lede="Settings are written to melee.toml. An exported environment variable still beats anything set here."
        action={
          running ? (
            <Button variant="danger" size="lg" onClick={onStop}>
              Stop
            </Button>
          ) : (
            <Button
              variant="primary"
              size="lg"
              disabled={!ready}
              onClick={() => {
                api.setProfile(profile).catch(() => {});
                onStart(profile);
              }}
              title={ready ? undefined : "Set a disc and a port binary first"}
            >
              ▸ Launch
            </Button>
          )
        }
      />

      {!ready ? (
        <Card className="p-6">
          <div className="text-eyebrow uppercase text-mu-violet">First run</div>
          <p className="mt-2 max-w-prose text-[0.9rem] leading-relaxed text-mu-dim">
            The port needs a retail disc image to read its assets from, and the
            launcher needs to know where the built binary is. Neither is copied
            or modified.
          </p>
        </Card>
      ) : null}

      <section>
        <div className="text-eyebrow uppercase text-mu-dim">Setup</div>
        <div className="mt-2">
          <Field
            label="Disc image"
            help={
              snap?.disc_found
                ? undefined
                : "Not found at this path. The port reads assets from it on every launch."
            }
            env="MELEE_DISC"
          >
            <div className="flex items-center gap-3">
              <StatusDot ok={!!snap?.disc_found} />
              <TextInput
                wide
                mono
                value={String(store.values["disc"] ?? "")}
                placeholder="/path/to/melee.iso"
                onChange={(v) => store.set("disc", v)}
              />
              <Button onClick={pickDisc}>Browse</Button>
            </div>
          </Field>

          <Field
            label="Port binary"
            help={
              snap?.port_found
                ? undefined
                : "Build it with cmake --build build/native, or point the launcher at it."
            }
          >
            <div className="flex items-center gap-3">
              <StatusDot ok={!!snap?.port_found} />
              <span className="selectable max-w-[28rem] truncate font-mono text-[0.8rem] text-mu-dim">
                {snap?.port_path}
              </span>
              <Button onClick={pickPort}>Browse</Button>
            </div>
          </Field>
        </div>
      </section>

      <section>
        <div className="text-eyebrow uppercase text-mu-dim">Profile</div>
        <div className="mt-2">
          <Field label="Launch profile" help={chosen.blurb}>
            <Select
              value={profile}
              onChange={(v) => {
                setProfile(v);
                api.setProfile(v).catch(() => {});
              }}
              options={PROFILES.map((p) => ({ value: p.id, label: p.label }))}
            />
          </Field>
        </div>
        <p className="mt-3 max-w-prose text-[0.8rem] leading-relaxed text-mu-dim">
          Profiles are applied as environment overrides, not saved settings, so
          recording a clip never rewrites the settings you play with.
        </p>
      </section>

      <section className="pb-4">
        <div className="flex items-center justify-between">
          <div className="text-eyebrow uppercase text-mu-dim">Output</div>
          <label className="flex cursor-pointer items-center gap-2 text-[0.78rem] text-mu-dim">
            <input
              type="checkbox"
              checked={follow}
              onChange={(e) => setFollow(e.target.checked)}
              className="accent-[var(--color-mu-violet)]"
            />
            Follow
          </label>
        </div>
        <div
          ref={logRef}
          className="selectable mt-2 h-64 overflow-y-auto border border-white/10 bg-mu-card p-4 font-mono text-[0.76rem] leading-relaxed"
        >
          {log.length === 0 ? (
            <div className="text-mu-dim/60">
              Nothing yet. The port's output appears here while it runs.
            </div>
          ) : (
            log.map((line, i) => (
              <div
                key={i}
                className={
                  line.includes("PANIC") || line.includes("SIG")
                    ? "text-mu-violet"
                    : line.startsWith("[")
                      ? "text-mu-dim"
                      : "text-mu-white/80"
                }
              >
                {line}
              </div>
            ))
          )}
        </div>
      </section>
    </div>
  );
}
