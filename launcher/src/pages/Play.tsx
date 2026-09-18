import { useEffect, useRef, useState } from "react";
import { open } from "@tauri-apps/plugin-dialog";
import type { Store } from "../App";
import { api, type ReleaseInfo } from "../lib/api";
import {
  Button,
  Field,
  PageHead,
  Select,
  StatusDot,
  TextInput,
} from "../components/controls";

const PROFILES = [
  { id: "play", label: "Play" },
  { id: "record", label: "Record" },
  { id: "debug", label: "Debug" },
];

function bytes(n: number) {
  if (!n) return "";
  const mb = n / (1024 * 1024);
  return mb >= 1 ? `${mb.toFixed(1)} MB` : `${Math.ceil(n / 1024)} kB`;
}

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
  const [release, setRelease] = useState<ReleaseInfo | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [note, setNote] = useState<string | null>(null);
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
    /* No extension filter.
     *
     * A filter is the dialog's *default* selection, not a hint, so anything
     * not matching it is simply invisible -- and disc images turn up as .iso,
     * .gcm, .gcz, .rvz, .ciso, .nkit.iso, or with no extension at all. The
     * owner could not see his own ISO through the list I guessed. The port
     * validates what it is handed; the picker should not second-guess it. */
    const picked = await open({ multiple: false, directory: false });
    if (typeof picked === "string") store.set("disc", picked);
  };

  const pickPort = async () => {
    const picked = await open({ multiple: false, directory: false });
    if (typeof picked === "string") {
      await api.setPortPath(picked);
      await store.reload();
    }
  };

  const check = async () => {
    setBusy("check");
    setNote(null);
    try {
      setRelease(await api.latestRelease());
    } catch (e) {
      setNote(String(e));
      setRelease(null);
    } finally {
      setBusy(null);
    }
  };

  const install = async () => {
    if (!release) return;
    setBusy("download");
    setNote(null);
    try {
      await api.downloadRelease(release);
      setRelease(null);
      setNote("Installed.");
      await store.reload();
    } catch (e) {
      setNote(String(e));
    } finally {
      setBusy(null);
    }
  };

  const ready = !!snap?.disc_found && !!snap?.port_found;
  const needsDisc = !snap?.disc_found;

  return (
    <div className="space-y-8">
      <PageHead
        eyebrow="Melee Unbound"
        title={running ? "Running" : ready ? "Ready" : "Setup"}
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
              Launch
            </Button>
          )
        }
      />

      <section>
        <Field label="Disc image" env="MELEE_DISC">
          <div className="flex items-center gap-3">
            <StatusDot ok={!!snap?.disc_found} />
            <TextInput
              wide
              mono
              value={String(store.values["disc"] ?? "")}
              placeholder="No disc selected"
              onChange={(v) => store.set("disc", v)}
            />
            <Button
              onClick={pickDisc}
              variant={needsDisc ? "primary" : "ghost"}
              attention={needsDisc}
            >
              Browse
            </Button>
          </div>
        </Field>

        <Field
          label="Port binary"
          help={release ? `${release.tag} · ${bytes(release.size)}` : undefined}
        >
          <div className="flex items-center gap-3">
            <StatusDot ok={!!snap?.port_found} />
            <span className="selectable max-w-[22rem] truncate font-mono text-[0.8rem] text-mu-dim">
              {snap?.port_found ? snap.port_path : "Not installed"}
            </span>
            {release ? (
              <Button variant="primary" onClick={install} disabled={!!busy}>
                {busy === "download" ? "Downloading…" : `Install ${release.tag}`}
              </Button>
            ) : (
              <Button onClick={check} disabled={!!busy}>
                {busy === "check" ? "Checking…" : "Check for update"}
              </Button>
            )}
            <Button onClick={pickPort}>Browse</Button>
          </div>
        </Field>

        {note ? (
          <p className="pt-3 text-[0.8rem] text-mu-dim">{note}</p>
        ) : null}
      </section>

      <section>
        <Field label="Profile">
          <Select
            value={profile}
            onChange={(v) => {
              setProfile(v);
              api.setProfile(v).catch(() => {});
            }}
            options={PROFILES.map((p) => ({ value: p.id, label: p.label }))}
          />
        </Field>
      </section>

      <section className="pb-4">
        <div className="flex items-center justify-between">
          <div className="text-eyebrow uppercase text-mu-dim">Output</div>
          <label className="flex cursor-pointer items-center gap-2 text-[0.78rem] text-mu-dim">
            <input
              type="checkbox"
              checked={follow}
              onChange={(e) => setFollow(e.target.checked)}
            />
            Follow
          </label>
        </div>
        <div
          ref={logRef}
          className="selectable mt-2 h-64 overflow-y-auto border border-white/10 bg-mu-card p-4 font-mono text-[0.76rem] leading-relaxed"
        >
          {log.map((line, i) => (
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
          ))}
        </div>
      </section>
    </div>
  );
}
