/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char b, g, r, a;
} Pixel;

static const int SIZES[] = {16, 24, 32, 48, 64, 128, 256};
static const int SIZE_COUNT = 7;

static const Pixel BACKGROUND = {40, 33, 27, 255};
static const Pixel GLYPH = {120, 235, 120, 255};

static double distance_to_segment(double x, double y, double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double length = dx * dx + dy * dy;
    double t = length > 0 ? ((x - x1) * dx + (y - y1) * dy) / length : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double px = x1 + t * dx - x;
    double py = y1 + t * dy - y;
    return sqrt(px * px + py * py);
}

static double coverage(double x, double y, int size) {
    double unit = size / 100.0;
    double thickness = size * 0.085;

    double spine = distance_to_segment(x, y, 30 * unit, 16 * unit, 68 * unit, 86 * unit);
    double leg = distance_to_segment(x, y, 52 * unit, 52 * unit, 30 * unit, 86 * unit);
    double nearest = spine < leg ? spine : leg;

    double edge = thickness / 2;
    double fade = size * 0.045;
    if (nearest <= edge) return 1.0;
    if (nearest >= edge + fade) return 0.0;
    return 1.0 - (nearest - edge) / fade;
}

static double plate_alpha(double cx, double cy, int size) {
    double inset = size * 0.02;
    double radius = size * 0.22;
    double left = inset, top = inset, right = size - inset, bottom = size - inset;

    double dx = 0, dy = 0;
    if (cx < left + radius) dx = left + radius - cx;
    else if (cx > right - radius) dx = cx - (right - radius);
    if (cy < top + radius) dy = top + radius - cy;
    else if (cy > bottom - radius) dy = cy - (bottom - radius);

    double outside = sqrt(dx * dx + dy * dy) - radius;
    if (outside <= 0) return 1.0;
    if (outside >= 1.0) return 0.0;
    return 1.0 - outside;
}

static void render(Pixel *pixels, int size) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double cx = x + 0.5;
            double cy = y + 0.5;

            double inside = plate_alpha(cx, cy, size);
            double glyph = coverage(cx, cy, size);

            Pixel result;
            result.b = (unsigned char)(BACKGROUND.b + (GLYPH.b - BACKGROUND.b) * glyph);
            result.g = (unsigned char)(BACKGROUND.g + (GLYPH.g - BACKGROUND.g) * glyph);
            result.r = (unsigned char)(BACKGROUND.r + (GLYPH.r - BACKGROUND.r) * glyph);
            result.a = (unsigned char)(255 * inside);

            pixels[(size - 1 - y) * size + x] = result;
        }
    }
}

static void write_u16(FILE *f, unsigned value) {
    fputc(value & 0xff, f);
    fputc((value >> 8) & 0xff, f);
}

static void write_u32(FILE *f, unsigned long value) {
    fputc(value & 0xff, f);
    fputc((value >> 8) & 0xff, f);
    fputc((value >> 16) & 0xff, f);
    fputc((value >> 24) & 0xff, f);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: makeicon <output.ico>\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "makeicon: cannot write %s\n", argv[1]);
        return 1;
    }

    write_u16(f, 0);
    write_u16(f, 1);
    write_u16(f, (unsigned)SIZE_COUNT);

    unsigned long offset = 6 + 16UL * SIZE_COUNT;
    for (int i = 0; i < SIZE_COUNT; i++) {
        int size = SIZES[i];
        unsigned long mask_row = ((unsigned long)size + 31) / 32 * 4;
        unsigned long bytes = 40 + (unsigned long)size * size * 4 + mask_row * size;

        fputc(size == 256 ? 0 : size, f);
        fputc(size == 256 ? 0 : size, f);
        fputc(0, f);
        fputc(0, f);
        write_u16(f, 1);
        write_u16(f, 32);
        write_u32(f, bytes);
        write_u32(f, offset);
        offset += bytes;
    }

    for (int i = 0; i < SIZE_COUNT; i++) {
        int size = SIZES[i];
        Pixel *pixels = malloc((size_t)size * size * sizeof(Pixel));
        if (!pixels) {
            fclose(f);
            return 1;
        }
        render(pixels, size);

        write_u32(f, 40);
        write_u32(f, (unsigned long)size);
        write_u32(f, (unsigned long)size * 2);
        write_u16(f, 1);
        write_u16(f, 32);
        write_u32(f, 0);
        write_u32(f, (unsigned long)size * size * 4);
        write_u32(f, 0);
        write_u32(f, 0);
        write_u32(f, 0);
        write_u32(f, 0);

        for (int p = 0; p < size * size; p++) {
            fputc(pixels[p].b, f);
            fputc(pixels[p].g, f);
            fputc(pixels[p].r, f);
            fputc(pixels[p].a, f);
        }

        unsigned long mask_row = ((unsigned long)size + 31) / 32 * 4;
        for (int y = 0; y < size; y++)
            for (unsigned long x = 0; x < mask_row; x++) fputc(0, f);

        free(pixels);
    }

    fclose(f);
    return 0;
}
