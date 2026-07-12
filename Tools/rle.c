// Programmer: Niki Mardari
// Date: 2024-06-10
// Purpose: This tool should compress 240x320 csv file images to RLE format

// Need to write headers in written file that tell it checksum
// Binary RLE 2bytes count and 2 bytes color is apparently more efficient. 

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

FILE* fptr_read;
FILE* fptr_write;

//void itob(uint64_t count);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]); // Could use printf or fprintf to standard output (Revision)
        return 1;
    } 
    else {

        // Currently size of forge_240x320.csv, need to get it to automatically get size of imputted file!

        fprintf(stdout, "Compressing %s to RLE format...\n", argv[1]);
        fptr_read = fopen(argv[1], "r");

        // Check if the file was opened successfully
        if (!fptr_read) {
            fprintf(stderr, "Error: Could not open file! %s\n", argv[1]);
            return 1;
        }
        fptr_write = fopen("rle.txt", "w");

    // checking if the file is created
    if (!fptr_write) 
        fprintf(stderr, "Error: Could not create file rle.txt!\n");
    else 
        fprintf(stdout, "The file is created Successfully.\n");

        // RLE logic:

        uint16_t current_val = 0;
        // Read the first hex value from the file
        if (fscanf(fptr_read, "%x,", &current_val) != 1) {
            fclose(fptr_read);
            return 0; // File is empty or improperly formatted
        }

        uint32_t count = 1; // Initialize first hex count
        uint16_t next_val;

    // Loop through the rest of the text file token by token
    while (fscanf(fptr_read, "%x,", &next_val) == 1) {
        if (next_val == current_val) {
            count++;
        } 
        else {
            // Delimiter/Value boundary hit! Print the sequence.
            // printf("%d*0x%04X, ", count, current_val);

            fprintf(fptr_write, "%d*0x%04X\n", count, current_val);

            // Reset values and start agin
            current_val = next_val;
            count = 1;
        }
    }
        // printf("%d*0x%04X\n", count, current_val); // Print the final tracking sequence
        fclose(fptr_read); // Close the file after reading
        fclose(fptr_write); // Close the output file

    }
    return 0;
}
/*
void itob(uint64_t count){
    for (int i = 63; i >= 0; i--) {
        int bit = (count >> i) & 1;
        printf("%d", bit);
    }
    printf("\n");
}
*/
