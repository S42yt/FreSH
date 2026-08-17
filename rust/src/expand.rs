/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

use crate::exec::{Shell, Value};
use std::time::{SystemTime, UNIX_EPOCH};

/// One field being built, and whether a glob character reached it unquoted.
struct Field {
    text: String,
    globbable: bool,
}

impl Shell {
    /// Full expansion of one word: substitutions, splitting, then globbing.
    pub fn expand_word(&mut self, word: &str) -> Vec<String> {
        let fields = self.expand_fields(word, true);
        let mut out = Vec::new();

        for field in fields {
            if field.globbable {
                let matches = glob(&field.text);
                if !matches.is_empty() {
                    out.extend(matches);
                    continue;
                }
            }
            out.push(field.text);
        }
        out
    }

    /// Expansion without splitting or globbing, for conditions and assignments.
    pub fn expand_to_string(&mut self, word: &str) -> String {
        let fields = self.expand_fields(word, false);
        fields
            .into_iter()
            .map(|f| f.text)
            .collect::<Vec<String>>()
            .join(" ")
    }

    fn expand_fields(&mut self, word: &str, split: bool) -> Vec<Field> {
        let chars: Vec<char> = word.chars().collect();
        let mut fields: Vec<Field> = vec![Field {
            text: String::new(),
            globbable: false,
        }];
        let mut index = 0;

        while index < chars.len() {
            let c = chars[index];

            if c == '\'' {
                let (text, next) = take_until(&chars, index + 1, '\'');
                push_literal(&mut fields, &text);
                index = next;
                continue;
            }

            if c == '"' {
                let (inner, next) = take_until(&chars, index + 1, '"');

                /* "$@" is the one quoted thing that still becomes one field per parameter. */
                if inner.trim() == "$@" || inner.trim() == "$*" {
                    let params = self.params.clone();
                    for (n, param) in params.iter().enumerate() {
                        if n > 0 {
                            fields.push(Field {
                                text: String::new(),
                                globbable: false,
                            });
                        }
                        push_literal(&mut fields, param);
                    }
                    index = next;
                    continue;
                }

                let parts = self.expand_fields(&unescape_double(&inner), false);
                for (n, part) in parts.into_iter().enumerate() {
                    if n > 0 {
                        push_literal(&mut fields, " ");
                    }
                    push_literal(&mut fields, &part.text);
                }
                index = next;
                continue;
            }

            if c == '\\' {
                if index + 1 < chars.len() {
                    push_literal(&mut fields, &chars[index + 1].to_string());
                    index += 2;
                } else {
                    index += 1;
                }
                continue;
            }

            if c == '$' && index + 1 < chars.len() {
                let (values, next) = self.expand_dollar(&chars, index);
                index = next;
                for (n, value) in values.iter().enumerate() {
                    if n > 0 {
                        fields.push(Field {
                            text: String::new(),
                            globbable: false,
                        });
                    }
                    if split {
                        push_split(&mut fields, value);
                    } else {
                        push_literal(&mut fields, value);
                    }
                }
                continue;
            }

            if c == '~' && index == 0 && (chars.len() == 1 || chars[1] == '/') {
                let home = self.var_string("HOME");
                push_literal(&mut fields, &home);
                index += 1;
                continue;
            }

            if c == '*' || c == '?' || c == '[' {
                let last = fields.last_mut().unwrap();
                last.globbable = true;
                last.text.push(c);
                index += 1;
                continue;
            }

            push_literal(&mut fields, &c.to_string());
            index += 1;
        }

        if fields.len() > 1 {
            fields.retain(|f| !f.text.is_empty());
        }
        if fields.is_empty() {
            fields.push(Field {
                text: String::new(),
                globbable: false,
            });
        }
        fields
    }

    /// Everything that starts with `$`. Returns one value, or many for `${arr[@]}`.
    fn expand_dollar(&mut self, chars: &[char], start: usize) -> (Vec<String>, usize) {
        let next = chars[start + 1];

        if next == '(' && chars.get(start + 2) == Some(&'(') {
            let (body, after) = balanced(chars, start + 2, '(', ')');
            let value = self.arith(&body).to_string();
            return (vec![value], after + 1);
        }

        if next == '(' {
            let (body, after) = balanced(chars, start + 1, '(', ')');
            let value = self.capture(&body);
            return (vec![value], after);
        }

        if next == '{' {
            let (body, after) = balanced(chars, start + 1, '{', '}');
            return (self.expand_braced(&body), after);
        }

        if next.is_ascii_digit() {
            let mut end = start + 1;
            while end < chars.len() && chars[end].is_ascii_digit() {
                end += 1;
            }
            let number: usize = chars[start + 1..end].iter().collect::<String>().parse().unwrap();
            return (vec![self.param(number)], end);
        }

        if next == '?' {
            return (vec![self.status.to_string()], start + 2);
        }
        if next == '#' {
            return (vec![self.params.len().to_string()], start + 2);
        }
        if next == '$' {
            return (vec![std::process::id().to_string()], start + 2);
        }
        if next == '@' || next == '*' {
            return (self.params.clone(), start + 2);
        }

        if next.is_ascii_alphabetic() || next == '_' {
            let mut end = start + 1;
            while end < chars.len() && (chars[end].is_ascii_alphanumeric() || chars[end] == '_') {
                end += 1;
            }
            let name: String = chars[start + 1..end].iter().collect();
            return (vec![self.var_string(&name)], end);
        }

        (vec!["$".to_string()], start + 1)
    }

    /// The inside of `${...}`.
    fn expand_braced(&mut self, body: &str) -> Vec<String> {
        if let Some(rest) = body.strip_prefix('#') {
            if let Some(name) = rest.strip_suffix("[@]").or_else(|| rest.strip_suffix("[*]")) {
                return vec![self.array(name).len().to_string()];
            }
            return vec![self.var_string(rest).chars().count().to_string()];
        }

        /* ${arr[@]} and ${arr[2]} */
        if let Some(open) = body.find('[') {
            if body.ends_with(']') {
                let name = &body[..open];
                let index = &body[open + 1..body.len() - 1];
                let items = self.array(name);
                if index == "@" || index == "*" {
                    return items;
                }
                let at = self.arith(index);
                return vec![items.get(at as usize).cloned().unwrap_or_default()];
            }
        }

        let operators = [":-", ":+", ":?", "##", "#", "%%", "%", "//", "/", ":"];
        for op in operators {
            if let Some(at) = find_operator(body, op) {
                let name = &body[..at];
                let argument = &body[at + op.len()..];
                return vec![self.apply_operator(name, op, argument)];
            }
        }

        vec![self.var_string(body)]
    }

    fn apply_operator(&mut self, name: &str, op: &str, argument: &str) -> String {
        let current = self.var_string(name);

        match op {
            ":-" => {
                if current.is_empty() {
                    self.expand_to_string(argument)
                } else {
                    current
                }
            }
            ":+" => {
                if current.is_empty() {
                    String::new()
                } else {
                    self.expand_to_string(argument)
                }
            }
            ":?" => {
                if current.is_empty() {
                    let message = self.expand_to_string(argument);
                    eprintln!(
                        "fresh-rs: {}: {}",
                        name,
                        if message.is_empty() {
                            "parameter is not set".to_string()
                        } else {
                            message
                        }
                    );
                    self.fatal = true;
                }
                current
            }
            "#" | "##" => {
                let pattern = self.expand_to_string(argument);
                strip(&current, &pattern, true, op == "##")
            }
            "%" | "%%" => {
                let pattern = self.expand_to_string(argument);
                strip(&current, &pattern, false, op == "%%")
            }
            "/" | "//" => {
                let text = self.expand_to_string(argument);
                let (pattern, replacement) = match text.find('/') {
                    Some(at) => (text[..at].to_string(), text[at + 1..].to_string()),
                    None => (text.clone(), String::new()),
                };
                replace(&current, &pattern, &replacement, op == "//")
            }
            ":" => {
                let text = self.expand_to_string(argument);
                let (offset_text, length_text) = match text.find(':') {
                    Some(at) => (text[..at].to_string(), Some(text[at + 1..].to_string())),
                    None => (text, None),
                };
                let offset = self.arith(&offset_text).max(0) as usize;
                let letters: Vec<char> = current.chars().collect();
                let from = offset.min(letters.len());
                let count = match length_text {
                    Some(text) => self.arith(&text).max(0) as usize,
                    None => letters.len() - from,
                };
                letters[from..(from + count).min(letters.len())].iter().collect()
            }
            _ => current,
        }
    }

    pub fn var_string(&mut self, name: &str) -> String {
        if name == "EPOCHREALTIME" {
            let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
            return format!("{}.{:06}", now.as_secs(), now.subsec_micros());
        }
        if name == "EPOCHSECONDS" {
            let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
            return now.as_secs().to_string();
        }
        if name == "SECONDS" {
            return self.started.elapsed().as_secs().to_string();
        }
        if name == "PWD" {
            return std::env::current_dir()
                .map(|p| p.to_string_lossy().replace('\\', "/"))
                .unwrap_or_default();
        }
        if name == "RANDOM" {
            let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap();
            return ((now.subsec_nanos() / 7) % 32768).to_string();
        }

        for scope in self.scopes.iter().rev() {
            if let Some(value) = scope.get(name) {
                return match value {
                    Value::Str(text) => text.clone(),
                    Value::Arr(items) => items.first().cloned().unwrap_or_default(),
                };
            }
        }
        std::env::var(name).unwrap_or_default()
    }

    pub fn array(&mut self, name: &str) -> Vec<String> {
        for scope in self.scopes.iter().rev() {
            if let Some(value) = scope.get(name) {
                return match value {
                    Value::Str(text) => vec![text.clone()],
                    Value::Arr(items) => items.clone(),
                };
            }
        }
        Vec::new()
    }

    fn param(&self, number: usize) -> String {
        if number == 0 {
            return self.name.clone();
        }
        self.params.get(number - 1).cloned().unwrap_or_default()
    }
}

fn push_literal(fields: &mut Vec<Field>, text: &str) {
    fields.last_mut().unwrap().text.push_str(text);
}

fn push_split(fields: &mut Vec<Field>, text: &str) {
    let mut first = true;
    for piece in text.split([' ', '\t', '\n']) {
        if piece.is_empty() {
            continue;
        }
        if !first {
            fields.push(Field {
                text: String::new(),
                globbable: false,
            });
        }
        push_literal(fields, piece);
        first = false;
    }
}

fn take_until(chars: &[char], start: usize, close: char) -> (String, usize) {
    let mut out = String::new();
    let mut index = start;
    while index < chars.len() {
        if chars[index] == '\\' && close == '"' && index + 1 < chars.len() {
            out.push(chars[index]);
            out.push(chars[index + 1]);
            index += 2;
            continue;
        }
        if chars[index] == close {
            return (out, index + 1);
        }
        out.push(chars[index]);
        index += 1;
    }
    (out, index)
}

fn unescape_double(text: &str) -> String {
    let chars: Vec<char> = text.chars().collect();
    let mut out = String::new();
    let mut index = 0;
    while index < chars.len() {
        if chars[index] == '\\' && index + 1 < chars.len() {
            let next = chars[index + 1];
            if next == '"' || next == '\\' || next == '$' || next == '`' {
                out.push(next);
                index += 2;
                continue;
            }
        }
        out.push(chars[index]);
        index += 1;
    }
    out
}

/// The text between `open` and its matching `close`, and the index just past it.
fn balanced(chars: &[char], start: usize, open: char, close: char) -> (String, usize) {
    let mut depth = 0;
    let mut out = String::new();
    let mut index = start;

    while index < chars.len() {
        let c = chars[index];
        if c == open {
            depth += 1;
            if depth == 1 {
                index += 1;
                continue;
            }
        } else if c == close {
            depth -= 1;
            if depth == 0 {
                return (out, index + 1);
            }
        }
        out.push(c);
        index += 1;
    }
    (out, index)
}

/// The first operator at brace level zero, so `${v:-${x}}` splits on the outer one.
fn find_operator(body: &str, op: &str) -> Option<usize> {
    let bytes: Vec<char> = body.chars().collect();
    let needle: Vec<char> = op.chars().collect();
    let mut depth = 0;

    for at in 0..bytes.len() {
        match bytes[at] {
            '{' => depth += 1,
            '}' => depth -= 1,
            _ => {}
        }
        if depth != 0 || at == 0 {
            continue;
        }
        if at + needle.len() <= bytes.len() && bytes[at..at + needle.len()] == needle[..] {
            return Some(body.char_indices().nth(at).map(|(i, _)| i).unwrap_or(at));
        }
    }
    None
}

fn strip(text: &str, pattern: &str, from_front: bool, longest: bool) -> String {
    let letters: Vec<char> = text.chars().collect();
    let mut best: Option<usize> = None;

    for length in 0..=letters.len() {
        let piece: String = if from_front {
            letters[..length].iter().collect()
        } else {
            letters[letters.len() - length..].iter().collect()
        };
        if pattern_match(pattern, &piece) {
            best = match best {
                Some(current) if !longest => Some(current),
                _ => Some(length),
            };
            if !longest && best.is_some() && length > 0 {
                break;
            }
        }
    }

    match best {
        Some(length) if from_front => letters[length..].iter().collect(),
        Some(length) => letters[..letters.len() - length].iter().collect(),
        None => text.to_string(),
    }
}

fn replace(text: &str, pattern: &str, replacement: &str, all: bool) -> String {
    let letters: Vec<char> = text.chars().collect();
    let mut out = String::new();
    let mut index = 0;
    let mut done = false;

    while index < letters.len() {
        let mut hit = None;
        if !done || all {
            for end in (index..=letters.len()).rev() {
                let piece: String = letters[index..end].iter().collect();
                if !piece.is_empty() && pattern_match(pattern, &piece) {
                    hit = Some(end);
                    break;
                }
            }
        }
        match hit {
            Some(end) => {
                out.push_str(replacement);
                index = end;
                done = true;
            }
            None => {
                out.push(letters[index]);
                index += 1;
            }
        }
    }
    out
}

/// `*`, `?` and `[...]`, the same shapes the C shell matches.
pub fn pattern_match(pattern: &str, text: &str) -> bool {
    let p: Vec<char> = pattern.chars().collect();
    let t: Vec<char> = text.chars().collect();
    matches_from(&p, 0, &t, 0)
}

fn matches_from(p: &[char], mut pi: usize, t: &[char], mut ti: usize) -> bool {
    while pi < p.len() {
        match p[pi] {
            '*' => {
                for skip in ti..=t.len() {
                    if matches_from(p, pi + 1, t, skip) {
                        return true;
                    }
                }
                return false;
            }
            '?' => {
                if ti >= t.len() {
                    return false;
                }
                pi += 1;
                ti += 1;
            }
            '[' => {
                if ti >= t.len() {
                    return false;
                }
                let mut end = pi + 1;
                let negate = end < p.len() && (p[end] == '!' || p[end] == '^');
                if negate {
                    end += 1;
                }
                let first = end;
                while end < p.len() && (p[end] != ']' || end == first) {
                    end += 1;
                }
                if end >= p.len() {
                    return false;
                }

                let mut hit = false;
                let mut at = first;
                while at < end {
                    if at + 2 < end && p[at + 1] == '-' {
                        if t[ti] >= p[at] && t[ti] <= p[at + 2] {
                            hit = true;
                        }
                        at += 3;
                    } else {
                        if p[at] == t[ti] {
                            hit = true;
                        }
                        at += 1;
                    }
                }
                if hit == negate {
                    return false;
                }
                pi = end + 1;
                ti += 1;
            }
            '\\' if pi + 1 < p.len() => {
                if ti >= t.len() || t[ti] != p[pi + 1] {
                    return false;
                }
                pi += 2;
                ti += 1;
            }
            c => {
                if ti >= t.len() || t[ti] != c {
                    return false;
                }
                pi += 1;
                ti += 1;
            }
        }
    }
    ti == t.len()
}

/// Expands a path pattern one directory level at a time.
pub fn glob(pattern: &str) -> Vec<String> {
    let normalised = pattern.replace('\\', "/");
    let absolute = normalised.starts_with('/') || normalised.chars().nth(1) == Some(':');

    let mut parts = normalised.split('/').peekable();
    let mut heads: Vec<String> = Vec::new();

    if absolute {
        let first = parts.next().unwrap_or("").to_string();
        heads.push(first);
    } else {
        heads.push(String::new());
    }

    for part in parts {
        if part.is_empty() {
            continue;
        }
        let wild = part.contains('*') || part.contains('?') || part.contains('[');
        let mut next: Vec<String> = Vec::new();

        for head in &heads {
            if !wild {
                let joined = if head.is_empty() {
                    part.to_string()
                } else {
                    format!("{}/{}", head, part)
                };
                next.push(joined);
                continue;
            }

            let directory = if head.is_empty() { ".".to_string() } else { head.clone() };
            let entries = match std::fs::read_dir(&directory) {
                Ok(entries) => entries,
                Err(_) => continue,
            };

            let mut names: Vec<String> = entries
                .filter_map(|entry| entry.ok())
                .map(|entry| entry.file_name().to_string_lossy().to_string())
                .filter(|name| pattern_match(part, name))
                .filter(|name| !name.starts_with('.') || part.starts_with('.'))
                .collect();
            names.sort();

            for name in names {
                next.push(if head.is_empty() {
                    name
                } else {
                    format!("{}/{}", head, name)
                });
            }
        }
        heads = next;
    }

    heads
        .into_iter()
        .filter(|path| !path.is_empty() && std::path::Path::new(path).exists())
        .collect()
}
