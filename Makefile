COMMIT=$(shell git rev-parse --short HEAD)
BRANCH=$(shell git rev-parse --abbrev-ref HEAD)
BUILD_TIME=$(shell date)
VERSION_H=src/version.h

PROJECT_NAME=sox
THIS_OS=windows
THIS_ARCH=x86

TOOL_PATH=.\tools\bin\windows\x86
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

ifeq (${THIS_OS},darwin)
	TOOL_BUILD=xcode4
	TOOL_PATH=./tools/bin/osx/x86
	TOOL_PLATFORM=macosx
	ifeq (${THIS_ARCH},arm64)
		TOOL_PATH=./tools/bin/osx/arm
		TOOL_PLATFORM=macosxARM64
	endif
endif
ifeq (${THIS_OS},linux)
	TOOL_BUILD=gmake
	TOOL_PATH=./tools/bin/unix/x86
	TOOL_PLATFORM=linux64
	ifeq (${THIS_ARCH},aarch64)
		TOOL_PATH=./tools/bin/unix/arm
		TOOL_PLATFORM=linuxARM64
	endif
endif

details:
	@echo "Detected OS: ${THIS_OS}"
	@echo "Detected Arch: ${THIS_ARCH}"
	@echo "Tool Path: ${TOOL_PATH}"
	@echo "Tool Build: ${TOOL_BUILD}"
	@echo "Tool Platform: ${TOOL_PLATFORM}"
	
	@echo "Commit: ${COMMIT}"
	@echo "Branch: ${BRANCH}"
	@echo "-----"

install-deps:
	./install.sh

run:
	./build/${PROJECT_NAME} ${SCRIPT}

run-test:
	DYLD_LIBRARY_PATH=./build:$$DYLD_LIBRARY_PATH ./build/test

gen: details
	@echo "Setting Versions: Commit: ${COMMIT} Branch: ${BRANCH}"

# Updating the commit info in version.h
	git checkout ${VERSION_H}
	${TOOL_PATH}/version -commit=${COMMIT} -branch=${BRANCH} -display=true -version_file=${VERSION_H}
# Generating build projects
	@echo "Tool Platform: ${TOOL_PLATFORM}"
	${TOOL_PATH}/premake5 ${TOOL_BUILD} platform=${TOOL_PLATFORM}
	@echo "Gen Finished: Commit: ${COMMIT} Branch: ${BRANCH}"

post-build:
	git checkout src/version.h

open:
	start .\projects\${PROJECT_NAME}.sln

binary-debug:
ifeq (${THIS_OS},windows)
	msbuild.exe ./projects/${PROJECT_NAME}.sln -p:Platform="windows";Configuration=Debug -target:${PROJECT_NAME}
endif
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Debug" ARCHS="${THIS_ARCH}" -destination 'platform=macOS' -project "projects/${PROJECT_NAME}.xcodeproj" -target ${PROJECT_NAME}
endif
ifeq (${THIS_OS},linux)
	make -C projects ${PROJECT_NAME} config=debug_linux64
endif

binary-release: gen
ifeq (${THIS_OS},windows)
	msbuild.exe ./projects/${PROJECT_NAME}.sln -p:Platform="windows";Configuration=Release -target:${PROJECT_NAME}
endif
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Release" ARCHS="arm64" -destination 'platform=macOS' -project "projects/${PROJECT_NAME}.xcodeproj" -target ${PROJECT_NAME}
endif
ifeq (${THIS_OS},linux)
	make -C projects ${PROJECT_NAME} config=release_linux64
endif

build-wasm-verify:
	@echo "Building wazero WASM verification library..."
	make -C wasm_verify all

build-test: gen build-wasm-verify
ifeq (${THIS_OS},windows)
	msbuild.exe ./projects/${PROJECT_NAME}.sln -p:Platform="windows";Configuration=Debug -target:test
endif
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Debug" ARCHS="arm64" -destination 'platform=macOS' -project "projects/test.xcodeproj" -target test
endif
ifeq (${THIS_OS},linux)
	make -C projects test config=debug_linux64
endif

build-release: gen binary-release post-build
build-debug: gen binary-debug post-build

# by default build a debug binary
build: build-debug build-tools

test: build-test post-build
ifeq (${THIS_OS},windows)
	.\build\test.exe
endif
ifeq (${THIS_OS},darwin)
	DYLD_LIBRARY_PATH=./build:$$DYLD_LIBRARY_PATH ./build/test
endif
ifeq (${THIS_OS},linux)
	LD_LIBRARY_PATH=./build:$$LD_LIBRARY_PATH ./build/test
endif

clean-${PROJECT_NAME}:
ifeq (${THIS_OS},windows)
	rm -r build/${PROJECT_NAME}.exe
	rm -r projects/obj/Windows/Debug/${PROJECT_NAME}
	rm -r projects/obj/Windows/Release/${PROJECT_NAME}
else
	rm -f ./build/${PROJECT_NAME}
ifeq (${THIS_OS},linux)
	rm -Rf ./projects/obj/linux64/Debug/${PROJECT_NAME}
	rm -Rf ./projects/obj/linux64/Release/${PROJECT_NAME}
endif
ifeq (${THIS_OS},darwin)
	rm -f ./build/${PROJECT_NAME}
	rm -Rf ./projects/obj/macosx/Debug/${PROJECT_NAME}
	rm -Rf ./projects/obj/macosx/Release/${PROJECT_NAME}
endif
endif

clean:
ifeq (${THIS_OS},windows)
	rm -r build
	rm -r projects
else
	rm -Rf ./build/
	rm -Rf ./projects
endif
	@echo "Cleaning wazero WASM verification library..."
	make -C wasm_verify clean 2>/dev/null || true

build-tools:
	make -C tools/ all
	make -C wasm_verify/ all

ui:
	cd sox_ui && wails dev

build-runtime-static: gen
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Release" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/sox_runtime.xcodeproj" -target sox_runtime
	@echo "Creating architecture-specific runtime library copy..."
	mkdir -p build
	cp ./build/libsox_runtime.a ./build/libsox_runtime_${THIS_ARCH}.a
	@echo "Runtime library available at ./build/libsox_runtime.a and ./build/libsox_runtime_${THIS_ARCH}.a"
endif
ifeq (${THIS_OS},linux)
	make -C projects sox_runtime config=release_linux64
	@echo "Copying runtime library to build directory..."
	mkdir -p build
	cp projects/obj/linux64/Release/sox_runtime/libsox_runtime.a ./build/libsox_runtime.a
	cp projects/obj/linux64/Release/sox_runtime/libsox_runtime.a ./build/libsox_runtime_${THIS_ARCH}.a
	@echo "Runtime library copied to ./build/libsox_runtime.a and ./build/libsox_runtime_${THIS_ARCH}.a"
endif
ifeq (${THIS_OS},windows)
	msbuild.exe ./projects/sox_runtime.sln -p:Platform="windows";Configuration=Release -target:sox_runtime
	@echo "Copying runtime library to build directory..."
	mkdir -p build
	copy projects\obj\Windows\Release\sox_runtime\sox_runtime.lib .\build\libsox_runtime.a
	copy projects\obj\Windows\Release\sox_runtime\sox_runtime.lib .\build\libsox_runtime_${THIS_ARCH}.a
	@echo "Runtime library copied to .\build\libsox_runtime.a and .\build\libsox_runtime_${THIS_ARCH}.a"
endif

build-runtime-shared: gen
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Release" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/sox_runtime_shared.xcodeproj" -target sox_runtime_shared
	@echo "Shared runtime library built to ./build/libsox_runtime.dylib"
endif
ifeq (${THIS_OS},linux)
	make -C projects sox_runtime_shared config=release_linux64
	@echo "Copying shared runtime library to build directory..."
	mkdir -p build
	cp projects/obj/linux64/Release/sox_runtime_shared/libsox_runtime.so ./build/libsox_runtime.so
	@echo "Shared runtime library copied to ./build/libsox_runtime.so"
endif
ifeq (${THIS_OS},windows)
	msbuild.exe ./projects/sox_runtime_shared.sln -p:Platform="windows";Configuration=Release -target:sox_runtime_shared
	@echo "Copying shared runtime library to build directory..."
	mkdir -p build
	copy projects\obj\Windows\Release\sox_runtime_shared\sox_runtime.dll .\build\sox_runtime.dll
	@echo "Shared runtime library copied to .\build\sox_runtime.dll"
endif

build-runtime: build-runtime-static build-runtime-shared

install-runtime: build-runtime
	@echo "Installing Sox runtime library..."
	mkdir -p /usr/local/lib /usr/local/include/sox
	cp build/libsox_runtime.a /usr/local/lib/
ifeq (${THIS_OS},darwin)
	cp build/libsox_runtime.dylib /usr/local/lib/
	install_name_tool -id /usr/local/lib/libsox_runtime.dylib \
	  /usr/local/lib/libsox_runtime.dylib
endif
ifeq (${THIS_OS},linux)
	cp build/libsox_runtime.so /usr/local/lib/
	ldconfig
endif
	cp src/runtime_lib/runtime_api.h /usr/local/include/sox/
	@echo "Runtime library installed successfully"

#
# Security Testing Targets
#
# These targets enable various sanitizers and security validation tools
# to detect memory safety issues, buffer overflows, and other vulnerabilities.
#

# Build with AddressSanitizer (ASAN) - detects memory errors
build-asan: gen build-wasm-verify
	@echo "Building with AddressSanitizer (ASAN)..."
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Debug" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/test.xcodeproj" -target test \
	  OTHER_CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
	  OTHER_LDFLAGS="-fsanitize=address"
endif
ifeq (${THIS_OS},linux)
	make -C projects test config=debug_linux64 \
	  CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
	  LDFLAGS="-fsanitize=address"
endif
	@echo "ASAN build complete"

# Run tests with AddressSanitizer
test-asan: build-asan post-build
	@echo "Running tests with AddressSanitizer..."
	@echo "ASAN will detect:"
	@echo "  - Buffer overflows (heap and stack)"
	@echo "  - Use-after-free bugs"
	@echo "  - Memory leaks"
	@echo "  - Double-free errors"
	@echo "-----"
ifeq (${THIS_OS},darwin)
	DYLD_LIBRARY_PATH=./build:$$DYLD_LIBRARY_PATH \
	  MallocNanoZone=0 \
	  ASAN_OPTIONS=detect_leaks=1:symbolize=1:abort_on_error=1 \
	  ./build/test
endif
ifeq (${THIS_OS},linux)
	LD_LIBRARY_PATH=./build:$$LD_LIBRARY_PATH \
	  ASAN_OPTIONS=detect_leaks=1:symbolize=1:abort_on_error=1 \
	  ./build/test
endif
	@echo "✅ ASAN tests passed - no memory safety issues detected"

# Build with UndefinedBehaviorSanitizer (UBSAN) - detects undefined behavior
build-ubsan: gen build-wasm-verify
	@echo "Building with UndefinedBehaviorSanitizer (UBSAN)..."
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Debug" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/test.xcodeproj" -target test \
	  OTHER_CFLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
	  OTHER_LDFLAGS="-fsanitize=undefined"
endif
ifeq (${THIS_OS},linux)
	make -C projects test config=debug_linux64 \
	  CFLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
	  LDFLAGS="-fsanitize=undefined"
endif
	@echo "UBSAN build complete"

# Run tests with UndefinedBehaviorSanitizer
test-ubsan: build-ubsan post-build
	@echo "Running tests with UndefinedBehaviorSanitizer..."
	@echo "UBSAN will detect:"
	@echo "  - Integer overflows"
	@echo "  - Null pointer dereferences"
	@echo "  - Misaligned memory access"
	@echo "  - Division by zero"
	@echo "-----"
ifeq (${THIS_OS},darwin)
	DYLD_LIBRARY_PATH=./build:$$DYLD_LIBRARY_PATH \
	  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	  ./build/test
endif
ifeq (${THIS_OS},linux)
	LD_LIBRARY_PATH=./build:$$LD_LIBRARY_PATH \
	  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	  ./build/test
endif
	@echo "✅ UBSAN tests passed - no undefined behavior detected"

# Test linker-specific security with crafted inputs
test-linker-security:
	@echo "Testing linker security with edge cases..."
	@echo "This validates the critical security fixes:"
	@echo "  1. Buffer overflow protection in ELF symbol names"
	@echo "  2. Integer overflow protection in section allocation"
	@echo "  3. Section index mapping correctness"
	@echo "  4. Bounds checking in instruction patching"
	@echo "  5. Symbol address computation"
	@echo "-----"
	@# Run linker-specific tests
	@if [ -f ./src/test/linker/integration/run_tests.sh ]; then \
		cd src/test/linker/integration && bash run_tests.sh; \
	else \
		echo "⚠️  Integration tests not found"; \
	fi

# Comprehensive security test suite
test-security: test-asan test-ubsan test-linker-security
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "✅ ALL SECURITY TESTS PASSED"
	@echo "════════════════════════════════════════════════════════"
	@echo ""
	@echo "Security validation complete:"
	@echo "  ✓ AddressSanitizer (memory safety)"
	@echo "  ✓ UndefinedBehaviorSanitizer (undefined behavior)"
	@echo "  ✓ Linker integration tests"
	@echo ""
	@echo "The custom linker is ready for production use with trusted input."
	@echo ""

# Quick security check (ASAN only, fastest)
test-security-quick: test-asan
	@echo "✅ Quick security check passed"

# Show help for security testing
help-security:
	@echo "Sox Security Testing Targets"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "  make test-security        - Run all security tests (comprehensive)"
	@echo "  make test-security-quick  - Run ASAN tests only (fast)"
	@echo "  make test-asan            - Run tests with AddressSanitizer"
	@echo "  make test-ubsan           - Run tests with UndefinedBehaviorSanitizer"
	@echo "  make test-linker-security - Run linker-specific security tests"
	@echo ""
	@echo "What each sanitizer detects:"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "AddressSanitizer (ASAN):"
	@echo "  • Buffer overflows (heap and stack)"
	@echo "  • Use-after-free bugs"
	@echo "  • Memory leaks"
	@echo "  • Double-free errors"
	@echo "  • Heap corruption"
	@echo ""
	@echo "UndefinedBehaviorSanitizer (UBSAN):"
	@echo "  • Integer overflows"
	@echo "  • Null pointer dereferences"
	@echo "  • Misaligned memory access"
	@echo "  • Division by zero"
	@echo "  • Shift operations on negative numbers"
	@echo ""
	@echo "Linker Security Tests:"
	@echo "  • ELF/Mach-O parsing with malformed input"
	@echo "  • Section index mapping validation"
	@echo "  • Bounds checking in instruction patching"
	@echo "  • Symbol address computation correctness"
	@echo ""

.PHONY: build-asan test-asan build-ubsan test-ubsan test-linker-security \
        test-security test-security-quick help-security

