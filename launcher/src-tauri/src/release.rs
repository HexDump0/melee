//! Fetching the port from GitHub releases.
//!
//! The naming scheme is a contract between whatever builds a release and this
//! code, so it is written down in `native/AI/reference/releases.md` rather than
//! only here. The short version:
//!
//! ```text
//! melee-<os>-<arch>[.exe]      e.g. melee-linux-x86_64, melee-windows-x86_64.exe
//! ```
//!
//! **Raw binaries, not archives.** The port is one executable and takes its
//! assets from the player's own disc image, so there is nothing to bundle
//! alongside it -- and an archive would mean carrying `tar`, `flate2` and
//! `zip` into the launcher to unpack something with one file in it. If a
//! release ever needs more than the binary, that decision changes and the
//! reference doc is where it changes.

use serde::{Deserialize, Serialize};
use std::io::Read;
use std::path::PathBuf;

const REPO: &str = "HexDump0/melee";
const USER_AGENT: &str = "melee-unbound-launcher";
/// A port binary is a few megabytes; this is a sanity bound, not a target.
const MAX_BYTES: u64 = 256 * 1024 * 1024;

#[derive(Debug, Clone, Serialize)]
pub struct ReleaseInfo {
    pub tag: String,
    pub asset: String,
    pub url: String,
    pub size: u64,
}

#[derive(Deserialize)]
struct GhAsset {
    name: String,
    browser_download_url: String,
    #[serde(default)]
    size: u64,
}

#[derive(Deserialize)]
struct GhRelease {
    tag_name: String,
    #[serde(default)]
    assets: Vec<GhAsset>,
}

/// The asset this machine wants. Kept as one function so the scheme has one
/// definition on this side.
pub fn asset_name() -> String {
    let os = if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else {
        "linux"
    };
    let arch = if cfg!(target_arch = "aarch64") {
        "aarch64"
    } else {
        "x86_64"
    };
    let ext = if cfg!(target_os = "windows") { ".exe" } else { "" };
    format!("melee-{os}-{arch}{ext}")
}

/// Where a downloaded port lives. Not next to the launcher: an installed
/// launcher may sit somewhere the user cannot write.
pub fn install_dir() -> PathBuf {
    dirs::data_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("melee-unbound")
        .join("bin")
}

fn get(url: &str) -> Result<ureq::Response, String> {
    ureq::get(url)
        .set("User-Agent", USER_AGENT)
        .set("Accept", "application/vnd.github+json")
        .call()
        .map_err(|e| match e {
            ureq::Error::Status(404, _) => {
                "no release found -- the repository may not have published one yet"
                    .to_string()
            }
            ureq::Error::Status(code, _) => format!("GitHub returned {code}"),
            other => format!("{other}"),
        })
}

pub fn latest() -> Result<ReleaseInfo, String> {
    let url = format!("https://api.github.com/repos/{REPO}/releases/latest");
    let release: GhRelease = get(&url)?
        .into_json()
        .map_err(|e| format!("cannot read the release list: {e}"))?;

    let want = asset_name();
    let asset = release
        .assets
        .iter()
        .find(|a| a.name == want)
        .ok_or_else(|| {
            // Name what was there. "No build for your platform" with no list
            // is the kind of message that costs someone an afternoon.
            let have: Vec<&str> = release.assets.iter().map(|a| a.name.as_str()).collect();
            if have.is_empty() {
                format!("release {} has no assets", release.tag_name)
            } else {
                format!(
                    "release {} has no {want}; it has: {}",
                    release.tag_name,
                    have.join(", ")
                )
            }
        })?;

    Ok(ReleaseInfo {
        tag: release.tag_name,
        asset: asset.name.clone(),
        url: asset.browser_download_url.clone(),
        size: asset.size,
    })
}

pub fn download(info: &ReleaseInfo) -> Result<PathBuf, String> {
    let dir = install_dir();
    std::fs::create_dir_all(&dir).map_err(|e| format!("cannot create {}: {e}", dir.display()))?;

    let response = get(&info.url)?;
    let mut bytes = Vec::new();
    response
        .into_reader()
        .take(MAX_BYTES)
        .read_to_end(&mut bytes)
        .map_err(|e| format!("download failed: {e}"))?;
    if bytes.is_empty() {
        return Err("download was empty".into());
    }

    // Write beside the target and rename, so an interrupted download cannot
    // leave a half-written binary where a working one used to be.
    let final_path = dir.join(&info.asset);
    let temp_path = dir.join(format!("{}.part", info.asset));
    std::fs::write(&temp_path, &bytes).map_err(|e| format!("cannot write: {e}"))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o755))
            .map_err(|e| format!("cannot mark executable: {e}"))?;
    }
    std::fs::rename(&temp_path, &final_path).map_err(|e| format!("cannot install: {e}"))?;
    Ok(final_path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn asset_name_follows_the_scheme() {
        let name = asset_name();
        assert!(name.starts_with("melee-"));
        assert_eq!(cfg!(target_os = "windows"), name.ends_with(".exe"));
        // The scheme is melee-<os>-<arch>, so exactly two separators.
        let stem = name.trim_end_matches(".exe");
        assert_eq!(stem.split('-').count(), 3, "{name} is not melee-<os>-<arch>");
    }
}
