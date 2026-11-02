# CartOS patch notes

Changes are listed newest first. Each entry describes the user-visible changes and any important compatibility notes.

## Unreleased — Device support groundwork

- Detect the handheld at startup, the way OnionOS and spruceOS do: the Miyoo Mini Plus by its AXP power-chip tool, and the screen size from the framebuffer. The result is written to the log.
- Open the window at the Mini Plus's native 640x480 size. The V4 and Mini Flip keep the tested 752x560 window.
- Read the battery level from OnionOS's `/tmp/percBat` first, then from the kernel's power-supply files (used on spruceOS, Anbernic and Powkiddy devices), then from SDL.
- Allow the SD card root, platform and screen size to be overridden from `launch.sh` (`CARTOS_SD_ROOT`, `CARTOS_PLATFORM`, `CARTOS_SCREEN_W`, `CARTOS_SCREEN_H`).
- Allow a native desktop preview build for trying changes on a PC against a copy of an SD card.

## 0.1.24 — Per-game artwork preview

- Speed up startup artwork discovery by indexing each extra-art folder once and looking up each game's images directly.
- Defer extra-image lookup until a game reaches its enlarged preview, and load the same artwork from Recent, Favourite, and custom lists.
- Resolve Recent entries through normalized ROM paths so their extra artwork folder is found even when history stores an alternate path form.
- Always use the primary cartridge image for the launch animation, even when A is pressed from Game Details or an alternate image.
- Accept a plain ROM-title PNG (for example, `Pokemon Emerald.png`) in `Imgs/CartOS` as one extra image; use names such as `Pokemon Emerald - Gameplay.png` for additional views. Continue supporting earlier per-ROM folders and show the plain-title image before labelled views.
- Restore the primary cartridge image in the game list after leaving the enlarged artwork preview; alternate images remain preview-only.
- Add a Game Details page after the artwork images in the enlarged launch preview, showing the title, system, playtime, and ROM filename.
- Let Left/Right browse artwork in the enlarged first launch step, A continue, and B return to the centered game list.
- Remove the Y set-default action from the artwork preview; artwork browsing is temporary to CartOS and OnionOS's artwork files are never changed.
- Keep two-step launch enabled, with double-tap A available for direct launch.
- Use one artwork discovery path for system lists, Recent, custom lists, and search.

## 0.1.23 — Cartridge startup animation

- Add a short startup sequence: the selected cartridge rises into view, the side cartridges slide into place, then the interface text appears.
- Add a CartOS Settings option to disable the startup animation; it remains on by default.
- Skip the animation automatically when CartOS is relaunched after GameSwitcher, using a one-time app-folder marker as well as a launch flag, and when the player presses a button or moves a direction.

## 0.1.22 — Search keyboard space and punctuation

- Add a visible SPACE key to the on-screen search keyboard.
- Make every displayed search key insert its character, including hyphen, apostrophe, and period.

## 0.1.21 — Music Player first-launch handoff

- Add a one-second settle delay only before launching OnionOS Music Player (GMU), which switches from OnionOS audio to its own SDL 1.2 stack.
- Keep every other OnionOS app launch command unchanged.

## 0.1.20 — Per-system visibility

- Add a saved Show/Hide visibility control beside each system's cartridge size setting.
- Skip hidden systems in the vertical system carousel and its up/down labels, while keeping their games available in search and other lists.
- Restore all systems to shown when CartOS settings are reset.

## 0.1.19 — Settings list organization

- Move per-system cartridge size controls to the bottom of CartOS Settings so the general options remain near the top as more systems are enabled.
- Keep each system's existing saved cartridge size and reset behavior unchanged.

## 0.1.18 — Background selection

- Add a Background option to CartOS Settings that cycles through `BG.png` and named `BG_<name>.png` images.
- Show the selected filename label in settings, save it to `settings.ini`, and reload the selected image after ROM refresh.
- Accept names such as `BG_(Forest).png` and display them as `Forest`.

## 0.1.17 — Darker inverted browser text

- Make all main-browser text use a high-contrast dark palette when `Invert text colour` is enabled, including secondary headers, counters, arrows, and control hints.

## 0.1.16 — Browser text display options

- Add a saved `Invert text colour` setting for the main cartridge browser to improve readability over light backgrounds.
- Add a saved `Hide game title` setting to remove the large game title from the main browser.
- Keep both options off by default and include them in the settings reset.

## 0.1.15 — Direct custom-background rendering

- Draw opaque custom backgrounds without texture alpha blending or an extra dark overlay.
- Record the SDL renderer's background draw result in `App/CartOS/background-debug.log`.

## 0.1.14 — Custom background loading diagnostics

- Retry `BG.png` from the CartOS app folder if the configured background path cannot load.
- Record each background path attempt and its load result in `App/CartOS/background-debug.log` for troubleshooting.
- Keep the existing background tint and display style unchanged.

## 0.1.13 — Cleaner game lists

- Hide OnionOS utility entries named `~Filter`, `~Refresh roms`, and `Search` from console, Recent, custom-list, and search-result game lists.
- Keep CartOS's Y-button search feature available.

## 0.1.12 — Reliable OnionOS app handoff

- Fix OnionOS app launching by matching its expected app-directory command format and restoring the framebuffer to its top page before returning control to an app.
- Confirmed repeated launches of Activity Tracker and Video Player on a Miyoo Mini V4.
- Remove temporary app-handoff and launcher startup diagnostic logging from the release build.

## 0.1.11 — OnionOS app launch compatibility

- Match OnionOS MainUI's app handoff: change to the app directory, make its configured launch target executable, and start it with OnionOS's `libpadsp` preload. This restores the launcher environment used by apps such as Video Player and supports apps that depend on their working directory.

## 0.1.10 — OnionOS app launch compatibility

- Stop forcing OnionOS's audio preload library onto every app. CartOS now hands the configured app launcher to OnionOS as-is so app packages can use their own audio setup and launcher environment.

## 0.1.9 — OnionOS app launch compatibility

- Match OnionOS's app command format when handing off an installed app. The configured launch target is now the first quoted executable path, without a prepended directory change or forced `/bin/sh`, so OnionOS can identify and launch app packages using their own interpreter, working directory, and environment setup.

## 0.1.7 — OnionOS discovery and launch compatibility

- Discover systems from OnionOS emulator configuration files instead of a fixed six-system list. Use each system's configured ROM folders, file extensions, artwork folder, and launch script; omit systems without ROMs and scan nested ROM folders.
- Discover OnionOS apps using the app's configured `launch` target, with the conventional `launch.sh` fallback for older packages. Run shell scripts through the system shell and set their app working directory.
- Flush the GameSwitcher and return markers before handing control back to OnionOS, and stop discarding a normal Menu press based on a short release-time cutoff.
- Describe CartOS as a cartridge launcher for OnionOS devices, tested on the Miyoo Mini V4.

## 0.1.8 — Ready-to-install handheld package
- Combine optional CartOS boot start and GameSwitcher return in `Startup/CartOS.sh`; it preserves queued OnionOS game launches, makes a guarded marked runtime change, and includes `Return/Restore-OnionOS.sh` to reverse that change without a backup.
- Reformat the installation guide with consistent section breaks and clearer hidden-file instructions.
- Update the installation guide layout, point to the ZIP-root `Startup` folder, and clarify the hidden-file steps.

- Add a downloadable ZIP containing the OnionOS app folder, install and uninstall guide, CartOS source, license notices, and patch notes; pin the exact Miyoo SDL2 source revision used for the bundled runtime.
- Include the optional boot-start hook in a simple `Startup/CartOS.sh` folder at the ZIP root, and document how to enable and disable it.
- Replace the public build walkthrough with direct download and installation instructions.
- Replace the upstream NEON helper with a CartOS-authored MIT-licensed library implementing the backend's memcpy and pixel-scaler interface.
- Build and package the compatibility library alongside the Miyoo SDL2 runtime, without downloading the upstream NEON helper.
- Include the SDL2 and backend license files with the app and provide a link to the exact matching backend source revision.
- Restore the accidentally truncated launcher methods so the source builds from a clean checkout.
- Rename the project source folder and the source copy in the release ZIP to `CartOS/`.

## 0.1.6 — Open-source source release

- Added the C++/SDL2 launcher source, Miyoo build helpers, and OnionOS app template in the source folder.
- Added beginner-friendly build and installation instructions, including custom background setup and controls.
- Added an MIT license for CartOS-authored source and separate notices for third-party assets and dependencies.
- Documented that CartOS has been tested on the Miyoo Mini V4.
- Excluded compiled device libraries, ROMs, box art, and personal SD-card data.

Future fixes should add a dated or versioned entry here describing the change and its user impact.
