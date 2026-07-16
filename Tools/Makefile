# Purpose:
# Build the binary RLE encoder and encode all 135x240 RGB565 CSV images.
#
# Input folder:
# data/images_135x240
#
# Output folder:
# data/Encoded

CC = gcc
CFLAGS = -Wall -Wextra -std=c11

# Find the folder where this Makefile is stored.
TOOLS_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Repo root is one folder above Tools.
ROOT_DIR := $(abspath $(TOOLS_DIR)/..)

# Safety check:
# This Makefile should only be run on the image-data branch.
CURRENT_BRANCH := $(shell git -C "$(ROOT_DIR)" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)

ifeq ($(CURRENT_BRANCH),unknown)
$(error Could not detect Git branch. Make sure Git is installed in MSYS2 and you are inside the repo)
endif

ifneq ($(CURRENT_BRANCH),image-data)
$(error This Makefile must only be run on the image-data branch. Current branch is $(CURRENT_BRANCH))
endif

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
	mkdir -p "$(ENCODED_DIR)"

# -------------------- Encode all images --------------------

encode: $(ENCODER) $(RLE_FILES)
	@echo Finished encoding all images.

$(ENCODED_DIR)/%.rle: $(IMAGE_DIR)/%.csv $(ENCODER) | $(ENCODED_DIR)
	"$(ENCODER)" "$<" "$@"

# -------------------- Debug/list files --------------------

list:
	@echo Current branch:
	@echo $(CURRENT_BRANCH)
	@echo Repo root:
	@echo $(ROOT_DIR)
	@echo Tools dir:
	@echo $(TOOLS_DIR)
	@echo CSV input files:
	@echo $(CSV_FILES)
	@echo.
	@echo RLE output files:
	@echo $(RLE_FILES)

# -------------------- Clean generated files --------------------

clean:
	rm -f "$(ENCODER)"
	rm -f "$(ENCODED_DIR)"/*.rle