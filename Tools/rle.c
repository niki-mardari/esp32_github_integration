// Programmer: Niki Mardari
// Purpose: Compress a 240x320 RGB565 CSV file into binary RLE format
//
// Input example:
// 0x0000,0x0000,0xFFFF,0xFFFF
//
// Output binary format:
// [count uint16][colour uint16]
// [count uint16][colour uint16]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIDTH  240
#define HEIGHT 320
#define TOTAL_PIXELS (WIDTH * HEIGHT)

#define OUTPUT_FILE "rle_output.bin"
#define MAX_RUN 65535



void write_u16_le(FILE *output, uint16_t value) {
    // Write 16-bit number in little-endian order
    fputc(value & 0xFF, output); // Getting low byte
    fputc((value >> 8) & 0xFF, output); // Getting High byete
}

// Write compressed run to file, split if bigger
void write_run(FILE *output, uint16_t colour, uint32_t count) {
    // A uint16_t count can only store up to 65535.
    // If a run is longer, split it into multiple runs.
    while (count > 0) {
        uint16_t chunk;

        if (count > MAX_RUN) {
            chunk = MAX_RUN;
        } else {
            chunk = (uint16_t)count;
        }

        write_u16_le(output, chunk);
        write_u16_le(output, colour);

        count -= chunk;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <input.csv>\n", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[1], "r");

    if (input == NULL) {
        printf("Error: could not open input file: %s\n", argv[1]);
        return 1;
    }

    FILE *output = fopen(OUTPUT_FILE, "wb");

    if (output == NULL) {
        printf("Error: could not create output file: %s\n", OUTPUT_FILE);
        fclose(input);
        return 1;
    }

    printf("Compressing %s to %s...\n", argv[1], OUTPUT_FILE);

    unsigned int current_val;
    unsigned int next_val;

    uint32_t count = 1;
    uint32_t pixels = 0;
    uint32_t runs = 0;

    // Read first pixel
    if (fscanf(input, " %x,", &current_val) != 1) {
        printf("Error: could not read first pixel.\n");
        fclose(input);
        fclose(output);
        return 1;
    }

    pixels = 1;

    // Read remaining pixels
    while (fscanf(input, " %x,", &next_val) == 1) {
        pixels++;

        if (next_val == current_val) {
            count++;
        } else {
            write_run(output, (uint16_t)current_val, count);
            runs++;

            current_val = next_val;
            count = 1;
        }
    }

    // Write final run
    write_run(output, (uint16_t)current_val, count);
    runs++;

    fclose(input);
    fclose(output);

    printf("Done!\n");
    printf("Pixels read: %u / %u\n", pixels, TOTAL_PIXELS);
    printf("Runs written: %u\n", runs);

    if (pixels != TOTAL_PIXELS) {
        printf("Warning: expected %u pixels but read %u pixels.\n", TOTAL_PIXELS, pixels);
    }

    return 0;
}