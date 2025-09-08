# Sox Programming Language Interpreter

Sox is a C-based programming language interpreter implementation inspired by the book "Crafting Interpreters". It features a bytecode virtual machine with comprehensive testing, cross-platform support, and serialization capabilities.

Always reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.

## Working Effectively

### Bootstrap, Build, and Test the Repository
Run these commands in sequence to set up a complete development environment:

- `chmod +x install.sh`
- `./install.sh` -- installs system dependencies. Takes ~3 minutes. NEVER CANCEL. Set timeout to 10+ minutes.
- `make build-tools` -- builds Go-based version management tools. Takes ~30 seconds. NEVER CANCEL. Set timeout to 5+ minutes.
- `make build` -- builds debug version of sox interpreter. Takes ~4 seconds. NEVER CANCEL. Set timeout to 5+ minutes.
- `make test` -- builds and runs complete test suite (54 tests). Takes ~13 seconds. NEVER CANCEL. Set timeout to 5+ minutes.

### Build Variants
- `make build-debug` -- builds debug version (default for `make build`)
- `make build-release` -- builds optimized release version. Takes ~3 seconds. NEVER CANCEL. Set timeout to 5+ minutes.
- `make clean` -- removes build artifacts and generated project files

### Run the Interpreter
- **Script execution**: `./build/sox path/to/script.sox`
- **REPL mode**: `./build/sox` (starts interactive shell, use Ctrl+C to exit)
- **Test with sample**: `./build/sox src/test/scripts/hello.sox` (should output "hello world")

## Validation

### Critical Manual Validation Steps
ALWAYS run through these complete end-to-end scenarios after making changes:

1. **Build validation**: Ensure clean build succeeds
   ```bash
   make clean && make build-tools && make build && make test
   ```

2. **Interpreter functionality**: Test basic script execution
   ```bash
   ./build/sox src/test/scripts/hello.sox
   ./build/sox src/test/scripts/control.sox
   ```

3. **REPL validation**: Start REPL and test simple expressions (manual validation)
   ```bash
   ./build/sox
   # In REPL, test basic operations (exit with Ctrl+C)
   ```

### Test Suite Validation
- All 54 tests must pass including unit tests and script validation tests
- Tests include: VM lifecycle, bytecode operations, serialization, and complete script execution
- Script tests validate against expected output files (*.sox.out)

### Build System Dependencies
- **Linux**: libx11-dev, clang, g++, libxcomposite-dev, libxi-dev, libxcursor-dev, libgl-dev, libasound2-dev
- **Go**: Required for building version management tools
- **Git**: Used for version information during build

## Common Tasks

### Repository Structure
```
sox/
├── .github/workflows/    # CI/CD configuration
├── src/                 # C source code
│   ├── lib/            # Utility libraries (debug, file, memory, etc.)
│   ├── test/           # Unit tests and test scripts
│   └── *.c, *.h        # Core interpreter implementation
├── tools/              # Go-based build tools
├── ext/                # External dependencies (munit, winstd)
├── docs/               # Documentation
├── Makefile            # Build system
├── premake5.lua        # Build configuration
└── install.sh          # Dependency installation script
```

### Key Files and Components
- `src/main.c` -- Main entry point for interpreter
- `src/vm.c` -- Virtual machine implementation
- `src/compiler.c` -- Sox language compiler
- `src/test/scripts/` -- Test scripts with expected outputs
- `tools/src/version/main.go` -- Version management tool
- `.github/workflows/ubuntu-build.yml` -- CI configuration

### Build System Details
- Uses **Premake5** to generate platform-specific build files
- Supports Windows (Visual Studio), macOS (Xcode), and Linux (gmake)
- Cross-compilation support for x86_64 and ARM architectures
- Automatic version stamping using Git commit information

### Expected Timings (Linux x86_64)
- Dependency installation: ~3 minutes
- Tool build: ~30 seconds  
- Main build (debug): ~4 seconds
- Main build (release): ~3 seconds
- Test suite: ~13 seconds
- Complete rebuild cycle: ~2.6 seconds (after tools built)

### Platform Detection
Use `make details` to see detected platform information:
```
Detected OS: linux
Detected Arch: x86_64
Tool Path: ./tools/bin/unix/x86
Tool Build: gmake
Tool Platform: linux64
```

### Sample Scripts Available for Testing
Located in `src/test/scripts/`:
- `hello.sox` -- Basic print statement
- `control.sox` -- Conditional statements and logic
- `loops.sox` -- Loop constructs
- `classes.sox` -- Object-oriented features
- `closure.sox` -- Function closures
- `table.sox` -- Built-in table data structure
- `switch.sox` -- Switch statement functionality
- `defer.sox` -- Defer statement behavior

### Language Features
Sox includes modern language features:
- Classes and inheritance
- Closures and first-class functions
- Built-in data structures (tables, lists)
- Switch statements with break/continue
- Defer statements
- Optional semicolons
- Bytecode serialization
- REPL environment

### CI/CD Integration
- GitHub Actions builds on Ubuntu and macOS
- Build validation includes both compilation and full test suite
- Uses same commands as local development: `make build-tools && make build && make test`