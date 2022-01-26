COMMIT=$(shell git rev-parse HEAD)
BRANCH=$(shell git rev-parse --abbrev-ref HEAD)
BUILD_TIME=$(shell date)
VERSION_H=tiny/src/version.h

THIS_OS=windows
THIS_ARCH=x86

TOOL_PATH=.\tools\windows\x86
TOOL_BUILD=vs2019
TOOL_PLATFORM=windows

ifeq ($(OS),Windows_NT)
	THIS_OS=windows
else
	UNAME_S := $(shell uname -s)
	THIS_ARCH=$(shell uname -m)
	ifeq ($(UNAME_S),Linux)
		THIS_OS=linux
	endif
	ifeq ($(UNAME_S),Darwin)
		THIS_OS=darwin
	endif
endif

ifeq (${OS},win)
	THIS_OS=windows
endif
ifeq (${OS},mac)
	THIS_OS=darwin
	TOOL_BUILD=xcode4
	TOOL_PATH=./tools/osx/x86
	TOOL_PLATFORM=macosx
endif
ifeq (${OS},linux)
	THIS_OS=linux
	TOOL_BUILD=gmake
	TOOL_PATH=./tools/unix/x86
	TOOL_PLATFORM=linux64
	ifeq (${THIS_ARCH},aarch64)
		TOOL_PATH=./tools/unix/arm
		TOOL_PLATFORM=linuxARM64
	endif
endif

details:
	@echo "Detected OS: ${THIS_OS}"
	@echo "Detected Arch: ${THIS_ARCH}"
	@echo "Commit: ${COMMIT}"
	@echo "Branch: ${BRANCH}"
	@echo "-----"


build-version:
	@echo 
	@echo Building version - version header generation tool
ifeq (${THIS_OS},windows)
	env GOOS=windows go build -o bin/windows/x86/version.exe src/version/main.go 
endif
ifeq (${THIS_OS},darwin)
	GOOS=darwin go build -o bin/osx/x86/version src/version/main.go 
endif
ifeq (${THIS_OS},linux)
	GOOS=linux go build -o bin/unix/x86/version src/version/main.go 
endif

all: build-version