/* A controller you can click.
 *
 * The artwork is Zacksly's GameCube outline (CC BY 3.0), recoloured to
 * `currentColor` so the page's own palette drives it and with the Nintendo
 * wordmark removed -- see launcher/README.md for the attribution the licence
 * requires, and `src/assets/gamecube-controller.svg` for the modified file.
 *
 * The hotspots are ours, positioned in the artwork's own 4096x2160 coordinate
 * space so they sit on the drawn buttons rather than near them. It is not
 * decoration: the list below answers "what is A bound to", this answers "which
 * one of these is A", which is the question someone holding the pad is asking.
 */
import artRaw from "../assets/gamecube-controller.svg?raw";

/* The file is a whole document; only its contents go inside our <svg>. */
const ART = artRaw
  .replace(/^[\s\S]*?<svg[^>]*>/, "")
  .replace(/<\/svg>\s*$/, "");

const VIOLET = "var(--color-mu-violet)";

/* Read off the artwork: the circles carry their own centres, and the rest were
 * measured against a render rather than guessed. */
const BUTTONS: { action: string; cx: number; cy: number; r: number; label: string }[] = [
  { action: "a", cx: 2582, cy: 833, r: 101, label: "A" },
  { action: "b", cx: 2381, cy: 931, r: 61, label: "B" },
  { action: "y", cx: 2528, cy: 645, r: 54, label: "Y" },
  { action: "x", cx: 2775, cy: 800, r: 54, label: "X" },
  { action: "start", cx: 2048, cy: 850, r: 42, label: "" },
];

/* L, R and Z are on the back edge and a top-down outline cannot show them, so
 * they are labelled pills above the body rather than hotspots floating over
 * artwork that does not depict them. */
const SHOULDERS: { action: string; x: number; label: string }[] = [
  { action: "l", x: 1360, label: "L" },
  { action: "r", x: 2480, label: "R" },
  { action: "z", x: 2800, label: "Z" },
];

const CLUSTERS: {
  prefix: string;
  cx: number;
  cy: number;
  spread: number;
  size: number;
}[] = [
  { prefix: "stick", cx: 1514, cy: 829, spread: 42, size: 26 },
  { prefix: "cstick", cx: 2334, cy: 1216, spread: 44, size: 26 },
  { prefix: "dpad", cx: 1762, cy: 1214, spread: 64, size: 30 },
];

const DIRS = [
  { key: "up", dx: 0, dy: -1 },
  { key: "down", dx: 0, dy: 1 },
  { key: "left", dx: -1, dy: 0 },
  { key: "right", dx: 1, dy: 0 },
];

export default function ControllerMap({
  selected,
  bound,
  onPick,
  interactive = true,
}: {
  selected: string | null;
  /** action -> has a binding, so unbound parts read as hollow. */
  bound: (action: string) => boolean;
  onPick: (action: string) => void;
  /** The gamepad tab binds through a list, so the map is a diagram there.
   *  A part that looks clickable and is not is worse than one that does not. */
  interactive?: boolean;
}) {
  const fill = (a: string) =>
    selected === a ? VIOLET : bound(a) ? "rgba(255,255,255,0.16)" : "transparent";
  const stroke = (a: string) =>
    selected === a ? VIOLET : bound(a) ? "rgba(255,255,255,0.7)" : "rgba(255,255,255,0.2)";
  const ink = (a: string) =>
    selected === a ? "#000000" : bound(a) ? "#ffffff" : "rgba(255,255,255,0.35)";

  const hit = (a: string) => ({
    onClick: interactive ? () => onPick(a) : undefined,
    style: { cursor: interactive ? "pointer" : "default" } as const,
    className:
      selected === a ? "animate-attention motion-reduce:animate-none" : undefined,
  });

  return (
    <div className="w-full max-w-3xl">
      <svg viewBox="0 0 4096 2160" role="group" aria-label="Controller map">
        {/* the artwork, dimmed so the hotspots read on top of it */}
        <g
          className="text-white/50"
          dangerouslySetInnerHTML={{ __html: ART }}
        />

        {BUTTONS.map((b) => (
          <g key={b.action} {...hit(b.action)}>
            <circle
              cx={b.cx}
              cy={b.cy}
              r={b.r}
              fill={fill(b.action)}
              stroke={stroke(b.action)}
              strokeWidth={10}
            />
            {b.label ? (
              <text
                x={b.cx}
                y={b.cy + b.r * 0.34}
                textAnchor="middle"
                fontSize={b.r}
                fontWeight={800}
                fill={ink(b.action)}
              >
                {b.label}
              </text>
            ) : null}
          </g>
        ))}

        {CLUSTERS.map((c) =>
          DIRS.map((d) => {
            const action = `${c.prefix}_${d.key}`;
            return (
              <rect
                key={action}
                {...hit(action)}
                x={c.cx + d.dx * c.spread - c.size / 2}
                y={c.cy + d.dy * c.spread - c.size / 2}
                width={c.size}
                height={c.size}
                fill={fill(action)}
                stroke={stroke(action)}
                strokeWidth={8}
              />
            );
          }),
        )}

        {SHOULDERS.map((s) => (
          <g key={s.action} {...hit(s.action)}>
            <rect
              x={s.x}
              y={270}
              width={s.action === "z" ? 150 : 260}
              height={110}
              fill={fill(s.action)}
              stroke={stroke(s.action)}
              strokeWidth={10}
            />
            <text
              x={s.x + (s.action === "z" ? 75 : 130)}
              y={345}
              textAnchor="middle"
              fontSize={70}
              fontWeight={800}
              fill={ink(s.action)}
            >
              {s.label}
            </text>
          </g>
        ))}
      </svg>

      {/* CC BY 3.0 requires credit; it costs one line. */}
      <p className="pt-1 text-center text-[0.68rem] text-mu-dim/70">
        Controller art by Zacksly (CC BY 3.0, modified) · zacksly.itch.io
      </p>
    </div>
  );
}
