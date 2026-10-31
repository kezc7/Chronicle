#pragma once
// Chronicle_API.h — the Chronicle ChatBox API for SKSE plugins. Header-only, no import library:
// Chronicle.dll is reached by GetModuleHandle/GetProcAddress, so a mod that includes this still
// loads when Chronicle is not installed (RequestApi returns nullptr).
//
// Served by Chronicle 1.4.0 and later (API version 1), in both builds. Same loader pattern, Result
// enum, size field and version rules as DovahMenu_API.h. Vendor this file unchanged.
//
// Chronicle is the suite's chat and message log: it captures subtitles and notifications into a
// PrismaUI (or Magelight) chat window. Since 1.3.0 Chronicle's DLL keeps each character's log
// natively: the newest 1000 lines, in Data/SKSE/Plugins/Chronicle/history/<character id>.json
// (following the player's Keep history setting). The chat window and DovahHub's Chronicle tab read
// it; this API lets other DLLs read it too:
//
// - DovahHub's "Chat history" tab reads GetMessages and Subscribes for live lines;
// - DovahHub's "What they told you" (Talk.cpp) SUBSCRIBES instead of polling subtitles itself when
//   Chronicle answers. Decided 2026-10-30: Chronicle is the one capture. For that, every message
//   carries what Talk stores: speaker form ID, the line, the topic's owner quest, game time, real time;
// - DovahWorks (and anyone) registers "/dworks ..." style commands through RegisterCommand.
//
//   #include "Chronicle_API.h"
//
//   // game thread; a copy of the message that is valid only during the call
//   static void OnLine(const CHRONICLE_API::Message* m, void*) {
//       if (m->kind == CHRONICLE_API::Kind::Subtitle && m->speakerFormId) Talk::Record(*m);
//   }
//   static bool OnCommand(const char* command, const char* args, void*) {   // "dworks", "dump all"
//       return Works::Debug(args);     // true = handled; false = "unknown command" in the box
//   }
//
//   // at SKSE kPostPostLoad or kDataLoaded:
//   if (const auto* chat = CHRONICLE_API::RequestApi()) {
//       CHRONICLE_API::SubscribeDesc s{}; s.size = sizeof(s); s.channels = "npc"; s.onMessage = &OnLine;
//       chat->Subscribe(&s, &g_sub);
//       CHRONICLE_API::CommandDesc c{}; c.size = sizeof(c); c.prefix = "dworks"; c.help = "dworks dump|resort|status"; c.onCommand = &OnCommand;
//       chat->RegisterCommand(&c, &g_cmd);
//   }
//
// C++17. Depends only on <windows.h> (for the loader; define CHRONICLE_API_NO_LOADER to drop it)
// and the standard library. Include CommonLib (or your precompiled header) before this header.

#include <cstdint>

#ifndef CHRONICLE_API_NO_LOADER
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace CHRONICLE_API
{
    inline constexpr std::uint32_t kApiVersion1 = 1;

    using SubscriptionId = std::uint32_t;   // 0 = invalid
    using CommandId = std::uint32_t;        // 0 = invalid

    enum class Result : std::int32_t
    {
        Ok = 0,
        HostAbsent = 1,       // consumer-side only: RequestApi returned nullptr
        InvalidArgument = 2,  // nullptr desc, missing callback/prefix, bad size, prefix not a slug, text empty or > 2000 bytes
        Duplicate = 3,        // a command with that prefix (case-insensitive) is already registered
        InvalidId = 4,        // unknown or unregistered SubscriptionId / CommandId
        NotReady = 5,         // no character loaded: history is per character and is not open yet
        Busy = 6,             // reserved: version 1 never returns it (Send always queues)
        NoTarget = 7,         // Send with kind Player when SkyrimNet (SkyrimNet.dll) is not loaded
    };

    // Chronicle's channels. A message has one; subscriptions and queries filter on a comma list of
    // these names: "npc,notification,system,narrator,player,combat,console,skyrimnet", plus a mod's
    // slug for its own lines. "dialogue" (the chat window's name for "npc") is accepted as "npc".
    enum class Kind : std::uint8_t
    {
        Subtitle = 0,       // channel "npc": a line an NPC said (the subtitle), with speaker and quest.
                            // The player's own dialogue choices are here too, speakerFormId 0x14
        Notification = 1,   // channel "notification": items, gold, quest objectives, as the HUD got them
        System = 2,         // channel "system": skill ups, Words of Power, level-ups, saves
        Narrator = 3,       // channel "narrator": SkyrimNet narrator lines
        Player = 4,         // channel "player": what the player typed in the input bar
        Combat = 5,         // channel "combat": hits and deaths involving the player
        Console = 6,        // channel "console": commands run and their result, and /command replies
        Mod = 7,            // channel "<sender>": a line another mod pushed through Send. The chat window
                            // shows it in its System tab, with the sender (or the given speaker) as speaker
        SkyrimNet = 8,      // channel "skyrimnet": SkyrimNet (and MinAI, Mantella) character lines
    };

    // One captured line. Everything is a plain copy: safe to read on the thread it arrived on and to
    // copy out; never keep the pointer past the callback or the query. Strings are UTF-8.
    struct Message
    {
        std::uint32_t size;          // = sizeof(Message)
        std::uint64_t id;            // increasing per character; the cursor for GetMessages(since). 0 for a line
                                     // the history does not keep (its channel is turned off in the chat window)
        Kind kind;
        const char* channel;         // the channel name ("npc", or the sender for Mod)
        const char* speaker;         // display name ("Lydia"), or "" (notifications, system)
        std::uint32_t speakerFormId; // the speaking actor's reference form ID, 0 when there is none
        const char* speakerBase;     // FormKey of the speaker's base NPC ("Skyrim.esm|0x0A2C8E"), or ""
        const char* text;            // the line as shown ([REF] markers stripped, UTF-8 sanitised)
        const char* ownerQuest;      // FormKey of the dialogue topic's owner quest ("Skyrim.esm|0x000D4E"), or ""
        const char* topicKind;       // "conversation" | "quest" | "rumour" | "greeting" | "chatter" | "overheard" | ""
                                     // ("" when the topic is unknown, e.g. lines read from the HUD alone)
        double gameDays;             // GameDaysPassed when captured (Talk's days)
        float gameHour;              // the in-game hour, 0..24
        std::int64_t realTimeMs;     // wall clock, ms since the Unix epoch
        const char* place;           // the player's location name when captured, or ""
    };

    // Game thread. One message, copied for the callback; valid only during the call.
    using MessageFn = void (*)(const Message* message, void* user);

    struct SubscribeDesc
    {
        std::uint32_t size;       // = sizeof(SubscribeDesc) — forward-compat
        const char* channels;     // comma list, or nullptr/"" = every channel
        std::uint32_t minSpeakerDistance; // 0 = Chronicle's own NPC speech radius setting; otherwise the speaker
                                          // must also be within this many units (lines are only ever captured
                                          // within Chronicle's radius). Lines with no known speaker pass
        MessageFn onMessage;      // required
        void* user;
    };

    // Game thread. A line typed as "/<prefix> <args>" in Chronicle's input bar (chat mode; the console
    // mode still runs Skyrim console commands). Return true when handled: Chronicle echoes the command
    // in the console channel. Return false: Chronicle prints "unknown command". Reply with Send(Mod...).
    // "/help" lists the registered commands. A line starting with "/" is never sent to SkyrimNet.
    using CommandFn = bool (*)(const char* command, const char* args, void* user);

    struct CommandDesc
    {
        std::uint32_t size;       // = sizeof(CommandDesc) — forward-compat
        const char* prefix;       // slug [a-z0-9_.-], 2..32 chars (any case; kept lower case): "dworks", "dsort",
                                  // "dfc". "help" is Chronicle's own. /help shows it
        const char* help;         // one line for /help; optional
        CommandFn onCommand;      // required
        void* user;
    };

    struct Query
    {
        std::uint32_t size;       // = sizeof(Query)
        std::uint64_t sinceId;    // messages with id > sinceId; 0 = from the start of the character's history
        const char* channels;     // comma list, or nullptr = all
        std::uint32_t speakerFormId;  // 0 = any speaker
        const char* ownerQuest;   // FormKey, or nullptr = any
        std::uint32_t limit;      // 0 = Chronicle's page size (200); hard cap 2000
    };

    // Game thread. Called once per matching message, oldest first, during GetMessages. Return false to stop.
    using VisitFn = bool (*)(const Message* message, void* user);

    struct ChronicleApi1
    {
        std::uint32_t apiVersion;        // = kApiVersion1
        const char* hostVersion;         // "1.4.0"
        std::uint32_t hostVersionNumber; // MAJOR*10000 + MINOR*100 + PATCH
        const char* host;                // "PrismaUI" | "Magelight": which build this is

        // Game thread. Walk the current character's history, filtered. The messages are Chronicle's own
        // copies; visit copies what it keeps. Returns the count visited: 0 before a character's history
        // is open (the co-save profile id is read at kPostLoadGame), or for a nullptr query or visit.
        // "Clear history" in Chronicle's settings empties it; ids carry on from the last one.
        std::uint32_t (*GetMessages)(const Query* query, VisitFn visit, void* user);
        // Game thread. The newest id, for a consumer that wants to resume with sinceId after a reload.
        std::uint64_t (*GetLastId)();

        // Game thread, at kPostPostLoad or kDataLoaded. Strings are copied. The callback runs on the game
        // thread, in the order messages are captured, for every message that passes the filter, including
        // channels the player turned off in the chat window; lines Chronicle drops as duplicates (its
        // dedup) are not delivered. Must not block, open menus, or call Chronicle except Send.
        Result (*Subscribe)(const SubscribeDesc* desc, SubscriptionId* outSubscription);
        Result (*Unsubscribe)(SubscriptionId subscription);

        // ANY thread. Put a line in the box as if captured. kind Mod: channel "<sender>" (a slug, required)
        // in the API, shown in the chat window's System tab under speaker (or sender when speaker is
        // nullptr/""). kind Player submits it as chat input (the SkyrimNet_PlayerInput mod event), exactly
        // as typing it would; sender and speaker are ignored. Other kinds are InvalidArgument. NotReady
        // while no game is loaded. Queued to the game thread; the strings are copied before return.
        Result (*Send)(Kind kind, const char* sender, const char* speaker, const char* text);

        // Game thread, at kPostPostLoad or kDataLoaded. Idempotent per prefix: the same prefix again
        // returns Duplicate and keeps the first.
        Result (*RegisterCommand)(const CommandDesc* desc, CommandId* outCommand);
        Result (*UnregisterCommand)(CommandId command);

        // Any thread. True while the chat box is visible (not hidden by a menu or loading screen).
        bool (*IsVisible)();
        // Game thread. Show or hide the box as Shift+V style toggles would; the player's setting is not changed.
        Result (*SetVisible)(bool visible);
    };

    inline std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
    {
        return major * 10000 + minor * 100 + patch;
    }

#ifndef CHRONICLE_API_NO_LOADER
    // nullptr when Chronicle is not installed or does not serve this version. Both builds (PrismaUI and
    // Magelight) ship the same Chronicle.dll name and the same export.
    inline const void* RequestApiRaw(std::uint32_t version)
    {
        const HMODULE mod = GetModuleHandleW(L"Chronicle.dll");
        if (!mod) {
            return nullptr;
        }
        using RequestFn = const void* (*)(std::uint32_t);
        const auto request = reinterpret_cast<RequestFn>(GetProcAddress(mod, "Chronicle_RequestApi"));
        return request ? request(version) : nullptr;
    }

    inline const ChronicleApi1* RequestApi()
    {
        return static_cast<const ChronicleApi1*>(RequestApiRaw(kApiVersion1));
    }
#endif
}

// ---- Threading notes --------------------------------------------------------------------------
//
// Chronicle captures subtitles on the game thread (its HUD/subtitle poll) and SkyrimNet lines from
// the SKSE mod-event sink (also game thread). Subscribers are called right there, so a subscriber
// must not block, must not open menus, and must not call back into Chronicle except Send (which
// queues). Anything that touches actors from a subscriber is already on the game thread; anything
// that touches the UI goes through the subscriber's own task queue.
//
// Send is the only entry point that may be called from a worker thread. It copies and queues.

// ---- SKSE messaging (push side) ---------------------------------------------------------------
//
// Dispatched to every plugin that RegisterListener("Chronicle", ...). Data is a UTF-8 JSON string
// (data = const char*, size = length + 1). Game thread.

namespace CHRONICLE_API
{
    inline constexpr std::uint32_t kMsgHistoryOpened = 0x43485201;  // 'CHR' 1 {"character":"<profile id>"}: history is open
    inline constexpr std::uint32_t kMsgVisibility = 0x43485202;     // 'CHR' 2 {"visible":true}: the box was shown or hidden
}
