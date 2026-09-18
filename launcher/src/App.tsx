import { useCallback, useEffect, useMemo, useState } from "react";
import { api, onPortExit, onPortLine, type Snapshot, type Value } from "./lib/api";
import { StatusDot } from "./components/controls";
import Play from "./pages/Play";
import Mods from "./pages/Mods";
import Graphics from "./pages/Graphics";
import Crashes from "./pages/Crashes";

export type PageId = "play" | "mods" | "graphics" | "crashes";

const NAV: { id: PageId; label: string }[] = [
  { id: "play", label: "Play" },
  { id: "mods", label: "Mods" },
  { id: "graphics", label: "Graphics" },
  { id: "crashes", label: "Crashes" },
];

/** The launcher's shared edit buffer.
 *
 * Pages read `values` and call `set`; nothing writes to disk until Save, so a
 * half-moved slider never reaches the file the port is about to read. */
export type Store = {
  snap: Snapshot | null;
  values: Record<string, Value>;
  dirty: boolean;
  set: (key: string, value: Value) => void;
  save: () => Promise<void>;
  reload: () => Promise<void>;
};

export default function App({ initialPage }: { initialPage?: PageId } = {}) {
  const [page, setPage] = useState<PageId>(initialPage ?? "play");
  const [snap, setSnap] = useState<Snapshot | null>(null);
  const [values, setValues] = useState<Record<string, Value>>({});
  const [dirty, setDirty] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const [log, setLog] = useState<string[]>([]);
  const [running, setRunning] = useState(false);
  const [lastExit, setLastExit] = useState<string | null>(null);

  const reload = useCallback(async () => {
    const s = await api.snapshot();
    setSnap(s);
    setValues(s.values);
    setDirty(false);
  }, []);

  useEffect(() => {
    reload().catch((e) => setError(String(e)));
  }, [reload]);

  useEffect(() => {
    const unlisten = [
      onPortLine((line) =>
        setLog((prev) => {
          const next = prev.concat(line);
          // The port can emit thousands of lines a minute; the pane keeps the
          // tail, and the crash report keeps the part that matters anyway.
          return next.length > 2000 ? next.slice(next.length - 2000) : next;
        }),
      ),
      onPortExit((e) => {
        setRunning(false);
        setLastExit(e.crashed ? `crashed — ${e.code}` : e.code);
        if (e.crashed) {
          setPage("crashes");
          reload().catch(() => {});
        }
      }),
    ];
    return () => {
      unlisten.forEach((p) => p.then((f) => f()).catch(() => {}));
    };
  }, [reload]);

  const store: Store = useMemo(
    () => ({
      snap,
      values,
      dirty,
      set: (key, value) => {
        setValues((prev) => ({ ...prev, [key]: value }));
        setDirty(true);
      },
      save: async () => {
        try {
          await api.save(values);
          setError(null);
          await reload();
        } catch (e) {
          setError(String(e));
        }
      },
      reload,
    }),
    [snap, values, dirty, reload],
  );

  const start = async (profile: string, seed?: string) => {
    try {
      if (dirty) await store.save();
      setLog([]);
      setLastExit(null);
      await api.start(profile, seed);
      setRunning(true);
      setError(null);
    } catch (e) {
      setError(String(e));
    }
  };

  const stop = async () => {
    await api.stop();
  };

  return (
    <div className="flex h-full bg-mu-black text-mu-white">
      <nav className="flex w-56 shrink-0 flex-col border-r border-white/10">
        <div className="px-6 pb-7 pt-7">
          <div className="text-h4 leading-none">MELEE</div>
          <div className="text-h4 leading-none text-mu-violet">UNBOUND</div>
          <div className="mt-2 text-micro uppercase text-mu-dim">Launcher</div>
        </div>

        <ul className="flex-1">
          {NAV.map((item) => {
            const active = page === item.id;
            return (
              <li key={item.id}>
                <button
                  type="button"
                  onClick={() => setPage(item.id)}
                  className={`flex w-full items-center gap-3 border-l-2 px-6 py-3 text-left text-micro uppercase transition-colors ${
                    active
                      ? "border-mu-violet bg-white/[0.04] text-mu-white"
                      : "border-transparent text-mu-dim hover:bg-white/[0.02] hover:text-mu-white"
                  }`}
                >
                  <span className="flex-1">{item.label}</span>
                  {item.id === "crashes" && snap && snap.crashes > 0 ? (
                    <span className="bg-mu-violet px-1.5 py-0.5 text-[0.65rem] font-bold text-mu-black">
                      {snap.crashes}
                    </span>
                  ) : null}
                </button>
              </li>
            );
          })}
        </ul>

        <div className="space-y-2 border-t border-white/10 px-6 py-5 text-[0.75rem] text-mu-dim">
          <div className="flex items-center gap-2">
            <StatusDot ok={!!snap?.port_found} />
            <span>{snap?.port_found ? "Port found" : "No port binary"}</span>
          </div>
          <div className="flex items-center gap-2">
            <StatusDot ok={!!snap?.disc_found} />
            <span>{snap?.disc_found ? "Disc found" : "No disc"}</span>
          </div>
          {running ? (
            <div className="flex items-center gap-2 text-mu-violet">
              <StatusDot ok />
              <span>Running</span>
            </div>
          ) : lastExit ? (
            <div className="truncate" title={lastExit}>
              {lastExit}
            </div>
          ) : null}
        </div>
      </nav>

      <main className="flex min-w-0 flex-1 flex-col">
        {error ? (
          <div className="on-violet flex items-center justify-between gap-4 bg-mu-violet px-8 py-3 text-[0.85rem] font-semibold text-mu-black">
            <span className="min-w-0 truncate">{error}</span>
            <button
              type="button"
              onClick={() => setError(null)}
              className="text-micro uppercase underline"
            >
              Dismiss
            </button>
          </div>
        ) : null}

        <div className="min-h-0 flex-1 overflow-y-auto px-8 py-8">
          {page === "play" ? (
            <Play
              store={store}
              log={log}
              running={running}
              onStart={start}
              onStop={stop}
            />
          ) : page === "mods" ? (
            <Mods store={store} />
          ) : page === "graphics" ? (
            <Graphics store={store} />
          ) : (
            <Crashes store={store} onReplay={(seed) => start("play", seed)} />
          )}
        </div>

        {dirty ? (
          <div className="flex items-center justify-between gap-4 border-t border-mu-violet/40 bg-white/[0.03] px-8 py-4">
            <span className="text-[0.85rem] text-mu-dim">
              Unsaved changes to{" "}
              <code className="selectable">{snap?.config_path}</code>
            </span>
            <div className="flex gap-3">
              <button
                type="button"
                onClick={() => reload()}
                className="border border-white/15 px-4 py-2.5 text-micro uppercase text-mu-dim transition-colors hover:border-white/40 hover:text-mu-white"
              >
                Revert
              </button>
              <button
                type="button"
                onClick={() => store.save()}
                className="on-violet bg-mu-violet px-6 py-2.5 text-micro uppercase text-mu-black transition-colors hover:bg-mu-violet-hi"
              >
                Save
              </button>
            </div>
          </div>
        ) : null}
      </main>
    </div>
  );
}
