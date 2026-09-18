/* The window chrome the page draws around the stage.

   Two variants. `browser` is the hero's frame: three dots, a label, and on
   the two-ways card a URL bar reading `localhost` — nothing on the page
   points at a hosted build, because none exists. `app` is the desktop port's
   launcher: a title bar and a sidebar, the way the reference draws it.

   The hero's window plays the capture (`media="video"`), with `stage.svg`
   as its poster. The two-ways windows are stills showing the lockup on
   black — a title card rather than a screenshot — so only one video ever
   decodes. */

import { LockIcon, ReloadIcon } from './icons.tsx';

type Props = {
  variant: 'browser' | 'app';
  /** Browser chrome only: show a URL bar reading `localhost`. */
  url?: boolean;
  /** `video` plays the capture; `banner` is the still title card. */
  media?: 'banner' | 'video';
};

export function StageWindow({ variant, url = false, media = 'banner' }: Props) {
  return (
    <div className="border border-mu-white/20 bg-mu-black shadow-[0_0_80px_-35px_rgba(255,255,255,0.35)]">
      {variant === 'browser' ? <BrowserBar url={url} /> : <AppBar />}

      {/* The aspect sits on the whole body, sidebar included, and the row is
          `1fr` so the media fills it exactly — without that the drawing's own
          ratio wins and runs past the frame. The two windows in "Browser or
          desktop?" come out exactly the same height. */}
      <div
        className={
          variant === 'app'
            ? 'grid aspect-video grid-rows-[minmax(0,1fr)] sm:grid-cols-[9rem_1fr]'
            : 'grid aspect-video grid-rows-[minmax(0,1fr)]'
        }
      >
        {variant === 'app' && <AppSidebar />}
        <Media media={media} />
      </div>
    </div>
  );
}

/* Both bars are exactly `h-14`, so the two windows in "Browser or desktop?"
   come out the same height whatever their chrome holds. */

function BrowserBar({ url }: { url: boolean }) {
  return (
    <div className="flex h-14 items-center gap-3 border-b border-mu-white/10 px-5">
      <Dots />

      {url && (
        <span className="mx-2 hidden min-w-0 flex-1 items-center gap-3 rounded-full bg-mu-white/5 px-4 py-1.5 text-sm text-mu-white/70 ring-1 ring-mu-white/10 ring-inset sm:flex">
          <LockIcon className="w-3.5 shrink-0 text-mu-dim" />
          <span className="truncate">localhost</span>
          <ReloadIcon className="ml-auto w-4 shrink-0 text-mu-dim" />
        </span>
      )}

      <span className="ml-auto text-sm tracking-[0.14em] uppercase text-mu-dim">Melee Unbound</span>
    </div>
  );
}

function AppBar() {
  return (
    <div className="flex h-14 items-center gap-3 border-b border-mu-white/10 px-5">
      <span className="text-sm tracking-[0.14em] uppercase text-mu-dim">Melee Unbound</span>
      <span aria-hidden className="ml-auto flex items-center gap-3 text-mu-dim">
        <span className="block h-px w-3.5 bg-current" />
        <span className="block h-3.5 w-3.5 border border-current" />
        <span className="block text-lg leading-none">×</span>
      </span>
    </div>
  );
}

/** The launcher's sidebar, with the section it opens on marked violet. */
function AppSidebar() {
  return (
    <aside className="hidden flex-col gap-1 border-r border-mu-white/10 p-5 sm:flex">
      <img src="/media/icon.svg" alt="" width={512} height={512} className="mb-5 w-10" />
      {['Play', 'Setup', 'Docs'].map((item, i) => (
        <span
          key={item}
          className={`px-3 py-2 text-sm tracking-[0.14em] uppercase ${
            i === 0 ? 'bg-mu-violet text-mu-black' : 'text-mu-dim'
          }`}
        >
          {item}
        </span>
      ))}
    </aside>
  );
}

function Media({ media }: { media: 'banner' | 'video' }) {
  if (media === 'video') {
    return (
      <div className="grid min-h-0 grid-rows-[minmax(0,1fr)] overflow-hidden">
        <video
          autoPlay
          muted
          loop
          playsInline
          poster="/media/stage.svg"
          className="col-start-1 row-start-1 h-full w-full bg-mu-card object-cover"
        >
          <source src="/media/gameplay.mp4" type="video/mp4" />
          {/* The owner's capture wins when present; until then the stage
              itself flies, generated from stage.svg. */}
          <source src="/media/stage-loop.mp4" type="video/mp4" />
        </video>
      </div>
    );
  }

  return (
    <div className="grid min-h-0 place-items-center overflow-hidden bg-mu-card p-8">
      <img
        src="/media/wordmark.svg"
        alt="Melee Unbound"
        width={880}
        height={193}
        className="w-3/5 max-w-lg select-none"
      />
    </div>
  );
}

function Dots() {
  return (
    <>
      <span className="h-3 w-3 rounded-full bg-mu-white/25" />
      <span className="h-3 w-3 rounded-full bg-mu-white/25" />
      <span className="h-3 w-3 rounded-full bg-mu-white/25" />
    </>
  );
}
