//! melee.toml, read and written the way the port reads it.
//!
//! The port maps a dotted config key to an environment variable by a rule --
//! `MELEE_` plus the path, uppercased, with `enabled` collapsing into its
//! section (see `native/platform/melee_config.c`). The launcher therefore does
//! not need a schema to *store* settings: it works in dotted keys and lets the
//! port do the mapping, so a new port setting needs no launcher change.
//!
//! Edits go through `toml_edit`, not a serialize-the-whole-struct round trip.
//! The example file ships with comments explaining every knob, and a launcher
//! that erased them the first time you moved a slider would be a downgrade
//! from editing the file by hand.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use toml_edit::{DocumentMut, Item, Value as TomlValue};

/// A setting value, in the three shapes TOML and the UI both understand.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum Value {
    Bool(bool),
    Num(f64),
    Str(String),
}

impl Value {
    fn from_toml(v: &TomlValue) -> Option<Value> {
        match v {
            TomlValue::Boolean(b) => Some(Value::Bool(*b.value())),
            TomlValue::Integer(i) => Some(Value::Num(*i.value() as f64)),
            TomlValue::Float(f) => Some(Value::Num(*f.value())),
            TomlValue::String(s) => Some(Value::Str(s.value().clone())),
            _ => None,
        }
    }

    fn to_toml(&self) -> TomlValue {
        match self {
            Value::Bool(b) => TomlValue::from(*b),
            Value::Num(n) => {
                // Keep whole numbers integral: the port parses either, but
                // `widescreen = 1.0` in a file a person reads is noise.
                if n.fract() == 0.0 && n.abs() < 1e15 {
                    TomlValue::from(*n as i64)
                } else {
                    TomlValue::from(*n)
                }
            }
            Value::Str(s) => TomlValue::from(s.as_str()),
        }
    }
}

pub struct ConfigFile {
    pub path: PathBuf,
    doc: DocumentMut,
}

impl ConfigFile {
    /// The file the port would pick: $MELEE_CONFIG, then XDG, then ~/.config.
    /// Deliberately the same order as `melee_config_load`, minus `./melee.toml`
    /// -- the launcher's working directory is its own, not the port's, and
    /// writing a melee.toml next to the launcher binary would be a surprise.
    pub fn default_path() -> PathBuf {
        if let Ok(p) = std::env::var("MELEE_CONFIG") {
            if !p.is_empty() {
                return PathBuf::from(p);
            }
        }
        if let Some(dir) = dirs::config_dir() {
            return dir.join("melee").join("melee.toml");
        }
        PathBuf::from("melee.toml")
    }

    pub fn load(path: &Path) -> ConfigFile {
        let text = std::fs::read_to_string(path).unwrap_or_default();
        let doc = text.parse::<DocumentMut>().unwrap_or_default();
        ConfigFile { path: path.to_path_buf(), doc }
    }

    /// Every scalar in the document, as dotted keys. Tables are walked so
    /// `[mods.unbound] widescreen` comes back as `mods.unbound.widescreen`,
    /// which is exactly what the port's mapping consumes.
    pub fn values(&self) -> std::collections::BTreeMap<String, Value> {
        let mut out = std::collections::BTreeMap::new();
        walk(self.doc.as_table(), "", &mut out);
        out
    }

    pub fn set(&mut self, dotted: &str, value: &Value) {
        let parts: Vec<&str> = dotted.split('.').filter(|s| !s.is_empty()).collect();
        if parts.is_empty() {
            return;
        }
        let (leaf, sections) = parts.split_last().unwrap();
        let mut item: &mut Item = self.doc.as_item_mut();
        for section in sections {
            // `or_insert` on a missing section creates an implicit table,
            // which round-trips as `[a.b]` rather than nesting braces.
            let table = item.as_table_like_mut().expect("table path");
            if table.get(section).is_none() {
                let mut fresh = toml_edit::Table::new();
                fresh.set_implicit(false);
                table.insert(section, Item::Table(fresh));
            }
            item = table.get_mut(section).unwrap();
        }
        if let Some(table) = item.as_table_like_mut() {
            table.insert(leaf, Item::Value(value.to_toml()));
        }
    }

    pub fn remove(&mut self, dotted: &str) {
        let parts: Vec<&str> = dotted.split('.').filter(|s| !s.is_empty()).collect();
        if parts.is_empty() {
            return;
        }
        let (leaf, sections) = parts.split_last().unwrap();
        let mut item: &mut Item = self.doc.as_item_mut();
        for section in sections {
            match item.as_table_like_mut().and_then(|t| t.get_mut(section)) {
                Some(next) => item = next,
                None => return,
            }
        }
        if let Some(table) = item.as_table_like_mut() {
            table.remove(leaf);
        }
    }

    pub fn save(&self) -> std::io::Result<()> {
        if let Some(parent) = self.path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(&self.path, self.doc.to_string())
    }

    pub fn text(&self) -> String {
        self.doc.to_string()
    }
}

fn walk(
    table: &dyn toml_edit::TableLike,
    prefix: &str,
    out: &mut std::collections::BTreeMap<String, Value>,
) {
    for (key, item) in table.iter() {
        let dotted =
            if prefix.is_empty() { key.to_string() } else { format!("{prefix}.{key}") };
        match item {
            Item::Value(v) => {
                if let Some(value) = Value::from_toml(v) {
                    out.insert(dotted, value);
                } else if let Some(inline) = v.as_inline_table() {
                    walk(inline, &dotted, out);
                }
            }
            Item::Table(t) => walk(t, &dotted, out),
            _ => {}
        }
    }
}

/// The port's own mapping, mirrored so the launcher can show which variable a
/// setting becomes -- and so the round trip is testable on this side too.
/// Kept byte-compatible with `melee_config_env_name`.
pub fn env_name(dotted: &str) -> Option<String> {
    let mut key = dotted.to_string();
    if key == "enabled" || key.is_empty() {
        return None;
    }
    if let Some(stripped) = key.strip_suffix(".enabled") {
        key = stripped.to_string();
    }
    if let Some(rest) = key.strip_prefix("mods.") {
        key = format!("mod.{rest}");
    }
    let mut out = String::from("MELEE_");
    for ch in key.chars() {
        match ch {
            '.' | '-' => out.push('_'),
            'a'..='z' => out.push(ch.to_ascii_uppercase()),
            'A'..='Z' | '0'..='9' | '_' => out.push(ch),
            _ => return None,
        }
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mapping_matches_the_port() {
        assert_eq!(env_name("match.cpu").unwrap(), "MELEE_MATCH_CPU");
        assert_eq!(
            env_name("mods.unbound.widescreen").unwrap(),
            "MELEE_MOD_UNBOUND_WIDESCREEN"
        );
        assert_eq!(env_name("cinematic.enabled").unwrap(), "MELEE_CINEMATIC");
        assert_eq!(env_name("cinematic.bloom").unwrap(), "MELEE_CINEMATIC_BLOOM");
        assert_eq!(env_name("mods.unbound.enabled").unwrap(), "MELEE_MOD_UNBOUND");
        assert_eq!(env_name("disc").unwrap(), "MELEE_DISC");
        assert!(env_name("enabled").is_none());
        assert!(env_name("bad key").is_none());
    }

    #[test]
    fn edits_keep_the_comments() {
        let dir = std::env::temp_dir().join("mu-launcher-test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("melee.toml");
        std::fs::write(&path, "# keep me\ndisc = \"/a.iso\"\n\n[cinematic]\nbloom = 0.2\n")
            .unwrap();

        let mut cfg = ConfigFile::load(&path);
        cfg.set("cinematic.bloom", &Value::Num(0.35));
        cfg.set("cinematic.enabled", &Value::Bool(true));
        cfg.set("mods.unbound.widescreen", &Value::Num(2.0));
        cfg.save().unwrap();

        let text = std::fs::read_to_string(&path).unwrap();
        assert!(text.contains("# keep me"), "comments survive an edit");
        assert!(text.contains("bloom = 0.35"));
        assert!(text.contains("[mods.unbound]"));
        // Whole numbers stay integral, so the file stays readable.
        assert!(text.contains("widescreen = 2"));

        let reloaded = ConfigFile::load(&path).values();
        assert_eq!(reloaded.get("cinematic.enabled"), Some(&Value::Bool(true)));
        assert_eq!(reloaded.get("disc"), Some(&Value::Str("/a.iso".into())));
        std::fs::remove_dir_all(&dir).ok();
    }
}
