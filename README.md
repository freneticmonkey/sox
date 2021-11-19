# sox
variation of the implementation of the lox interpreter as described on http://craftinginterpreters.com

## Build Status

| Platform |  Build |
|----------|-------|
| Ubuntu | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/ubuntu-build.yml/badge.svg) |
<!-- | Windows | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/windows-build.yml/badge.svg) | -->
<!-- | macOS | ![Build](https://github.com/freneticmonkey/sox/actions/workflows/macos-build.yml/badge.svg) | -->

### Tests TODO:
[ ] Globals
[ ] Locals
[ ] String operations
[ ] Math operations
[ ] Invalid + - / *
[ ] invalid assignment - 3 * 4 = 12
[ ] local var aliasing across scopes
[ ] duplicate variable definition
[ ] File parsing
[ ] Run each script in the scripts folder without errors

## Future Language features
[ ] Defer
[ ] Switch statements
[ ] bracketless expressions
[ ] ternary operator
[ ] remove semi-colon line ending
[ ] multi-line comment blocks
[ ] for-in container iteration
[ ] remove global variables
[ ] impl break
[ ] impl continue
[ ] run scripts from repl
[ ] default parameter values

## Advanced language features
[ ] interfaces
[ ] coroutines
[ ] Debugger?


## Things to better understand
- Rule parsing - precidence logic
- max number of up values (256) - per function or program?
- 