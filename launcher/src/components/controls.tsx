/* The control vocabulary.
 *
 * Every page is built from these, so the rules from branding.md live here once:
 * square corners, one violet, and BLACK text on any violet fill -- white on
 * violet is 2.77:1 and fails, which is the single contrast trap in this
 * palette. */
import type { ReactNode } from "react";

export function Eyebrow({ children }: { children: ReactNode }) {
  return (
    <div className="text-eyebrow uppercase text-mu-violet">{children}</div>
  );
}

export function PageHead({
  eyebrow,
  title,
  lede,
  action,
}: {
  eyebrow: string;
  title: string;
  lede?: string;
  action?: ReactNode;
}) {
  return (
    <div className="flex items-end justify-between gap-6 border-b border-white/10 pb-6">
      <div className="min-w-0 animate-enter motion-reduce:animate-none">
        <Eyebrow>{eyebrow}</Eyebrow>
        <h1 className="mt-2 text-h3">{title}</h1>
        {lede ? (
          <p className="mt-2 max-w-prose text-lede text-mu-dim">{lede}</p>
        ) : null}
      </div>
      {action ? <div className="shrink-0">{action}</div> : null}
    </div>
  );
}

export function Button({
  children,
  onClick,
  variant = "ghost",
  disabled,
  title,
  size = "md",
}: {
  children: ReactNode;
  onClick?: () => void;
  variant?: "primary" | "ghost" | "danger";
  disabled?: boolean;
  title?: string;
  size?: "md" | "lg";
}) {
  const base =
    "inline-flex items-center justify-center gap-2 text-micro uppercase " +
    "transition-colors disabled:opacity-40 disabled:pointer-events-none";
  const sizing = size === "lg" ? "px-8 py-4 text-eyebrow" : "px-4 py-2.5";
  const look =
    variant === "primary"
      ? /* black label on violet -- see the contrast note above */
        "on-violet bg-mu-violet text-mu-black hover:bg-mu-violet-hi"
      : variant === "danger"
        ? "border border-white/15 text-mu-dim hover:border-white/40 hover:text-mu-white"
        : "border border-white/15 text-mu-white hover:border-mu-violet hover:text-mu-violet";
  return (
    <button
      type="button"
      title={title}
      onClick={onClick}
      disabled={disabled}
      className={`${base} ${sizing} ${look}`}
    >
      {children}
    </button>
  );
}

export function Field({
  label,
  help,
  env,
  children,
}: {
  label: string;
  help?: string | null;
  env?: string | null;
  children: ReactNode;
}) {
  return (
    <div className="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-x-6 gap-y-1 border-b border-white/5 py-4">
      <div className="min-w-0">
        <div className="text-[0.95rem] font-semibold">{label}</div>
        {help ? (
          <p className="mt-1 max-w-prose text-[0.82rem] leading-relaxed text-mu-dim">
            {help}
          </p>
        ) : null}
        {env ? (
          /* Naming the variable is not decoration: everything here is also an
             environment variable, and an exported one silently outranks this
             file. Someone chasing "why did my change do nothing" needs it. */
          <p className="mt-1 font-mono text-[0.72rem] text-mu-dim/60">{env}</p>
        ) : null}
      </div>
      <div className="justify-self-end">{children}</div>
    </div>
  );
}

export function Toggle({
  checked,
  onChange,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      onClick={() => onChange(!checked)}
      className={`relative h-7 w-14 border transition-colors ${
        checked
          ? "border-mu-violet bg-mu-violet"
          : "border-white/25 bg-white/[0.06] hover:border-white/50"
      }`}
    >
      {/* Black knob on violet, white on the empty track: the same contrast
          rule as the buttons, and the reason the two states cannot be
          confused at a glance. */}
      <span
        className={`absolute left-1 top-1/2 block h-5 w-5 -translate-y-1/2 transition-transform ${
          checked
            ? "translate-x-[1.75rem] bg-mu-black"
            : "translate-x-0 bg-white/60"
        }`}
      />
    </button>
  );
}

export function Slider({
  value,
  min,
  max,
  step,
  onChange,
}: {
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
}) {
  const pct = max > min ? ((value - min) / (max - min)) * 100 : 0;
  return (
    <div className="flex items-center gap-4">
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        className="mu-range h-1 w-56 appearance-none bg-white/15 outline-none"
        style={{
          background: `linear-gradient(to right, var(--color-mu-violet) ${pct}%, rgba(255,255,255,0.15) ${pct}%)`,
        }}
      />
      <span className="w-14 text-right font-mono text-[0.82rem] tabular-nums text-mu-white">
        {Number.isInteger(step) ? value : value.toFixed(2)}
      </span>
    </div>
  );
}

export function Select({
  value,
  options,
  onChange,
}: {
  value: string;
  options: { value: string; label: string }[];
  onChange: (v: string) => void;
}) {
  return (
    <select
      value={value}
      onChange={(e) => onChange(e.target.value)}
      className="min-w-48 border border-white/15 bg-mu-card px-3 py-2 text-[0.88rem] text-mu-white outline-none hover:border-white/40 focus-visible:border-mu-violet"
    >
      {options.map((o) => (
        <option key={o.value} value={o.value} className="bg-mu-card">
          {o.label}
        </option>
      ))}
    </select>
  );
}

export function TextInput({
  value,
  onChange,
  placeholder,
  mono,
  wide,
}: {
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  mono?: boolean;
  wide?: boolean;
}) {
  return (
    <input
      type="text"
      value={value}
      placeholder={placeholder}
      onChange={(e) => onChange(e.target.value)}
      className={`selectable border border-white/15 bg-mu-card px-3 py-2 text-[0.88rem] text-mu-white outline-none placeholder:text-mu-dim/50 hover:border-white/40 focus-visible:border-mu-violet ${
        mono ? "font-mono text-[0.8rem]" : ""
      } ${wide ? "w-[28rem]" : "w-64"}`}
    />
  );
}

export function Card({
  children,
  className = "",
}: {
  children: ReactNode;
  className?: string;
}) {
  return (
    <div className={`border border-white/10 bg-mu-card ${className}`}>
      {children}
    </div>
  );
}

export function StatusDot({ ok }: { ok: boolean }) {
  return (
    <span
      className={`inline-block h-2 w-2 shrink-0 ${ok ? "bg-mu-violet" : "bg-white/25"}`}
      aria-hidden
    />
  );
}
