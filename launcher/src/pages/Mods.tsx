import type { Store } from "../App";
import type { ModInfo, Setting } from "../lib/api";
import {
  Card,
  Field,
  PageHead,
  Select,
  Slider,
  TextInput,
  Toggle,
} from "../components/controls";

/* This page is generated, not written.
 *
 * Each control comes from a `[[setting]]` block in that mod's own mod.toml, so
 * a mod that ships one gets a labelled, ranged, explained control without the
 * launcher knowing it exists. A mod with no schema still appears -- it just
 * has nothing to configure here. */
function control(store: Store, mod: ModInfo, setting: Setting) {
  const key = `mods.${mod.id}.${setting.key}`;
  const raw = store.values[key];
  const label =
    setting.label ??
    setting.key.replace(/_/g, " ").replace(/^./, (c) => c.toUpperCase());
  const env = `MELEE_MOD_${mod.id.toUpperCase()}_${setting.key.toUpperCase()}`;

  if (setting.kind === "bool") {
    const value = raw === true || raw === 1 || raw === "1";
    return (
      <Field key={key} label={label} help={setting.help} env={env}>
        <Toggle checked={value} onChange={(v) => store.set(key, v)} />
      </Field>
    );
  }

  if (setting.kind === "enum" && setting.choices.length > 0) {
    const value = typeof raw === "number" ? raw : (setting.default ?? 0);
    return (
      <Field key={key} label={label} help={setting.help} env={env}>
        <Select
          value={String(value)}
          options={setting.choices.map((c) => ({
            value: String(c.value),
            label: c.label,
          }))}
          onChange={(v) => store.set(key, Number(v))}
        />
      </Field>
    );
  }

  if (setting.kind === "string") {
    const value = typeof raw === "string" ? raw : (setting.default_str ?? "");
    return (
      <Field key={key} label={label} help={setting.help} env={env}>
        <TextInput value={value} onChange={(v) => store.set(key, v)} />
      </Field>
    );
  }

  const isInt = setting.kind === "int";
  const value = typeof raw === "number" ? raw : (setting.default ?? 0);
  return (
    <Field key={key} label={label} help={setting.help} env={env}>
      <Slider
        value={value}
        min={setting.min ?? 0}
        max={setting.max ?? (isInt ? 10 : 1)}
        step={setting.step ?? (isInt ? 1 : 0.01)}
        onChange={(v) => store.set(key, v)}
      />
    </Field>
  );
}

export default function Mods({ store }: { store: Store }) {
  const snap = store.snap;
  const mods = snap?.mods ?? [];

  return (
    <div className="space-y-8">
      <PageHead
        eyebrow="Mods"
        title={`${mods.length} loaded`}
        lede="Loaded in ascending priority, so a mod later in this list layers on top of the ones above it."
      />

      {mods.length === 0 ? (
        <Card className="p-6">
          <p className="text-[0.9rem] text-mu-dim">
            No mods found in{" "}
            <code className="selectable">{snap?.mods_dir}</code>. A mod is a
            folder with a <code>mod.toml</code> in it.
          </p>
        </Card>
      ) : null}

      {mods.map((mod) => (
        <section key={mod.id}>
          <div className="flex items-baseline justify-between gap-4 border-b border-white/10 pb-3">
            <div className="min-w-0">
              <h2 className="text-h4">{mod.name}</h2>
              <p className="mt-1 font-mono text-[0.75rem] text-mu-dim">
                {mod.id} · {mod.version} · priority {mod.priority}
                {mod.module ? "" : " · built in"}
              </p>
            </div>
            <span className="shrink-0 text-micro uppercase text-mu-violet">
              {mod.settings.length > 0
                ? `${mod.settings.length} settings`
                : "No settings"}
            </span>
          </div>

          {mod.settings.length > 0 ? (
            <div className="mt-2">
              {mod.settings.map((s) => control(store, mod, s))}
            </div>
          ) : (
            <p className="mt-4 max-w-prose text-[0.82rem] leading-relaxed text-mu-dim">
              This mod declares no settings. Adding a{" "}
              <code>[[setting]]</code> block to its <code>mod.toml</code> gives
              it controls here, with no change to the launcher.
            </p>
          )}
        </section>
      ))}
    </div>
  );
}
