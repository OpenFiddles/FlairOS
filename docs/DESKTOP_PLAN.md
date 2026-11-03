# macOS-style desktop plan

Goal: a desktop with the same layout and behavior people expect from macOS: a top menu bar, a bottom dock, draggable windows with traffic-light controls, and a launcher. The look is original. We do not use Apple's icons, fonts, wallpapers, sounds or trademarks.

## Build order

Each step lands on `macos-style-desktop` only after CI passes on the branch. Each step is one commit with a short description.

1. Menu bar: a 1-row strip at the top showing the system name, the focused app's name, and a clock on the right. Clicking the name opens a menu with About and Quit.
2. Dock: a bottom strip with one slot per launchable app. Clicking a slot opens the app; a dot under the slot marks running apps.
3. Window chrome: a title bar with three round controls (close, minimize, zoom). Close removes the window. Minimize hides it to the dock. Zoom toggles full screen.
4. Focus and stacking: clicking a window raises it and makes it the focused app. Only the focused window accepts keys.
5. Launcher: a grid of app icons opened from the dock's first slot. Search filters the grid by name.
6. Input: arrow keys move the pointer, Enter clicks, Esc closes the active menu. This keeps the iPad keyboard path working.

## Acceptance checks

- CI builds the image on every push to this branch.
- `tests/desktop_smoke.py` boots the image in QEMU and checks each step's visible text: menu bar label, dock slots, window controls.
- No step merges to `main` until its checks pass.

## Out of scope for now

Appearance themes, Spotlight-style search, file previews, and any Apple-owned asset.

## Open questions for XiLy

- Should the default wallpaper be a plain gradient we draw, or a picture you supply?
- Which apps go in the dock first: Terminal, Files, Piano, Clock?
