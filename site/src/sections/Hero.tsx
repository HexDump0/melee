/* The hero, after the owner's comp: one full viewport, with the window hung
   from the top right and the copy centred in the row beside it. The headline
   never crosses into the window.

   Nothing here offers a download or a browser build — see the note at the
   top of content.tsx. The window plays `/media/gameplay.mp4` when that file
   exists; without it, the stage artwork shows, which is what the comp
   draws. */

import { HERO, TAGLINE } from '../content.tsx';
import { Button } from '../components/Button.tsx';
import { StageWindow } from '../components/StageWindow.tsx';
import { WRAP } from '../lib/layout.ts';

export function Hero() {
  return (
    <section id="play" className="flex flex-1 scroll-mt-4 flex-col bg-mu-black">
      <div className={`${WRAP} flex flex-1 flex-col pt-8 pb-8`}>
        {/* Two columns at `xl`, reading order below. */}
        <div className="grid flex-1 content-start gap-x-10 gap-y-12 xl:grid-cols-[minmax(0,37fr)_minmax(0,63fr)] xl:grid-rows-[1fr] xl:content-stretch">
          <div
            className="animate-enter-media order-2 self-start motion-reduce:animate-none xl:col-start-2 xl:row-start-1"
            style={{ animationDelay: '200ms' }}
          >
            <StageWindow variant="browser" video />
          </div>

          <div className="animate-enter order-1 flex flex-col justify-center self-stretch motion-reduce:animate-none xl:col-start-1 xl:row-start-1">
            <h1 className="text-4xl leading-[1.06] font-black tracking-tight text-balance lg:text-5xl">
              {HERO.statement}
            </h1>

            <p className="mt-6 max-w-md text-lede text-mu-dim">{HERO.lede}</p>

            <div className="mt-8 flex max-w-sm flex-col gap-4">
              {HERO.doors.map((door) => (
                <Button key={door.to} {...door} className="w-full" />
              ))}
            </div>
          </div>
        </div>

        <div className="mt-10 flex flex-wrap items-center justify-between gap-x-10 gap-y-5">
          <p className="flex items-center gap-5 text-sm font-extrabold uppercase tracking-[0.16em] text-mu-dim">
            <span aria-hidden className="block h-px w-14 flex-none bg-mu-white/40" />
            <span>
              {TAGLINE.map((line, i) => (
                <span key={line}>
                  {line}
                  {i < TAGLINE.length - 1 && <br />}
                </span>
              ))}
            </span>
          </p>

          <p className="flex items-center gap-5 text-sm font-extrabold tracking-[0.2em] uppercase text-mu-dim lg:text-base">
            <span aria-hidden className="block h-px w-28 flex-none bg-mu-white/25" />
            {HERO.status}
            <span aria-hidden className="h-7 w-px bg-mu-white/25" />
          </p>
        </div>
      </div>
    </section>
  );
}
