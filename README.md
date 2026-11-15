# MixieLand

**MixieLang — simple, yet so elegant in power.**

MixieLand is the development suite. MixieLang is its programming language, and Builgine will become its build orchestration system.

## Rust rewrite

The active implementation is being rewritten in Rust. The legacy C/Flex/Bison interpreter is being retired rather than kept as a runtime dependency.

### Requirements
- Rust stable toolchain (Cargo included)

### Build and run
```sh
cargo build --release
cargo run --release -- -f examples/hello.mx
```

Or run source from standard input:
```sh
printf 'print "Hello, World!";\n' | cargo run --quiet
```

### Current implementation
- Rust CLI, lexer, parser, AST, and interpreter modules
- Quoted strings and MixieLang line, block, and metadata comments
- Existing `print "..." ;` prototype compatibility
- Initial `.mxpg` container boundary validation
- Automated integration tests

The canonical grammar, Beg/Adv/AdvN modes, package/import handling, shared IR, and CIDDAT toolchain are next migration stages.

## Layout
- `src/` — Rust implementation
- `tests/` — integration tests
- `examples/` — sample programs
- `playground/` — browser playground (Rust/WASM port follows after core stabilization)
- `docs/` — architecture and language specifications

## License
GPL-3.0-only.
