//! Mod discovery, and the settings each mod declares.
//!
//! The launcher builds its Mods page from `mod.toml` rather than from a list
//! kept in the launcher. A mod that ships a `[[setting]]` block gets a real
//! control -- labelled, ranged, explained -- without the launcher knowing it
//! exists, which is the difference between a settings screen and a mod
//! platform.
//!
//! `[[setting]]` is an array of tables, deliberately, rather than
//! `[settings.widescreen]`: `[settings]` already carries the *defaults* the
//! loader reads, and TOML will not let a key be both a value and a table.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Choice {
    pub value: f64,
    pub label: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Setting {
    pub key: String,
    /// "bool" | "int" | "float" | "enum" | "string"
    #[serde(default = "default_kind")]
    pub kind: String,
    #[serde(default)]
    pub label: Option<String>,
    #[serde(default)]
    pub help: Option<String>,
    #[serde(default)]
    pub default: Option<f64>,
    #[serde(default)]
    pub default_str: Option<String>,
    #[serde(default)]
    pub min: Option<f64>,
    #[serde(default)]
    pub max: Option<f64>,
    #[serde(default)]
    pub step: Option<f64>,
    #[serde(default)]
    pub choices: Vec<Choice>,
}

fn default_kind() -> String {
    "int".to_string()
}

#[derive(Debug, Clone, Serialize)]
pub struct ModInfo {
    pub id: String,
    pub name: String,
    pub version: String,
    pub priority: i64,
    pub dir: String,
    /// Absent on the browser build, where the mod is compiled in.
    pub module: Option<String>,
    pub settings: Vec<Setting>,
}

#[derive(Deserialize)]
struct Manifest {
    id: Option<String>,
    name: Option<String>,
    version: Option<String>,
    priority: Option<i64>,
    module: Option<String>,
    #[serde(default)]
    setting: Vec<Setting>,
}

/// Where the port looks: $MELEE_MODS_DIR, else `mods/` beside the repo.
pub fn mods_dir(repo_root: &Path) -> PathBuf {
    if let Ok(dir) = std::env::var("MELEE_MODS_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    repo_root.join("mods")
}

pub fn scan(dir: &Path) -> Vec<ModInfo> {
    let mut out = Vec::new();
    let entries = match std::fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return out,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let manifest_path = path.join("mod.toml");
        let text = match std::fs::read_to_string(&manifest_path) {
            Ok(t) => t,
            Err(_) => continue,
        };
        let manifest: Manifest = match toml::from_str(&text) {
            Ok(m) => m,
            Err(_) => continue,
        };
        let id = match manifest.id {
            Some(id) if !id.is_empty() => id,
            _ => continue,
        };
        out.push(ModInfo {
            name: manifest.name.clone().unwrap_or_else(|| id.clone()),
            version: manifest.version.unwrap_or_else(|| "0".into()),
            priority: manifest.priority.unwrap_or(100),
            dir: path.to_string_lossy().to_string(),
            module: manifest.module,
            settings: manifest.setting,
            id,
        });
    }
    // The order the loader uses, so the list reads like the load order.
    out.sort_by(|a, b| a.priority.cmp(&b.priority).then(a.id.cmp(&b.id)));
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Against the real `mods/` in this repo, so a schema that stops parsing
    /// shows up here rather than as a Mods page that is silently empty --
    /// which is what a scan returning `Vec::new()` on any error looks like.
    #[test]
    fn reads_the_shipped_mod_and_its_schema() {
        let repo = Path::new(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .nth(2)
            .expect("repo root")
            .to_path_buf();
        let found = scan(&repo.join("mods"));
        let unbound = found
            .iter()
            .find(|m| m.id == "unbound")
            .expect("the shipped mod is discoverable");

        assert_eq!(unbound.name, "Melee Unbound");
        assert_eq!(unbound.priority, 100);
        assert!(unbound.module.is_some(), "desktop build loads a module");

        let widescreen = unbound
            .settings
            .iter()
            .find(|s| s.key == "widescreen")
            .expect("widescreen is declared in mod.toml");
        assert_eq!(widescreen.kind, "enum");
        assert_eq!(widescreen.choices.len(), 3);
        assert!(widescreen.label.is_some() && widescreen.help.is_some());

        // Every declared key has to map to something the port can read, or
        // the launcher would render a control that writes a dead setting.
        for setting in &unbound.settings {
            let dotted = format!("mods.{}.{}", unbound.id, setting.key);
            assert!(
                crate::config::env_name(&dotted).is_some(),
                "{dotted} maps to no variable"
            );
        }
    }
}
