/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

mod arith;
mod builtins;
mod exec;
mod expand;
mod lex;
mod parse;

use exec::{Io, Shell};

const VERSION: &str = "0.1.0-experiment";

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    let mut source: Option<String> = None;
    let mut script: Option<String> = None;
    let mut params: Vec<String> = Vec::new();
    let mut at = 1;

    while at < arguments.len() {
        match arguments[at].as_str() {
            "-c" => {
                at += 1;
                source = arguments.get(at).cloned();
                at += 1;
                params = arguments[at.min(arguments.len())..].to_vec();
                break;
            }
            "--version" | "-v" => {
                println!("fresh-rs {}", VERSION);
                return;
            }
            "--norc" | "--noprofile" | "-i" => at += 1,
            other => {
                script = Some(other.to_string());
                at += 1;
                params = arguments[at.min(arguments.len())..].to_vec();
                break;
            }
        }
    }

    let text = match (&source, &script) {
        (Some(text), _) => text.clone(),
        (None, Some(path)) => match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(error) => {
                eprintln!("fresh-rs: {}: {}", path, error);
                std::process::exit(127);
            }
        },
        (None, None) => {
            eprintln!("fresh-rs: this core runs -c or a script, it has no prompt");
            std::process::exit(2);
        }
    };

    let name = script.clone().unwrap_or_else(|| "fresh-rs".to_string());
    let mut shell = Shell::new(&name);
    shell.params = params;

    let mut io = Io::inherit();
    let status = shell.run_source(text.trim_start_matches('\u{feff}'), &mut io);

    let code = shell.exiting.unwrap_or(if shell.fatal { 1 } else { status });
    std::process::exit(code);
}
