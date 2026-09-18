import type { Store } from "../App";
import { Field, PageHead, Select, Slider, Toggle } from "../components/controls";

/* The cinematic preset's controls.
 *
 * Ranges and defaults match native/decomp/gx/gx_gl.c. The help text carries
 * the one thing a slider cannot: threshold is in *linear* light, so 0.90 is
 * about 0.96 on screen, and dropping it is what hazed the whole picture the
 * first time this shipped. */
const CINEMATIC: {
  key: string;
  label: string;
  help: string;
  min: number;
  max: number;
  step: number;
  def: number;
}[] = [
  {
    key: "cinematic.threshold",
    label: "Bloom threshold",
    help: "Linear light. Lower it and bright stages haze over.",
    min: 0.3,
    max: 1,
    step: 0.01,
    def: 0.9,
  },
  {
    key: "cinematic.bloom",
    label: "Bloom",
    help: "",
    min: 0,
    max: 1,
    step: 0.01,
    def: 0.2,
  },
  {
    key: "cinematic.wide",
    label: "Wide halo",
    help: "",
    min: 0,
    max: 1,
    step: 0.01,
    def: 0.1,
  },
  {
    key: "cinematic.rim",
    label: "Rim light",
    help: "Per-pixel fresnel on lit geometry.",
    min: 0,
    max: 1.5,
    step: 0.01,
    def: 0.35,
  },
  {
    key: "cinematic.sharpen",
    label: "Sharpen",
    help: "",
    min: 0,
    max: 1,
    step: 0.01,
    def: 0.35,
  },
  {
    key: "cinematic.exposure",
    label: "Exposure",
    help: "Holds brightness against the tone curve.",
    min: 0.4,
    max: 1.4,
    step: 0.01,
    def: 0.85,
  },
  {
    key: "cinematic.sat",
    label: "Saturation",
    help: "",
    min: 0.5,
    max: 1.6,
    step: 0.01,
    def: 1.1,
  },
  {
    key: "cinematic.vignette",
    label: "Vignette",
    help: "",
    min: 0,
    max: 0.8,
    step: 0.01,
    def: 0.26,
  },
];

const WIDESCREEN = [
  { value: "0", label: "Off — 4:3" },
  { value: "1", label: "Gameplay only" },
  { value: "2", label: "Everywhere" },
];

export default function Graphics({ store }: { store: Store }) {
  const num = (key: string, fallback: number) => {
    const v = store.values[key];
    return typeof v === "number" ? v : fallback;
  };
  const on = (key: string) => {
    const v = store.values[key];
    return v === true || v === 1 || v === "1";
  };
  const enabled = on("cinematic.enabled");

  return (
    <div className="space-y-8">
      <PageHead
        eyebrow="Graphics"
        title="Cinematic"
      />

      <section>
        <Field
          label="Cinematic preset"
          help="Presentation only. F8 toggles it in game."
          env="MELEE_CINEMATIC"
        >
          <Toggle
            checked={enabled}
            onChange={(v) => store.set("cinematic.enabled", v)}
          />
        </Field>

        <div
          className={
            enabled
              ? ""
              : "pointer-events-none opacity-35 transition-opacity"
          }
          aria-hidden={!enabled}
        >
          {CINEMATIC.map((s) => (
            <Field
              key={s.key}
              label={s.label}
              help={s.help || undefined}
              env={`MELEE_${s.key.replace(".", "_").toUpperCase()}`}
            >
              <Slider
                value={num(s.key, s.def)}
                min={s.min}
                max={s.max}
                step={s.step}
                onChange={(v) => store.set(s.key, v)}
              />
            </Field>
          ))}
        </div>
      </section>

      <section>
        <div className="text-eyebrow uppercase text-mu-dim">Presentation</div>
        <div className="mt-2">
          <Field
            label="Widescreen"
            help="Everywhere widens menus too, which shows gaps at the edges."
            env="MELEE_MOD_UNBOUND_WIDESCREEN"
          >
            <Select
              value={String(num("mods.unbound.widescreen", 1))}
              options={WIDESCREEN}
              onChange={(v) => store.set("mods.unbound.widescreen", Number(v))}
            />
          </Field>
          <Field
            label="Correct the aspect"
            help="Retail is ~9.5% wider than neutral. A departure, not a fix."
            env="MELEE_MOD_UNBOUND_ASPECT_CORRECT"
          >
            <Toggle
              checked={on("mods.unbound.aspect_correct")}
              onChange={(v) => store.set("mods.unbound.aspect_correct", v)}
            />
          </Field>
        </div>
      </section>

    </div>
  );
}
