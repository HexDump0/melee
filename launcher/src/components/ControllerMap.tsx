/* A controller you can click.
 *
 * Drawn rather than pulled from a component library: the palette is three
 * colours and square corners, and a third-party widget set would have to be
 * fought back to that on every control. This is one SVG with the same tokens
 * as everything else.
 *
 * It is not decoration. The list below it answers "what is A bound to"; this
 * answers "what is this button on my controller called", which is the question
 * someone holding the pad is actually asking. Clicking a part starts the
 * rebind for it, and the part being bound pulses. */

export type Part = {
  action: string;
  label: string;
  /** Where the label sits, when the shape is too small to hold one. */
  tag?: { x: number; y: number };
};

const VIOLET = "var(--color-mu-violet)";

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
  const fill = (action: string) =>
    selected === action ? VIOLET : bound(action) ? "#24242a" : "transparent";
  const stroke = (action: string) =>
    selected === action ? VIOLET : bound(action) ? "#9a9aa4" : "#33333b";
  const text = (action: string) =>
    selected === action ? "#000000" : bound(action) ? "#ffffff" : "#55555f";

  const hit = (action: string) => ({
    onClick: interactive ? () => onPick(action) : undefined,
    style: { cursor: interactive ? "pointer" : "default" } as const,
    className:
      selected === action
        ? "animate-attention motion-reduce:animate-none"
        : undefined,
  });

  const Round = ({
    action,
    cx,
    cy,
    r,
    label,
    size = 13,
  }: {
    action: string;
    cx: number;
    cy: number;
    r: number;
    label: string;
    size?: number;
  }) => (
    <g {...hit(action)}>
      <circle
        cx={cx}
        cy={cy}
        r={r}
        fill={fill(action)}
        stroke={stroke(action)}
        strokeWidth={2}
      />
      <text
        x={cx}
        y={cy + size * 0.35}
        textAnchor="middle"
        fontSize={size}
        fontWeight={800}
        fill={text(action)}
      >
        {label}
      </text>
    </g>
  );

  const Slab = ({
    action,
    x,
    y,
    w,
    h,
    label,
  }: {
    action: string;
    x: number;
    y: number;
    w: number;
    h: number;
    label: string;
  }) => (
    <g {...hit(action)}>
      <rect
        x={x}
        y={y}
        width={w}
        height={h}
        fill={fill(action)}
        stroke={stroke(action)}
        strokeWidth={2}
      />
      <text
        x={x + w / 2}
        y={y + h / 2 + 4}
        textAnchor="middle"
        fontSize={12}
        fontWeight={800}
        fill={text(action)}
      >
        {label}
      </text>
    </g>
  );

  /* The stick and d-pad are four directions each, so they are drawn as a hub
   * with four wedges rather than one shape -- you bind "stick up", never
   * "stick". */
  const Cluster = ({
    prefix,
    cx,
    cy,
    r,
    title,
  }: {
    prefix: string;
    cx: number;
    cy: number;
    r: number;
    title: string;
  }) => {
    const arm = r * 0.62;
    const dirs = [
      { key: "up", dx: 0, dy: -1 },
      { key: "down", dx: 0, dy: 1 },
      { key: "left", dx: -1, dy: 0 },
      { key: "right", dx: 1, dy: 0 },
    ];
    return (
      <g>
        <circle cx={cx} cy={cy} r={r} fill="none" stroke="#2a2a31" strokeWidth={2} />
        <text
          x={cx}
          y={cy + r + 16}
          textAnchor="middle"
          fontSize={9}
          fontWeight={700}
          letterSpacing="0.12em"
          fill="#6b6b76"
        >
          {title}
        </text>
        {dirs.map((d) => {
          const action = `${prefix}_${d.key}`;
          return (
            <g key={d.key} {...hit(action)}>
              <rect
                x={cx + d.dx * arm - 8}
                y={cy + d.dy * arm - 8}
                width={16}
                height={16}
                fill={fill(action)}
                stroke={stroke(action)}
                strokeWidth={2}
              />
            </g>
          );
        })}
      </g>
    );
  };

  return (
    <svg
      viewBox="0 0 460 250"
      className="w-full max-w-xl"
      role="group"
      aria-label="Controller map"
    >
      {/*
        A capsule, not a silhouette.  A traced controller outline drawn badly
        looks like a mistake; a deliberate schematic does not, and the job here
        is to say where the buttons are relative to each other.
      */}
      <rect
        x={70}
        y={40}
        width={320}
        height={160}
        rx={80}
        fill="#0e0e10"
        stroke="#23232a"
        strokeWidth={2}
      />

      <Slab action="l" x={110} y={14} w={60} h={20} label="L" />
      <Slab action="z" x={222} y={14} w={44} h={20} label="Z" />
      <Slab action="r" x={300} y={14} w={60} h={20} label="R" />

      <Cluster prefix="stick" cx={155} cy={90} r={30} title="STICK" />
      <Cluster prefix="dpad" cx={185} cy={160} r={20} title="D-PAD" />

      <Round action="a" cx={320} cy={100} r={24} label="A" size={16} />
      <Round action="b" cx={282} cy={128} r={14} label="B" />
      <Round action="y" cx={302} cy={62} r={14} label="Y" />
      <Round action="x" cx={356} cy={80} r={14} label="X" />

      <Cluster prefix="cstick" cx={272} cy={170} r={20} title="C-STICK" />
      <Round action="start" cx={230} cy={112} r={11} label="S" size={10} />
    </svg>
  );
}
