//! Running the port, and catching it when it falls over.
//!
//! The port already prints everything a bug report needs -- a backtrace, the
//! live fighters, and the seed that replays the run exactly. What it has never
//! had is somewhere for that to go: it scrolls past in a terminal and gets
//! pasted into a chat window by hand. This captures it.

use serde::{Deserialize, Serialize};
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter};

/// Kept small on purpose: enough to hold a panic with its backtrace and the
/// fighter dump above it, not a whole session's frame statistics.
const LOG_TAIL: usize = 400;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashReport {
    pub id: String,
    pub when: String,
    pub reason: String,
    pub exit: String,
    /// `MELEE_RNG_SEED=...` replays the run exactly; the port prints it at boot.
    pub seed: Option<String>,
    pub profile: String,
    pub log: Vec<String>,
    pub config: String,
}

#[derive(Default)]
pub struct RunState {
    pub child: Option<Child>,
}

pub type Shared = Arc<Mutex<RunState>>;

#[derive(Clone, Serialize)]
struct LineEvent {
    line: String,
}

#[derive(Clone, Serialize)]
struct ExitEvent {
    code: String,
    crashed: bool,
    crash_id: Option<String>,
}

fn crash_dir() -> PathBuf {
    dirs::data_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("melee-unbound")
        .join("crashes")
}

pub fn list_crashes() -> Vec<CrashReport> {
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir(crash_dir()) {
        for entry in entries.flatten() {
            if entry.path().extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            if let Ok(text) = std::fs::read_to_string(entry.path()) {
                if let Ok(report) = serde_json::from_str::<CrashReport>(&text) {
                    out.push(report);
                }
            }
        }
    }
    out.sort_by(|a, b| b.id.cmp(&a.id)); // newest first
    out
}

pub fn delete_crash(id: &str) {
    // Join the file name rather than the id, so an id from the UI cannot walk
    // out of the directory.
    let name = format!("{}.json", id.replace(['/', '\\', '.'], "_"));
    std::fs::remove_file(crash_dir().join(name)).ok();
}

fn save_crash(report: &CrashReport) {
    let dir = crash_dir();
    if std::fs::create_dir_all(&dir).is_err() {
        return;
    }
    let name = format!("{}.json", report.id.replace(['/', '\\', '.'], "_"));
    if let Ok(text) = serde_json::to_string_pretty(report) {
        std::fs::write(dir.join(name), text).ok();
    }
}

fn now_id() -> (String, String) {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    // A sortable id without pulling in a date crate: the epoch second is
    // already monotonic, and the human-readable form is only a label.
    (format!("{secs}"), format!("{secs}"))
}

/// What the port says when it dies. Recognising these is what turns "it
/// crashed" into a titled, searchable report.
fn crash_reason(log: &[String]) -> Option<String> {
    for line in log.iter().rev() {
        let t = line.trim();
        if t.starts_with("*** PANIC") {
            // The assertion text is the line after; the file/line is here.
            return Some(t.trim_start_matches("*** PANIC:").trim().to_string());
        }
        if t.contains("controlled stop: SIG") {
            return Some(t.split("controlled stop: ").nth(1)?.to_string());
        }
        if t.contains("assertion \"") && t.contains("\" failed") {
            return Some(t.to_string());
        }
    }
    None
}

fn seed_of(log: &[String]) -> Option<String> {
    for line in log {
        if let Some(at) = line.find("MELEE_RNG_SEED=") {
            let rest = &line[at + "MELEE_RNG_SEED=".len()..];
            let end = rest.find(|c: char| !c.is_ascii_alphanumeric() && c != 'x');
            return Some(rest[..end.unwrap_or(rest.len())].to_string());
        }
    }
    None
}

pub struct LaunchSpec {
    pub port: PathBuf,
    pub config: PathBuf,
    pub profile: String,
    pub env: Vec<(String, String)>,
    pub args: Vec<String>,
    pub cwd: Option<PathBuf>,
}

pub fn launch(app: AppHandle, shared: Shared, spec: LaunchSpec) -> Result<(), String> {
    {
        let mut state = shared.lock().map_err(|_| "launcher state is poisoned")?;
        if let Some(child) = state.child.as_mut() {
            if matches!(child.try_wait(), Ok(None)) {
                return Err("the game is already running".into());
            }
        }
    }
    if !spec.port.exists() {
        return Err(format!("no port binary at {}", spec.port.display()));
    }

    let mut cmd = Command::new(&spec.port);
    cmd.arg("--config").arg(&spec.config);
    for arg in &spec.args {
        cmd.arg(arg);
    }
    for (k, v) in &spec.env {
        cmd.env(k, v);
    }
    if let Some(dir) = &spec.cwd {
        cmd.current_dir(dir);
    }
    cmd.stdout(Stdio::piped()).stderr(Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("cannot start the port: {e}"))?;
    let stdout = child.stdout.take();
    let stderr = child.stderr.take();
    {
        let mut state = shared.lock().map_err(|_| "launcher state is poisoned")?;
        state.child = Some(child);
    }

    let log: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));

    // The port says almost everything on stderr, but a stream that fills its
    // pipe buffer blocks the process, so both are drained.
    let streams: Vec<Box<dyn std::io::Read + Send>> = [
        stdout.map(|s| Box::new(s) as Box<dyn std::io::Read + Send>),
        stderr.map(|s| Box::new(s) as Box<dyn std::io::Read + Send>),
    ]
    .into_iter()
    .flatten()
    .collect();
    for stream in streams {
        let app = app.clone();
        let log = log.clone();
        std::thread::spawn(move || {
            let reader = BufReader::new(stream);
            for line in reader.lines().map_while(Result::ok) {
                if let Ok(mut buf) = log.lock() {
                    buf.push(line.clone());
                    if buf.len() > LOG_TAIL {
                        let drop = buf.len() - LOG_TAIL;
                        buf.drain(0..drop);
                    }
                }
                app.emit("port:line", LineEvent { line }).ok();
            }
        });
    }

    let config_text = std::fs::read_to_string(&spec.config).unwrap_or_default();
    std::thread::spawn(move || {
        /*
         * Poll rather than `wait()`.
         *
         * The child lives in the shared state so `stop()` can kill it, and
         * holding that lock across a blocking `wait()` would mean `stop()`
         * could never be serviced -- the Stop button would hang until the
         * game exited on its own, which is the one moment it is useless.
         */
        let status = loop {
            {
                let mut state = match shared.lock() {
                    Ok(s) => s,
                    Err(_) => return,
                };
                match state.child.as_mut() {
                    Some(child) => match child.try_wait() {
                        Ok(Some(status)) => break Ok(status),
                        Ok(None) => {}
                        Err(e) => break Err(e),
                    },
                    None => return,
                }
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
        };
        // Give the reader threads a moment to flush the last lines, which are
        // the ones a crash report is made of.
        std::thread::sleep(std::time::Duration::from_millis(120));

        let lines = log.lock().map(|l| l.clone()).unwrap_or_default();
        let (code_text, ok) = match status {
            Ok(s) => (
                s.code().map(|c| format!("exit {c}")).unwrap_or_else(|| "signal".into()),
                s.success(),
            ),
            Err(e) => (format!("{e}"), false),
        };
        let reason = crash_reason(&lines);
        let crashed = !ok || reason.is_some();
        let mut crash_id = None;
        if crashed {
            let (id, when) = now_id();
            let report = CrashReport {
                id: id.clone(),
                when,
                reason: reason.unwrap_or_else(|| code_text.clone()),
                exit: code_text.clone(),
                seed: seed_of(&lines),
                profile: spec.profile.clone(),
                log: lines,
                config: config_text,
            };
            save_crash(&report);
            crash_id = Some(id);
        }
        if let Ok(mut state) = shared.lock() {
            state.child = None;
        }
        app.emit("port:exit", ExitEvent { code: code_text, crashed, crash_id }).ok();
    });

    Ok(())
}

pub fn stop(shared: &Shared) {
    if let Ok(mut state) = shared.lock() {
        if let Some(child) = state.child.as_mut() {
            child.kill().ok();
        }
    }
}

pub fn port_candidates(repo_root: &Path) -> Vec<PathBuf> {
    vec![
        repo_root.join("build/native/melee"),
        repo_root.join("melee"),
        PathBuf::from("/usr/local/bin/melee"),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_ports_own_crash_vocabulary() {
        let log: Vec<String> = vec![
            "[rng] seed=0x6adb28af (MELEE_RNG_SEED=0x6adb28af replays this run, tick=1)".into(),
            "[match] frame 30 draws=32".into(),
            "*** PANIC:  in \"/src/melee/ft/ftdata.c\" on line 1745.".into(),
        ];
        assert_eq!(seed_of(&log).unwrap(), "0x6adb28af");
        assert!(crash_reason(&log).unwrap().contains("ftdata.c"));

        let sig: Vec<String> =
            vec!["[boot] controlled stop: SIGSEGV at 0x4".into()];
        assert_eq!(crash_reason(&sig).unwrap(), "SIGSEGV at 0x4");

        let clean: Vec<String> = vec!["viewer: audio 32000 Hz stereo".into()];
        assert!(crash_reason(&clean).is_none());
    }
}
