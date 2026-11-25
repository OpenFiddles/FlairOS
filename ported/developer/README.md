# FlairOS `/ported/developer/` Ecosystem

- `ported/developer/libs/` — Shared compatibility libraries outside individual apps (`flair_posix`, `flair_zlib`), packaged into `dev_libs.pck` via `tools/pancake.py`.
- `ported/developer/openjdk/` — FlairOS OpenJDK `0xCAFEBABE` `.class` bytecode runtime (`jvm_class.c`), Java 21 sample (`samples/HelloFlair.class`), and Pancake bundle (`openjdk.pck`).
- To clone upstream OpenJDK for full source reference or future `Zero` JVM builds:
  ```bash
  git clone --depth 1 https://github.com/openjdk/jdk.git ported/developer/openjdk/upstream
  ```
