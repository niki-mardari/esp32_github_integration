# Purpose:
# Build the binary RLE encoder and encode all 135x240 RGB565 CSV images.
#
# Input folder:
# data/images_135x240
#
# Output folder:
# data/Encoded
#
# Example:
# data/images_135x240/miku1_135x240_cover.csv
#
# becomes:
# data/Encoded/miku1_135x240_cover.rle

CC = gcc
CFLAGS = -Wall -Wextra -std=c11

# Find the folder where this Makefile is stored.
TOOLS_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Repo root is one folder above Tools.
ROOT_DIR := $(abspath $(TOOLS_DIR)/..)

SRC = $(TOOLS_DIR)/rle.c
ENCODER = $(TOOLS_DIR)/rle.exe

IMAGE_DIR = $(ROOT_DIR)/data/images_135x240
ENCODED_DIR = $(ROOT_DIR)/data/Encoded

CSV_FILES = $(wildcard $(IMAGE_DIR)/*.csv)

RLE_FILES = $(patsubst $(IMAGE_DIR)/%.csv,$(ENCODED_DIR)/%.rle,$(CSV_FILES))

.PHONY: all build encode clean list

all: encode

# -------------------- Build encoder --------------------

build: $(ENCODER)

$(ENCODER): $(SRC)
	$(CC) $(CFLAGS) "$(SRC)" -o "$(ENCODER)"

# -------------------- Create output directory --------------------

$(ENCODED_DIR):
	@if not exist "$(ENCODED_DIR)" mkdir "$(ENCODED_DIR)"

# -------------------- Encode all images --------------------

encode: $(ENCODER) $(RLE_FILES)
	@echo Finished encoding all images.

# Each CSV file becomes one RLE file with the same base name.
#
# Example:
# data/images_135x240/coke_135x240_cover.csv
#
# becomes:
# data/Encoded/coke_135x240_cover.rle
$(ENCODED_DIR)/%.rle: $(IMAGE_DIR)/%.csv $(ENCODER) | $(ENCODED_DIR)
	"$(ENCODER)" "$<" "$@"

# -------------------- Debug/list files --------------------

list:
	@echo CSV input files:
	@echo $(CSV_FILES)
	@echo.
	@echo RLE output files:
	@echo $(RLE_FILES)

# -------------------- Clean generated files --------------------

clean:
	@if exist "$(ENCODER)" del /Q "$(ENCODER)"
	@if exist "$(ENCODED_DIR)" del /Q "$(ENCODED_DIR)\*.rle"