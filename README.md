# sox
variation of the implementation of the sox interpreter as described on http://craftinginterpreters.com

## Build Status

| Platform |  Build |
|----------|-------|
| Ubuntu | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/ubuntu-build.yml/badge.svg) |
<!-- | Windows | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/windows-build.yml/badge.svg) | -->
<!-- | macOS | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/macos-build.yml/badge.svg) | -->

## Features

### WebAssembly Support
Sox can generate WebAssembly (WASM) binary and WebAssembly Text Format (WAT) output. See [WebAssembly Documentation](docs/wasm.md) for details on supported opcodes and usage.

### Tests TODO:
- [x] Run each script in the scripts folder without errors
- [x] Script output validation
- [ ] Globals
- [ ] Locals
- [ ] String operations
- [ ] Math operations
- [ ] Invalid + - / *
- [ ] invalid assignment - 3 * 4 = 12
- [ ] local var aliasing across scopes
- [ ] duplicate variable definition
- [ ] File parsing

## Future Language features
- [x] Defer
- [x] replace fun with fn
- [x] remove semi-colon line ending
- [x] bracketless expressions
- [x] optional main entrypoint
- [x] Switch statements
- [x] impl break
- [x] impl continue
- [x] argc / argv parameters to main
- [ ] unit test functions
    - [ ] assert keyword + functionality
    - [ ] benchmark option
- [ ] native container implementations
    - [x] table
    - [x] list / slice
    - [ ] helper functions
        - [x] pop/push
        - [x] len
        - [ ] range -> for in range
- [ ] for-in container iteration
- [ ] run scripts from repl
- [ ] default parameter values
- [ ] remove global variables
- [ ] multi-line comment blocks
- [ ] load multiple scripts / modules
- [x] Bytecode scripts

## Advanced language features
- [ ] interfaces
- [ ] coroutines
- [ ] Debugger?


## Things to better understand
- Rule parsing - precidence logic
- max number of up values (256) - per function or program?
- 