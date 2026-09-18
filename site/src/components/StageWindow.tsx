/* The window chrome the page draws around the stage.

   Two variants. `browser` is the hero's frame: three dots, a label, and on
   the two-ways card a URL bar reading `localhost` — nothing on the page
   points at a hosted build, because none exists. `app` is the desktop port's
   launcher: a title bar and a sidebar, the way the reference draws it.

   The media itself is `stage.svg` — the line-art stage — used as the
   video's poster. A gameplay capture dropped in at `/media/gameplay.mp4`
   replaces it in the hero with no code change; the two-ways cards stay
   stills, so the page only ever decodes one video. */

import { PlayIcon } from './icons.tsx';

type Props = {
  variant: 'browser' | 'app';
  /** Browser chrome only: show a URL bar reading `localhost`. */
  url?: boolean;
  /** Play the video rather than showing the still. */
  video?: boolean;
  /** Draw the violet play ring over the media. */
  play?: boolean;
  /** Small label in the media's bottom-right corner. */
  status?: string;
  /** The hero's window is boxier than the two-ways cards, like the comp. */
  ratio?: 'video' | 'photo';
};

const ASPECT = {
  video: 'aspect-video',
  photo: 'aspect-[16/10]',
} as const;

export function StageWindow({
  variant,
  url = false,
  video = false,
  play = false,
  status,
  ratio = 'video',
}: Props) {
  return (
    <div className="overflow-hidden rounded-2xl border border-mu-white/20 bg-mu-black shadow-[0_0_80px_-35px_rgba(255,255,255,0.35)]">
      {variant === 'browser' ? <BrowserBar url={url} /> : <AppBar />}

      {/* The aspect sits on the whole body, sidebar included, so the two
          windows in "Browser or desktop?" end up exactly the same height. */}
      <div
        className={
          variant === 'app'
            ? `grid ${ASPECT[ratio]} sm:grid-cols-[9rem_1fr]`
            : `grid ${ASPECT[ratio]}`
        }
      >
        {variant === 'app' && <AppSidebar />}
        <Media video={video} play={play} status={status} />
      </div>
    </div>
  );
}

function BrowserBar({ url }: { url: boolean }) {
  return (
    <div className="flex items-center gap-2.5 border-b border-mu-white/10 px-5 py-4">
      <Dots />
      {url && (
        <span className="mx-auto hidden items-center border border-mu-white/10 px-4 py-1 text-sm text-mu-dim sm:flex">
          localhost
        </span>
      )}
      <span className="ml-auto text-sm tracking-[0.14em] uppercase text-mu-dim">Melee Unbound</span>
    </div>
  );
}

function AppBar() {
  return (
    <div className="flex items-center gap-2 border-b border-mu-white/10 px-5 py-4">
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

function Media({ video, play, status }: { video: boolean; play: boolean; status?: string }) {
  return (
    <div className="grid min-h-0">
      {video ? (
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
      ) : (
        <img
          src="/media/stage.svg"
          alt=""
          aria-hidden
          width={1600}
          height={900}
          className="col-start-1 row-start-1 h-full w-full select-none bg-mu-card object-cover"
        />
      )}

      {status && (
        <span className="col-start-1 row-start-1 self-end justify-self-end px-6 pb-5 text-sm tracking-[0.14em] uppercase text-mu-dim">
          {status}{' '}
          <span className="animate-caret motion-reduce:animate-none" aria-hidden>
            _
          </span>
        </span>
      )}

      {play && (
        <span className="col-start-1 row-start-1 place-self-center text-mu-violet">
          <PlayIcon className="animate-ring h-24 w-24 motion-reduce:animate-none lg:h-28 lg:w-28" />
        </span>
      )}
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
