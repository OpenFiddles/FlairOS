# MixieLand Rust architecture

- **MixieLand** is the complete language and development suite.
- **MixieLang** is the programming language and its language tooling.
- **Builgine** is the build orchestration, target configuration, external compiler integration, and packaging system.

The intended pipeline is `Source → Lexer → Parser → AST → Semantic Analysis → Shared IR → Backends`. The interpreter executes the shared IR; compiler, decompiler, assembler, disassembler, and translator components will consume or produce explicit representations around it.

The first Rust milestone separates the lexer, parser, AST, interpreter, CLI, and .mxpg validation into modules. These will grow into workspace crates as their interfaces stabilize. GCC, NASM, and other specialized tools remain optional Builgine backends.

Beg, Adv, and AdvN are modes of one language, not separate languages. The existing print prototype is retained as a migration smoke test while the newer syntax becomes the canonical grammar.
