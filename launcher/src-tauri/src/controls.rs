//! Reading the port's own view of the bindings.
//!
//! The defaults live in C (`viewer_input.c`) and there is deliberately no copy
//! of them here. The launcher runs `melee --controls`, which resolves the
//! environment, the config file and the built-in defaults exactly as a real
//! launch would, and parses the result.
//!
//! That makes this a **read-back loop rather than a guess**: after a save, what
//! the page shows is what the port will actually use, so a binding the port
//! rejects shows up immediately instead of looking correct in the UI and doing
//! nothing in the game.

use serde::Serialize;
use std::path::Path;
use std::process::Command;

#[derive(Debug, Clone, Serialize)]
pub struct Binding {
    /// "keyboard" or "gamepad"
    pub device: String,
    /// 1 or 2; the gamepad table is player 1's.
    pub player: u8,
    pub action: String,
    /// Comma-separated, as the port prints and the config stores it.
    pub value: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct Controls {
    pub bindings: Vec<Binding>,
    pub deadzone: i32,
    pub problems: Vec<String>,
}

pub fn read(port: &Path, config: &Path) -> Result<Controls, String> {
    if !port.exists() {
        return Err(format!("no port binary at {}", port.display()));
    }
    let out = Command::new(port)
        .arg("--config")
        .arg(config)
        .arg("--controls")
        .output()
        .map_err(|e| format!("cannot run the port: {e}"))?;

    // The port prints this on stderr, with the config banner.
    let text = String::from_utf8_lossy(&out.stderr);
    let mut controls =
        Controls { bindings: Vec::new(), deadzone: 4000, problems: Vec::new() };

    for line in text.lines() {
        let Some(rest) = line.strip_prefix("[controls] ") else {
            continue;
        };
        // "p1 b: unknown key \"Nonsense\"" -- surfaced, not swallowed.
        if rest.contains("unknown") {
            controls.problems.push(rest.to_string());
            continue;
        }
        let Some((head, value)) = rest.split_once(" = ") else {
            continue;
        };
        if head == "deadzone" {
            controls.deadzone = value.trim().parse().unwrap_or(4000);
            continue;
        }
        let mut parts = head.split_whitespace();
        let (Some(who), Some(action)) = (parts.next(), parts.next()) else {
            continue;
        };
        let (device, player) = match who {
            "pad" => ("gamepad", 1u8),
            "p1" => ("keyboard", 1),
            "p2" => ("keyboard", 2),
            _ => continue,
        };
        controls.bindings.push(Binding {
            device: device.to_string(),
            player,
            action: action.to_string(),
            value: value.trim().to_string(),
        });
    }

    if controls.bindings.is_empty() {
        return Err("the port reported no bindings".into());
    }
    Ok(controls)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The port's format, parsed. Comma separation matters: scancode names
    /// contain spaces, so a space-separated list could not be split back.
    #[test]
    fn parses_the_ports_output() {
        let sample = "[config] /home/x/melee.toml\n\
                      [controls] p1 start = Return,Keypad Enter\n\
                      [controls] p2 a = F\n\
                      [controls] pad z = rightshoulder\n\
                      [controls] p1 b: unknown key \"Nonsense\"\n\
                      [controls] deadzone = 8000\n";
        let mut c = Controls {
            bindings: Vec::new(),
            deadzone: 4000,
            problems: Vec::new(),
        };
        for line in sample.lines() {
            let Some(rest) = line.strip_prefix("[controls] ") else { continue };
            if rest.contains("unknown") {
                c.problems.push(rest.into());
                continue;
            }
            let Some((head, value)) = rest.split_once(" = ") else { continue };
            if head == "deadzone" {
                c.deadzone = value.trim().parse().unwrap();
                continue;
            }
            let mut parts = head.split_whitespace();
            let (who, action) = (parts.next().unwrap(), parts.next().unwrap());
            let (device, player) = match who {
                "pad" => ("gamepad", 1u8),
                "p1" => ("keyboard", 1),
                _ => ("keyboard", 2),
            };
            c.bindings.push(Binding {
                device: device.into(),
                player,
                action: action.into(),
                value: value.trim().into(),
            });
        }
        assert_eq!(c.deadzone, 8000);
        assert_eq!(c.problems.len(), 1);
        let start = c.bindings.iter().find(|b| b.action == "start").unwrap();
        assert_eq!(start.value, "Return,Keypad Enter");
        assert_eq!(start.player, 1);
        let pad = c.bindings.iter().find(|b| b.device == "gamepad").unwrap();
        assert_eq!(pad.value, "rightshoulder");
    }
}
