import { useEffect, useState } from "react";
import type { Store } from "../App";
import { api, type CrashReport } from "../lib/api";
import { Button, Card, PageHead } from "../components/controls";

/* The port already prints everything a bug report needs: a backtrace, the live
 * fighters, and the seed that replays the run exactly. What it never had was
 * somewhere for that to go -- it scrolled past in a terminal and got pasted
 * into a chat window by hand. */
function when(id: string) {
  const secs = Number(id);
  if (!Number.isFinite(secs) || secs <= 0) return id;
  return new Date(secs * 1000).toLocaleString();
}

export default function Crashes({
  store,
  onReplay,
}: {
  store: Store;
  onReplay: (seed: string) => void;
}) {
  const [reports, setReports] = useState<CrashReport[]>([]);
  const [openId, setOpenId] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);

  const refresh = () => {
    api
      .crashes()
      .then((r) => {
        setReports(r);
        setOpenId((prev) => prev ?? (r.length > 0 ? r[0].id : null));
      })
      .catch(() => {});
  };

  useEffect(refresh, []);

  const forget = async (id: string) => {
    await api.forgetCrash(id);
    if (openId === id) setOpenId(null);
    refresh();
    store.reload().catch(() => {});
  };

  const copy = async (report: CrashReport) => {
    const text = [
      `Melee Unbound crash — ${when(report.id)}`,
      `Reason: ${report.reason}`,
      `Exit: ${report.exit}`,
      `Profile: ${report.profile}`,
      report.seed ? `Seed: MELEE_RNG_SEED=${report.seed}` : "Seed: not recorded",
      "",
      "--- melee.toml ---",
      report.config.trim(),
      "",
      "--- output ---",
      ...report.log,
    ].join("\n");
    await navigator.clipboard.writeText(text);
    setCopied(report.id);
    setTimeout(() => setCopied(null), 1600);
  };

  const open = reports.find((r) => r.id === openId) ?? null;

  return (
    <div className="space-y-8">
      <PageHead
        eyebrow="Crashes"
        title={reports.length === 0 ? "Nothing to report" : `${reports.length} captured`}
      />

      {reports.length === 0 ? (
        <Card className="p-6">
          <p className="text-[0.9rem] text-mu-dim">
            Reports appear here when the port panics or dies on a signal.
          </p>
        </Card>
      ) : (
        <div className="grid grid-cols-[18rem_minmax(0,1fr)] gap-6">
          <ul className="space-y-2">
            {reports.map((r) => {
              const active = r.id === openId;
              return (
                <li key={r.id}>
                  <button
                    type="button"
                    onClick={() => setOpenId(r.id)}
                    className={`w-full border-l-2 px-4 py-3 text-left transition-colors ${
                      active
                        ? "border-mu-violet bg-mu-card-hi"
                        : "border-transparent bg-mu-card hover:bg-mu-card-hi"
                    }`}
                  >
                    <div className="truncate text-[0.82rem] font-semibold">
                      {r.reason}
                    </div>
                    <div className="mt-1 text-[0.72rem] text-mu-dim">
                      {when(r.id)} · {r.profile}
                    </div>
                  </button>
                </li>
              );
            })}
          </ul>

          {open ? (
            <div className="min-w-0 space-y-4">
              <Card className="p-5">
                <div className="text-eyebrow uppercase text-mu-violet">
                  {open.exit}
                </div>
                <h2 className="selectable mt-2 break-words text-h4">
                  {open.reason}
                </h2>
                <dl className="mt-4 grid grid-cols-[7rem_minmax(0,1fr)] gap-y-2 text-[0.8rem]">
                  <dt className="text-mu-dim">When</dt>
                  <dd>{when(open.id)}</dd>
                  <dt className="text-mu-dim">Profile</dt>
                  <dd>{open.profile}</dd>
                  <dt className="text-mu-dim">Seed</dt>
                  <dd className="selectable font-mono">
                    {open.seed ?? "not recorded"}
                  </dd>
                </dl>
                <div className="mt-5 flex flex-wrap gap-3">
                  <Button
                    variant="primary"
                    disabled={!open.seed}
                    onClick={() => open.seed && onReplay(open.seed)}
                    title={
                      open.seed
                        ? "Launch again with this run's seed"
                        : "This run printed no seed"
                    }
                  >
                    ▸ Replay this crash
                  </Button>
                  <Button onClick={() => copy(open)}>
                    {copied === open.id ? "Copied" : "Copy report"}
                  </Button>
                  <Button variant="danger" onClick={() => forget(open.id)}>
                    Delete
                  </Button>
                </div>
              </Card>

              <div>
                <div className="text-eyebrow uppercase text-mu-dim">Output</div>
                <div className="selectable mt-2 max-h-[22rem] overflow-auto border border-white/10 bg-mu-card p-4 font-mono text-[0.75rem] leading-relaxed">
                  {open.log.map((line, i) => (
                    <div
                      key={i}
                      className={
                        line.includes("PANIC") || line.includes("SIG")
                          ? "text-mu-violet"
                          : "text-mu-white/75"
                      }
                    >
                      {line}
                    </div>
                  ))}
                </div>
              </div>
            </div>
          ) : null}
        </div>
      )}
    </div>
  );
}
