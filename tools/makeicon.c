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

typedef Pixel (*Painter)(double x, double y, double unit);

static const int SIZES[] = {16, 24, 32, 48};
static const int SIZE_COUNT = 4;

static const Pixel CLEAR = {0, 0, 0, 0};
static const Pixel PLATE = {40, 33, 27, 255};
static const Pixel RIM = {69, 56, 45, 255};
static const Pixel GREEN = {120, 235, 120, 255};
static const Pixel PAGE = {249, 246, 243, 255};
static const Pixel FOLD = {220, 210, 201, 255};
static const Pixel INK = {139, 125, 111, 255};
static const Pixel SCRIPT_GREEN = {77, 169, 34, 255};

static double clamp(double value, double low, double high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static double segment_distance(double x, double y, double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double length = dx * dx + dy * dy;
    double t = length > 0 ? ((x - x1) * dx + (y - y1) * dy) / length : 0;
    t = clamp(t, 0, 1);
    double px = x1 + t * dx - x;
    double py = y1 + t * dy - y;
    return sqrt(px * px + py * py);
}

static double round_rect_distance(double x, double y, double left, double top, double right,
                                  double bottom, double radius) {
    double cx = (left + right) / 2;
    double cy = (top + bottom) / 2;
    double qx = fabs(x - cx) - ((right - left) / 2 - radius);
    double qy = fabs(y - cy) - ((bottom - top) / 2 - radius);
    double ox = qx > 0 ? qx : 0;
    double oy = qy > 0 ? qy : 0;
    double inner = qx > qy ? qx : qy;
    return sqrt(ox * ox + oy * oy) + (inner < 0 ? inner : 0) - radius;
}

static double edge(double distance) {
    return clamp(0.5 - distance, 0, 1);
}

static Pixel blend(Pixel under, Pixel over, double amount) {
    double a = over.a / 255.0 * clamp(amount, 0, 1);
    double base = under.a / 255.0;
    double out = a + base * (1 - a);
    Pixel result = CLEAR;
    if (out <= 0) return result;
    result.r = (unsigned char)((over.r * a + under.r * base * (1 - a)) / out + 0.5);
    result.g = (unsigned char)((over.g * a + under.g * base * (1 - a)) / out + 0.5);
    result.b = (unsigned char)((over.b * a + under.b * base * (1 - a)) / out + 0.5);
    result.a = (unsigned char)(out * 255 + 0.5);
    return result;
}

static double lambda_distance(double x, double y, double left, double top, double tip,
                              double bottom, double tail) {
    double middle = (top + bottom) / 2;
    double dx = tip - left;
    double dy = middle - bottom;
    double length = sqrt(dx * dx + dy * dy);
    double end_x = tip + dx / length * tail;
    double end_y = middle + dy / length * tail;
    double leg = segment_distance(x, y, left, top, tip, middle);
    double spine = segment_distance(x, y, left, bottom, end_x, end_y);
    return leg < spine ? leg : spine;
}

static Pixel paint_app(double x, double y, double u) {
    double rim = 3.2 * u > 1.0 ? 3.2 * u : 1.0;
    double plate = round_rect_distance(x, y, 2 * u, 2 * u, 98 * u, 98 * u, 23 * u);

    Pixel pixel = blend(CLEAR, RIM, edge(plate));
    pixel = blend(pixel, PLATE, edge(plate + rim));

    double stroke = 11 * u;
    double prompt = lambda_distance(x, y, 18 * u, 27 * u, 56 * u, 73 * u, 16 * u) - stroke / 2;
    pixel = blend(pixel, GREEN, edge(prompt));

    double cursor = round_rect_distance(x, y, 64 * u, 69 * u, 88 * u, 80 * u, 3 * u);
    pixel = blend(pixel, GREEN, edge(cursor));
    return pixel;
}

static Pixel paint_script(double x, double y, double u) {
    double ink = 3 * u > 1.0 ? 3 * u : 1.0;
    double body = round_rect_distance(x, y, 17 * u, 5 * u, 83 * u, 95 * u, 7 * u);
    double crease = ((x - 60 * u) - (y - 5 * u)) / sqrt(2.0);
    double page = body > crease ? body : crease;

    Pixel pixel = blend(CLEAR, INK, edge(page));
    pixel = blend(pixel, PAGE, edge(page + ink));

    double fold_x = 60 * u - x;
    double fold_y = y - 27 * u;
    double fold = fold_x > fold_y ? fold_x : fold_y;
    if (fold < page) fold = page;
    pixel = blend(pixel, INK, edge(fold));
    pixel = blend(pixel, FOLD, edge(fold + ink));

    double stroke = 9 * u;
    double prompt = lambda_distance(x, y, 33 * u, 44 * u, 52 * u, 76 * u, 12 * u) - stroke / 2;
    pixel = blend(pixel, SCRIPT_GREEN, edge(prompt));

    double cursor = round_rect_distance(x, y, 58 * u, 71 * u, 76 * u, 80 * u, 2.5 * u);
    pixel = blend(pixel, SCRIPT_GREEN, edge(cursor));
    return pixel;
}

static void render(Pixel *pixels, int size, Painter paint) {
    double unit = size / 100.0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            Pixel sum = CLEAR;
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < 3; sy++) {
                for (int sx = 0; sx < 3; sx++) {
                    Pixel p = paint(x + (sx + 0.5) / 3, y + (sy + 0.5) / 3, unit);
                    double weight = p.a / 255.0;
                    r += p.r * weight;
                    g += p.g * weight;
                    b += p.b * weight;
                    a += weight;
                }
            }
            if (a > 0) {
                sum.r = (unsigned char)(r / a + 0.5);
                sum.g = (unsigned char)(g / a + 0.5);
                sum.b = (unsigned char)(b / a + 0.5);
                sum.a = (unsigned char)(a / 9 * 255 + 0.5);
            }
            pixels[(size - 1 - y) * size + x] = sum;
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

static int write_icon(const char *path, Painter paint) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "makeicon: cannot write %s\n", path);
        return 0;
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
            return 0;
        }
        render(pixels, size, paint);

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

    return fclose(f) == 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: makeicon <app.ico> <script.ico>\n");
        return 1;
    }
    if (!write_icon(argv[1], paint_app)) return 1;
    if (!write_icon(argv[2], paint_script)) return 1;
    return 0;
}
