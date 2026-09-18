// The launcher's window is the UI; this side owns the filesystem, the child
// process and the crash store.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod launch;
mod mods;

use config::{ConfigFile, Value};
use serde::Serialize;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use tauri::{Manager, State};

/// Launcher-only preferences, kept out of `melee.toml` on purpose: that file
/// is the *port's* settings, and every key in it becomes an environment
/// variable the port reads. Where the launcher last found a binary is nobody's
/// business but the launcher's.
#[derive(Debug, Clone, Serialize, serde::Deserialize, Default)]
#[serde(default)]
struct Prefs {
    port_path: Option<String>,
    profile: Option<String>,
}

fn prefs_path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("melee")
        .join("launcher.json")
}

fn load_prefs() -> Prefs {
    std::fs::read_to_string(prefs_path())
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

fn save_prefs(prefs: &Prefs) {
    let path = prefs_path();
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).ok();
    }
    if let Ok(text) = serde_json::to_string_pretty(prefs) {
        std::fs::write(path, text).ok();
    }
}

/// Where the repo is, seen from the launcher binary. In development that is
/// two levels up from `src-tauri`; installed, it is wherever the person put
/// the port, which is why `port_path` is a preference.
fn repo_root() -> PathBuf {
    if let Ok(dir) = std::env::var("MELEE_REPO") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    let mut dir = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    loop {
        if dir.join("native").join("CMakeLists.txt").exists() {
            return dir;
        }
        match dir.parent() {
            Some(parent) => dir = parent.to_path_buf(),
            None => break,
        }
    }
    std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
}

struct AppState {
    run: launch::Shared,
}

#[derive(Serialize)]
struct Snapshot {
    config_path: String,
    config_text: String,
    values: BTreeMap<String, Value>,
    port_path: String,
    port_found: bool,
    disc_path: String,
    disc_found: bool,
    mods_dir: String,
    mods: Vec<mods::ModInfo>,
    profile: String,
    crashes: usize,
}

fn resolve_port(prefs: &Prefs, root: &Path) -> PathBuf {
    if let Some(p) = prefs.port_path.as_ref().filter(|p| !p.is_empty()) {
        return PathBuf::from(p);
    }
    launch::port_candidates(root)
        .into_iter()
        .find(|p| p.exists())
        .unwrap_or_else(|| root.join("build/native/melee"))
}

#[tauri::command]
fn snapshot() -> Snapshot {
    let root = repo_root();
    let prefs = load_prefs();
    let path = ConfigFile::default_path();
    let cfg = ConfigFile::load(&path);
    let values = cfg.values();
    let disc = match values.get("disc") {
        Some(Value::Str(s)) => s.clone(),
        _ => String::new(),
    };
    let port = resolve_port(&prefs, &root);
    let mods_dir = mods::mods_dir(&root);
    Snapshot {
        config_path: path.to_string_lossy().to_string(),
        config_text: cfg.text(),
        disc_found: !disc.is_empty() && Path::new(&disc).is_file(),
        disc_path: disc,
        port_found: port.exists(),
        port_path: port.to_string_lossy().to_string(),
        mods: mods::scan(&mods_dir),
        mods_dir: mods_dir.to_string_lossy().to_string(),
        profile: prefs.profile.unwrap_or_else(|| "play".into()),
        crashes: launch::list_crashes().len(),
        values,
    }
}

#[tauri::command]
fn save_settings(values: BTreeMap<String, Value>, removed: Vec<String>) -> Result<(), String> {
    let path = ConfigFile::default_path();
    let mut cfg = ConfigFile::load(&path);
    for (key, value) in &values {
        // Refuse anything the port could not read back. Writing a key it will
        // silently ignore is worse than refusing it here, where there is a
        // person to tell.
        if config::env_name(key).is_none() {
            return Err(format!("\"{key}\" is not a settable key"));
        }
        cfg.set(key, value);
    }
    for key in &removed {
        cfg.remove(key);
    }
    cfg.save().map_err(|e| format!("cannot write {}: {e}", path.display()))
}

#[tauri::command]
fn set_port_path(path: String) {
    let mut prefs = load_prefs();
    prefs.port_path = if path.is_empty() { None } else { Some(path) };
    save_prefs(&prefs);
}

#[tauri::command]
fn set_profile(profile: String) {
    let mut prefs = load_prefs();
    prefs.profile = Some(profile);
    save_prefs(&prefs);
}

#[tauri::command]
fn env_for(key: String) -> Option<String> {
    config::env_name(&key)
}

/// The profiles, as environment overrides on top of the file.
///
/// They are overrides rather than saved settings so that "record a clip" does
/// not quietly rewrite the settings you play with -- picking Record and then
/// picking Play again has to leave the file exactly as it found it.
fn profile_env(profile: &str) -> Vec<(String, String)> {
    match profile {
        "record" => vec![
            ("MELEE_CINEMATIC".into(), "1".into()),
            ("MELEE_MATCH_CPU".into(), "9".into()),
            ("MELEE_NO_CARD".into(), "1".into()),
        ],
        "debug" => vec![
            ("MELEE_VIEWER_TRIAGE".into(), "1".into()),
            ("MELEE_CONFIG_TRACE".into(), "1".into()),
        ],
        _ => Vec::new(),
    }
}

fn profile_args(profile: &str) -> Vec<String> {
    match profile {
        "record" => vec!["--match".into(), "--no-items".into()],
        _ => Vec::new(),
    }
}

#[tauri::command]
fn start(
    app: tauri::AppHandle,
    state: State<'_, AppState>,
    profile: String,
    seed: Option<String>,
) -> Result<(), String> {
    let root = repo_root();
    let prefs = load_prefs();
    let mut env = profile_env(&profile);
    if let Some(seed) = seed.filter(|s| !s.is_empty()) {
        env.push(("MELEE_RNG_SEED".into(), seed));
    }
    launch::launch(
        app,
        state.run.clone(),
        launch::LaunchSpec {
            port: resolve_port(&prefs, &root),
            config: ConfigFile::default_path(),
            args: profile_args(&profile),
            profile,
            env,
            cwd: Some(root),
        },
    )
}

#[tauri::command]
fn stop(state: State<'_, AppState>) {
    launch::stop(&state.run);
}

#[tauri::command]
fn crashes() -> Vec<launch::CrashReport> {
    launch::list_crashes()
}

#[tauri::command]
fn forget_crash(id: String) {
    launch::delete_crash(&id);
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            app.manage(AppState { run: Arc::new(Mutex::new(launch::RunState::default())) });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            snapshot,
            save_settings,
            set_port_path,
            set_profile,
            env_for,
            start,
            stop,
            crashes,
            forget_crash
        ])
        .run(tauri::generate_context!())
        .expect("launcher failed to start");
}
