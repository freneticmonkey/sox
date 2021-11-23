# sox
variation of the implementation of the sox interpreter as described on http://craftinginterpreters.com

## Build Status

| Platform |  Build |
|----------|-------|
| Ubuntu | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/ubuntu-build.yml/badge.svg) |
<!-- | Windows | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/windows-build.yml/badge.svg) | -->
<!-- | macOS | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/macos-build.yml/badge.svg) | -->

### Tests TODO:
- [x] Run each script in the scripts folder without errors
- [ ] Script output validation
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
- [ ] Switch statements
- [ ] unit test functions
- [ ] impl break
- [ ] impl continue
- [ ] native container implementation
- [ ] argc / argv parameters to main
- [ ] for-in container iteration
- [ ] run scripts from repl
- [ ] default parameter values
- [ ] remove global variables
- [ ] multi-line comment blocks
- [ ] load multiple scripts

## Advanced language features
- [ ] interfaces
- [ ] coroutines
- [ ] Debugger?


## Things to better understand
- Rule parsing - precidence logic
- max number of up values (256) - per function or program?
- 