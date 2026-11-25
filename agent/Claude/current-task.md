# Current Task

- Task: Plan 4K output, GPU-backend path, battery + network indicators, Airnimation toggle, expanded Settings for FlairOS.
- Objective: Produce a grounded plan from the real code; implement only after user picks an order.
- Progress: Survey done (see what-i-did.md 21:50). No code changed.
- Files involved: kernel/vbe.c, kernel/desktop_gfx.c, userland/settings.c, kernel/kernel.c (tweens), kernel/hwinfo.c, drivers/net_rtl8139.c, tools/run-vnc.sh.
- Dependencies: Dirty working tree from ChatGPT/Gemini (preserve). BSS headroom only 0x2AC bytes (ChatGPT record) - new state must be heap-allocated.
- Blockers: Awaiting user go-ahead and order of work.
- Next steps: Implement in order proposed in chat: (1) profiles + QEMU vgamem, (2) Airnim toggle + UI.CFG, (3) network indicator, (4) battery bridge, (5) Settings expansion, (6) GPU backend probe.
