# Makefile for building wasm_verify shared library

# Detect OS
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Library name and output
LIB_NAME = libwasm_verify
OUTPUT_DIR = ../build

# OS-specific settings
ifeq ($(UNAME_S),Linux)
    LIB_EXT = so
    LIB_FLAGS = -shared
    PLATFORM = linux
endif
ifeq ($(UNAME_S),Darwin)
    LIB_EXT = dylib
    LIB_FLAGS = -dynamiclib
    PLATFORM = darwin
endif

LIB_FILE = $(OUTPUT_DIR)/$(LIB_NAME).$(LIB_EXT)

# Go build flags
GO_BUILD_FLAGS = -buildmode=c-shared

.PHONY: all clean deps test

all: deps $(LIB_FILE)

deps:
	@echo "Downloading Go dependencies..."
	go mod download
	go mod verify

$(LIB_FILE): verify.go
	@echo "Building $(LIB_NAME) for $(PLATFORM) ($(UNAME_M))..."
	@mkdir -p $(OUTPUT_DIR)
	go build $(GO_BUILD_FLAGS) -o $(LIB_FILE) verify.go
	@echo "Built: $(LIB_FILE)"

clean:
	@echo "Cleaning wasm_verify build artifacts..."
	rm -f $(OUTPUT_DIR)/$(LIB_NAME).*
	rm -f $(LIB_NAME).h

test: $(LIB_FILE)
	@echo "Running Go tests..."
	go test -v ./...

install: all
	@echo "Library built at $(LIB_FILE)"
	@echo "Header available at wasm_verify.h"
