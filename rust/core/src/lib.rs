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
