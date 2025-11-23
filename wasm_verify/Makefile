# Makefile for building wasm_verify shared library

# Detect OS
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
UNAME_M := $(shell uname -m 2>/dev/null || echo unknown)

# Library name and output
LIB_NAME = libwasm_verify
OUTPUT_DIR = ../build

# OS-specific settings
ifeq ($(UNAME_S),Linux)
    LIB_EXT = so
    LIB_FLAGS = -shared
    PLATFORM = linux
    # Match the architecture from the main build
    ifeq ($(UNAME_M),aarch64)
        GOARCH = arm64
    else
        GOARCH = amd64
    endif
else ifeq ($(UNAME_S),Darwin)
    LIB_EXT = dylib
    LIB_FLAGS = -dynamiclib
    PLATFORM = darwin
    # Force x86_64 to match the C build (premake5.lua defaults to x86_64)
    GOARCH = amd64
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    LIB_EXT = dll
    LIB_FLAGS = -shared
    PLATFORM = windows
    GOARCH = amd64
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    LIB_EXT = dll
    LIB_FLAGS = -shared
    PLATFORM = windows
    GOARCH = amd64
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    LIB_EXT = dll
    LIB_FLAGS = -shared
    PLATFORM = windows
    GOARCH = amd64
else ifeq ($(UNAME_S),Windows)
    LIB_EXT = dll
    LIB_FLAGS = -shared
    PLATFORM = windows
    GOARCH = amd64
else
    $(error Unsupported platform: $(UNAME_S). Supported: Linux, Darwin, Windows (MINGW/MSYS/Cygwin))
endif

LIB_FILE = $(OUTPUT_DIR)/$(LIB_NAME).$(LIB_EXT)

# Go build flags
GO_BUILD_FLAGS = -buildmode=c-shared

# Set Go OS for cross-compilation
ifeq ($(PLATFORM),linux)
    GOOS = linux
else ifeq ($(PLATFORM),darwin)
    GOOS = darwin
else ifeq ($(PLATFORM),windows)
    GOOS = windows
endif

.PHONY: all clean deps test

all: deps $(LIB_FILE)

deps:
	@echo "Downloading Go dependencies..."
	go mod download
	go mod verify

$(LIB_FILE): verify.go
	@echo "Building $(LIB_NAME) for $(PLATFORM) (GOARCH=$(GOARCH))..."
	@mkdir -p $(OUTPUT_DIR)
	GOOS=$(GOOS) GOARCH=$(GOARCH) CGO_ENABLED=1 go build $(GO_BUILD_FLAGS) -o $(LIB_FILE) verify.go
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
