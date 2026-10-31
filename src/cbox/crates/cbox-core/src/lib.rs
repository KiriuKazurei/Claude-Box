//! Platform-independent: spec parsing, image/instance store, state. C0 SKELETON.
use serde::Deserialize;

#[derive(Debug, Deserialize)]
pub struct Spec {
    pub name: String,
    pub image: String,            // unpacked app dir under <store>/images/<image>
    pub entrypoint: String,       // e.g. "Claude.exe"
    pub root: String,             // container root (profile/, logs/)
    #[serde(default)] pub mounts: Vec<Mount>,
    pub network: Network,
    #[serde(default)] pub env_spoof: Option<EnvSpoof>,
    #[serde(default = "default_isolation")] pub isolation: String, // "user" (default) | "appcontainer"
}
fn default_isolation() -> String { "user".into() }

#[derive(Debug, Deserialize)]
pub struct Mount { pub host: String, #[serde(default)] pub readonly: bool }

#[derive(Debug, Deserialize)]
pub struct Network { pub gateway: String, #[serde(default = "t")] pub enforce: bool }
fn t() -> bool { true }

#[derive(Debug, Deserialize)]
pub struct EnvSpoof { pub hostname: Option<String>, pub timezone: Option<String>, pub locale: Option<String> }

impl Spec {
    pub fn load(path: &str) -> anyhow::Result<Self> {
        let s: Spec = toml::from_str(&std::fs::read_to_string(path)?)?;
        for m in &s.mounts {
            let h = m.host.to_ascii_lowercase();
            if h == "c:\\" || h == "c:" || h.starts_with("c:\\users") {
                anyhow::bail!("mount {} rejected: host C:\\ / user profiles not allowed", m.host);
            }
        }
        Ok(s)
    }
}
