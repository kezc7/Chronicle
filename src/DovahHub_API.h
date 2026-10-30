#pragma once
// DovahHub_API.h — the DovahHub module host API for SKSE plugins. Header-only, no import library:
// DovahHub.dll is reached by GetModuleHandle/GetProcAddress, so a mod that includes this still
// loads when DovahHub is not installed (RequestApi returns nullptr).
//
// Served by DovahHub 0.92.0 and later (API version 1). Same loader pattern, Result enum, size field
// and version rules as DovahMenu_API.h.
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
//
// Rows and buttons that should take the controller's highlight use the same .wf-* classes and plain
// <button>s as DovahHub's own tabs; the panel is an ordinary tab for LB/RB and the D-pad.
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
        NoSave = 8,           // outfit calls before a save is loaded
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

    // Every call marked "Game thread" returns NotReady (and logs it) when made from another thread;
    // GetOutfitsJson returns {"error":"called off the game thread"}.
    struct DovahHubApi1
    {
        std::uint32_t apiVersion;        // = kApiVersion1
        const char* hostVersion;         // "0.92.0"
        std::uint32_t hostVersionNumber; // MAJOR*10000 + MINOR*100 + PATCH
        const char* host;                // "Magelight" | "PrismaUI" | "None": which UI host DovahHub found

        // Game thread, at kPostPostLoad or kDataLoaded (later works too). Strings are copied; the desc need
        // not outlive the call. Idempotent per id: registering the same id again returns Duplicate and keeps
        // the first. The tab appears the next time DovahHub opens; the page files are loaded from pageDir then.
        Result (*RegisterModule)(const ModuleDesc* desc, ModuleId* outModule);
        // Game thread. The tab goes the next time DovahHub opens.
        Result (*UnregisterModule)(ModuleId module);

        // ANY thread. Queued to the UI thread through SKSE's task interface (DovahHub's OBody rule), so it is
        // safe from a worker or from inside a game event. The page receives it as
        // window.dovahhubModulePush("<id>", json) once the view exists; before that it is dropped (NotReady).
        // The JSON is copied and checked; over 1 MiB, or not JSON, is InvalidArgument.
        Result (*PushJson)(ModuleId module, const char* json);

        // Game thread. Open DovahHub on a tab: a module id ("stable") or a core tab id: people, companions,
        // relationships, family, character, stats, milestones, wardrobe, homes, journey, library, bestiary,
        // loot, settings (and skills, inventory, magic, story, quests, map, mods when that optional module is
        // installed). DovahMenu uses this for "DovahHub › Stables" entries. Opens the hub as its hotkey would
        // when it is closed; switches tabs when it is already open.
        Result (*OpenTab)(const char* moduleOrTabId);

        // Game thread, once after RegisterModule (again replaces it). Adds the module's options to DovahHub's
        // Settings tab, so DovahHub becomes the suite's MCM. schemaJson: {"title":"MiniPets","settings":[
        //   {"key":"scale","label":"Card size","type":"range","min":50,"max":200,"step":10,"value":100},
        //   {"key":"clock","label":"Show the clock","type":"toggle","value":true},
        //   {"key":"style","label":"Card style","type":"choice","options":[{"value":"plain","label":"Plain"},...],"value":"plain"},
        //   {"key":"title","label":"Card title","type":"text","max":40,"value":""}]}
        // "note" on any setting is shown under its label; options may also be plain strings. When the player
        // changes one, your onRequest gets request "setting" with {"key":...,"value":...}; you write your own
        // INI. The values shown come from your onRequest "settings" reply each time Settings opens ("value"
        // in the schema until then). Over 64 KiB, or not JSON, is InvalidArgument.
        Result (*AddSettings)(ModuleId module, const char* schemaJson);

        // Any thread. True while the DovahHub page is on screen.
        bool (*IsOpen)();

        // ---- outfit calls (game thread; a save must be loaded) -------------------------------------
        //
        // Actor keys are FormKeys of the actor's base or reference ("Skyrim.esm|0x13BAB" for Lydia;
        // "player" for the player). Unique NPCs only: a generic NPC's outfit is shared by every copy.
        // Outfit ids are DovahHub's wardrobe.json ids (shared by every save) for NPCs, and the player's own
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
    };

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
    inline constexpr std::uint32_t kMsgOutfitChanged = 0x44484203;
    // 'DHB' 4   reserved for "DovahHub's per-character state is ready"; not sent by API version 1.
    inline constexpr std::uint32_t kMsgSaveLoaded = 0x44484204;
}
