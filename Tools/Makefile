# Purpose:
# Build the RLE encoder and convert RGB565 CSV files into binary .rle files.
#
# Folder structure expected:
#
# repo/
# ├── Tools/
# │   ├── Makefile
# │   └── rle.c
# └── data/
#     ├── images_135x240/
#     │   ├── image1.csv
#     │   └── image2.csv
#     └── Encoded/
#
# How to use from the repo root:
#
# Compile only:
#   make -C Tools compile
#
# Encode one specific file:
#   make -C Tools run FILE=../data/images_135x240/miku1_135x240_fullscreen.csv
#
# Encode all CSV files:
#   make -C Tools batch
#
# Remove generated files:
#   make -C Tools clean


# C compiler to use
CC = gcc

# Compiler warning/settings flags
CFLAGS = -Wall -Wextra -std=c11

# Source C file
SRC = rle.c

# Output encoder program
# This is stored in /tmp so it does not leave rle.exe inside Tools/
# when you switch back to the main branch.
ENCODER = /tmp/rle_encoder.exe

# Folder containing input CSV images
INPUT_DIR = ../data/images_135x240

# Folder where output RLE files will be saved
OUTPUT_DIR = ../data/Encoded


# These are command names, not real files.
.PHONY: compile run batch clean


# -------------------- Compile encoder --------------------
# Builds rle.c into the encoder program.

compile:
	$(CC) $(CFLAGS) $(SRC) -o $(ENCODER)


# -------------------- Run on one file --------------------
# Usage:
# make -C Tools run FILE=../data/images_135x240/image_name.csv

run: compile
	@if [ -z "$(FILE)" ]; then \
		echo "Error: No FILE specified."; \
		echo "Usage:"; \
		echo "  make -C Tools run FILE=../data/images_135x240/image_name.csv"; \
		exit 1; \
	fi
	mkdir -p $(OUTPUT_DIR)
	$(ENCODER) "$(FILE)" "$(OUTPUT_DIR)/$(basename $(notdir $(FILE))).rle"


# -------------------- Run on batch --------------------
# Converts every .csv file in ../data/images_135x240
# into a .rle file in ../data/Encoded.

batch: compile
	mkdir -p $(OUTPUT_DIR)
	@for file in $(INPUT_DIR)/*.csv; do \
		if [ ! -e "$$file" ]; then \
			echo "No CSV files found in $(INPUT_DIR)"; \
			exit 1; \
		fi; \
		name=$$(basename "$$file" .csv); \
		echo "Encoding $$file -> $(OUTPUT_DIR)/$$name.rle"; \
		$(ENCODER) "$$file" "$(OUTPUT_DIR)/$$name.rle"; \
	done
	@echo "Finished encoding all images."


# -------------------- Clean generated files --------------------
# Removes the compiled encoder and generated .rle files.

clean:
	rm -f $(ENCODER)
#	rm -f $(OUTPUT_DIR)/*.rle