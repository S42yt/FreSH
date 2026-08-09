/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage: bin2c <input> <output.h> <symbol>\n");
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "bin2c: cannot open %s\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "bin2c: cannot write %s\n", argv[2]);
        fclose(in);
        return 1;
    }

    fprintf(out, "#ifndef FRESH_%s_H\n#define FRESH_%s_H\n\n", argv[3], argv[3]);
    fprintf(out, "static const unsigned char %s[] = {", argv[3]);

    unsigned long count = 0;
    int byte;
    while ((byte = fgetc(in)) != EOF) {
        if (count % 16 == 0) fputs("\n    ", out);
        fprintf(out, "0x%02x,", (unsigned)byte);
        count++;
    }

    fprintf(out, "\n};\n\nstatic const unsigned long %s_size = %luUL;\n\n#endif\n", argv[3], count);
    fclose(in);
    fclose(out);
    return 0;
}
