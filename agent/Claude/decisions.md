# Decisions

## 2026-11-21 - Do not commit or touch other agents' files
- Decision: Created agent/Claude/ only; left the working tree's existing uncommitted changes and agent/ChatGPT, agent/Gemini untouched; made no commit.
- Reasoning: The tree has many uncommitted modifications from other work, and the policy says never overwrite other agents' records and inspect Git state before editing shared files.
- Alternatives considered: Committing agent/Claude/ alone (deferred; no implementation work to pair it with and the user did not ask for a commit).
- Consequences: agent/Claude/ is untracked until the user or a later task commits it.

## 2026-11-21 - Plan only, no code yet
- Decision: Survey and propose instead of editing, because the tree is dirty with another agent's uncommitted work and the 0x30000 BSS limit makes careless additions break the link.
- Alternatives: Start editing immediately (rejected: user asked "tell me how").
- Consequences: Implementation waits for user confirmation.

## 2026-11-21 21:41 - User requirement: all drivers must be generic
- Decision: New display/GPU, battery, network/Wi-Fi and animation work must go through the existing generic driver model (include/drv.h: DRV_CLASS_GPU/DISPLAY/NET, drv_find_class) plus small class-level interfaces, not device-specific code in the desktop. Device-specific backends (Bochs DISPI, RTL8139, etc.) plug in underneath and probe by PCI class code (hwinfo already records class/subclass/prog_if), with a CPU/generic fallback.
- Consequences: UI asks "is there a battery / link / GPU?" via class queries; absence is reported honestly, never faked.
