# CartOS source

CartOS is a cartridge-style game browser for OnionOS handhelds. Version 0.1.24 has been tested on the Miyoo Mini V4.

## Features

- Browse multiple PNG artwork images for a game during the enlarged launch preview.
- Search for a game's extra images only when its enlarged preview opens, including when the game is opened from Recent, Favourite, or a custom list.
- Keep the normal OnionOS artwork first, then show CartOS alternatives. A single extra image can use the plain ROM title, such as `Pokemon Emerald.png`; additional views can use labelled names such as `Pokemon Emerald - Gameplay.png` in `Imgs/CartOS`. The earlier per-ROM folder layout still works.
- Press Left/Right to browse the artwork and then a Game Details page with the title, system, playtime, and ROM filename. A continues launching, and B returns to the centered game list.
- Always use the main cartridge image for the launch animation, including when A is pressed from an alternate image or Game Details. Extra-art discovery is indexed to avoid scanning the full artwork folder for every ROM during startup.
- Keep the two-step launch preview enabled; double-tap A from the game list to launch directly.
- Keep artwork browsing temporary to CartOS; OnionOS artwork is not modified, and there is no Y action to save a default image.

See the release `INSTALL.txt` for the alternate-image folder layout, installation steps, and startup-hook details.

## Devices

| Device | Screen | Status |
|---|---|---|
| Miyoo Mini V4 | 752x560 | Tested |
| Miyoo Mini Flip | 750x560 | Expected to work on OnionOS (same layout as V4); untested |
| Miyoo Mini Plus | 640x480 | Detected and scaled to 640x480; untested on hardware |
| Other handhelds (Anbernic, Powkiddy, spruceOS devices) | various | Not supported yet: games and apps are launched through OnionOS |

Device details live in `src/platform.cpp`. Detection is file based, like OnionOS and spruceOS, and any value can be overridden in `launch.sh`:

- `CARTOS_PLATFORM`: `miyoo-mini`, `miyoo-mini-plus`, or another name.
- `CARTOS_SD_ROOT`: SD card mount point (default `/mnt/SDCARD`).
- `CARTOS_SCREEN_W` and `CARTOS_SCREEN_H`: panel size (default: read from `/dev/fb0`).

## Desktop preview

A native build opens CartOS in a window for trying layout and browsing changes without a handheld. It reads a copy of an OnionOS SD card (at least `Emu/` and `Roms/`). Games and apps do not launch.

```sh
cmake -S . -B build/host && cmake --build build/host
CARTOS_SD_ROOT=/path/to/sd-copy CARTOS_FONT=assets/font_atlas.png build/host/cartos
```

Add `CARTOS_SCREEN_W=640 CARTOS_SCREEN_H=480` to preview the Mini Plus screen. The arrow keys move and Enter opens. Linux needs the SDL2 and libpng development packages.

Handheld builds still use `scripts/build-miyoo.sh` with the Miyoo toolchain.

## Source layout

- `src/` — launcher, input, artwork discovery, and rendering code. `platform.cpp` holds device detection and battery reading.
- `assets/` — interface artwork and font assets.
- `scripts/` — source packaging and Miyoo build helpers.
- `tools/` — toolchain support and asset-generation utilities.
- `packaging/OnionOS/` — OnionOS packaging resources and optional startup hooks.

CartOS Settings can hide a system from the vertical browser list without deleting its ROMs or removing games from search and saved lists. Backgrounds named `BG.png` or `BG_<name>.png` can be selected in settings. The browser also includes search, game details, playtime information, battery level, configurable cartridge sizing per system, and custom lists.

CartOS-authored code is covered by the MIT License. Third-party assets and libraries retain their own licenses; see `LICENSE` and `THIRD-PARTY-NOTICES.md`.
