# Chronicle 1.2.2

Chronicle is an in-game Skyrim SE chatbox and message log. It uses PrismaUI
for the overlay and CommonLibSSE-NG for the native SKSE plugin.

It captures Skyrim subtitles and notifications into a compact, filterable chat
window. Notifications (items, quests) are captured as the game queues them;
skill increases, Words of Power, level-ups and game saves go to System. SkyrimNet/narrator/player/combat channels are optional and hidden by
default on a fresh install. Existing preferences are preserved.

## Controls

- **F8** activates mouse interaction: click tabs, scroll, search, filter by speaker,
  copy/export history, drag the empty tab-bar area, or resize the lower-right corner.
- **Shift+N** opens settings. **Apply** saves and keeps the panel open to show the result.
- Settings includes live **Position X/Y** sliders; the box moves as the sliders move.
- **Channels → NPC speaker name colours** lets you assign exact display-name colours;
  changes preview immediately and are saved with **Apply**.
- **Additional GFx paths** under General → HUD subtitle compatibility lets HUD
  replacers supply extra subtitle fields without recompiling. Diagnostics reports
  the path currently resolving, or that none resolved.
- **Enter** opens chat input. Submitting sends a SkyrimNet player-input event;
  responding to it requires a compatible integration. Console commands are optional
  and are executed in Skyrim when the Console channel is enabled and selected.
- **Escape** releases interaction and returns control to Skyrim.
- **Position** in settings restores an on-screen window position.

Hotkeys can be letters, digits, Enter, or F1–F12. Settings adds Shift to its key.
The overlay is always hidden on the main menu and during loading. Hiding it during
other menus is configurable (this includes Meridian UI menus such as Wayframe, which
open `MeridianUI_FocusMenu`); captured messages remain in history while hidden.

## Settings and history

The authoritative settings file is `Data/SKSE/Plugins/Chronicle.json`, created on
the first successful settings save. It is not included in update packages, so
updates do not overwrite it. Saves use a temporary file and preserve the previous
file as `Chronicle.json.bak`. In MO2, newly written files may appear in Overwrite.
The bundled `PrismaUI/views/Chronicle/config.json` supplies first-run defaults.
Old browser preferences are migrated when there is no native settings file.
Settings edited in the panel are previews until Apply; Close/Escape discards them.
If the native save is unavailable or fails, Chronicle marks the settings as
pending and retries them on the next successful handshake.

History is stored in the web view's local storage per character, using an identifier
saved in the SKSE co-save. Make a new game save after installing to persist that
identifier. Loading an older save that predates this identifier starts a fresh
history profile; keep the `.skse` co-save alongside its Skyrim save. Legacy shared
history is left intact but is not assigned to an arbitrary character.

Copy and Export use the current channel/search/speaker filters. Export writes a
timestamped text file under `Documents/My Games/Skyrim Special Edition/SKSE/Chronicle-exports`
when the SKSE log directory is available; otherwise it uses `Data/SKSE/Plugins/Chronicle-exports`.
The result displays the actual location. Copy provides a selectable Ctrl+C fallback.

Diagnostics queries the native plugin for its build version, connection, focus,
polling status, message count, and last captured subtitle. It does not generate
sample messages or claim that an integration works merely because text can render.

## Requirements and installation

Requires Skyrim Special Edition/Anniversary Edition 1.6.1170, SKSE64 for
1.6.1170, Address Library for SKSE Plugins, and PrismaUI 1.3.0 or newer.

In Mod Organizer 2, install the ZIP as a mod and enable it. For a manual
install, extract the ZIP into Skyrim's `Data` folder. The final paths must be:

```text
Data/SKSE/Plugins/Chronicle.dll
Data/PrismaUI/views/Chronicle/index.html
```

Do not extract the archive into the Skyrim game root unless your extractor is
configured to merge the archive's contents into `Data`.

## Build

The project expects Visual Studio 18 with CMake, Ninja, and vcpkg installed.
Run `build.ps1` from PowerShell:

```powershell
.\build.ps1 -Config Debug
```

The resulting plugin is written to `out/build/x64-Debug/Chronicle.dll` and the
PrismaUI view files are copied beside it.

Builds run C++ regression tests and Node.js bridge/history tests (Node.js required
for development only). To build, test, and package matching DLL/UI assets:

```powershell
.\package.ps1 -Config Debug
```

Archives use the project version, configuration, and timestamp. Every archive entry
is compared by SHA-256 with the staged build. The obsolete standalone settings
page and development preview files are excluded.

For browser development, `node tests/preview.cjs` serves a local preview with mock
native callbacks at `http://127.0.0.1:47831`. Browser checks cannot validate Skyrim's
actual cursor routing, menu events, or SKSE serialization; those need an in-game run.

## 1.2.2 changes

- Notifications are captured by hooking `HUDNotifications::ProcessMessage`; the old
  queue poll never matched the game's HUD object, so notifications were missed.
- System channel: skill increases, Words of Power, level-ups and game saves.
- Console commands no longer crash on 1.6.1130+: `Script::CompileAndRun` uses
  address ID 441582 there (CommonLibSSE-NG's 21890 is absent from those versions).
- `Chronicle.log` and exports go to `My Games/Skyrim Special Edition/SKSE` instead of
  `My Games/Skyrim.INI/SKSE`.
- Keyboard input is processed before other mods while typing, so their hotkeys
  no longer fire; the interact hotkey or Escape releases focus natively.
- Packaging writes `/`-separated zip entries under Windows PowerShell 5.1.

## 1.2.1 changes

Chronicle now hides while a Meridian UI menu (such as Wayframe) has focus: Meridian
opens `MeridianUI_FocusMenu` for its focused views, and PrismaUI draws above Meridian,
so Chronicle previously stayed on top of them. It follows the hide-in-menus setting.

## 1.2.0 changes

Corrected PrismaUI callback dispatch and focus ownership; added native hotkeys,
startup/load visibility guards, managed subtitle polling with stale-task rejection,
capture of all active subtitle entries, speaker-aware duplicate suppression, and
UTF-8 sanitization with escaped diagnostic context. Added interaction controls,
filtered history tools, per-character history, settings save feedback, and native
diagnostics. Added live window positioning and per-speaker name colour overrides.
UTF-8 regression coverage includes byte `0xC1`, all single-byte values,
truncated/overlong/surrogate sequences, valid accented text, and emoji.

