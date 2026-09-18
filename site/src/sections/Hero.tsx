/* The hero, after the owner's comp: one full viewport, with the window hung
   from the top right and the name standing on the floor of the same row. The
   name overlaps the window's lower-left corner, which is the comp's whole
   poster effect — and why at `xl` the two are stacked in one grid cell
   rather than sitting in two columns.

   Nothing here offers a download or a browser build — see the note at the
   top of content.tsx. The window plays `/media/gameplay.mp4` when that file
   exists; without it, the stage artwork shows, which is what the comp
   draws. */

import { HERO, NAV, TAGLINE } from '../content.tsx';
import { Button } from '../components/Button.tsx';
import { StageWindow } from '../components/StageWindow.tsx';
import { WRAP } from '../lib/layout.ts';

const LINK =
  'text-sm font-extrabold tracking-[0.2em] uppercase text-mu-dim transition-colors hover:text-mu-white lg:text-base';

export function Hero() {
  return (
    <section id="play" className="flex flex-1 scroll-mt-4 flex-col bg-mu-black">
      <div className={`${WRAP} flex flex-1 flex-col pt-8 pb-8`}>
        {/* At `xl` both children sit in the one cell: the window hangs from
            the top, the copy stands on the floor, and they overlap. Below
            `xl` they stack in reading order. */}
        <div className="grid flex-1 content-start gap-y-12 xl:grid-rows-[1fr] xl:content-stretch">
          <div
            className="animate-enter-media order-2 self-start motion-reduce:animate-none xl:col-start-1 xl:row-start-1 xl:w-[60%] xl:justify-self-end"
            style={{ animationDelay: '200ms' }}
          >
            <StageWindow variant="browser" video play ratio="photo" status="Ready to play" />
          </div>

          <div className="animate-enter order-1 z-10 self-end motion-reduce:animate-none xl:col-start-1 xl:row-start-1">
            <h1 className="font-display text-display uppercase italic">
              <span className="block">{HERO.headline[0]}</span>
              <span className="block">{HERO.headline[1]}</span>
            </h1>

            <p className="mt-6 text-sm font-extrabold uppercase tracking-[0.4em] text-mu-white lg:text-base">
              {HERO.tagline}
            </p>

            <div className="mt-8 flex flex-wrap gap-4">
              {HERO.doors.map((door) => (
                <Button key={door.to} {...door} className="min-w-56 flex-1 sm:flex-none" />
              ))}
            </div>
          </div>
        </div>

        <div className="mt-10 flex flex-wrap items-center justify-between gap-x-10 gap-y-5">
          <p className="flex items-center gap-5 text-sm font-extrabold uppercase tracking-[0.16em] text-mu-dim">
            <span aria-hidden className="block h-px w-14 flex-none bg-mu-white/40" />
            <span>
              {TAGLINE[0]}
              <br />
              {TAGLINE[1]}
            </span>
          </p>

          <nav aria-label="Hero" className="flex items-center gap-7">
            <span aria-hidden className="block h-px w-28 flex-none bg-mu-white/25" />
            <a href="#play" className={LINK}>
              Play
            </a>
            {NAV.map(({ id, label }) => (
              <a key={id} href={`#${id}`} className={LINK}>
                {label}
              </a>
            ))}
            <span aria-hidden className="h-7 w-px bg-mu-white/25" />
          </nav>
        </div>
      </div>
    </section>
  );
}
