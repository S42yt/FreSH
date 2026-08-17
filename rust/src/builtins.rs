/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

use std::io::Read;
use std::path::Path;

use crate::exec::{Io, Shell, Sink, Value};
use crate::expand::pattern_match;

impl Shell {
    /// `None` means the name is not a builtin and belongs to a real process.
    pub fn builtin(&mut self, words: &[String], io: &mut Io) -> Option<i32> {
        let name = words[0].as_str();
        let args = &words[1..];

        let status = match name {
            "echo" => self.builtin_echo(args, io),
            "printf" => self.builtin_printf(args, io),
            "cat" => self.builtin_cat(args, io),
            "true" | ":" => 0,
            "false" => 1,
            "pwd" => {
                let here = self.var_string("PWD");
                io.say(&format!("{}\n", here));
                0
            }
            "cd" => match args.first() {
                Some(path) => match std::env::set_current_dir(path) {
                    Ok(()) => 0,
                    Err(error) => {
                        io.write_err(format!("cd: {}: {}\n", path, error).as_bytes());
                        1
                    }
                },
                None => 0,
            },
            "exit" => {
                let code = args.first().and_then(|a| a.parse().ok()).unwrap_or(self.status);
                self.exiting = Some(code);
                code
            }
            "return" => {
                let code = args.first().and_then(|a| a.parse().ok()).unwrap_or(self.status);
                self.returning = true;
                code
            }
            "break" => {
                self.breaking = args.first().and_then(|a| a.parse().ok()).unwrap_or(1);
                0
            }
            "continue" => {
                self.continuing = args.first().and_then(|a| a.parse().ok()).unwrap_or(1);
                0
            }
            "local" => {
                for argument in args {
                    match argument.find('=') {
                        Some(at) => {
                            let value = self.expand_to_string(&argument[at + 1..]);
                            self.declare_local(&argument[..at], Value::Str(value));
                        }
                        None => self.declare_local(argument, Value::Str(String::new())),
                    }
                }
                0
            }
            "export" => {
                for argument in args {
                    match argument.find('=') {
                        Some(at) => {
                            let value = self.expand_to_string(&argument[at + 1..]);
                            std::env::set_var(&argument[..at], &value);
                            self.set(&argument[..at], Value::Str(value));
                        }
                        None => {
                            let value = self.var_string(argument);
                            std::env::set_var(argument, value);
                        }
                    }
                }
                0
            }
            "unset" => {
                for argument in args {
                    for scope in self.scopes.iter_mut() {
                        scope.remove(argument);
                    }
                    std::env::remove_var(argument);
                }
                0
            }
            "shift" => {
                let count: usize = args.first().and_then(|a| a.parse().ok()).unwrap_or(1);
                if count <= self.params.len() {
                    self.params.drain(..count);
                    0
                } else {
                    1
                }
            }
            "test" | "[" | "[[" => {
                let mut operands: Vec<String> = args.to_vec();
                if name != "test" {
                    let closing = if name == "[[" { "]]" } else { "]" };
                    if operands.last().map(|w| w.as_str()) == Some(closing) {
                        operands.pop();
                    }
                }
                let patterns = name == "[[";
                if self.condition(&operands, patterns) {
                    0
                } else {
                    1
                }
            }
            "command" => {
                if args.first().map(|a| a.as_str()) == Some("-v") {
                    match args.get(1) {
                        Some(wanted) => match self.locate(wanted) {
                            Some(found) => {
                                io.say(&format!("{}\n", found));
                                0
                            }
                            None => 1,
                        },
                        None => 1,
                    }
                } else if args.is_empty() {
                    1
                } else {
                    return Some(self.run_words(&args.to_vec(), io));
                }
            }
            "eval" => {
                let source = args.join(" ");
                self.run_source(&source, io)
            }
            "source" | "." => match args.first() {
                Some(path) => match std::fs::read_to_string(path) {
                    Ok(text) => self.run_source(&text, io),
                    Err(error) => {
                        io.write_err(format!("source: {}: {}\n", path, error).as_bytes());
                        1
                    }
                },
                None => 1,
            },
            "read" => self.builtin_read(args, io),
            "mkdir" => {
                let mut status = 0;
                for path in args.iter().filter(|a| !a.starts_with('-')) {
                    let made = if args.iter().any(|a| a == "-p") {
                        std::fs::create_dir_all(path)
                    } else {
                        std::fs::create_dir(path)
                    };
                    if made.is_err() && !args.iter().any(|a| a == "-p") {
                        io.write_err(format!("mkdir: {}: cannot create\n", path).as_bytes());
                        status = 1;
                    }
                }
                status
            }
            "rm" => {
                let recursive = args.iter().any(|a| a.starts_with('-') && a.contains('r'));
                let quiet = args.iter().any(|a| a.starts_with('-') && a.contains('f'));
                let mut status = 0;
                for path in args.iter().filter(|a| !a.starts_with('-')) {
                    let gone = if Path::new(path).is_dir() && recursive {
                        std::fs::remove_dir_all(path)
                    } else {
                        std::fs::remove_file(path)
                    };
                    if gone.is_err() && !quiet {
                        io.write_err(format!("rm: {}: cannot remove\n", path).as_bytes());
                        status = 1;
                    }
                }
                status
            }
            "set" | "shopt" | "unalias" | "hash" => 0,
            _ => return None,
        };

        Some(status)
    }

    fn run_words(&mut self, words: &[String], io: &mut Io) -> i32 {
        match self.builtin(words, io) {
            Some(status) => status,
            None => {
                let quoted: Vec<String> = words.iter().map(|w| format!("'{}'", w)).collect();
                self.run_source(&quoted.join(" "), io)
            }
        }
    }

    fn builtin_echo(&mut self, args: &[String], io: &mut Io) -> i32 {
        let mut newline = true;
        let mut escapes = false;
        let mut at = 0;

        while at < args.len() {
            match args[at].as_str() {
                "-n" => newline = false,
                "-e" => escapes = true,
                "-E" => escapes = false,
                _ => break,
            }
            at += 1;
        }

        let joined = args[at..].join(" ");
        let text = if escapes { unescape(&joined) } else { joined };
        io.say(&text);
        if newline {
            io.say("\n");
        }
        0
    }

    fn builtin_printf(&mut self, args: &[String], io: &mut Io) -> i32 {
        if args.is_empty() {
            return 1;
        }
        let format = unescape(&args[0]);
        let mut rest = args[1..].to_vec();

        let mut out = String::new();
        loop {
            let used = format_once(&format, &rest, &mut out);
            if used == 0 || used >= rest.len() {
                break;
            }
            rest.drain(..used);
        }
        io.say(&out);
        0
    }

    fn builtin_cat(&mut self, args: &[String], io: &mut Io) -> i32 {
        let files: Vec<&String> = args.iter().filter(|a| !a.starts_with('-')).collect();

        if files.is_empty() {
            let bytes = match io.input.take() {
                Some(bytes) => bytes,
                None => {
                    let mut bytes = Vec::new();
                    let _ = std::io::stdin().read_to_end(&mut bytes);
                    bytes
                }
            };
            io.write(&bytes);
            return 0;
        }

        let mut status = 0;
        for path in files {
            match std::fs::read(path.as_str()) {
                Ok(bytes) => io.write(&bytes),
                Err(error) => {
                    io.write_err(format!("cat: {}: {}\n", path, error).as_bytes());
                    status = 1;
                }
            }
        }
        status
    }

    fn builtin_read(&mut self, args: &[String], io: &mut Io) -> i32 {
        let names: Vec<&String> = args.iter().filter(|a| !a.starts_with('-')).collect();
        let line = match io.input.take() {
            Some(bytes) => {
                let text = String::from_utf8_lossy(&bytes).to_string();
                let (first, rest) = match text.find('\n') {
                    Some(at) => (text[..at].to_string(), text[at + 1..].to_string()),
                    None => (text.clone(), String::new()),
                };
                if !rest.is_empty() {
                    io.input = Some(rest.into_bytes());
                }
                first
            }
            None => {
                let mut text = String::new();
                if std::io::stdin().read_line(&mut text).unwrap_or(0) == 0 {
                    return 1;
                }
                text.trim_end_matches(['\n', '\r']).to_string()
            }
        };

        match names.first() {
            Some(name) => self.set(name, Value::Str(line)),
            None => self.set("REPLY", Value::Str(line)),
        }
        0
    }

    /// A name that can be run: a builtin, a function, or something on PATH.
    pub fn locate(&mut self, wanted: &str) -> Option<String> {
        const BUILTINS: [&str; 22] = [
            "echo", "printf", "cat", "true", "false", "pwd", "cd", "exit", "return", "break",
            "continue", "local", "export", "unset", "shift", "test", "[", "command", "eval",
            "source", "read", "mkdir",
        ];
        if BUILTINS.contains(&wanted) || self.funcs.contains_key(wanted) {
            return Some(wanted.to_string());
        }
        if wanted.contains('/') || wanted.contains('\\') {
            return Path::new(wanted).exists().then(|| wanted.to_string());
        }

        let path = std::env::var("PATH").unwrap_or_default();
        let extensions = std::env::var("PATHEXT").unwrap_or_else(|_| ".EXE".to_string());

        for directory in path.split(';') {
            if directory.is_empty() {
                continue;
            }
            let plain = Path::new(directory).join(wanted);
            if plain.is_file() {
                return Some(plain.to_string_lossy().replace('\\', "/"));
            }
            for extension in extensions.split(';') {
                if extension.is_empty() {
                    continue;
                }
                let candidate = Path::new(directory).join(format!("{}{}", wanted, extension));
                if candidate.is_file() {
                    return Some(candidate.to_string_lossy().replace('\\', "/"));
                }
            }
        }
        None
    }

    /// `test`, `[ ]` and `[[ ]]`, with && and || read left to right.
    pub fn condition(&mut self, words: &[String], patterns: bool) -> bool {
        if words.is_empty() {
            return false;
        }

        for (at, word) in words.iter().enumerate() {
            if word == "&&" || word == "-a" {
                return self.condition(&words[..at], patterns)
                    && self.condition(&words[at + 1..], patterns);
            }
        }
        for (at, word) in words.iter().enumerate() {
            if word == "||" || word == "-o" {
                return self.condition(&words[..at], patterns)
                    || self.condition(&words[at + 1..], patterns);
            }
        }

        if words[0] == "!" {
            return !self.condition(&words[1..], patterns);
        }

        if words.len() == 1 {
            return !words[0].is_empty();
        }

        if words.len() == 2 {
            let operand = &words[1];
            return match words[0].as_str() {
                "-z" => operand.is_empty(),
                "-n" => !operand.is_empty(),
                "-f" => Path::new(operand).is_file(),
                "-d" => Path::new(operand).is_dir(),
                "-e" | "-r" | "-s" | "-x" => Path::new(operand).exists(),
                _ => !operand.is_empty(),
            };
        }

        let left = &words[0];
        let operator = words[1].as_str();
        let right = &words[2];

        match operator {
            "=" | "==" => {
                if patterns {
                    pattern_match(right, left)
                } else {
                    left == right
                }
            }
            "!=" => {
                if patterns {
                    !pattern_match(right, left)
                } else {
                    left != right
                }
            }
            "<" => left < right,
            ">" => left > right,
            "-eq" => number(left) == number(right),
            "-ne" => number(left) != number(right),
            "-lt" => number(left) < number(right),
            "-le" => number(left) <= number(right),
            "-gt" => number(left) > number(right),
            "-ge" => number(left) >= number(right),
            _ => false,
        }
    }
}

fn number(text: &str) -> i64 {
    text.trim().parse().unwrap_or(0)
}

fn unescape(text: &str) -> String {
    let chars: Vec<char> = text.chars().collect();
    let mut out = String::new();
    let mut index = 0;

    while index < chars.len() {
        if chars[index] != '\\' || index + 1 >= chars.len() {
            out.push(chars[index]);
            index += 1;
            continue;
        }
        let next = chars[index + 1];
        index += 2;
        match next {
            'n' => out.push('\n'),
            't' => out.push('\t'),
            'r' => out.push('\r'),
            'e' => out.push('\x1b'),
            '0' => out.push('\0'),
            '\\' => out.push('\\'),
            other => {
                out.push('\\');
                out.push(other);
            }
        }
    }
    out
}

/// One pass over the format, consuming as many arguments as it has conversions.
fn format_once(format: &str, args: &[String], out: &mut String) -> usize {
    let chars: Vec<char> = format.chars().collect();
    let mut used = 0;
    let mut index = 0;

    while index < chars.len() {
        if chars[index] != '%' {
            out.push(chars[index]);
            index += 1;
            continue;
        }
        if index + 1 < chars.len() && chars[index + 1] == '%' {
            out.push('%');
            index += 2;
            continue;
        }

        let mut spec = String::new();
        index += 1;
        while index < chars.len() && !"sdifxXgeu".contains(chars[index]) {
            spec.push(chars[index]);
            index += 1;
        }
        if index >= chars.len() {
            break;
        }
        let conversion = chars[index];
        index += 1;

        let argument = args.get(used).cloned().unwrap_or_default();
        used += 1;

        let left = spec.contains('-');
        let zero = spec.trim_start_matches('-').starts_with('0');
        let cleaned: String = spec.chars().filter(|c| c.is_ascii_digit() || *c == '.').collect();
        let (width_text, precision_text) = match cleaned.find('.') {
            Some(at) => (cleaned[..at].to_string(), Some(cleaned[at + 1..].to_string())),
            None => (cleaned.clone(), None),
        };
        let width: usize = width_text.trim_start_matches('0').parse().unwrap_or(0);
        let precision: usize = precision_text
            .as_deref()
            .and_then(|p| p.parse().ok())
            .unwrap_or(6);

        let mut body = match conversion {
            's' => argument,
            'd' | 'i' | 'u' => argument.trim().parse::<i64>().unwrap_or(0).to_string(),
            'x' => format!("{:x}", argument.trim().parse::<i64>().unwrap_or(0)),
            'X' => format!("{:X}", argument.trim().parse::<i64>().unwrap_or(0)),
            'f' | 'g' | 'e' => {
                let value = argument.trim().parse::<f64>().unwrap_or(0.0);
                format!("{:.*}", precision, value)
            }
            _ => argument,
        };

        if body.chars().count() < width {
            let padding = width - body.chars().count();
            if left {
                body.push_str(&" ".repeat(padding));
            } else if zero && conversion != 's' {
                body = format!("{}{}", "0".repeat(padding), body);
            } else {
                body = format!("{}{}", " ".repeat(padding), body);
            }
        }
        out.push_str(&body);
    }

    used
}

/// Kept next to the builtins because only they capture output like this.
pub fn taken(sink: Sink) -> Vec<u8> {
    match sink {
        Sink::Capture(bytes) => bytes,
        _ => Vec::new(),
    }
}
