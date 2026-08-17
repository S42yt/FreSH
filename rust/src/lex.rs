/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#[derive(Debug, Clone, PartialEq)]
pub enum Tok {
    Word(String),
    Op(String),
    Newline,
}

const OPERATORS: [&str; 12] = [
    "2>&1", "&>", "2>>", "2>", ">>", "<<", "||", "&&", ";;", "|", "&", ";",
];

pub fn lex(source: &str) -> Vec<Tok> {
    let text: Vec<char> = source.chars().collect();
    let mut out: Vec<Tok> = Vec::new();
    let mut at_word_start = true;
    let mut index = 0;

    while index < text.len() {
        let c = text[index];

        if c == ' ' || c == '\t' || c == '\r' {
            index += 1;
            at_word_start = true;
            continue;
        }

        if c == '\\' && index + 1 < text.len() && text[index + 1] == '\n' {
            index += 2;
            continue;
        }

        if c == '\n' {
            out.push(Tok::Newline);
            index += 1;
            at_word_start = true;
            continue;
        }

        if c == '#' && at_word_start {
            while index < text.len() && text[index] != '\n' {
                index += 1;
            }
            continue;
        }

        if let Some(op) = operator_at(&text, index) {
            out.push(Tok::Op(op.to_string()));
            index += op.chars().count();
            at_word_start = true;
            continue;
        }

        if c == '(' || c == ')' {
            out.push(Tok::Op(c.to_string()));
            index += 1;
            at_word_start = true;
            continue;
        }

        if (c == '<' || c == '>') && !starts_process_word(&text, index) {
            out.push(Tok::Op(c.to_string()));
            index += 1;
            at_word_start = true;
            continue;
        }

        let (word, next) = read_word(&text, index);
        index = next;
        at_word_start = false;
        out.push(Tok::Word(word));
    }

    out
}

fn starts_process_word(_text: &[char], _index: usize) -> bool {
    false
}

fn operator_at(text: &[char], index: usize) -> Option<&'static str> {
    for op in OPERATORS.iter() {
        let chars: Vec<char> = op.chars().collect();
        if index + chars.len() <= text.len() && text[index..index + chars.len()] == chars[..] {
            return Some(op);
        }
    }
    None
}

fn read_word(text: &[char], start: usize) -> (String, usize) {
    let mut word = String::new();
    let mut index = start;

    while index < text.len() {
        let c = text[index];

        if c == ' ' || c == '\t' || c == '\r' || c == '\n' {
            break;
        }
        if c == '(' || c == ')' {
            break;
        }
        if c == '<' || c == '>' || c == '|' || c == '&' || c == ';' {
            break;
        }

        if c == '\\' {
            word.push(c);
            index += 1;
            if index < text.len() {
                word.push(text[index]);
                index += 1;
            }
            continue;
        }

        if c == '\'' || c == '"' {
            let (quoted, next) = read_quoted(text, index, c);
            word.push_str(&quoted);
            index = next;
            continue;
        }

        if c == '$' && index + 1 < text.len() && (text[index + 1] == '(' || text[index + 1] == '{') {
            let (nested, next) = read_nested(text, index);
            word.push_str(&nested);
            index = next;
            continue;
        }

        word.push(c);
        index += 1;
    }

    (word, index)
}

fn read_quoted(text: &[char], start: usize, quote: char) -> (String, usize) {
    let mut out = String::new();
    out.push(quote);
    let mut index = start + 1;

    while index < text.len() {
        let c = text[index];
        if c == '\\' && quote == '"' && index + 1 < text.len() {
            out.push(c);
            out.push(text[index + 1]);
            index += 2;
            continue;
        }
        if c == quote {
            out.push(c);
            return (out, index + 1);
        }
        if c == '$' && quote == '"' && index + 1 < text.len() && (text[index + 1] == '(' || text[index + 1] == '{') {
            let (nested, next) = read_nested(text, index);
            out.push_str(&nested);
            index = next;
            continue;
        }
        out.push(c);
        index += 1;
    }

    (out, index)
}

fn read_nested(text: &[char], start: usize) -> (String, usize) {
    let open = text[start + 1];
    let close = if open == '(' { ')' } else { '}' };

    let mut out = String::new();
    out.push('$');
    out.push(open);

    let mut depth = 1;
    let mut index = start + 2;

    while index < text.len() && depth > 0 {
        let c = text[index];

        if c == '\'' || c == '"' {
            let (quoted, next) = read_quoted(text, index, c);
            out.push_str(&quoted);
            index = next;
            continue;
        }
        if c == '$' && index + 1 < text.len() && (text[index + 1] == '(' || text[index + 1] == '{') {
            let (nested, next) = read_nested(text, index);
            out.push_str(&nested);
            index = next;
            continue;
        }

        if c == open {
            depth += 1;
        } else if c == close {
            depth -= 1;
            if depth == 0 {
                out.push(c);
                return (out, index + 1);
            }
        }

        out.push(c);
        index += 1;
    }

    (out, index)
}
