/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#![no_std]

use core::cmp::Ordering;
use core::panic::PanicInfo;
use core::slice;

extern "C" {
    fn abort() -> !;
    fn atof(text: *const u8) -> f64;
}

#[panic_handler]
fn panicked(_info: &PanicInfo) -> ! {
    unsafe { abort() }
}

#[no_mangle]
pub extern "C" fn rust_eh_personality() {}

const SORT_FOLD: u32 = 1;
const SORT_NUMERIC: u32 = 2;

#[inline]
fn fold(byte: u8) -> u8 {
    if byte.is_ascii_uppercase() {
        byte + 32
    } else {
        byte
    }
}

#[inline]
unsafe fn compare_bytes(left: *const u8, right: *const u8) -> Ordering {
    let mut a = left;
    let mut b = right;
    loop {
        let x = *a;
        let y = *b;
        if x != y {
            return x.cmp(&y);
        }
        if x == 0 {
            return Ordering::Equal;
        }
        a = a.add(1);
        b = b.add(1);
    }
}

#[inline]
unsafe fn compare_folded(left: *const u8, right: *const u8) -> Ordering {
    let mut a = left;
    let mut b = right;
    loop {
        let x = fold(*a);
        let y = fold(*b);
        if x != y {
            return x.cmp(&y);
        }
        if x == 0 {
            return Ordering::Equal;
        }
        a = a.add(1);
        b = b.add(1);
    }
}

#[inline]
unsafe fn compare_numeric(left: *const u8, right: *const u8) -> Ordering {
    let x = atof(left);
    let y = atof(right);
    match x.partial_cmp(&y) {
        Some(Ordering::Equal) | None => compare_bytes(left, right),
        Some(order) => order,
    }
}

#[no_mangle]
pub extern "C" fn fresh_sort_pointers(items: *mut *const u8, len: usize, mode: u32) {
    if items.is_null() || len < 2 {
        return;
    }

    let list = unsafe { slice::from_raw_parts_mut(items, len) };
    list.sort_unstable_by(|a, b| unsafe {
        match mode {
            SORT_FOLD => compare_folded(*a, *b),
            SORT_NUMERIC => compare_numeric(*a, *b),
            _ => compare_bytes(*a, *b),
        }
    });
}

#[repr(C)]
pub struct Counts {
    pub lines: u64,
    pub words: u64,
    pub bytes: u64,
    pub in_word: u32,
}

#[inline]
fn is_blank(byte: u8) -> bool {
    byte == b' ' || (0x09..=0x0d).contains(&byte)
}

#[no_mangle]
pub extern "C" fn fresh_count_block(data: *const u8, len: usize, counts: *mut Counts) {
    if data.is_null() || counts.is_null() {
        return;
    }

    let block = unsafe { slice::from_raw_parts(data, len) };
    let out = unsafe { &mut *counts };

    let mut lines = out.lines;
    let mut words = out.words;
    let mut in_word = out.in_word != 0;

    for &byte in block {
        if byte == b'\n' {
            lines += 1;
        }
        if is_blank(byte) {
            in_word = false;
        } else if !in_word {
            in_word = true;
            words += 1;
        }
    }

    out.lines = lines;
    out.words = words;
    out.bytes += len as u64;
    out.in_word = in_word as u32;
}

const MAX_ENTRIES: usize = 512;

#[inline]
unsafe fn c_len(text: *const u8) -> usize {
    let mut len = 0;
    while *text.add(len) != 0 {
        len += 1;
    }
    len
}

#[inline]
fn folded_hash(entry: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for &byte in entry {
        hash ^= fold(byte) as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

#[inline]
fn same_folded(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).all(|(x, y)| fold(*x) == fold(*y))
}

#[no_mangle]
pub extern "C" fn fresh_path_merge(
    parts: *const *const u8,
    count: usize,
    out: *mut u8,
    cap: usize,
) -> usize {
    if parts.is_null() || out.is_null() || cap == 0 {
        return 0;
    }

    let sources = unsafe { slice::from_raw_parts(parts, count) };
    let buffer = unsafe { slice::from_raw_parts_mut(out, cap) };

    let mut hashes = [0u64; MAX_ENTRIES];
    let mut starts = [0usize; MAX_ENTRIES];
    let mut lengths = [0usize; MAX_ENTRIES];
    let mut kept = 0usize;
    let mut written = 0usize;

    for &part in sources {
        if part.is_null() {
            continue;
        }
        let text = unsafe { slice::from_raw_parts(part, c_len(part)) };

        for entry in text.split(|&byte| byte == b';') {
            if entry.is_empty() {
                continue;
            }

            let hash = folded_hash(entry);
            let mut seen = false;
            for i in 0..kept {
                if hashes[i] == hash && same_folded(&buffer[starts[i]..starts[i] + lengths[i]], entry) {
                    seen = true;
                    break;
                }
            }
            if seen {
                continue;
            }

            let separator = usize::from(written > 0);
            if written + separator + entry.len() + 1 > cap {
                buffer[written] = 0;
                return written;
            }
            if separator == 1 {
                buffer[written] = b';';
                written += 1;
            }

            let start = written;
            buffer[written..written + entry.len()].copy_from_slice(entry);
            written += entry.len();

            if kept < MAX_ENTRIES {
                hashes[kept] = hash;
                starts[kept] = start;
                lengths[kept] = entry.len();
                kept += 1;
            }
        }
    }

    buffer[written] = 0;
    written
}
