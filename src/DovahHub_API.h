#pragma once
// DovahHub_API.h — the DovahHub module host API for SKSE plugins. Header-only, no import library:
// DovahHub.dll is reached by GetModuleHandle/GetProcAddress, so a mod that includes this still
// loads when DovahHub is not installed (RequestApi returns nullptr).
//
// Served by DovahHub 0.92.0 and later (API version 1). Same loader pattern, Result enum, size field
// and version rules as DovahMenu_API.h. DovahHub 0.95.0 appends Close, AddStoryEvent, AddSection and
// SetSettingStatus to the end of the table, and the "button", "status" and "binding" setting types: check
// hostVersionNumber before using them (Has095 below does), so a module still runs on 0.92 to 0.94.
// DovahHub 0.95.17 appends GetLanguage (HasLanguage), and the "settings" and "section" requests carry
// "language"; a module built before then never sees either and works as it did.
// DovahHub 0.96.4 sends kMsgStoryEvent, and the player's kMsgOutfitChanged names the outfit
// (HasStoryMessages). Nothing in the table changes; a module that doesn't listen sees no difference.
// DovahHub 0.97.0 appends the reads (GetBestiaryJson to RemoveLibraryEntry, HasKeepsakes), the "people"
// section tab and the "libraryOpen" request.
// DovahHub 0.97.4 appends the game-thread pump: RunOnGameThread, IsGameThread and GameThreadId (HasPump).
// DovahGameThread.h, beside this header, uses it when it's there and hooks the frame itself when it isn't.
//
// DovahHub is the suite's character hub: one page with tabs grouped under People, Character,
// Journey, Collections, Integrations and Settings, drawn on Magelight UI or PrismaUI. A PrismaUI view can only be
// listened to by the DLL that created it, so a sibling mod cannot put a page of its own into DovahHub
// and talk to it directly. This API is the relay: the sibling registers a MODULE (a tab, its page
// files, and a request callback); DovahHub loads the page files into its own view, forwards the
// page's calls to the sibling's DLL, and hands the sibling a PushJson it can call to update the page.
// Every module tab goes under DovahHub's Integrations group, so name the tab after your mod.
// The sibling ships its page files inside DovahHub's view folder, under modules/<id>/, and DovahHub
// never reads the sibling's co-save: everything crosses this API as JSON.
//
//   #include "DovahHub_API.h"
//
//   // game thread; return JSON (the host copies it before you return) or nullptr for "unknown request"
//   static const char* OnStableRequest(const char* request, const char* json, void*) {
//       if (std::strcmp(request, "list") == 0) return Stable::HorsesJson();  // owned by the sibling
//       return nullptr;
//   }
//   static void OnStableEvent(const char* event, void*) { /* "opened", "closed", "tabShown", "tabHidden" */ }
//
//   // at SKSE kPostPostLoad or kDataLoaded (every plugin DLL is loaded by then):
//   if (const auto* hub = DOVAHHUB_API::RequestApi()) {
//       DOVAHHUB_API::ModuleDesc d{};
//       d.size      = sizeof(d);
//       d.id        = "stable";               // also the folder: PrismaUI/views/DovahHub/modules/stable/
//       d.tab       = "DovahStable";          // shown under Integrations
//       d.pageDir   = "modules/stable/";      // index.html, *.css, *.js, relative to DovahHub's view
//       d.version   = "0.3.0";
//       d.onRequest = &OnStableRequest;
//       d.onEvent   = &OnStableEvent;
//       hub->RegisterModule(&d, &g_stableModule);
//   }
//
//   // later, from any thread (a horse was renamed): the page's window.dovahhubModulePush("stable", json)
//   hub->PushJson(g_stableModule, R"({"renamed":{"formId":"Skyrim.esm|0x109C5","name":"Frost"}})");
//
// The page side. DovahHub reads the module's index.html, *.css and *.js (pageDir, not its
// subfolders; css and js in name order) through the game's Data view and puts them in a tab panel of
// its own. The module's scripts get one object and nothing else of DovahHub's:
//
//   const mod = window.dovahhubModule("stable");
//   mod.root                         // the tab panel element the index.html was put into
//   mod.call("list", {}).then(...)   // a Promise of your onRequest reply (parsed JSON), through the relay;
//                                    // {"error":"unknown request"} for nullptr, {"error":"no such module"}
//   mod.onPush(json => ...)          // what PushJson sent
//   mod.onEvent(name => ...)         // "tabShown", "tabHidden"
//   mod.confirm({text, yes, no, danger}).then(yes => ...)   // 0.95.1+: DovahHub's question box, works with a
//                                    // controller (window.confirm doesn't); false for cancel or B
//
// Rows and buttons that should take the controller's highlight use the same .wf-* classes and plain
// <button>s as DovahHub's own tabs; the panel is an ordinary tab for LB/RB and the D-pad. MODULE-STYLE.md,
// beside this header, lists the classes, variables and rules that make a page look like part of DovahHub.
//
// C++17. Depends only on <windows.h> (for the loader; define DOVAHHUB_API_NO_LOADER to drop it)
// and the standard library. Include CommonLib (or your precompiled header) before this header.

#include <cstdint>

#ifndef DOVAHHUB_API_NO_LOADER
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace DOVAHHUB_API
{
    inline constexpr std::uint32_t kApiVersion1 = 1;

    using ModuleId = std::uint32_t;   // 0 = invalid

    enum class Result : std::int32_t
    {
        Ok = 0,
        HostAbsent = 1,       // consumer-side only: RequestApi returned nullptr
        InvalidArgument = 2,  // nullptr desc, missing id/onRequest, tab without pageDir, bad size, id not a slug, bad JSON
        Duplicate = 3,        // a module with that id (case-insensitive) is already registered
        InvalidModule = 4,    // unknown or unregistered ModuleId
        NotReady = 5,         // the hub page does not exist yet (host UI absent, or before DOM ready)
        Busy = 6,             // OpenTab while another mod holds the host's UI mode
        NotFound = 7,         // OpenTab: no tab or module with that id; outfit calls: no such outfit or actor
        NoSave = 8,           // outfit calls before a save is loaded; OpenTab at the main menu or during a load
        Refused = 9,          // outfit calls while the actor is busy (dead, disabled, in combat, being previewed)
    };

    // ---- Modules: a sibling's tab inside DovahHub -------------------------------------------------

    // Game thread. The page asked your module something (mod.call(request, payload) on the page).
    // json is the payload as JSON text ("{}" when the page sent none). Return a JSON string the host
    // copies before you return (keep it alive until then: a static or a member is fine), or nullptr for
    // "unknown request" (the page gets {"error":"unknown request"}). A reply that isn't JSON reaches the
    // page as {"error":"bad reply"}. Do not touch the UI here, and do not call back into the host from
    // inside this callback.
    //
    // Two requests come from DovahHub itself when you called AddSettings: "settings" (reply with the
    // current values, {"<key>":value,...}) and "setting" ({"key":...,"value":...}: the player changed one).
    // From 0.95.0 also "action" ({"key":...}: the player pressed a "button" setting; reply {} or
    // {"message":"Done."}, shown beside the button) and, after AddSection, "section" ({"tab":...,"actor":...}:
    // reply with the rows to draw, see AddSection).
    using ModuleRequestFn = const char* (*)(const char* request, const char* json, void* user);

    // Game thread. event is "opened" (DovahHub opened), "closed", "tabShown" (your tab is on screen),
    // "tabHidden". tabShown is the moment to PushJson a fresh snapshot. Opening DovahHub on your tab
    // sends "opened" then "tabShown"; closing it on your tab sends "tabHidden" then "closed".
    using EventFn = void (*)(const char* event, void* user);

    struct ModuleDesc
    {
        std::uint32_t size;       // = sizeof(ModuleDesc) — forward-compat. Must cover at least onRequest;
                                  // onEvent and user are read only when size covers them
        const char* id;           // slug [a-z0-9_-], 1..32 chars: "stable", "pets", "works", "chronicle", "dfc", "wheel", "damage".
                                  // Upper case is accepted and kept as lower case (so "Stable" after "stable" is Duplicate)
        const char* tab;          // tab title as shown under Integrations, in alphabetical order: your mod's
                                  // name, "DovahStable". nullptr = no tab: a settings-only module (DovahDamage)
                                  // that exists for AddSettings and the diagnostics row
        const char* group;        // ignored: every module tab is under Integrations. Kept so the layout stays
        const char* pageDir;      // "modules/<id>/" under PrismaUI/views/DovahHub/ (index.html, *.css, *.js); the
                                  // sibling's zip ships these files; DovahHub reads them through the game's Data view.
                                  // Relative, no "..". nullptr only when tab is nullptr
        const char* version;      // "1.2.3"; shown in Settings → Diagnostics → Dovah suite; optional
        ModuleRequestFn onRequest;  // required
        EventFn onEvent;          // optional
        void* user;               // handed back to both callbacks
        bool menuRow;             // true: DovahHub adds a "DovahHub › <tab>" DovahMenu entry that opens your tab.
                                  // Set it only when your mod has no DovahMenu entry of its own (no PrismaUI view
                                  // folder or Magelight manifest of its own), or DovahMenu lists the mod twice.
                                  // Page-only modules (module.json) always get the entry. Read only when size covers it
    };

    // ---- Outfits: DovahHub's Wardrobe owns follower and player outfits --------------------------
    //
    // DFC's "About what you wear..." dialogue and its NFF importer call these instead of keeping an
    // outfit system of their own. Everything is keyed by FormKey strings ("Plugin.esp|0x00F0C1") so
    // no game pointer crosses the DLL boundary.

    enum class Situation : std::uint8_t
    {
        Everyday = 0,   // adventuring: the usual outfit
        Town = 1,
        Home = 2,
        Bed = 3,
    };

    inline constexpr std::uint32_t kHostWith095 = 9500;  // PackVersion(0, 95, 0): Close to SetSettingStatus
    inline constexpr std::uint32_t kHostWithLanguage = 9517;  // PackVersion(0, 95, 17): GetLanguage
    inline constexpr std::uint32_t kHostWithStoryMessages = 9604;  // PackVersion(0, 96, 4): kMsgStoryEvent
    inline constexpr std::uint32_t kHostWithKeepsakes = 9700;  // PackVersion(0, 97, 0): GetBestiaryJson to RemoveLibraryEntry
    inline constexpr std::uint32_t kHostWithPump = 9704;  // PackVersion(0, 97, 4): RunOnGameThread, IsGameThread, GameThreadId

    // Only RegisterModule, UnregisterModule and AddSettings (and from 0.95.0 AddStoryEvent, AddSection and
    // SetSettingStatus) may be called off the game thread (some loaders send kDataLoaded from a loading thread). Every call marked "Game thread" returns NotReady (and logs it)
    // when made from another thread;
    // GetOutfitsJson returns {"error":"called off the game thread"}.
    //
    // "The game thread" is the game's main thread, RE::Main::GetSingleton()->threadID. In some load orders
    // SKSE's AddTask runs tasks on other threads as well, so being in a task proves nothing. DovahHub calls
    // your onRequest, onEvent and every SKSE message on the game thread (from 0.97.2 also in those load orders).
    // To call DovahHub from your own code: call it from those callbacks, or queue the work with SKSE's
    // AddUITask (UI tasks run on the game thread), or check RE::Main's threadID yourself; not from AddTask.
    // From 0.97.4: in some load orders UI tasks run on worker threads too, so the game thread is the thread the
    // game's frame update runs on, as DovahHub's pump sees it (GameThreadId). Queue work with RunOnGameThread,
    // or with DovahGameThread.h, which does that for you; never with SKSE's AddTask or AddUITask.
    struct DovahHubApi1
    {
        std::uint32_t apiVersion;        // = kApiVersion1
        const char* hostVersion;         // "0.92.0"
        std::uint32_t hostVersionNumber; // MAJOR*10000 + MINOR*100 + PATCH
        const char* host;                // "Magelight" | "PrismaUI" | "None": which UI host DovahHub found

        // Any thread, at kPostPostLoad or kDataLoaded (later works too; some loaders send kDataLoaded off the
        // game thread, so this one call is taken from any thread). Strings are copied; the desc need
        // not outlive the call. Idempotent per id: registering the same id again returns Duplicate and keeps
        // the first. The tab appears the next time DovahHub opens; the page files are loaded from pageDir then.
        Result (*RegisterModule)(const ModuleDesc* desc, ModuleId* outModule);
        // Any thread. The tab goes the next time DovahHub opens.
        Result (*UnregisterModule)(ModuleId module);

        // ANY thread. Queued to the game thread (from 0.97.4 by DovahHub's pump; before, SKSE's task
        // interface), so it is safe from a worker or from inside a game event. The page receives it as
        // window.dovahhubModulePush("<id>", json) once the view exists; before that it is dropped (NotReady).
        // The JSON is copied and checked; over 1 MiB, or not JSON, is InvalidArgument.
        Result (*PushJson)(ModuleId module, const char* json);

        // Game thread. Open DovahHub on a tab: a module id ("stable") or a core tab id: people, companions,
        // relationships, family, character, stats, milestones, wardrobe, homes, journey, library, bestiary,
        // loot, settings (and skills, inventory, magic, story, quests, map, mods when that optional module is
        // installed). DovahMenu uses this for "DovahHub › Stables" entries. Opens the hub as its hotkey would
        // when it is closed; switches tabs when it is already open. NoSave until a game is loaded and the
        // player is in the world.
        Result (*OpenTab)(const char* moduleOrTabId);

        // Any thread, once after RegisterModule (again replaces it). Adds the module's options to DovahHub's
        // Settings tab, so DovahHub becomes the suite's MCM. schemaJson: {"title":"MiniPets","settings":[
        //   {"key":"scale","label":"Card size","type":"range","min":50,"max":200,"step":10,"value":100},
        //   {"key":"clock","label":"Show the clock","type":"toggle","value":true},
        //   {"key":"style","label":"Card style","type":"choice","options":[{"value":"plain","label":"Plain"},...],"value":"plain"},
        //   {"key":"title","label":"Card title","type":"text","max":40,"value":""}]}
        // From 0.95.0 three more types:
        //   {"key":"rebuild","label":"Rebuild the list","type":"button","text":"Rebuild"}  -> onRequest "action" {"key"}
        //   {"key":"forge","label":"Forges","type":"status","value":"Starting"}            -> SetSettingStatus changes it
        //   {"key":"summon","label":"Call your horse","type":"binding","allow":"both"}       -> "setting" with the binding:
        //      {"device":"keyboard","code":34,"name":"G","shift":false,"ctrl":false,"alt":false} (code: the
        //      DirectInput scan code, as the game's own key events give it), {"device":"gamepad","button":"north",
        //      "modifier":"leftShoulder","name":"LB + Y"} (modifier "" for one button; the control names DovahHub's
        //      own open chord uses), or null when the player cleared it. "allow": "keyboard", "gamepad" or "both"
        //      (the default). Reply to "settings" with the same object to show the binding. The pad's B and
        //      Escape cancel a capture, so neither can be bound.
        // A host before 0.95.0 shows those three as text fields: send them only when Has095(hub).
        // "note" on any setting is shown under its label; options may also be plain strings. When the player
        // changes one, your onRequest gets request "setting" with {"key":...,"value":...}; you write your own
        // INI. The values shown come from your onRequest "settings" reply each time Settings opens ("value"
        // in the schema until then). From 0.95.17 the "settings" request's json is {"language":"de"}, DovahHub's
        // language, and it comes again when the player changes it. Over 64 KiB, or not JSON, is InvalidArgument.
        Result (*AddSettings)(ModuleId module, const char* schemaJson);

        // Any thread. True while the DovahHub page is on screen.
        bool (*IsOpen)();

        // ---- outfit calls (game thread; a save must be loaded) -------------------------------------
        //
        // Actor keys are FormKeys of the actor's base or reference ("Skyrim.esm|0x13BAB" for Lydia;
        // "player" for the player). Unique NPCs only: a generic NPC's outfit is shared by every copy.
        // Outfit ids are DovahHub's save-specific NPC outfit ids for NPCs, and the player's own
        // outfit ids (kept per save, in DovahHub's co-save record) for "player".

        // Save what the actor is wearing now (armour, jewellery, no quest pieces) as a named outfit in
        // DovahHub's library. An existing name (any case) is updated in place. The outfit belongs to the
        // library, not the actor: assign it with EquipOutfit or SetSituationOutfit. Pieces are matched by
        // FormKey. Done before it returns; nothing is re-equipped.
        Result (*SaveOutfit)(const char* actorKey, const char* name, std::uint32_t* outOutfitId);

        // Make the outfit the actor's usual (Everyday) outfit and dress them now. 0 = their own outfit
        // (undo DovahHub's assignment; for "player" it is InvalidArgument). Queued to the main thread;
        // returns Ok when queued, Refused when the actor is busy.
        Result (*EquipOutfit)(const char* actorKey, std::uint32_t outfitId);

        // Set the outfit for one situation. outfitId 0 = "their usual", -1 (0xFFFFFFFF) = "their own
        // outfit". DovahHub's situation tick (every 300 ms, one actor per pass) does the changing;
        // nothing is re-equipped by this call itself. For "player" a non-zero outfit also turns the
        // player's automatic outfits on, and -1 means the same as 0.
        Result (*SetSituationOutfit)(const char* actorKey, Situation situation, std::uint32_t outfitId);

        // Game thread. JSON of the library and the actor's assignments, for a dialogue menu or an importer
        // to show: {"outfits":[{"id":3,"name":"Town"}],"usual":3,"situations":{"everyday":0,"town":3,"home":0,
        // "bed":-1}}. actorKey nullptr or "" gives the library only. "player" adds "automatic":true|false.
        // {"error":"..."} for an unknown actor or before a save is loaded.
        // The host copies nothing: the pointer is valid until the next call on the game thread.
        const char* (*GetOutfitsJson)(const char* actorKey);

        // ---- appended in 0.95.0: check Has095(hub) first ----------------------------------------------

        // Game thread. Closes DovahHub as Escape would, for a mod that takes the screen next (DovahMenu
        // handing over to another menu). Ok when it was open; NotReady when it wasn't.
        //   if (DOVAHHUB_API::Has095(hub) && hub->IsOpen()) hub->Close();
        Result (*Close)();

        // Any thread; recorded on the game thread a moment later. One moment of the player's life for Your
        // Story, sent as the module sees it happen (send new happenings only: what was already true when the
        // module started is not news). eventJson, at most 4 KiB:
        //   {"key":"horse:Skyrim.esm|0x109C5:bought",   // required, 1..200 chars: one key is told once per save
        //    "kind":"joined",                           // required, below
        //    "subject":"Frost",                         // who or what, at most 120 chars
        //    "detail":"bought at Riften's stables",     // optional, at most 240 chars, after the sentence
        //    "value":0,                                 // optional, 0..1000000
        //    "date":42.5,                               // optional: game days passed when it happened (now if absent)
        //    "text":"Frost carried me out of Riften.",  // optional, at most 240 chars: your own line, used as is
        //    "photo":true}                              // optional: a Your Story picture of the moment, when the
        //                                               // player has pictures on (at most one every 20 seconds)
        // Kinds, the story category each one follows, and DovahHub's sentence (shown in the first person; the
        // player's perspective setting changes "I" and "me"):
        //   "joined"    life         "Frost joined me."
        //   "acquired"  life         "I came by Frost."           (bought, built or given)
        //   "lost"      life         "I lost Frost."              (a pet died, a horse was sold)
        //   "built"     progress     "I built a smelter."
        //   "achieved"  progress     "I achieved Master Smith."   (a rank, a title, a record)
        //   "found"     exploration  "I found the Dwemer cog."
        //   "defeated"  combat       "I defeated the Frost Troll."
        //   "note"      life         your "text" (required for this kind)
        // "text" replaces the sentence for any kind; write it in the first person or neutrally, since DovahHub
        // can't rewrite it for the third-person settings. Life moments are recorded whenever the story is on;
        // the others only while the player records that category. Ok (queued), Duplicate (that key is already
        // in this save's story), Refused (the story, or that kind's category, is off), NoSave (no game loaded),
        // InvalidModule, InvalidArgument (not JSON, no key, an unknown kind, "note" without text).
        // DovahHub 0.94 and older read these moments as milestones, so a save keeps its story if DovahHub
        // goes back a version.
        //   hub->AddStoryEvent(g_stable, R"({"key":"horse:Skyrim.esm|0x109C5","kind":"joined","subject":"Frost"})");
        Result (*AddStoryEvent)(ModuleId module, const char* eventJson);

        // Any thread. A block of your rows on one of DovahHub's own tabs: tabId "companions" (the selected
        // companion), "character" (the player) or "bestiary" (the selected foe). Each time that tab shows
        // someone, DovahHub calls your onRequest("section", {"tab":tabId,"actor":...,"language":"de"}) on the game thread and
        // draws the reply, [{"label":"Fed","value":"2 hours ago"},...] (at most 16 rows; a label at most 40
        // chars, a value at most 120), under the title. actor is the companion's FormKey ("Skyrim.esm|0xA2C94"),
        // "player", or for the bestiary the foe's name with its race beside it:
        // {"tab":"bestiary","actor":"Frost Troll","race":"Troll"}. Reply [] to show nothing for that one.
        // One block per module per tab: again changes its title; title nullptr or "" removes it.
        // InvalidArgument for another tabId or a title over 64 chars.
        //   hub->AddSection(g_pets, "companions", "Pet");
        Result (*AddSection)(ModuleId module, const char* tabId, const char* title);

        // Any thread. The text of a "status" setting (AddSettings), such as "2 forges running"; at most 200
        // chars. Shown at once while Settings is on screen, and kept for the next time it opens. NotFound
        // when the module's settings have no "status" setting with that key.
        //   hub->SetSettingStatus(g_works, "forge", "2 forges running");
        Result (*SetSettingStatus)(ModuleId module, const char* key, const char* text);

        // ---- appended in 0.95.17: check HasLanguage(hub) first -----------------------------------------

        // Any thread. DovahHub's interface language, one of the suite's codes: "en", "fr", "de", "it", "es",
        // "pl", "tr", "zh-CN", "ja", "ko". The pointer stays valid for the whole game. For text a module
        // writes outside DovahHub's requests (its own HUD lines, notifications): a module whose Language
        // setting is "Follow DovahHub" uses this. The "settings" and "section" requests carry the same code
        // as "language" (from 0.95.17), and DovahHub asks both again when the player changes its language.
        //   const char* lang = DOVAHHUB_API::HasLanguage(hub) ? hub->GetLanguage() : "en";
        const char* (*GetLanguage)();

        // ---- appended in 0.97.0: check HasKeepsakes(api) first ------------------------------------------
        // Reads of what DovahHub keeps for the loaded save. Game thread: off it they answer "" (and log it).
        // Each answer is UTF-8 JSON that lives until the next read on the same thread. Keys are in the
        // "Skyrim.esm|0x13BAB" form; "player" is the player. Lists answer {"total":N,"items":[...]}: limit 0
        // gives 100, the most is 500. Before a save is loaded, lists are empty and the others "{}".

        // The Bestiary, most kills first: {"name","race","category","kills","maxLevel",
        // "first":{"days","date","place"},"last":{...}}.
        const char* (*GetBestiaryJson)(std::uint32_t limit, std::uint32_t offset);
        // Everyone People lists: {"key","name","race","female","rank" (DovahHub's rank name, custom ranks
        // included; "" when none),"gameRank" (the game's, -4 to 4),"following","dead","met" ("talked" or "seen"),
        // "place","hold" (where they are now)}. The first read after a load scans and can take a moment.
        const char* (*GetPeopleJson)(std::uint32_t limit, std::uint32_t offset);
        // One person, as above, plus "told": the newest lines of What they told you (at most lines, at most 50;
        // text at most 240 bytes, cut on a whole character): {"text","place","date","days","kind"}. {} when
        // People doesn't list them.
        const char* (*GetPersonJson)(const char* actorKey, std::uint32_t lines);
        // The homes the player owns (unbought ones never): {"key","name","hold","holdKey","cells":[every
        // interior cell of the home],"why" ("plot","rooms","key","quest" or "confirmed")}. key is the one the
        // "home" story event carries as subjectId.
        const char* (*GetHomesJson)();
        // The picture DovahHub has of them: the one chosen for them, otherwise their automatic portrait (the
        // player: the character card picture). {"path" (absolute),"width","height","kind" ("chosen",
        // "automatic" or "card")}, PNG or JPEG at the size it was saved; {} when there's none. Never made here.
        const char* (*GetPortraitJson)(const char* actorKey);
        // A keepsake of yours on the Library's Keepsakes shelf, kept with the save (any thread):
        // {"key" (1..120, one per module per save; again = update),"title" (at most 120, in the player's
        // language),"kind" ("letter", "poster"...),"subject" (optional key),"days" (optional)}.
        // Opening it opens your tab and sends you the request "libraryOpen" {"key","language"}.
        // Ok, InvalidArgument, InvalidModule, NoSave, Refused (2000 entries already).
        Result (*AddLibraryEntry)(ModuleId module, const char* entryJson);
        Result (*RemoveLibraryEntry)(ModuleId module, const char* key);

        // ---- appended in 0.97.4: check HasPump(api) first -------------------------------------------------
        // DovahHub hooks the game's main loop where it calls the frame update (Main::Update), and runs queued
        // work there, at the start of each frame. That is the one place sure to be the game's main thread,
        // whatever SKSE's task queues do in a load order.
        //
        // Any thread. fn(ctx) runs once, on the game thread, at the start of the next frame, in posting order
        // with everything else posted there. Each frame runs what was queued when it started: work posted from
        // inside fn waits for the next frame, so nothing chains within one. Work posted before the first frame
        // (at kDataLoaded, during the first load) waits for it. fn is a plain function pointer: box what it needs
        // on the heap, pass it as ctx and free it in fn; it must not throw. If DovahHub couldn't install its hook
        // (the log says why), the work goes to SKSE's UI tasks instead. Ok; InvalidArgument for a null fn.
        Result (*RunOnGameThread)(void (*fn)(void* ctx), void* ctx);
        // Any thread. True only on the thread the frame update runs on; false everywhere before the first frame.
        bool (*IsGameThread)();
        // That thread's id; 0 before the first frame, or when the hook couldn't be installed.
        std::uint32_t (*GameThreadId)();
    };

    // True when the host serves the 0.95.0 calls (Close, AddStoryEvent, AddSection, SetSettingStatus) and
    // the "button", "status" and "binding" setting types.
    inline bool Has095(const DovahHubApi1* api)
    {
        return api && api->hostVersionNumber >= kHostWith095;
    }

    // True when the host serves GetLanguage (and sends "language" with "settings" and "section").
    inline bool HasLanguage(const DovahHubApi1* api)
    {
        return api && api->hostVersionNumber >= kHostWithLanguage;
    }

    // True when the host serves GetBestiaryJson to RemoveLibraryEntry, the "people" section and "libraryOpen".
    inline bool HasKeepsakes(const DovahHubApi1* api)
    {
        return api && api->hostVersionNumber >= kHostWithKeepsakes;
    }

    // True when the host serves RunOnGameThread, IsGameThread and GameThreadId.
    inline bool HasPump(const DovahHubApi1* api)
    {
        return api && api->hostVersionNumber >= kHostWithPump && api->RunOnGameThread && api->IsGameThread && api->GameThreadId;
    }

    // True when the host sends kMsgStoryEvent and the player's outfit fields in kMsgOutfitChanged.
    inline bool HasStoryMessages(const DovahHubApi1* api)
    {
        return api && api->hostVersionNumber >= kHostWithStoryMessages;
    }

    inline std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
    {
        return major * 10000 + minor * 100 + patch;
    }

#ifndef DOVAHHUB_API_NO_LOADER
    // nullptr when DovahHub is not installed or does not serve this version.
    inline const void* RequestApiRaw(std::uint32_t version)
    {
        const HMODULE mod = GetModuleHandleW(L"DovahHub.dll");
        if (!mod) {
            return nullptr;
        }
        using RequestFn = const void* (*)(std::uint32_t);
        const auto request = reinterpret_cast<RequestFn>(GetProcAddress(mod, "DovahHub_RequestApi"));
        return request ? request(version) : nullptr;
    }

    inline const DovahHubApi1* RequestApi()
    {
        return static_cast<const DovahHubApi1*>(RequestApiRaw(kApiVersion1));
    }
#endif

    // ---- SKSE messaging (push side) -----------------------------------------------------------
    //
    // Pull data with the table above; push events with SKSE messaging so several mods can listen.
    // DovahHub dispatches these with SKSE::GetMessagingInterface()->Dispatch(type, data, size, nullptr),
    // so any plugin that RegisterListener("DovahHub", ...) receives them. Data is a UTF-8 JSON string
    // (data = const char*, size = length + 1). All arrive on the game thread.
    inline constexpr std::uint32_t kMsgOpened = 0x44484201;         // 'DHB' 1   {}   DovahHub opened
    inline constexpr std::uint32_t kMsgClosed = 0x44484202;         // 'DHB' 2   {}   DovahHub closed
    // 'DHB' 3   {"actor":"<FormKey>"|"player","outfit":3,"situation":"town"}: Wardrobe re-dressed someone,
    // whichever path did it (the Wardrobe tab, the situation tick, a load, a cell attach, this API).
    // outfit 0 = their own outfit (or, for the player, the outfit isn't known).
    // From 0.96.4 every one has "reason": what dressed them, "wardrobe" (the Wardrobe tab), "situation"
    // (they moved, or the player's automatic outfits), "load" (a save loaded), "attach" (their cell loaded)
    // or "api" (another mod). The player's also says which of the player's outfits went on (a different
    // list from Wardrobe's): {"actor":"player","outfit":0,"situation":"home","reason":"situation",
    // "playerOutfit":2,"name":"Home clothes","how":"automatic"|"manual"}. playerOutfit 0 = what they had on
    // before an automatic change.
    inline constexpr std::uint32_t kMsgOutfitChanged = 0x44484203;
    // 'DHB' 4   reserved for "DovahHub's per-character state is ready"; not sent by API version 1.
    inline constexpr std::uint32_t kMsgSaveLoaded = 0x44484204;
    // 'DHB' 5   from 0.96.4: Your Story recorded an event (only while it's recording that event's category,
    // as the story itself does). One per event, on the game thread, after it's saved:
    //   {"id":417,"kind":"joined","category":"life","source":"stable","module":"stable",
    //    "key":"horse:Skyrim.esm|0x109C5:bought","subject":"Frost","subjectId":"","detail":"","value":0,
    //    "location":"Whiterun Stables","date":"Morndas, 17th of Last Seed","days":12.5,"text":"Frost joined me."}
    // For an event a module sent with AddStoryEvent: kind is that module's kind ("joined", "acquired", "lost",
    // "built", "achieved", "found", "defeated", "note"), source and module its id. For DovahHub's own: source
    // "dovahhub", module "", kind DovahHub's ("companion" when a follower joins, "marriage", "adoption", "home",
    // "milestone", "quest-complete", "discovery", "level", "skill", "dragon", "soul", ...). category is
    // "life", "progress", "quests", "exploration" or "combat". A milestone tier is kind "milestone" with
    // "tier" (1 = the first) and "title" (the tier's name in DovahHub's language, "Wanderer II"); value is then
    // the count it was earned at. subject is "" when there's none. text is the story's English sentence;
    // dates are the game's English. Ids only grow within a save.
    inline constexpr std::uint32_t kMsgStoryEvent = 0x44484205;
}
