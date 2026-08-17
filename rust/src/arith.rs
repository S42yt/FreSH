/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

use crate::exec::{Shell, Value};

#[derive(Debug, Clone, PartialEq)]
enum Atom {
    Number(i64),
    Name(String),
    Op(String),
}

impl Shell {
    /// `$(( ))`, after the parameters inside it have been expanded.
    pub fn arith(&mut self, expression: &str) -> i64 {
        let text = self.expand_to_string(expression);
        let atoms = split_atoms(&text);
        let mut at = 0;
        let value = self.binary(&atoms, &mut at, 0);
        value
    }

    fn binary(&mut self, atoms: &[Atom], at: &mut usize, level: usize) -> i64 {
        const LEVELS: [&[&str]; 8] = [
            &["||"],
            &["&&"],
            &["==", "!="],
            &["<=", ">=", "<", ">"],
            &["<<", ">>"],
            &["+", "-"],
            &["*", "/", "%"],
            &[],
        ];

        if level + 1 == LEVELS.len() {
            return self.unary(atoms, at);
        }

        let mut left = self.binary(atoms, at, level + 1);
        loop {
            let operator = match atoms.get(*at) {
                Some(Atom::Op(op)) if LEVELS[level].contains(&op.as_str()) => op.clone(),
                _ => return left,
            };
            *at += 1;

            /* && and || stop at a decided answer, the way the shell does. */
            if operator == "&&" && left == 0 {
                self.binary(atoms, at, level + 1);
                left = 0;
                continue;
            }
            if operator == "||" && left != 0 {
                self.binary(atoms, at, level + 1);
                left = 1;
                continue;
            }

            let right = self.binary(atoms, at, level + 1);
            left = match operator.as_str() {
                "||" => ((left != 0) || (right != 0)) as i64,
                "&&" => ((left != 0) && (right != 0)) as i64,
                "==" => (left == right) as i64,
                "!=" => (left != right) as i64,
                "<" => (left < right) as i64,
                "<=" => (left <= right) as i64,
                ">" => (left > right) as i64,
                ">=" => (left >= right) as i64,
                "<<" => left << right,
                ">>" => left >> right,
                "+" => left + right,
                "-" => left - right,
                "*" => left * right,
                "/" => {
                    if right == 0 {
                        eprintln!("fresh-rs: division by zero");
                        self.fatal = true;
                        0
                    } else {
                        left / right
                    }
                }
                "%" => {
                    if right == 0 {
                        eprintln!("fresh-rs: division by zero");
                        self.fatal = true;
                        0
                    } else {
                        left % right
                    }
                }
                _ => left,
            };
        }
    }

    fn unary(&mut self, atoms: &[Atom], at: &mut usize) -> i64 {
        match atoms.get(*at).cloned() {
            Some(Atom::Op(op)) if op == "-" => {
                *at += 1;
                -self.unary(atoms, at)
            }
            Some(Atom::Op(op)) if op == "+" => {
                *at += 1;
                self.unary(atoms, at)
            }
            Some(Atom::Op(op)) if op == "!" => {
                *at += 1;
                (self.unary(atoms, at) == 0) as i64
            }
            Some(Atom::Op(op)) if op == "~" => {
                *at += 1;
                !self.unary(atoms, at)
            }
            Some(Atom::Op(op)) if op == "(" => {
                *at += 1;
                let value = self.binary(atoms, at, 0);
                if matches!(atoms.get(*at), Some(Atom::Op(op)) if op == ")") {
                    *at += 1;
                }
                value
            }
            Some(Atom::Number(value)) => {
                *at += 1;
                value
            }
            Some(Atom::Name(name)) => {
                *at += 1;
                /* name++ and name-- read then step, which is all the loops here need. */
                if let Some(Atom::Op(op)) = atoms.get(*at).cloned() {
                    if op == "++" || op == "--" {
                        *at += 1;
                        let current = self.number_of(&name);
                        let next = if op == "++" { current + 1 } else { current - 1 };
                        self.set(&name, Value::Str(next.to_string()));
                        return current;
                    }
                    if op == "=" {
                        *at += 1;
                        let value = self.binary(atoms, at, 0);
                        self.set(&name, Value::Str(value.to_string()));
                        return value;
                    }
                }
                self.number_of(&name)
            }
            _ => 0,
        }
    }

    fn number_of(&mut self, name: &str) -> i64 {
        let text = self.var_string(name);
        parse_number(text.trim()).unwrap_or(0)
    }
}

/// `10#0042` is decimal 42, `0x2a` is 42, and a leading zero is not octal here.
fn parse_number(text: &str) -> Option<i64> {
    if let Some(rest) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        return i64::from_str_radix(rest, 16).ok();
    }
    if let Some(at) = text.find('#') {
        let base: u32 = text[..at].parse().ok()?;
        if (2..=36).contains(&base) {
            return i64::from_str_radix(text[at + 1..].trim_start_matches('0'), base)
                .ok()
                .or(Some(0));
        }
    }
    text.parse().ok()
}

fn split_atoms(text: &str) -> Vec<Atom> {
    let chars: Vec<char> = text.chars().collect();
    let mut out = Vec::new();
    let mut index = 0;

    while index < chars.len() {
        let c = chars[index];

        if c.is_whitespace() {
            index += 1;
            continue;
        }

        if c.is_ascii_digit() {
            let start = index;
            while index < chars.len()
                && (chars[index].is_ascii_alphanumeric() || chars[index] == '#')
            {
                index += 1;
            }
            let piece: String = chars[start..index].iter().collect();
            out.push(Atom::Number(parse_number(&piece).unwrap_or(0)));
            continue;
        }

        if c.is_ascii_alphabetic() || c == '_' {
            let start = index;
            while index < chars.len() && (chars[index].is_ascii_alphanumeric() || chars[index] == '_')
            {
                index += 1;
            }
            out.push(Atom::Name(chars[start..index].iter().collect()));
            continue;
        }

        let two: String = chars[index..(index + 2).min(chars.len())].iter().collect();
        if ["==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "++", "--"].contains(&two.as_str()) {
            out.push(Atom::Op(two));
            index += 2;
            continue;
        }

        out.push(Atom::Op(c.to_string()));
        index += 1;
    }

    out
}
