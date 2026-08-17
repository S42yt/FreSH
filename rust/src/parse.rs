/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

use crate::lex::Tok;

#[derive(Debug, Clone)]
pub enum Redir {
    Out(String),
    Append(String),
    In(String),
    ErrOut(String),
    ErrAppend(String),
    ErrToOut,
}

#[derive(Debug, Clone)]
pub struct Assign {
    pub name: String,
    pub values: Vec<String>,
    pub append: bool,
    pub array: bool,
}

#[derive(Debug, Clone)]
pub struct Simple {
    pub assigns: Vec<Assign>,
    pub words: Vec<String>,
    pub redirs: Vec<Redir>,
}

#[derive(Debug, Clone)]
pub struct CaseArm {
    pub patterns: Vec<String>,
    pub body: Vec<Node>,
    pub fallthrough: bool,
}

#[derive(Debug, Clone)]
pub enum Node {
    Cmd(Simple),
    Pipe(Vec<Node>),
    And(Box<Node>, Box<Node>),
    Or(Box<Node>, Box<Node>),
    Not(Box<Node>),
    Seq(Vec<Node>),
    Subshell(Vec<Node>),
    Group(Vec<Node>),
    If {
        cond: Vec<Node>,
        then: Vec<Node>,
        otherwise: Vec<Node>,
    },
    While {
        cond: Vec<Node>,
        body: Vec<Node>,
        until: bool,
    },
    For {
        name: String,
        items: Vec<String>,
        body: Vec<Node>,
    },
    Case {
        word: String,
        arms: Vec<CaseArm>,
    },
    Func {
        name: String,
        body: Vec<Node>,
    },
}

pub struct Parser {
    toks: Vec<Tok>,
    at: usize,
}

const RESERVED: [&str; 15] = [
    "if", "then", "elif", "else", "fi", "while", "until", "do", "done", "for", "in", "case", "esac",
    "function", "{",
];

pub fn parse(toks: Vec<Tok>) -> Result<Vec<Node>, String> {
    let mut parser = Parser { toks, at: 0 };
    let list = parser.list(&[])?;
    parser.skip_separators();
    if parser.at < parser.toks.len() {
        return Err(format!("unexpected {:?}", parser.toks[parser.at]));
    }
    Ok(list)
}

impl Parser {
    fn peek(&self) -> Option<&Tok> {
        self.toks.get(self.at)
    }

    fn word_is(&self, text: &str) -> bool {
        matches!(self.peek(), Some(Tok::Word(w)) if w == text)
    }

    fn op_is(&self, text: &str) -> bool {
        matches!(self.peek(), Some(Tok::Op(o)) if o == text)
    }

    fn take(&mut self) -> Option<Tok> {
        let tok = self.toks.get(self.at).cloned();
        if tok.is_some() {
            self.at += 1;
        }
        tok
    }

    fn skip_separators(&mut self) {
        while matches!(self.peek(), Some(Tok::Newline)) || self.op_is(";") {
            self.at += 1;
        }
    }

    fn at_terminator(&self, stops: &[&str]) -> bool {
        match self.peek() {
            None => true,
            Some(Tok::Word(w)) => stops.contains(&w.as_str()),
            Some(Tok::Op(o)) => o == ")" || (o == ";;" && stops.contains(&";;")),
            _ => false,
        }
    }

    fn list(&mut self, stops: &[&str]) -> Result<Vec<Node>, String> {
        let mut out = Vec::new();
        loop {
            self.skip_separators();
            if self.at_terminator(stops) {
                return Ok(out);
            }
            out.push(self.and_or()?);

            match self.peek() {
                Some(Tok::Newline) | Some(Tok::Op(_)) | None => {}
                Some(Tok::Word(_)) => {}
            }
            if !matches!(self.peek(), Some(Tok::Newline)) && !self.op_is(";") {
                return Ok(out);
            }
        }
    }

    fn and_or(&mut self) -> Result<Node, String> {
        let mut left = self.pipeline()?;
        loop {
            if self.op_is("&&") {
                self.at += 1;
                self.skip_newlines();
                let right = self.pipeline()?;
                left = Node::And(Box::new(left), Box::new(right));
            } else if self.op_is("||") {
                self.at += 1;
                self.skip_newlines();
                let right = self.pipeline()?;
                left = Node::Or(Box::new(left), Box::new(right));
            } else {
                return Ok(left);
            }
        }
    }

    fn skip_newlines(&mut self) {
        while matches!(self.peek(), Some(Tok::Newline)) {
            self.at += 1;
        }
    }

    fn pipeline(&mut self) -> Result<Node, String> {
        let mut negate = false;
        if self.word_is("!") {
            self.at += 1;
            negate = true;
        }

        let mut stages = vec![self.command()?];
        while self.op_is("|") {
            self.at += 1;
            self.skip_newlines();
            stages.push(self.command()?);
        }

        let node = if stages.len() == 1 {
            stages.pop().unwrap()
        } else {
            Node::Pipe(stages)
        };
        Ok(if negate {
            Node::Not(Box::new(node))
        } else {
            node
        })
    }

    fn command(&mut self) -> Result<Node, String> {
        if self.word_is("if") {
            return self.parse_if();
        }
        if self.word_is("while") || self.word_is("until") {
            return self.parse_while();
        }
        if self.word_is("for") {
            return self.parse_for();
        }
        if self.word_is("case") {
            return self.parse_case();
        }
        if self.word_is("{") {
            self.at += 1;
            let body = self.list(&["}"])?;
            self.expect_word("}")?;
            return Ok(Node::Group(body));
        }
        if self.op_is("(") {
            self.at += 1;
            let body = self.list(&[])?;
            self.expect_op(")")?;
            return Ok(Node::Subshell(body));
        }
        if self.word_is("function") {
            self.at += 1;
            let name = self.plain_word()?;
            if self.op_is("(") {
                self.at += 1;
                self.expect_op(")")?;
            }
            return self.parse_body(name);
        }

        if let Some(Tok::Word(name)) = self.peek().cloned() {
            if !RESERVED.contains(&name.as_str())
                && matches!(self.toks.get(self.at + 1), Some(Tok::Op(o)) if o == "(")
                && matches!(self.toks.get(self.at + 2), Some(Tok::Op(o)) if o == ")")
            {
                self.at += 3;
                return self.parse_body(name);
            }
        }

        self.simple()
    }

    fn parse_body(&mut self, name: String) -> Result<Node, String> {
        self.skip_newlines();
        if self.word_is("{") {
            self.at += 1;
            let body = self.list(&["}"])?;
            self.expect_word("}")?;
            return Ok(Node::Func { name, body });
        }
        Err(format!("{}: expected a body in braces", name))
    }

    fn plain_word(&mut self) -> Result<String, String> {
        match self.take() {
            Some(Tok::Word(w)) => Ok(w),
            other => Err(format!("expected a word, found {:?}", other)),
        }
    }

    fn expect_word(&mut self, text: &str) -> Result<(), String> {
        if self.word_is(text) {
            self.at += 1;
            return Ok(());
        }
        Err(format!("expected {}", text))
    }

    fn expect_op(&mut self, text: &str) -> Result<(), String> {
        if self.op_is(text) {
            self.at += 1;
            return Ok(());
        }
        Err(format!("expected {}", text))
    }

    fn parse_if(&mut self) -> Result<Node, String> {
        self.at += 1;
        let cond = self.list(&["then"])?;
        self.expect_word("then")?;
        let then = self.list(&["elif", "else", "fi"])?;

        let otherwise = if self.word_is("elif") {
            vec![self.parse_if_tail()?]
        } else if self.word_is("else") {
            self.at += 1;
            let body = self.list(&["fi"])?;
            self.expect_word("fi")?;
            body
        } else {
            self.expect_word("fi")?;
            Vec::new()
        };

        Ok(Node::If {
            cond,
            then,
            otherwise,
        })
    }

    fn parse_if_tail(&mut self) -> Result<Node, String> {
        self.at += 1;
        let cond = self.list(&["then"])?;
        self.expect_word("then")?;
        let then = self.list(&["elif", "else", "fi"])?;

        let otherwise = if self.word_is("elif") {
            vec![self.parse_if_tail()?]
        } else if self.word_is("else") {
            self.at += 1;
            let body = self.list(&["fi"])?;
            self.expect_word("fi")?;
            body
        } else {
            self.expect_word("fi")?;
            Vec::new()
        };

        Ok(Node::If {
            cond,
            then,
            otherwise,
        })
    }

    fn parse_while(&mut self) -> Result<Node, String> {
        let until = self.word_is("until");
        self.at += 1;
        let cond = self.list(&["do"])?;
        self.expect_word("do")?;
        let body = self.list(&["done"])?;
        self.expect_word("done")?;
        Ok(Node::While { cond, body, until })
    }

    fn parse_for(&mut self) -> Result<Node, String> {
        self.at += 1;
        let name = self.plain_word()?;
        let mut items = Vec::new();

        if self.word_is("in") {
            self.at += 1;
            while let Some(Tok::Word(w)) = self.peek().cloned() {
                if w == "do" {
                    break;
                }
                items.push(w);
                self.at += 1;
            }
        }
        self.skip_separators();
        self.expect_word("do")?;
        let body = self.list(&["done"])?;
        self.expect_word("done")?;
        Ok(Node::For { name, items, body })
    }

    fn parse_case(&mut self) -> Result<Node, String> {
        self.at += 1;
        let word = self.plain_word()?;
        self.skip_newlines();
        self.expect_word("in")?;

        let mut arms = Vec::new();
        loop {
            self.skip_separators();
            if self.word_is("esac") {
                self.at += 1;
                break;
            }
            if self.peek().is_none() {
                return Err("case without esac".to_string());
            }

            if self.op_is("(") {
                self.at += 1;
            }
            let mut patterns = vec![self.pattern_word()?];
            while self.op_is("|") {
                self.at += 1;
                patterns.push(self.pattern_word()?);
            }
            self.expect_op(")")?;

            let body = self.list(&[";;", "esac"])?;
            let mut fallthrough = false;
            if self.op_is(";;") {
                self.at += 1;
            } else if self.word_is(";&") {
                self.at += 1;
                fallthrough = true;
            }
            arms.push(CaseArm {
                patterns,
                body,
                fallthrough,
            });
        }

        Ok(Node::Case { word, arms })
    }

    fn pattern_word(&mut self) -> Result<String, String> {
        match self.take() {
            Some(Tok::Word(w)) => Ok(w),
            Some(Tok::Op(o)) => Ok(o),
            other => Err(format!("expected a pattern, found {:?}", other)),
        }
    }

    fn simple(&mut self) -> Result<Node, String> {
        let mut cmd = Simple {
            assigns: Vec::new(),
            words: Vec::new(),
            redirs: Vec::new(),
        };

        loop {
            if let Some(redir) = self.redirection()? {
                cmd.redirs.push(redir);
                continue;
            }

            let word = match self.peek() {
                Some(Tok::Word(w)) => w.clone(),
                _ => break,
            };

            if cmd.words.is_empty() && RESERVED.contains(&word.as_str()) && !cmd.assigns.is_empty() {
                break;
            }
            if cmd.words.is_empty() {
                if let Some((name, append)) = assignment_name(&word) {
                    self.at += 1;
                    let value = word[word.find('=').unwrap() + 1..].to_string();
                    if value.is_empty() && self.op_is("(") {
                        self.at += 1;
                        let mut items = Vec::new();
                        loop {
                            match self.peek() {
                                Some(Tok::Word(w)) => {
                                    items.push(w.clone());
                                    self.at += 1;
                                }
                                Some(Tok::Newline) => self.at += 1,
                                Some(Tok::Op(o)) if o == ")" => {
                                    self.at += 1;
                                    break;
                                }
                                _ => return Err("array without a closing bracket".to_string()),
                            }
                        }
                        cmd.assigns.push(Assign {
                            name,
                            values: items,
                            append: false,
                            array: true,
                        });
                    } else {
                        cmd.assigns.push(Assign {
                            name,
                            values: vec![value],
                            append,
                            array: false,
                        });
                    }
                    continue;
                }
            }

            self.at += 1;
            cmd.words.push(word);

            if cmd.words.len() == 1 && (cmd.words[0] == "[[" || cmd.words[0] == "[") {
                let close = if cmd.words[0] == "[[" { "]]" } else { "]" };
                loop {
                    match self.peek().cloned() {
                        Some(Tok::Word(w)) => {
                            self.at += 1;
                            let done = w == close;
                            cmd.words.push(w);
                            if done {
                                break;
                            }
                        }
                        Some(Tok::Op(o)) if o == "&&" || o == "||" || o == "|" => {
                            self.at += 1;
                            cmd.words.push(o);
                        }
                        _ => break,
                    }
                }
            }
        }

        if cmd.words.is_empty() && cmd.assigns.is_empty() && cmd.redirs.is_empty() {
            return Err(format!("unexpected {:?}", self.peek()));
        }
        Ok(Node::Cmd(cmd))
    }

    fn redirection(&mut self) -> Result<Option<Redir>, String> {
        let op = match self.peek() {
            Some(Tok::Op(o)) => o.clone(),
            _ => return Ok(None),
        };

        let redir = match op.as_str() {
            "2>&1" => {
                self.at += 1;
                return Ok(Some(Redir::ErrToOut));
            }
            ">" | ">>" | "<" | "2>" | "2>>" | "&>" => {
                self.at += 1;
                let target = self.plain_word()?;
                match op.as_str() {
                    ">" => Redir::Out(target),
                    "&>" => Redir::Out(target),
                    ">>" => Redir::Append(target),
                    "<" => Redir::In(target),
                    "2>" => Redir::ErrOut(target),
                    _ => Redir::ErrAppend(target),
                }
            }
            _ => return Ok(None),
        };
        Ok(Some(redir))
    }
}

pub fn assignment_name(word: &str) -> Option<(String, bool)> {
    let equals = word.find('=')?;
    if equals == 0 {
        return None;
    }

    let (mut name, append) = if word.as_bytes()[equals - 1] == b'+' {
        (word[..equals - 1].to_string(), true)
    } else {
        (word[..equals].to_string(), false)
    };

    if let Some(bracket) = name.find('[') {
        if !name.ends_with(']') {
            return None;
        }
        name.truncate(bracket);
    }
    if name.is_empty() {
        return None;
    }

    let mut chars = name.chars();
    let first = chars.next().unwrap();
    if !first.is_ascii_alphabetic() && first != '_' {
        return None;
    }
    if !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return None;
    }
    Some((name, append))
}
