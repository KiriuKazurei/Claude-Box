//! cbox — Docker-like CLI over Windows primitives. C0 SKELETON, NOT IMPLEMENTED.
use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "cbox")]
struct Cli { #[command(subcommand)] cmd: Cmd }

#[derive(Subcommand)]
enum Cmd {
    /// Import an unpacked Claude app dir as an "image"
    Import { src: String, name: String },
    /// Create + start an instance from a spec file (cbox.toml)
    Run { spec: String },
    Ps,
    Stop { id: String },
    Rm { id: String },
}

fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();
    match cli.cmd {
        Cmd::Run { spec } => {
            let spec = cbox_core::Spec::load(&spec)?;
            cbox_win::run(&spec) // TODO C0
        }
        _ => anyhow::bail!("not implemented (C0 skeleton)"),
    }
}
