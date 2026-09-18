/* The /play page: load the browser port staged in /play/ and hand it the
   user's disc.

   The port is `native/tools/wasm_census.sh` output — the game, the port layer
   and Unbound compiled into one wasm module (ADR-0026) — plus the `wasm-opt
   -O2` pass, staged by `site/scripts/build-play.sh`. React owns the chrome;
   the module owns everything on the canvas.

   Two things here are load-bearing against the port rather than cosmetic:

   - The canvas must be `id="canvas"`. SDL3's emscripten backend resolves its
     GL canvas by selector (`SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR`, default
     `#canvas`); with any other id `SDL_GL_CreateContext` fails with "Could not
     create webgl context".
   - The disc never leaves the tab: `platform/disc.c`'s wasm backend reads
     `Module.meleeDiscFile` through `Blob.slice()` ranges.

   The module lifecycle is imperative (globals, callbacks, a canvas), so it
   lives in refs and the React state is only the UI around it: which panel is
   showing, the log, fullscreen. */

import { useCallback, useEffect, useRef, useState } from 'react';

/** Pages of 64 KiB the port reserves: `INITIAL_MEMORY=2415919104`. */
const MEM1_PAGES = 36864;
const GLUE = '/play/melee.js';

type MeleeModule = {
  noInitialRun: boolean;
  canvas?: HTMLCanvasElement;
  locateFile?: (path: string) => string;
  print?: (line: string) => void;
  printErr?: (line: string) => void;
  setStatus?: (text: string) => void;
  onRuntimeInitialized?: () => void;
  onAbort?: (what: unknown) => void;
  onExit?: (code: number) => void;
  meleeDiscFile?: File;
  callMain?: (args: string[]) => void;
};

declare global {
  interface Window {
    Module?: MeleeModule;
  }
}

type Phase = 'ready' | 'loading' | 'playing' | 'notice' | 'crash';
type Panel = { title?: string; body: string; tone?: 'warn' | 'error' };

const DT = 'text-mu-dim';
const DD = 'text-mu-white/80';

export function PlayPage() {
  const canvas = useRef<HTMLCanvasElement>(null);
  const logEl = useRef<HTMLPreElement>(null);
  const dropEl = useRef<HTMLLabelElement>(null);
  const consoleEl = useRef<HTMLDetailsElement>(null);

  const [phase, setPhase] = useState<Phase>('ready');
  const [panel, setPanel] = useState<Panel>({ body: '' });
  const [filename, setFilename] = useState('Melee Unbound');
  const [progress, setProgress] = useState('Loading the port…');
  const [bar, setBar] = useState(0);
  const [consoleOpen, setConsoleOpen] = useState(false);
  const [fullscreen, setFullscreen] = useState(false);

  const started = useRef(false);
  const moduleReady = useRef(false);
  const pendingRun = useRef<(() => void) | null>(null);
  const crashCause = useRef<string | null>(null);

  const log = useCallback((line: string) => {
    const el = logEl.current;
    if (!el) return;
    el.textContent += `${line}\n`;
    el.scrollTop = el.scrollHeight;
  }, []);

  /** The game stopped: say so in the window, with a way back in. A named
      cause (no WebGL2, out of memory, the port never loaded) wins over the
      generic abort/exit that follows it, so the panel says why. */
  const crashed = useCallback(
    (title: string, body: string, specific = false) => {
      if (crashCause.current) return;
      crashCause.current = specific ? title : 'stopped';
      setPanel({
        title: specific ? title : title,
        body,
        tone: specific ? 'error' : 'error',
      });
      setConsoleOpen(true);
      setPhase('crash');
    },
    [],
  );

  /** Turn the first recognisable failure into a sentence, once. */
  const hinted = useRef(false);
  const hint = useCallback(
    (line: string) => {
      if (hinted.current) return;
      if (/webgl context|SDL_GL_CreateContext failed/i.test(line)) {
        hinted.current = true;
        crashed(
          'No WebGL2',
          'The port could not get a WebGL2 context. Turn on hardware acceleration in your browser settings, then press Play again.',
          true,
        );
      } else if (/Cannot enlarge memory|out of memory|allocation failed/i.test(line)) {
        hinted.current = true;
        crashed(
          'Out of memory',
          'The port could not reserve the memory it needs. Close other tabs and press Play again.',
          true,
        );
      }
    },
    [crashed],
  );

  const buildMissing = useCallback(() => {
    crashed(
      'The port is not built',
      'public/play/melee.js is missing from this deploy. Run site/scripts/build-play.sh (needs Emscripten), then reload. The desktop build is under How to play.',
      true,
    );
  }, [crashed]);

  /** Why this browser cannot run the build, or null when it can. */
  const unsupportedReason = useCallback((): string | null => {
    if (typeof WebAssembly !== 'object') return 'This browser has no WebAssembly.';
    if (
      typeof (WebAssembly as unknown as { Suspending?: unknown }).Suspending !==
      'function'
    ) {
      return 'This build suspends the wasm stack for disc reads (JSPI), which needs a recent desktop browser — Chrome/Edge 137+ or Firefox 139+.';
    }
    try {
      new WebAssembly.Memory({ initial: MEM1_PAGES });
    } catch {
      return `This browser will not reserve the ${(MEM1_PAGES * 64) / 1024 / 1024} MiB the port maps its game RAM into. Desktop browsers on a 64-bit machine can.`;
    }
    return null;
  }, []);

  const run = useCallback(
    (mod: MeleeModule) => {
      if (typeof mod.callMain !== 'function') {
        buildMissing();
        return;
      }
      setPhase('playing');
      setProgress('');
      canvas.current?.focus();
      log('booting main()');
      try {
        mod.callMain([]);
      } catch (error) {
        log(`callMain threw: ${String(error)}`);
        setConsoleOpen(true);
      }
    },
    [buildMissing, log],
  );

  const start = useCallback(
    async (file: File) => {
      started.current = true;
      setPhase('loading');
      setFilename(`${file.name} · ${(file.size / 1024 / 1024).toFixed(0)} MB`);
      setProgress('Loading the port…');
      log(`disc: ${file.name} (${file.size} bytes)`);

      const mod: MeleeModule = {
        noInitialRun: true,
        canvas: canvas.current ?? undefined,
        locateFile: (path) => `/play/${path}`,
        print: log,
        printErr: (line) => {
          log(line);
          hint(line);
        },
        setStatus: (text) => {
          if (!text) return;
          setProgress(text);
          const done = /\((\d+)\s*\/\s*(\d+)\)/.exec(text);
          if (done) setBar((Number(done[1]) / Number(done[2])) * 100);
        },
        onAbort: (what) => {
          const message = String(what);
          log(`abort: ${message}`);
          const missing = message.includes('fetch') || message.includes('404');
          crashed(
            missing ? 'The port did not load' : 'The game crashed',
            missing
              ? 'Its melee.wasm was missing or would not compile. Rebuild with site/scripts/build-play.sh, then try again.'
              : 'It stopped before you closed it. The console has the last lines the port printed — the first error is usually the whole story.',
          );
        },
        onExit: (code) => {
          log(`exit: ${code}`);
          crashed(
            code === 0 ? 'The game closed' : 'The game crashed',
            code === 0
              ? 'That was a clean exit. Press Play again when you want another match.'
              : `It exited with code ${code}. The console has the last lines the port printed.`,
          );
        },
        onRuntimeInitialized: () => {
          moduleReady.current = true;
          pendingRun.current?.();
        },
        meleeDiscFile: file,
      };
      window.Module = mod;

      try {
        await new Promise<void>((resolve, reject) => {
          const script = document.createElement('script');
          script.src = GLUE;
          script.onload = () => resolve();
          script.onerror = () => reject(new Error(`could not load ${GLUE}`));
          document.head.appendChild(script);
        });
      } catch {
        buildMissing();
        return;
      }

      pendingRun.current = () => {
        if (started.current && moduleReady.current) run(mod);
      };
      if (moduleReady.current) run(mod);
    },
    [buildMissing, crashed, hint, log, run],
  );

  // Support gate, once.
  useEffect(() => {
    const reason = unsupportedReason();
    if (reason) {
      setPanel({
        body: reason,
        tone: 'warn',
      });
      setPhase('notice');
    }
  }, [unsupportedReason]);

  // Fullscreen state drives the chrome button.
  useEffect(() => {
    const onChange = () => setFullscreen(document.fullscreenElement != null);
    document.addEventListener('fullscreenchange', onChange);
    return () => document.removeEventListener('fullscreenchange', onChange);
  }, []);

  // Emscripten's abort throws after `onAbort` has already drawn the panel, so
  // the browser would otherwise log an uncaught error beside it.
  useEffect(() => {
    const quiet = (event: ErrorEvent) => {
      if (/Aborted\(|native code called abort/.test(event.message)) {
        event.preventDefault();
      }
    };
    window.addEventListener('error', quiet);
    return () => window.removeEventListener('error', quiet);
  }, []);

  // Dropping a file anywhere else must not navigate away from a running game.
  useEffect(() => {
    const stop = (event: DragEvent) => event.preventDefault();
    for (const type of ['dragover', 'drop'] as const) {
      document.addEventListener(type, stop, { capture: true });
    }
    return () => {
      for (const type of ['dragover', 'drop'] as const) {
        document.removeEventListener(type, stop, { capture: true });
      }
    };
  }, []);

  const onDrop = (event: React.DragEvent) => {
    event.preventDefault();
    dropEl.current?.classList.remove('border-mu-violet', 'bg-mu-violet/10');
    const file = event.dataTransfer?.files?.[0];
    if (file && !started.current) void start(file);
  };

  const toggleFullscreen = () => {
    if (document.fullscreenElement) void document.exitFullscreen?.();
    else void canvas.current?.requestFullscreen?.();
  };

  const openConsole = () => {
    setConsoleOpen(true);
    consoleEl.current?.scrollIntoView({ behavior: 'smooth', block: 'center' });
  };

  const dialogClass =
    panel.tone === 'warn'
      ? 'max-w-lg border border-mu-violet/40 bg-mu-black/90 p-6 text-lede'
      : 'max-w-lg border border-mu-violet/60 bg-mu-black/90 p-6 text-lede';

  return (
    <div className="mx-auto flex min-h-svh w-full max-w-[100rem] flex-col px-5 md:px-10 lg:px-16">
      <header className="flex flex-wrap items-center gap-x-8 gap-y-4 pt-5">
        <a href="/" aria-label="Melee Unbound — back to the site">
          <img src="/media/wordmark.svg" alt="Melee Unbound" width={880} height={193} className="w-52 lg:w-60" />
        </a>
        <span aria-hidden className="hidden h-px flex-1 bg-mu-white/15 sm:block" />
        <a
          href="/"
          className="text-sm font-extrabold tracking-[0.2em] uppercase text-mu-white/60 transition-colors hover:text-mu-white"
        >
          Back to site
        </a>
      </header>

      <main className="flex flex-1 flex-col items-center gap-8 py-8 lg:gap-10 lg:py-10">
        <div className="max-w-2xl text-center">
          <h1 className="text-3xl leading-[1.06] font-black tracking-tight text-balance lg:text-4xl">
            NATIVE WEB BUILD
          </h1>
        </div>

        <Controls />

        <div className="w-full max-w-4xl lg:max-w-5xl">
          <div className="border border-mu-white/20 bg-mu-black shadow-[0_0_80px_-35px_rgba(255,255,255,0.35)]">
            <div className="flex h-14 items-center gap-2.5 border-b border-mu-white/10 px-5">
              <span className="min-w-0 flex-1 truncate text-sm tracking-[0.14em] uppercase text-mu-dim">
                {filename}
              </span>
              {phase === 'playing' && (
                <button
                  type="button"
                  onClick={toggleFullscreen}
                  title={fullscreen ? 'Exit fullscreen' : 'Fullscreen'}
                  aria-label={fullscreen ? 'Exit fullscreen' : 'Fullscreen'}
                  className="inline-flex h-9 w-9 shrink-0 items-center justify-center border border-mu-white/15 text-mu-dim transition-colors hover:border-mu-white/40 hover:text-mu-white"
                >
                  {fullscreen ? (
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.8} strokeLinecap="round" strokeLinejoin="round" aria-hidden className="w-4">
                      <path d="M9 4v5H4M20 9h-5V4M15 20v-5h5M4 15h5v5" />
                    </svg>
                  ) : (
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.8} strokeLinecap="round" strokeLinejoin="round" aria-hidden className="w-4">
                      <path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5" />
                    </svg>
                  )}
                </button>
              )}
            </div>

            <div className="grid bg-mu-card">
              <canvas
                id="canvas"
                ref={canvas}
                tabIndex={-1}
                width={1280}
                height={800}
                className="col-start-1 row-start-1 block h-auto w-full bg-black outline-none"
              />

              {phase !== 'playing' && (
                <div
                  className="col-start-1 row-start-1 grid bg-mu-card"
                  onDragOver={(event) => {
                    event.preventDefault();
                    dropEl.current?.classList.add('border-mu-violet', 'bg-mu-violet/10');
                  }}
                  onDragLeave={() =>
                    dropEl.current?.classList.remove('border-mu-violet', 'bg-mu-violet/10')
                  }
                  onDrop={onDrop}
                >
                  {phase === 'ready' && (
                    <div className="col-start-1 row-start-1 grid place-items-center p-6">
                      <label
                        ref={dropEl}
                        className="flex max-w-md cursor-pointer flex-col items-center gap-4 border border-dashed border-mu-white/25 px-10 py-12 text-center transition-colors hover:border-mu-violet hover:bg-mu-violet/5"
                      >
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.7} strokeLinecap="round" strokeLinejoin="round" aria-hidden className="w-9 text-mu-violet">
                          <circle cx="12" cy="12" r="9" />
                          <circle cx="12" cy="12" r="2.5" />
                        </svg>
                        <span className="flex flex-col gap-1">
                          <span className="text-h4 uppercase">Choose your disc image</span>
                          <span className="text-lede text-mu-dim">Drop it here or click to browse</span>
                        </span>
                        <span className="text-micro uppercase text-mu-dim">
                          Melee v1.02 · GALE01 · .iso .gcm .ciso
                        </span>
                        <input
                          type="file"
                          accept=".iso,.gcm,.ciso"
                          className="sr-only"
                          onChange={(event) => {
                            const file = event.target.files?.[0];
                            if (file && !started.current) void start(file);
                          }}
                        />
                      </label>
                    </div>
                  )}

                  {phase === 'loading' && (
                    <div className="col-start-1 row-start-1 flex flex-col items-center justify-center gap-4 p-6 text-center">
                      <p className="text-lede text-mu-white/80">{progress}</p>
                      <div className="h-0.5 w-56 overflow-hidden bg-mu-white/10">
                        <div
                          className="h-full bg-mu-violet transition-[width] duration-200"
                          style={{ width: `${bar}%` }}
                        />
                      </div>
                      <p className="text-micro uppercase text-mu-dim">
                        First run fetches the module; it is cached after that.
                      </p>
                    </div>
                  )}

                  {phase === 'notice' && (
                    <div className="col-start-1 row-start-1 grid place-items-center p-6">
                      <div className={dialogClass}>
                        {panel.body}{' '}
                        <a className="underline underline-offset-4" href="/#download">
                          The desktop build
                        </a>{' '}
                        runs anywhere.
                      </div>
                    </div>
                  )}

                  {phase === 'crash' && (
                    <div className="col-start-1 row-start-1 grid place-items-center p-6">
                      <div className="flex max-w-md flex-col items-center gap-6 text-center">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" aria-hidden className="w-14 text-mu-violet">
                          <path d="M12 3.8 21 19.5H3z" />
                          <path d="M12 9.6v4.3" />
                          <path d="M12 17h.01" />
                        </svg>
                        <div className="flex flex-col gap-3">
                          <h2 className="text-h3 uppercase">{panel.title}</h2>
                          <p className="text-lede text-mu-dim">{panel.body}</p>
                        </div>
                        <div className="flex flex-wrap justify-center gap-3">
                          <button
                            type="button"
                            onClick={() => location.reload()}
                            className="inline-flex items-center justify-center gap-4 bg-mu-violet px-8 py-4 text-sm font-extrabold tracking-[0.14em] text-mu-black uppercase transition-colors hover:bg-mu-violet-hi"
                          >
                            Play again
                          </button>
                          <button
                            type="button"
                            onClick={openConsole}
                            className="inline-flex items-center justify-center gap-4 border border-mu-white/25 px-8 py-4 text-sm font-extrabold tracking-[0.14em] text-mu-white uppercase transition-colors hover:border-mu-white"
                          >
                            Open console
                          </button>
                        </div>
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>
        </div>

        <div className="flex w-full max-w-3xl flex-col gap-6">
          <details
            ref={consoleEl}
            open={consoleOpen}
            onToggle={(event) => setConsoleOpen(event.currentTarget.open)}
            className="group border-t border-mu-white/10 pt-5"
          >
            <summary className="flex cursor-pointer items-center justify-between gap-4 text-micro uppercase text-mu-white/80">
              Console
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} strokeLinecap="round" aria-hidden className="w-4 text-mu-dim transition-transform duration-300 group-open:rotate-180">
                <path d="m6 9 6 6 6-6" />
              </svg>
            </summary>
            <pre
              ref={logEl}
              className="mt-5 max-h-64 overflow-y-auto border border-mu-white/10 bg-mu-card p-4 font-mono text-xs leading-relaxed whitespace-pre-wrap text-mu-dim"
            />
          </details>

          <p className="border-t border-mu-white/10 pt-5 text-center text-micro uppercase text-mu-dim">
            Need the desktop build?{' '}
            <a href="/#download" className="text-mu-white underline-offset-4 hover:underline">
              Get it Here
            </a>
          </p>
        </div>
      </main>
    </div>
  );
}

function Controls() {
  return (
    <details className="group w-full max-w-2xl border-mu-white/10 pt-5">
      <summary className="flex cursor-pointer items-center justify-between gap-4 text-micro uppercase text-mu-white/80">
        Controls
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} strokeLinecap="round" aria-hidden className="w-4 text-mu-dim transition-transform duration-300 group-open:rotate-180">
          <path d="m6 9 6 6 6-6" />
        </svg>
      </summary>
      <dl className="mt-5 grid grid-cols-[auto_1fr] gap-x-6 gap-y-2 text-sm">
        <dt className={DT}>P1 stick</dt>
        <dd className={DD}>arrow keys</dd>
        <dt className={DT}>P1 buttons</dt>
        <dd className={DD}>
          <code>Z</code> A · <code>X</code> B · <code>C</code> X · <code>V</code> Y
        </dd>
        <dt className={DT}>P1 shoulders</dt>
        <dd className={DD}>
          <code>A</code> L · <code>S</code> R · <code>Q</code> Z · <code>Enter</code> start
        </dd>
        <dt className={DT}>P2</dt>
        <dd className={DD}>
          <code>I J K L</code> stick · <code>F</code> A · <code>G</code> B · <code>T</code> start
        </dd>
        <dt className={DT}>Gamepad</dt>
        <dd className={DD}>SDL3 pads, one per port, hot-plugged</dd>
      </dl>
    </details>
  );
}
