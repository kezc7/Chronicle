# Chronicle 1.4.18

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
- **Shift+0** opens settings (Shift+N up to 1.4.0-beta). **Apply** saves and keeps
  the panel open to show the result.
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
- **Enter** opens chat input. With SkyrimNet installed, a submitted line is handed
  to SkyrimNet's API as your character's words, said to an NPC when Chronicle can
  tell who: the one you are talking to in the Dialogue Menu, else one the line
  starts with by name ("Sally, ...", found by SkyrimNet among nearby NPCs), else
  the NPC you last looked at (within 30 seconds and still nearby). With no one,
  the line is a remark to nobody in particular. How NPCs answer is up to SkyrimNet. `"skyrimnet_send"` at the top level of `Chronicle.json`
  changes this: `"transform"` uses SkyrimNet's Transform Dialogue instead (the line
  is rewritten in character and spoken), `"off"` keeps lines in Chronicle only.
  `Chronicle.log` records each submit and SkyrimNet's answer code. Every input
  channel except Console sends chat. Console commands are optional and are
  executed in Skyrim when the Console channel is enabled and selected.
- A chat line starting with `/` is a command for Chronicle or another mod and is
  not sent to SkyrimNet: `/help` lists the commands mods have registered, and an
  unknown one says so in the Console channel.
- **Escape** releases interaction and returns control to Skyrim.
- **Position** in settings restores an on-screen window position.

### Controller

Hold **LB** and press **Y** to open the chat window with a controller (General →
**Open with the controller** picks LB + Y, LB + X, RB + Y, RB + X or Off). Only that Y
press is kept from the game. The buttons follow the rest of the Dovah suite:

| Button | In the log | In the chat bar |
|---|---|---|
| **LB / RB** | previous / next channel tab | channel to send on |
| **D-pad or left stick** up / down | scroll (held: repeats) | go through the recent commands |
| **D-pad** left / right | previous / next channel tab | |
| **Right stick** | scroll | |
| **A** | open the chat bar with the recent commands | send |
| **B** | close the window | close the chat bar |
| **Y** | next speaker in the speaker filter | |
| **X** | jump to the newest line | |

A recent command goes back out on the channel it was first sent on, so a console
command stays a console command. The prompts at the bottom of the window show the
buttons while the controller is in use, and the keyboard's arrow keys do the same
in the log.

On **PrismaUI** (1.5 or newer) the window reads the controller itself. On **Magelight**
(1.6 or newer, with its gamepad layer on) Magelight hands the controller over as keys:
the d-pad and A work as above, B closes the whole window (as Escape does there), the
right stick scrolls under the pointer, and LB, RB, X and Y aren't passed to the window.
Use left / right for the channel tabs there.

Hotkeys can be letters, digits, Enter, or F1–F12. Settings adds Shift to its key.
The overlay is always hidden on the main menu and during loading. Hiding it during
other menus is configurable (this includes PrismaUI menus such as DFC, which open
`PrismaUI_FocusMenu`, Meridian UI menus such as Wayframe, which
open `MeridianUI_FocusMenu`, and Magelight UI menus, which open `MagelightFocus`);
captured messages remain in history while hidden.

## Settings and history

The authoritative settings file is `Data/SKSE/Plugins/Chronicle.json`, created on
the first successful settings save. It is not included in update packages, so
updates do not overwrite it. Saves use a temporary file and preserve the previous
file as `Chronicle.json.bak`. In MO2, newly written files may appear in Overwrite.
The bundled `PrismaUI/views/Chronicle/config.json` supplies first-run defaults.
Old browser preferences are migrated when there is no native settings file.
Settings edited in the panel are previews until Apply; Close/Escape discards them.
The interface comes in English, French, German, Italian, Spanish, Polish, Turkish,
Simplified Chinese, Japanese and Korean (Language codes en, fr, de, it, es, pl, tr,
zh-CN, ja, ko). Translations other than English are machine translations;
corrections welcome.
If the native save is unavailable or fails, Chronicle marks the settings as
pending and retries them on the next successful handshake.

History is stored in the web view's local storage per character, using an identifier
saved in the SKSE co-save. Make a new game save after installing to persist that
identifier. Loading an older save that predates this identifier starts a fresh
history profile; keep the `.skse` co-save alongside its Skyrim save. Legacy shared
history is left intact but is not assigned to an arbitrary character.

The log follows the save, not just the character. Each save remembers the stretches
of play that led to it. Load an older save and the lines written after it (play it
never saw) are folded away, in the chat window and in DovahHub's Chronicle tab. They
aren't deleted: load the later save and they're back. Play on from the older save
and its new lines are a branch of their own, which the later save doesn't show.
Saves made before 1.4.17 have no record of this and show every line, as before;
the first save made with 1.4.17 starts keeping it. The chat window keeps up to its
**Max messages** lines per character across all saves, the shown ones first, so a
long new branch can push out folded lines there; DovahHub's tab keeps the last 1000.

### Chronicle in DovahMenu

With DovahMenu installed, Chronicle registers its own entry in DovahMenu's list,
the same on the PrismaUI and Magelight builds. It is greyed out until a game is
loaded; opening it shows the chat box (also if the show/hide key hid it) and opens
Chronicle's settings. The entry shows the settings key (Shift + the settings hotkey);
the chat bar stays on the input hotkey.

### Chronicle tab in DovahHub

With DovahHub 0.92.1 or newer installed, Chronicle adds a **Chronicle** tab under
DovahHub's Integrations group. It lists the lines Chronicle has shown for the current
character (up to 1000), with channel filters, search and **Load older**; new lines
appear while the tab is open. Channels turned off in Chronicle's settings are not
kept (your own typed lines always are, as in the chat window's All tab).
Without DovahHub nothing changes.

This history is kept by the DLL in
`Data/SKSE/Plugins/Chronicle/history/<character id>.json` (in MO2 it appears in
Overwrite), following **Keep history**: turning it off deletes the file. Clear
history in the chat window clears it too. The first time a character loads with
1.3.0, the chat window's existing history for that character is copied over.

Copy and Export use the current channel/search/speaker filters. Export writes a
timestamped text file under `Documents/My Games/Skyrim Special Edition/SKSE/Chronicle-exports`
when the SKSE log directory is available; otherwise it uses `Data/SKSE/Plugins/Chronicle-exports`.
The result displays the actual location. Copy provides a selectable Ctrl+C fallback.

Diagnostics queries the native plugin for its build version, connection, focus,
polling status, message count, and last captured subtitle. It also shows the
history (lines, size, file), how many mods subscribe to Chronicle and which
commands they registered, and the form ID and owner quest of the last NPC line.
It does not generate sample messages or claim that an integration works merely
because text can render.

### Self-test

Type `/selftest` in the chat bar with a game loaded. With DovahHub 0.98 or newer,
**Settings > Diagnostics > Run self-test** (shown when `DovahHub.ini` has
`[Debug] bSelfTest=1`) runs the same steps as part of the suite's run. It takes a
second or two and changes nothing in the save: it writes a scratch file next to the
history files and removes it again. It never runs on its own.

| Step | What it checks |
|---|---|
| `pump` | Work posted to the game thread runs on the frame's thread, and which pump ran it (DovahHub's or Chronicle's own hook) |
| `threads` | No game work has run off the game thread this session; fails naming each place that did |
| `commands.dispatch` | A throwaway command registered through `Chronicle_RequestApi`, as another mod would, is parsed and dispatched with its arguments; plain text, unknown and built-in commands aren't; reserved and duplicate prefixes are refused; it unregisters |
| `commands.mods`, `commands.mod.<prefix>` | Other mods' commands are listed with a handler. They aren't called: commands have no dry run |
| `journal.write`, `journal.read`, `journal.remove` | Three lines (one in several scripts) go through the history writer to `history/selftest.json`, come back from disk unchanged, and the file is removed |
| `ui.view` | The chat window's view exists, its listeners are registered and its handshake is done |
| `saveleak.character` | The history open is the loaded character's |
| `saveleak.lineage` | The log follows the save: the save's stretches read back from the co-save record, the DovahHub tab shows no line from outside them, and the chat window folds by the same ones |
| `saveleak.page` | The chat window shows the same character as the game, with its search and speaker filter |
| `saveleak.mainmenu`, `saveleak.newgame` | Skipped: they need a quit to the Main Menu or a New Game |
| `pad` | The controller can reach the window: on PrismaUI the window sees a pad; on Magelight the gamepad layer is on and a pad is connected. Names the open combo. Skipped with no controller connected |
| `dovahhub.module` | Chronicle's DovahHub tab answers a history request for this character (skipped without DovahHub) |
| `dovahmenu.entry` | DovahMenu still knows Chronicle's entry, greyed or not as it should be, with the settings key (skipped without DovahMenu) |

Each step writes one line to `Chronicle.log` in the SKSE log folder, in the suite's
format: `SELFTEST chronicle <step> PASS|FAIL|SKIP <detail>`, then
`SELFTEST chronicle DONE pass=N fail=N skip=N ms=N`. The chat box shows the totals
and any failed step. In DovahHub's run the steps also go to its
`DovahSuite-SelfTest-<run>.log`.

`/selftest <mod>` runs another mod's self-test command, `/<mod>.selftest`, for mods
that register one through Chronicle_API.h.

### For mod authors: Chronicle_API.h

Other SKSE plugins can read Chronicle's log through `src/api/Chronicle_API.h`
(header-only; vendor it unchanged). `CHRONICLE_API::RequestApi()` returns
nullptr when Chronicle is not installed. The API can:

- page through the current character's history (`GetMessages`, after an id);
- subscribe to lines as they are captured;
- put a line in the box, or send chat to SkyrimNet (`Send`);
- register `/commands`;
- show or hide the box.

Each NPC line carries the speaker's form ID and base FormKey, the dialogue
topic's owner quest and kind, the game day and hour, the real time and the
player's location. Lines read from the HUD alone (no SubtitleManager entry) have
no speaker or quest. The header documents the thread rules. Adding
`"debug_commands": true` at the top level of `Chronicle.json` registers a
`/chronicle.echo` test command. `/help` and `/selftest` are Chronicle's own; a mod
that wants its self-test started from the chat bar registers `<its slug>.selftest`,
and `/selftest <its slug>` runs it.

## Requirements and installation

Requires Skyrim Special Edition/Anniversary Edition 1.6.1170, SKSE64 for
1.6.1170, Address Library for SKSE Plugins, and PrismaUI 1.3.0 or newer.

In Mod Organizer 2, install the ZIP as a mod and enable it. For a manual
install, extract the ZIP into Skyrim's `Data` folder. The final paths must be:

```text
Data/SKSE/Plugins/Chronicle.dll
Data/PrismaUI/views/Chronicle/index.html
Data/PrismaUI/views/DovahHub/modules/chronicle/index.html
```

The `DovahHub/modules/chronicle` folder is the Chronicle tab's page; DovahHub loads it
from there on either of its UI hosts. It does nothing without DovahHub.

Do not extract the archive into the Skyrim game root unless your extractor is
configured to merge the archive's contents into `Data`.

### Magelight UI build (beta)

A separate `Chronicle-Magelight-<version>` archive runs the same chat window on
[Magelight UI](https://www.nexusmods.com/skyrimspecialedition/mods/193303)
(0.30.1 or newer) instead of PrismaUI. Install one or the other, not both: both
archives ship `Data/SKSE/Plugins/Chronicle.dll`. The Magelight archive's page is
at `Data/Magelight/Chronicle/views/index.html`. Settings, hotkeys and the
settings file are shared with the PrismaUI build; chat history is not, because
Magelight keeps page storage in its own folder. Known differences: Magelight
draws its own cursor while Chronicle has focus, Page Up (Magelight's toggle
key) also releases Chronicle's focus, and the host's text gamma differs
slightly. Diagnostics shows which host the DLL was built for.

## Build

The project expects Visual Studio 18 with CMake, Ninja, and vcpkg installed.
Run `build.ps1` from PowerShell:

```powershell
.\build.ps1 -Config Debug
```

The resulting plugin is written to `out/build/x64-Debug/Chronicle.dll` and the
PrismaUI view files are copied beside it. `-UiHost Magelight` builds the
Magelight UI variant into `out/build/x64-Debug-Magelight` instead; the same
switch on `package.ps1` produces the `Chronicle-Magelight-<version>` archive.
The host is chosen at build time (CMake `CHRONICLE_UI_HOST`); one DLL talks to
one host.

Builds run C++ regression tests and Node.js bridge/history tests (Node.js required
for development only). To build, test, and package matching DLL/UI assets:

```powershell
.\package.ps1 -Config Debug
```

Archives use the project version, configuration, and timestamp; `-Label beta`
names them `Chronicle-<version>-beta-PrismaUI` (or `-Magelight`) instead, replacing
an earlier archive of that name. Every archive entry
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

## 1.4.18-beta

Nineteenth beta, PrismaUI and Magelight builds.

- Game events no longer do game work on the thread they arrive on (CHRON-20). In some
  load orders menu and input events come on Magelight's thread, hits and deaths on
  worker threads and mod events on Papyrus's threads. Each event now copies what it
  needs and hands the work to the game-thread pump. Input still decides at once
  whether a key belongs to Chronicle, from state the game thread publishes. This
  ends the "game work off the game thread" warnings in Chronicle.log.
- The self-test gains a `threads` step that fails if any such warning was logged.

## 1.4.17-beta

Eighteenth beta, PrismaUI and Magelight builds.

- The log follows the save (CHRON-18). Loading an older save folds away the lines from
  play after it, in the chat window and in DovahHub's Chronicle tab; loading the later
  save brings them back. Nothing is deleted. Saves from before 1.4.17 show every line.
  Mods reading the log through Chronicle_API.h get the loaded save's lines too.
- The self-test gains `saveleak.lineage`, which also logs how many lines are folded.

## 1.4.16-beta

Seventeenth beta, PrismaUI and Magelight builds.

- Controller support for the chat window (CHRON-17). LB + Y opens it; LB / RB change
  the channel, the d-pad and right stick scroll, A opens the chat bar where up and down
  pick a recent command, A sends, B closes. Y walks the speaker filter and X jumps to the
  newest line. Prompts show the buttons while the controller is in use. See Controller
  above. The combo can be changed or switched off in General.
- A recalled command (up / down in the chat bar) is sent on the channel it was first sent
  on, with the keyboard too.
- The arrow keys scroll the log and change the channel tab while the window is open.
- The self-test gains a `pad` step.

## 1.4.15-beta

Sixteenth beta, PrismaUI and Magelight builds.

- Chronicle's part of the suite self-test (CHRON-16): `/selftest` in the chat bar,
  and DovahHub's Diagnostics > Run self-test with DovahHub 0.98. It checks the
  game-thread pump, command dispatch, the history file round trip, the chat
  window's view, that the history and chat window belong to the loaded character,
  and the DovahHub tab and DovahMenu entry. See Self-test above.
- `/selftest <mod>` starts another mod's `<mod>.selftest` command.

## 1.4.14-beta

Fifteenth beta, PrismaUI and Magelight builds.

- Choosing Chronicle in DovahMenu opens Chronicle's settings, not the chat bar (DMN-25).
  The entry shows Shift + the settings hotkey.

## 1.4.13-beta

Fourteenth beta, PrismaUI and Magelight builds.

- The Dovah suite look. The settings window wears the suite's theme kit: spaced
  capitals in Barlow Condensed, gold on dark panels with the diamond corners,
  square toggles, gold sliders and selects, keycap hotkeys and the Esc Close
  prompt. Window settings (theme, anchor, position, size) have their own Window
  tab. The chat window keeps its layout and takes the suite's type and colours in
  the default Skyrim theme. Chronicle's History tab in DovahHub follows DovahHub's
  chosen theme. Every setting works as before.

## 1.4.12-beta

Thirteenth beta, PrismaUI and Magelight builds.

- Chronicle hides while DFC's party panel or command wheel has PrismaUI focus,
  including on the Magelight build (CHRON-15). This follows Hide in menus;
  Chronicle's own interaction stays visible. Visibility is also reconciled when
  focus changes between views without the host's focus menu closing.

## 1.4.11-beta

Twelfth beta, PrismaUI and Magelight builds.

- Chronicle's game work runs from the game's own frame update. Some load orders run
  both of SKSE's task queues on other threads, so 1.4.10's move to the UI queue
  wasn't enough. With DovahHub 0.97.4 or later the work goes to DovahHub's
  game-thread pump; without DovahHub, Chronicle hooks the same call itself, so it
  still works on its own. If the hook can't be installed, the log says why and
  Chronicle uses SKSE's UI tasks as before. The log names the frame's thread at
  startup.

## 1.4.10-beta

Eleventh beta, PrismaUI and Magelight builds.

- Chronicle's game work runs on the game thread. Some load orders run SKSE's
  general task queue on other threads, at the same time as the game's own update;
  Chronicle now uses SKSE's UI task queue, which runs on the game thread, for
  everything that touches the game. The log names the game thread at startup and
  warns once, with the place, if any of Chronicle's game work still finds itself
  on another thread.

## 1.4.9-beta

Tenth beta, PrismaUI and Magelight builds.

- A search or speaker filter no longer carries over to another character. The chat
  window's search and speaker filter, and the search on Chronicle's History tab in
  DovahHub, are cleared when a different character is loaded; reloading the same
  character keeps them.
- A session started from the Main Menu without loading a save or starting a New Game
  no longer writes into the history of the character played before it.

## 1.4.8-beta

Ninth beta, PrismaUI and Magelight builds.

- Translation review in all nine languages (CHRON-14). Chronicle's History tab in
  DovahHub now follows Chronicle's language: its channel chips, search box, Load older,
  empty and error notes and dates were English only. The narrator's name on narrated
  lines, the DovahMenu entry's description, the settings-load warning, the diagnostics'
  yes/no and the raw key details under an unusable hotkey are translated too. The
  leftover words "Auto" and "vanilla" (French, German, Italian, Spanish, Polish,
  Turkish) are translated, and Italian, Spanish and German say PNG, PNJ and NSC
  throughout instead of "NPC". `tests/i18n-coverage.cjs` lists any missing or untranslated text and the
  page tests fail while it finds any. Game text (dialogue, notifications, quest names)
  is never translated; it already arrives in the game's language.

## 1.4.7-beta

Eighth beta, PrismaUI and Magelight builds.

- With DovahMenu installed, Chronicle has its own entry in DovahMenu's list on both
  builds (the Magelight build was missing from it). Opening it shows the chat box and
  its input bar. Without DovahMenu nothing changes (CHRON-13).
- Settings → Language says that translations other than English are machine
  translations, and corrections are welcome, in every language.
- Simplified Chinese is stored as `zh-CN`, the code every Dovah Suite mod uses
  (en, fr, de, it, es, pl, tr, zh-CN, ja, ko). A `Chronicle.json` holding the older
  `zh`, `zh-Hans` or an English language name still reads and is rewritten with the
  code once at load (`Chronicle.log`: "language 'zh' is stored as zh-CN from now on").

## 1.4.6-beta

Seventh beta, PrismaUI and Magelight builds.

- Opening the chat input no longer hides the last messages: the list keeps its
  last line just above the input bar, and the "new messages" badge stays inside
  the list instead of sitting on what you type (CHRON-11).
- Notifications that mods send as translation keys show the translated text, as
  the HUD does ("Assault has started!", not "$Assault has started!"), through the
  game's own translator. A key with no translation is shown without its "$"
  (CHRON-12).

## 1.4.5-beta

Sixth beta, PrismaUI and Magelight builds.

- SkyrimNet's voice directions, such as "[say with a hint of amusement]",
  "[growl]" or "[chuckle]", are no longer shown in the box; the spoken words
  stay. Your own dialogue choices keep their tags ("[Persuade] ...").
- A narrated action inside a SkyrimNet reply ("Herman is right. *Coralyn glances
  toward the necromancer* ...") is shown as its own Narrator line, in order,
  between the spoken parts.
- Fewer repeated lines: a sentence already shown as part of a longer SkyrimNet
  line is left out, and the same NPC repeating a line within 20 seconds is shown
  once. Different NPCs saying the same line are all shown.

## 1.4.4-beta

Fifth beta, PrismaUI and Magelight builds.

- A typed line is now said to someone, so SkyrimNet knows who should answer. Outside
  the Dialogue Menu, start the line with a name ("Sally, what are your thoughts?")
  or look at the NPC before opening the chat. Without either, SkyrimNet gets a
  remark to nobody in particular, which it may leave unanswered.
- `Chronicle.log` names the listener and how it was chosen (dialogue menu, named in
  the line, crosshair), or says there was none.

## 1.4.3-beta

Fourth beta, PrismaUI and Magelight builds.

- SkyrimNet thoughts and narration no longer appear twice, the second time as
  your character speaking. SkyrimNet's line keeps its own label ("Player
  Thoughts"), and a copy of the same text from the player, SkyrimNet or the
  narrator within 20 seconds is left out (`Chronicle.log` says so). Ordinary NPC
  dialogue is not affected.
- A SkyrimNet line written entirely in *asterisks* (third-person narration) goes
  to the Narrator channel as "Narrator" instead of being shown as someone's
  dialogue. The Narrator channel is off by default; turn it on under Channels to
  see these lines.

## 1.4.2-beta

Third beta, PrismaUI and Magelight builds.

- Typed chat now actually reaches SkyrimNet. Chronicle used to announce the line
  with a mod event that SkyrimNet does not listen for; it now calls SkyrimNet's
  API with the line as your character's words, to the NPC you are talking to when
  the Dialogue Menu is open. If NPCs do not answer that way, put
  `"skyrimnet_send": "transform"` at the top level of `Chronicle.json` to use
  SkyrimNet's Transform Dialogue instead (it rewrites the line in character).
- `Chronicle.log` now records each chat submit (input channel and length), the
  SkyrimNet call made, and the code SkyrimNet returns, or why a submit was dropped.
- Magelight build: your typed line no longer goes missing from the box after the
  input bar closes. The view that shows the log replaced its lines with the input
  view's copy taken a moment earlier, losing anything that arrived in between.
- A dialogue option with no text of its own no longer appears as your line under
  its internal name (for example "DialogueGenericHello").

## 1.4.1-beta

Second beta, PrismaUI and Magelight builds.

- The settings key's default is now **Shift+0** instead of Shift+N, so it no longer
  shares a key with MiniPets' feed (Shift+N). Only new settings files get it: if
  you already pressed Apply once, Chronicle keeps the key you saved. To switch,
  press Shift+N, go to Hotkeys, click the Settings box, press 0 and Apply. Or set
  `"config_hotkey": "0"` in `Data/SKSE/Plugins/Chronicle.json` (under MO2 it is in
  Overwrite). Use the 0 above the letters; the number-pad 0 is a different key.
  With the page focused (F8) the key is now matched by its key code, so a digit
  works there too.
- Magelight build: compiled against the Magelight UI 1.7.1 API header. It still
  uses API v4, and Magelight UI 0.30.1 is still the oldest host it runs on.
- `package.ps1 -Label beta` names the release zips (build scripts only).

## 1.4.0-beta

First beta of 1.4.0, with both builds cut from `main`: the PrismaUI release build
and the Magelight UI build (`Chronicle-Magelight-<version>`).

- Everything in the 1.4.0 notes below, in both builds.
- Magelight build: Chronicle no longer shows its log or asks for UI mode before a
  game is loaded (Magelight UI 1.7.1 opens a page for a request made at the main
  menu), and quitting to the main menu hides it. Early requests are refused with a
  line in `Chronicle.log`.
- Both DLLs now carry their Windows file and product version (1.4.0.0).
- The browser preview's settings mock now finishes each save, so later Apply
  clicks are no longer stuck behind the first one.

## 1.4.0 changes

- Other mods can now read and add to Chronicle's log through a native API
  (`src/api/Chronicle_API.h`). DovahHub's "What they told you" will use it to take
  NPC lines from Chronicle instead of reading subtitles itself.
- NPC lines now record who said them (form ID), the dialogue topic's quest and
  kind, the game time and the place. History files from 1.3 still load.
- `/help` and mod commands in the chat input. Lines starting with `/` no longer
  go to SkyrimNet.
- Diagnostics shows the history size and file, API subscribers and commands, and
  the last NPC line's speaker and quest.

## 1.3.2 changes

- The DovahHub tab is now called **Chronicle** and sits under DovahHub's
  Integrations group (DovahHub 0.92.1 puts every mod's tab there).

## 1.3.1 changes

- Chronicle now hides while DovahHub is open, instead of drawing on top of it,
  and comes back when DovahHub closes.

## 1.3.0 changes

- New tab in DovahHub (called **Chronicle**, under Integrations, since 1.3.2):
  this character's Chronicle lines, with channel filters, search, Load older, and new
  lines arriving live. Without DovahHub installed nothing changes.
- The DLL now keeps each character's history itself, in
  `Data/SKSE/Plugins/Chronicle/history/`, following Keep history and Clear
  history. A character's existing chat-window history is copied over the first
  time it loads.
- The Magelight beta build, packaged separately as `Chronicle-Magelight-<version>`
  (see the 1.2.39 notes below), ships the same History tab.

## 1.2.39 changes

- New beta build on Magelight UI, packaged separately as
  `Chronicle-Magelight-<version>`. The native code now talks to the web host
  through one small interface with a PrismaUI backend (the release build,
  unchanged in behaviour) and a Magelight backend. Under Magelight the page
  cannot fetch its own `config.json`, so the DLL sends the first-run defaults
  with the handshake; Magelight's focus menu (`MagelightFocus`) no longer
  counts as a menu that closes Chronicle's input, and it hides Chronicle only
  while another mod's Magelight view holds focus, not while Chronicle's own
  does.
- The Magelight build runs the page as two views: a click-through HUD view
  that shows the log, and an input view that appears only while you type or
  have settings open. Before this, Chronicle's always-visible input view kept
  Magelight's overlay menu open over normal play, so the game treated a menu
  as open: the Dialogue Menu stopped responding, and QuickLoot and the
  Journal would not open. Only the input view saves history and settings, and
  it hands its state to the HUD view when you close it. Not yet verified in
  Skyrim.

## 1.2.38 changes

- Chronicle now hides while a Magelight UI menu has focus (for example
  SeverActions' menus). Magelight opens its own `MagelightFocus` menu for its
  focused views, and PrismaUI draws above Magelight, so Chronicle stayed on top
  of them. It follows the hide-in-menus setting; Magelight overlays that do not
  take focus leave Chronicle alone.

## 1.2.37 changes

- New troubleshooting setting for HUD crashes. Adding
  `"hud_subtitle_capture": false` at the top level of
  `Data/SKSE/Plugins/Chronicle.json` (next to `hud_subtitle_paths`) stops
  Chronicle reading the HUD's subtitle text field, which is the only place it
  touches the HUD. Dialogue subtitles, notifications and everything else keep
  working; ambient lines that only ever appear on the HUD are not logged while
  it is off. Diagnostics shows the HUD path as "disabled (hud_subtitle_capture)"
  when the setting is active. Leave the key out, or set it to `true`, for
  normal use. If Skyrim crashes with Chronicle installed and the crash log
  mentions the HUD, try this setting first and include the result in your
  report.

1.2.36 was a test build.

## 1.2.35 changes

- Fixed a crash in the HUD subtitle reader. Chronicle read the HUD's Scaleform
  movie from SKSE's general task queue, which could race the HUD's own updates
  and crash inside the game (seen with InfinityUI, moreHUD and other HUD mods).
  The HUD is now only read from SKSE's UI task queue.
- Lines you type in the input bar now always show in the All tab, even with
  the Player channel turned off (it is off by default, which hid them).
- Chronicle now hides while SKSE Menu Framework (version 3) has a window open
  that takes your input (its mod settings windows); its overlay widgets do not
  hide it. Chronicle also hides under mod menus opened as SKSE's CustomMenu
  that pause the game or take the cursor, such as Swiftly Order Squad's
  follower commands. CustomMenu widgets that only draw on screen leave
  Chronicle alone.

1.2.33 and 1.2.34 were test builds.

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
  (default on) keeps it hidden after loading. The settings hotkey (Shift+N at the time) also
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

