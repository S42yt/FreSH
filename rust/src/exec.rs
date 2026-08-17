/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::process::{Command, Stdio};
use std::time::Instant;

use crate::expand::pattern_match;
use crate::lex::lex;
use crate::parse::{parse, Assign, CaseArm, Node, Redir, Simple};

#[derive(Debug, Clone)]
pub enum Value {
    Str(String),
    Arr(Vec<String>),
}

pub enum Sink {
    Inherit,
    Capture(Vec<u8>),
    Handle(File),
    Discard,
}

pub enum ErrSink {
    Inherit,
    ToOut,
    Handle(File),
    Discard,
}

pub struct Io {
    pub input: Option<Vec<u8>>,
    pub out: Sink,
    pub err: ErrSink,
}

impl Io {
    pub fn inherit() -> Io {
        Io {
            input: None,
            out: Sink::Inherit,
            err: ErrSink::Inherit,
        }
    }

    pub fn capturing() -> Io {
        Io {
            input: None,
            out: Sink::Capture(Vec::new()),
            err: ErrSink::Inherit,
        }
    }

    pub fn write(&mut self, bytes: &[u8]) {
        match &mut self.out {
            Sink::Inherit => {
                let stdout = std::io::stdout();
                let mut lock = stdout.lock();
                let _ = lock.write_all(bytes);
            }
            Sink::Capture(buffer) => buffer.extend_from_slice(bytes),
            Sink::Handle(file) => {
                let _ = file.write_all(bytes);
            }
            Sink::Discard => {}
        }
    }

    pub fn write_err(&mut self, bytes: &[u8]) {
        match &mut self.err {
            ErrSink::Inherit => {
                let _ = std::io::stderr().write_all(bytes);
            }
            ErrSink::ToOut => self.write(bytes),
            ErrSink::Handle(file) => {
                let _ = file.write_all(bytes);
            }
            ErrSink::Discard => {}
        }
    }

    pub fn say(&mut self, text: &str) {
        self.write(text.as_bytes());
    }
}

pub struct Shell {
    pub scopes: Vec<HashMap<String, Value>>,
    pub funcs: HashMap<String, Vec<Node>>,
    pub params: Vec<String>,
    pub name: String,
    pub status: i32,
    pub fatal: bool,
    pub started: Instant,
    pub exiting: Option<i32>,
    pub breaking: u32,
    pub continuing: u32,
    pub returning: bool,
}

impl Shell {
    pub fn new(name: &str) -> Shell {
        Shell {
            scopes: vec![HashMap::new()],
            funcs: HashMap::new(),
            params: Vec::new(),
            name: name.to_string(),
            status: 0,
            fatal: false,
            started: Instant::now(),
            exiting: None,
            breaking: 0,
            continuing: 0,
            returning: false,
        }
    }

    pub fn run_source(&mut self, source: &str, io: &mut Io) -> i32 {
        let nodes = match parse(lex(source)) {
            Ok(nodes) => nodes,
            Err(message) => {
                io.write_err(format!("fresh-rs: {}\n", message).as_bytes());
                return 2;
            }
        };
        self.run_list(&nodes, io)
    }

    pub fn run_list(&mut self, nodes: &[Node], io: &mut Io) -> i32 {
        let mut status = self.status;
        for node in nodes {
            status = self.run(node, io);
            if self.stopping() {
                break;
            }
        }
        status
    }

    fn stopping(&self) -> bool {
        self.exiting.is_some()
            || self.fatal
            || self.returning
            || self.breaking > 0
            || self.continuing > 0
    }

    pub fn run(&mut self, node: &Node, io: &mut Io) -> i32 {
        match node {
            Node::Cmd(cmd) => self.run_simple(cmd, io),
            Node::Seq(list) | Node::Group(list) => self.run_list(list, io),
            Node::Subshell(list) => {
                let depth = self.scopes.len();
                self.scopes.push(HashMap::new());
                let status = self.run_list(list, io);
                self.scopes.truncate(depth);
                status
            }
            Node::Not(inner) => {
                let status = self.run(inner, io);
                self.status = if status == 0 { 1 } else { 0 };
                self.status
            }
            Node::And(left, right) => {
                let status = self.run(left, io);
                if status != 0 || self.stopping() {
                    return status;
                }
                self.run(right, io)
            }
            Node::Or(left, right) => {
                let status = self.run(left, io);
                if status == 0 || self.stopping() {
                    return status;
                }
                self.run(right, io)
            }
            Node::Pipe(stages) => self.run_pipe(stages, io),
            Node::Func { name, body } => {
                self.funcs.insert(name.clone(), body.clone());
                self.status = 0;
                0
            }
            Node::If {
                cond,
                then,
                otherwise,
            } => {
                let status = self.run_list(cond, io);
                if self.stopping() {
                    return status;
                }
                if status == 0 {
                    self.run_list(then, io)
                } else {
                    self.run_list(otherwise, io)
                }
            }
            Node::While { cond, body, until } => self.run_while(cond, body, *until, io),
            Node::For { name, items, body } => self.run_for(name, items, body, io),
            Node::Case { word, arms } => self.run_case(word, arms, io),
        }
    }

    fn run_while(&mut self, cond: &[Node], body: &[Node], until: bool, io: &mut Io) -> i32 {
        let mut status = 0;
        loop {
            let test = self.run_list(cond, io);
            if self.exiting.is_some() || self.fatal || self.returning {
                return status;
            }
            let go = if until { test != 0 } else { test == 0 };
            if !go {
                return status;
            }

            status = self.run_list(body, io);
            if self.take_loop_signal() {
                return status;
            }
            if self.exiting.is_some() || self.fatal || self.returning {
                return status;
            }
        }
    }

    fn run_for(&mut self, name: &str, items: &[String], body: &[Node], io: &mut Io) -> i32 {
        let mut expanded: Vec<String> = Vec::new();
        for item in items {
            expanded.extend(self.expand_word(item));
        }

        let mut status = 0;
        for value in expanded {
            self.set(name, Value::Str(value));
            status = self.run_list(body, io);
            if self.take_loop_signal() {
                break;
            }
            if self.exiting.is_some() || self.fatal || self.returning {
                break;
            }
        }
        status
    }

    fn take_loop_signal(&mut self) -> bool {
        if self.breaking > 0 {
            self.breaking -= 1;
            return true;
        }
        if self.continuing > 0 {
            self.continuing -= 1;
            return self.continuing > 0;
        }
        false
    }

    fn run_case(&mut self, word: &str, arms: &[CaseArm], io: &mut Io) -> i32 {
        let subject = self.expand_to_string(word);
        let mut running = false;

        for arm in arms {
            if !running {
                let mut hit = false;
                for pattern in &arm.patterns {
                    let text = self.expand_to_string(pattern);
                    if pattern_match(&text, &subject) {
                        hit = true;
                        break;
                    }
                }
                if !hit {
                    continue;
                }
            }
            let status = self.run_list(&arm.body, io);
            if arm.fallthrough && !self.stopping() {
                running = true;
                continue;
            }
            return status;
        }
        self.status = 0;
        0
    }

    fn run_pipe(&mut self, stages: &[Node], io: &mut Io) -> i32 {
        let mut carried: Option<Vec<u8>> = io.input.take();
        let mut status = 0;

        for (at, stage) in stages.iter().enumerate() {
            let last = at + 1 == stages.len();
            if last {
                let mut stage_io = Io {
                    input: carried.take(),
                    out: std::mem::replace(&mut io.out, Sink::Inherit),
                    err: std::mem::replace(&mut io.err, ErrSink::Inherit),
                };
                status = self.run(stage, &mut stage_io);
                io.out = stage_io.out;
                io.err = stage_io.err;
            } else {
                let mut stage_io = Io {
                    input: carried.take(),
                    out: Sink::Capture(Vec::new()),
                    err: ErrSink::Inherit,
                };
                status = self.run(stage, &mut stage_io);
                carried = match stage_io.out {
                    Sink::Capture(bytes) => Some(bytes),
                    _ => Some(Vec::new()),
                };
            }
            if self.exiting.is_some() || self.fatal {
                break;
            }
        }
        status
    }

    pub fn capture(&mut self, source: &str) -> String {
        let mut io = Io::capturing();
        self.run_source(source, &mut io);
        let bytes = match io.out {
            Sink::Capture(bytes) => bytes,
            _ => Vec::new(),
        };
        let mut text = String::from_utf8_lossy(&bytes).to_string();
        while text.ends_with('\n') || text.ends_with('\r') {
            text.pop();
        }
        text
    }

    fn run_simple(&mut self, cmd: &Simple, io: &mut Io) -> i32 {
        const KEEPS_WHOLE: [&str; 5] = ["local", "export", "declare", "readonly", "[["];

        let mut words: Vec<String> = Vec::new();
        for (at, word) in cmd.words.iter().enumerate() {
            if at > 0 && !words.is_empty() && KEEPS_WHOLE.contains(&words[0].as_str()) {
                words.push(self.expand_to_string(word));
                continue;
            }
            words.extend(self.expand_word(word));
        }

        if words.is_empty() {
            for assign in &cmd.assigns {
                self.assign(assign);
            }
            self.status = 0;
            return 0;
        }

        let mut local_io = match self.apply_redirs(cmd, io) {
            Ok(io) => io,
            Err(message) => {
                io.write_err(format!("fresh-rs: {}\n", message).as_bytes());
                self.status = 1;
                return 1;
            }
        };

        for assign in &cmd.assigns {
            self.assign(assign);
        }

        let target = local_io.as_mut().unwrap_or(io);
        let status = self.dispatch(&words, target);

        if let Some(inner) = local_io {
            if let Sink::Capture(bytes) = inner.out {
                io.write(&bytes);
            }
        }

        self.status = status;
        status
    }

    fn apply_redirs(&mut self, cmd: &Simple, io: &mut Io) -> Result<Option<Io>, String> {
        if cmd.redirs.is_empty() {
            return Ok(None);
        }

        let mut fresh = Io {
            input: io.input.take(),
            out: Sink::Inherit,
            err: ErrSink::Inherit,
        };
        let outer_is_capture = matches!(io.out, Sink::Capture(_));
        if outer_is_capture {
            fresh.out = Sink::Capture(Vec::new());
        }

        for redir in &cmd.redirs {
            match redir {
                Redir::Out(path) | Redir::Append(path) => {
                    let target = self.expand_to_string(path);
                    if is_null(&target) {
                        fresh.out = Sink::Discard;
                        continue;
                    }
                    let append = matches!(redir, Redir::Append(_));
                    fresh.out = Sink::Handle(open_for_write(&target, append)?);
                }
                Redir::ErrOut(path) | Redir::ErrAppend(path) => {
                    let target = self.expand_to_string(path);
                    if is_null(&target) {
                        fresh.err = ErrSink::Discard;
                        continue;
                    }
                    let append = matches!(redir, Redir::ErrAppend(_));
                    fresh.err = ErrSink::Handle(open_for_write(&target, append)?);
                }
                Redir::ErrToOut => fresh.err = ErrSink::ToOut,
                Redir::In(path) => {
                    let target = self.expand_to_string(path);
                    let mut bytes = Vec::new();
                    File::open(&target)
                        .map_err(|e| format!("{}: {}", target, e))?
                        .read_to_end(&mut bytes)
                        .map_err(|e| format!("{}: {}", target, e))?;
                    fresh.input = Some(bytes);
                }
            }
        }
        Ok(Some(fresh))
    }

    fn dispatch(&mut self, words: &[String], io: &mut Io) -> i32 {
        let name = words[0].as_str();

        if let Some(body) = self.funcs.get(name).cloned() {
            let saved = std::mem::replace(&mut self.params, words[1..].to_vec());
            self.scopes.push(HashMap::new());
            let status = self.run_list(&body, io);
            self.scopes.pop();
            self.params = saved;
            self.returning = false;
            return status;
        }

        match self.builtin(words, io) {
            Some(status) => status,
            None => self.spawn(words, io),
        }
    }

    fn spawn(&mut self, words: &[String], io: &mut Io) -> i32 {
        let mut command = Command::new(&words[0]);
        command.args(&words[1..]);

        command.stdin(match &io.input {
            Some(_) => Stdio::piped(),
            None => Stdio::inherit(),
        });
        let capture_out = !matches!(io.out, Sink::Inherit);
        command.stdout(match &io.out {
            Sink::Inherit => Stdio::inherit(),
            Sink::Discard => Stdio::null(),
            _ => Stdio::piped(),
        });
        command.stderr(match &io.err {
            ErrSink::Inherit => Stdio::inherit(),
            ErrSink::Discard => Stdio::null(),
            _ => Stdio::piped(),
        });

        let mut child = match command.spawn() {
            Ok(child) => child,
            Err(_) => {
                io.write_err(format!("fresh-rs: {}: command not found\n", words[0]).as_bytes());
                return 127;
            }
        };

        if let (Some(bytes), Some(stdin)) = (io.input.take(), child.stdin.take()) {
            let mut stdin = stdin;
            let _ = stdin.write_all(&bytes);
        }

        let output = match child.wait_with_output() {
            Ok(output) => output,
            Err(_) => return 1,
        };

        if capture_out && !output.stdout.is_empty() {
            io.write(&output.stdout);
        }
        if !output.stderr.is_empty() {
            io.write_err(&output.stderr);
        }
        output.status.code().unwrap_or(1)
    }

    pub fn set(&mut self, name: &str, value: Value) {
        for scope in self.scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), value);
                return;
            }
        }
        self.scopes[0].insert(name.to_string(), value);
    }

    fn assign(&mut self, assign: &Assign) {
        let Assign {
            name,
            values,
            append,
            array,
        } = assign;

        if *array {
            let mut items = Vec::new();
            for value in values {
                items.extend(self.expand_word(value));
            }
            self.set(name, Value::Arr(items));
            return;
        }

        let mut text = String::new();
        if *append {
            text.push_str(&self.var_string(name));
        }
        for value in values {
            let piece = self.expand_to_string(value);
            text.push_str(&piece);
        }
        self.set(name, Value::Str(text));
    }

    pub fn declare_local(&mut self, name: &str, value: Value) {
        let depth = self.scopes.len() - 1;
        self.scopes[depth].insert(name.to_string(), value);
    }
}

fn is_null(path: &str) -> bool {
    let lowered = path.to_ascii_lowercase();
    lowered == "/dev/null" || lowered == "nul"
}

fn open_for_write(path: &str, append: bool) -> Result<File, String> {
    let mut options = OpenOptions::new();
    options.write(true).create(true);
    if append {
        options.append(true);
    } else {
        options.truncate(true);
    }
    options.open(path).map_err(|e| format!("{}: {}", path, e))
}
