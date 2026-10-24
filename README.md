# Chronicle 1.2.33

Chronicle is an in-game Skyrim SE chatbox and message log. It uses PrismaUI
for the overlay and CommonLibSSE-NG for the native SKSE plugin.

It captures Skyrim subtitles and notifications into a compact, filterable chat
window. Notifications from the game and mods (items, gold, quest objectives) are
captured as the HUD receives them; skill increases, Words of Power, level-ups and
game saves go to System. Optionally, the game's own corner notifications can be
hidden so they only appear in Chronicle. SkyrimNet/narrator/player/combat/console
channels are optional and hidden by default on a fresh install. Existing preferences
are preserved.

## Controls

- **F8** activates mouse interaction: click tabs, scroll, search, filter by speaker,
  copy/export history, drag the empty tab-bar area, or resize the lower-right corner.
- **Shift+N** opens settings. **Apply** saves and keeps the panel open to show the result.
- Settings includes live **Position X/Y** sliders; the box moves as the sliders move.
- **Channels → NPC speaker name colours** lets you assign exact display-name colours;
  changes preview immediately and are saved with **Apply**. The same section can
  optionally assign stable automatic colours to speakers while keeping manual
  overrides; white remains the default.
- **Additional GFx paths** under General → HUD subtitle compatibility lets HUD
  replacers supply extra subtitle fields without recompiling. Diagnostics reports
  the path currently resolving, or that none resolved.
- **NPC speech radius** under General → Behaviour filters attributed NPC dialogue
  by distance from the player. `0` keeps the default unlimited range; the value
  uses Skyrim world units.
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

## License

Chronicle's original code, web UI, tests, build scripts, and documentation are
licensed under the GNU General Public License, version 3 or any later version.
The complete license text is in [`LICENSE`](LICENSE). Third-party components and
their separate notices are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Release archives include both files. The corresponding source is the repository
contents at [github.com/kezc7/Chronicle](https://github.com/kezc7/Chronicle),
including the build and packaging scripts and the pinned dependency configuration.
The archive does not include Skyrim, SKSE, Address Library, or PrismaUI runtime
files; obtain those separately under their own licenses.

For browser development, `node tests/preview.cjs` serves a local preview with mock
native callbacks at `http://127.0.0.1:47831`. Browser checks cannot validate Skyrim's
actual cursor routing, menu events, or SKSE serialization; those need an in-game run.

## 1.2.33 changes

- Fixed a crash in the HUD subtitle reader. Chronicle read the HUD's Scaleform
  movie from SKSE's general task queue, which could race the HUD's own updates
  and crash inside the game (seen with InfinityUI, moreHUD and other HUD mods).
  The HUD is now only read from SKSE's UI task queue.
- Lines you type in the input bar now always show in the All tab, even with
  the Player channel turned off (it is off by default, which hid them).

## 1.2.32 changes

- Shift+N settings no longer close straight after opening: menus from other mods
  that do not take input (HUD widgets, minimaps) no longer close Chronicle, and
  focus is not treated as lost before PrismaUI reports it.
- Chronicle no longer keeps the keyboard when nothing is open: a late focus grant
  is handed back, and the first key press returns control to Skyrim.
- The Clear button only works on a click during an interaction, so game keys
  (Tab, Space) can no longer erase the history through it.

## 1.2.30 changes

- Added optional automatic NPC speaker colours with a stable 24-colour palette.
- White remains the default, and manually assigned NPC colours take priority.
- Notification channel colours now apply to the `[INFO]` prefix and message text;
  the default is a brighter, more readable yellow.
- System channel colours now apply to the `[SYSTEM]` prefix and message text.

## 1.2.29 changes

## 1.2.28 changes

## 1.2.27 changes

- Added configurable NPC speech radius filtering. Set it under General → Behaviour;
  `0` keeps the previous unlimited range.

## 1.2.26 changes

- Reissued the current build with the GPLv3-or-later license and complete
  third-party notices in every release archive.

## 1.2.25 changes

- The player's dialogue choices now appear in the Dialogue channel under the
  player's name. Skyrim's player lines are silent menu options rather than
  subtitles, so Chronicle logs the chosen option as the NPC starts answering it.

## 1.2.24 changes

- Chronicle hides in more full-screen menus, following "Hide in menus": character
  creation (RaceSex Menu, used by RaceMenu), books and notes, lockpicking, training,
  gifting to followers, tutorials, credits and the Creations/mod menu. The Dialogue
  Menu stays visible so conversations remain readable.

## 1.2.23 changes

- **Auto-hide** (General → Behaviour → Auto-hide after): the whole window fades out
  after the chosen number of seconds (0–300; slider in 1 s steps or type an exact
  value) without new messages and returns on the next message or interaction.
  0 (default) = off. It never fades while typing, in mouse
  mode or with settings open.
- **Show/hide hotkey** (Hotkeys): hides or shows Chronicle instantly; no key by
  default, and Backspace clears it. It works while Chronicle is hidden by it, but is
  ignored while typing, in menus and in the console. **Remember hidden state**
  (default on) keeps it hidden after loading. The settings hotkey (Shift+N) also
  brings a hidden Chronicle back and opens settings, so a forgotten key cannot
  leave it lost. While hidden, the game shows its own
  notifications even with "Notifications only in Chronicle" on.
- Chronicle hides while Skyrim's console (~) is open, following "Hide in menus".
- Hotkey clash warnings cover Interact, Input and Show/hide.

## 1.2.20 changes

- More languages: Italian, Spanish, Polish, Turkish, Simplified Chinese, Japanese and
  Korean (ten in total). "Auto" maps ITALIAN, SPANISH, POLISH, TURKISH, CHINESE,
  JAPANESE and KOREAN; Skyrim's official Chinese is Traditional and uses the
  Simplified table. Polish, Turkish and Korean native phrases use colon or noun
  forms so game-supplied names need no grammatical endings.
- Chinese, Japanese and Korean text uses Windows' CJK fonts (Microsoft YaHei,
  Yu Gothic UI/Meiryo, Malgun Gothic) as fallbacks, ordered by the UI language.

## 1.2.19 changes

- Translations: French and German, alongside English. Settings → General → Language
  (default **Auto**, which follows Skyrim's `sLanguage` setting). English is the
  fallback for anything not translated.
- The UI text lives in `i18n.js` (one table per language; elements use `data-i18n`
  attributes). The game side sends status messages as keys (`focus.active`,
  `settings.saved`, …) and lines Chronicle composes itself ("killed X", "Game saved",
  quest labels, the console "more lines" note) come from a small native phrase table.
  Game-originated text (dialogue, notifications, names) is already in the game's
  language and is not translated.
- Tests check that every language has every key with matching placeholders, that
  every key used by the page and scripts exists, and the native phrases.
- Sources compile as UTF-8 (`/utf-8`) for the accented native phrases.
- Settings rows built by script (channel names, prefix placeholders, speaker-colour
  rows, "All speakers") carry `data-i18n` keys, so changing the language with the
  settings open re-translates them. The 1.2.18 test build left them in the old language.

## 1.2.17 changes

- New themes (Settings → General → Appearance → Theme): **Daedric** (black and
  crimson), **Dwemer** (bronze and brass), **Classic MMO** (dark translucent panels,
  gold tabs) and **Modern** (neutral greys, rounded corners). Skyrim stays the default
  and looks unchanged, as do Minimal, Transparent and High contrast.
- The chat window's colours, borders, corners and font are now theme tokens
  (`--theme-*` CSS variables); each theme is one declarative block in `styles.css`.
  New themes respect the Opacity setting. Channel and speaker colours remain user
  settings layered on top. Only fonts installed with Windows are used, and no
  third-party UI assets are bundled. The settings panel keeps a single look.
- Unknown theme values fall back to Skyrim (`normalizeTheme`), with browser tests for
  the fallback, theme options/style blocks and token defaults.

## 1.2.16 changes

- Hotkey settings are set by pressing the key: click a box, then press a letter,
  number, Enter or F1–F12. Previously the key's name had to be typed, and an invalid
  entry silently fell back to the default, so changes seemed to do nothing.
- Unsupported keys show a message instead of being ignored, and the panel warns when
  the Interact and Input hotkeys are the same key. Number-pad keys are explained as
  unsupported (natively they differ from the top-row digits).
- Keys are read from `KeyboardEvent.keyCode`, the Windows virtual-key code the
  native side uses. PrismaUI's engine reports `key`/`code` differently from desktop
  browsers, so the 1.2.14 and 1.2.15 test builds rejected digits in-game. A rejected
  key's message includes the raw key details for bug reports.
- Tip: Chronicle handles keys before other mods, so it wins any clash with another
  mod's hotkey (for example Enter). Change Chronicle's hotkey to free that key up.

## 1.2.13 changes

1.2.8–1.2.12 were test builds; this is the first update after the 1.2.7 Nexus release.

- Notifications now actually reach Chronicle. Since 1.2.2 the hook sat on
  `HUDNotifications::ProcessMessage`, which on 1.6.1170 never receives
  notifications (the HUD menu queues them itself). Chronicle now hooks
  `HUDMenu::ProcessMessage` and reads each notification's `HUDData` text, identified
  by its vtable. This covers game and mod notifications alike.
- Mod notification formatting (`<font color=...>` and similar) is stripped.
- Quest and objective lines are labelled, e.g. "New objective: …" and
  "Objective completed: …".
- New setting **Notifications only in Chronicle** (General → Behaviour, default off):
  hides the game's corner notifications while Chronicle is visible, the chatbox is
  on and the Notifications channel is enabled. Quest and skill banners still show.
- Console commands echo in Chronicle (`> command`) along with their output (first 50
  lines; the rest stay in Skyrim's console). Console lines now also show in the
  All tab. Every command and its outcome is written to `Chronicle.log`.
- Messages you type (Player channel) show your character's name instead of "Player".

## 1.2.7 changes

- Position X/Y in settings gain −/+ nudge buttons (1 px; Shift-click for 10 px) and
  an editable value box for exact positions. The sliders span the whole screen,
  so on wide displays each mouse pixel moved the box by ~30 px.

## 1.2.6 changes

- Settings → Channels → Combat → **Show hits** turns hit messages off (setting
  `general.combat_hits`, default on). Kills and deaths still show. Useful with
  spells such as Flames, which register a hit every second.

## 1.2.5 changes

- Combat messages use real names (your character's name instead of "Player").
- Kills and deaths involving followers and your summons are reported, e.g.
  "Serana killed Vampire" or "Vampire killed Serana". Hits stay limited to the
  player, and fights between other NPCs are left out, to keep the channel quiet.

## 1.2.4 changes

- Choosing a channel from the input-bar menu (or with Tab) only sets where the
  message goes; the history view stays on the current tab. Clicking a tab still
  sets both, so typing on the Console tab runs commands.

## 1.2.3 changes

- The channel label in the input bar (e.g. `[All]`) is now a menu: click it to pick
  any enabled channel, such as SkyrimNet or Console. Typing continues afterwards;
  Tab still cycles channels.

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

