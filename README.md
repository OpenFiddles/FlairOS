# MixieLang

Simple, yet so elegant in power.

## Build

Needs `bison`, `flex` and `gcc`.

```
./scripts/compile-interpreter.sh
./bin/mixie-interpreter -f examples/hello.mx
```

Cross-builds use the environment variables `CC`, `OUT` and `FLEX_LIBS` (set `FLEX_LIBS=` when the toolchain has no libfl).

## CI/CD

`.gitlab-ci.yml` builds the interpreter for Linux, Windows (MinGW cross-build) and WebAssembly. The WebAssembly playground in `playground/` is published to GitLab Pages from the default branch. A release tag publishes the binaries as release links. The macOS job is manual and runs only on a macOS runner. Android and iOS come later.

## Layout

- `interpreter_projet/` Flex and Bison sources for the lexer, parser and driver
- `scripts/compile-interpreter.sh` generates the parsers and compiles the interpreter
- `playground/` WebAssembly playground page
- `examples/` sample `.mx` programs

## Status

The grammar is an early prototype. It runs `print "...";` statements and the image block format. The token design (`push`, `pull`, `pixie`, `const`, `glob`, `->`) is not implemented yet.
