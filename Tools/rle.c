// Purpose: Compress a 135x240 RGB565 CSV file into binary RLE format
//
// Input CSV example:
// 0x0000,0x0000,0xFFFF,0xFFFF
//
// Output binary RLE format:
// [count uint16][colour uint16]
// [count uint16][colour uint16]
//
// Example:
// 4 pixels of 0x0000 becomes:
//
// count  = 4      -> 04 00
// colour = 0x0000 -> 00 00
//
// So one RLE run is always 4 bytes.
//
// Important:
// This program does not create data/Encoded by itself.
// The Makefile creates the output directory and passes the output path.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define SCREEN_WIDTH  135
#define SCREEN_HEIGHT 240
#define TOTAL_PIXELS  (SCREEN_WIDTH * SCREEN_HEIGHT)

#define OUTPUT_EXTENSION ".rle"

#define MAX_RUN 65535
#define MAX_PATH_SIZE 512

// -------------------- File name helper --------------------

// Gets only the file name from a path.
//
// Example:
// data/images_135x240/miku1_135x240_cover.csv
//
// Returns:
// miku1_135x240_cover.csv
const char *get_file_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');

    const char *last_separator = slash;

    // Support both Linux-style / paths and Windows-style \ paths.
    if (backslash != NULL && (last_separator == NULL || backslash > last_separator)) {
        last_separator = backslash;
    }

    if (last_separator == NULL) {
        return path;
    }

    return last_separator + 1;
}

// Builds a fallback output path if the user does not give one.
//
// Example input:
// data/images_135x240/miku1_135x240_cover.csv
//
// Output:
// miku1_135x240_cover.rle
//
// Note:
// This fallback writes to the current directory.
// The Makefile normally gives the correct output path:
// data/Encoded/miku1_135x240_cover.rle
void build_default_output_path(const char *input_path, char *output_path, size_t output_size) {
    const char *file_name = get_file_name(input_path);

    char name_without_extension[MAX_PATH_SIZE];

    strncpy(name_without_extension, file_name, sizeof(name_without_extension) - 1);
    name_without_extension[sizeof(name_without_extension) - 1] = '\0';

    char *dot = strrchr(name_without_extension, '.');

    if (dot != NULL) {
        *dot = '\0';
    }

    snprintf(output_path,
             output_size,
             "%s%s",
             name_without_extension,
             OUTPUT_EXTENSION);
}

// -------------------- Binary writer --------------------

// Writes one 16-bit number into the file using little-endian byte order.
// A uint16_t is 2 bytes.
//
// Example:
// value = 0x5317
//
// Low byte  = 0x17
// High byte = 0x53
//
// File output:
// 17 53
void write_u16_le(FILE *output, uint16_t value) {
    fputc(value & 0xFF, output);          // Write low byte first
    fputc((value >> 8) & 0xFF, output);   // Write high byte second
}

// Writes one compressed RLE run to the file.
//
// A run is:
// colour repeated count times
//
// Example:
// 21271 pixels of 0x0000
//
// Text RLE:
// 21271*0x0000
//
// Binary RLE:
// [count: 2 bytes][colour: 2 bytes]
void write_run(FILE *output, uint16_t colour, uint32_t count, uint32_t *runs_written) {
    // count is stored as uint16_t, so the biggest value is 65535.
    // If a run is bigger than that, split it into smaller runs.
    while (count > 0) {
        uint16_t chunk;

        if (count > MAX_RUN) {
            chunk = MAX_RUN;
        } else {
            chunk = (uint16_t)count;
        }

        write_u16_le(output, chunk);
        write_u16_le(output, colour);

        (*runs_written)++;
        count -= chunk;
    }
}

// -------------------- CSV reader --------------------

// Reads the next RGB565 value from the CSV file.
//
// Accepts values like:
// 0x0000
// 0000
// 0xFFFF
//
// It skips commas, spaces, tabs, and new lines.
int read_next_pixel(FILE *input, uint16_t *pixel) {
    char token[32];
    int token_index = 0;
    int c;

    // Skip separators before the next value.
    do {
        c = fgetc(input);

        if (c == EOF) {
            return 0; // End of file
        }
    } while (c == ',' || isspace(c));

    // Read the hex token until the next separator.
    while (c != EOF && c != ',' && !isspace(c)) {
        if (token_index >= (int)sizeof(token) - 1) {
            printf("Error: pixel token is too long.\n");
            return -1;
        }

        token[token_index++] = (char)c;
        c = fgetc(input);
    }

    token[token_index] = '\0';

    char *end_pointer;
    unsigned long value = strtoul(token, &end_pointer, 16);

    if (*end_pointer != '\0' || value > 0xFFFF) {
        printf("Error: invalid RGB565 value: %s\n", token);
        return -1;
    }

    *pixel = (uint16_t)value;
    return 1;
}

// -------------------- RLE encoder --------------------

int encode_file(FILE *input, FILE *output) {
    uint16_t current_val;
    uint16_t next_val;

    uint32_t count = 1;
    uint32_t pixels_read = 0;
    uint32_t runs_written = 0;

    // Read the first pixel first.
    // This avoids needing a fake sentinel value like -1.
    int result = read_next_pixel(input, &current_val);

    if (result == 0) {
        printf("Error: input file is empty.\n");
        return 1;
    }

    if (result < 0) {
        return 1;
    }

    pixels_read = 1;

    // Read the rest of the pixels.
    while ((result = read_next_pixel(input, &next_val)) == 1) {
        pixels_read++;

        if (next_val == current_val) {
            count++;
        } else {
            // Colour changed, so write the completed run.
            write_run(output, current_val, count, &runs_written);

            current_val = next_val;
            count = 1;
        }
    }

    if (result < 0) {
        return 1;
    }

    // Write the final run because there is no next value to trigger it.
    write_run(output, current_val, count, &runs_written);

    printf("Done!\n");
    printf("Pixels read:  %u / %u\n", pixels_read, TOTAL_PIXELS);
    printf("Runs written: %u\n", runs_written);

    if (pixels_read != TOTAL_PIXELS) {
        printf("Warning: expected %u pixels but read %u pixels.\n",
               TOTAL_PIXELS,
               pixels_read);
    }

    return 0;
}

// -------------------- Main --------------------

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <input.csv> [output.rle]\n", argv[0]);
        return 1;
    }

    char output_path[MAX_PATH_SIZE];

    // If the user gives an output path, use it.
    // The Makefile will normally provide:
    // data/Encoded/image_name.rle
    if (argc >= 3) {
        strncpy(output_path, argv[2], sizeof(output_path) - 1);
        output_path[sizeof(output_path) - 1] = '\0';
    } else {
        build_default_output_path(argv[1], output_path, sizeof(output_path));
    }

    FILE *input = fopen(argv[1], "r");

    if (input == NULL) {
        printf("Error: could not open input file: %s\n", argv[1]);
        return 1;
    }

    FILE *output = fopen(output_path, "wb");

    if (output == NULL) {
        printf("Error: could not create output file: %s\n", output_path);
        fclose(input);
        return 1;
    }

    printf("Compressing:\n");
    printf("Input:  %s\n", argv[1]);
    printf("Output: %s\n\n", output_path);

    int status = encode_file(input, output);

    fclose(input);
    fclose(output);

    return status;
}