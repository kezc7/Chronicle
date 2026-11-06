#pragma once
// DovahMenu_API.h — the DovahMenu launcher API for SKSE plugins. Header-only, no import library:
// DovahMenu.dll is reached by GetModuleHandle/GetProcAddress, so a mod that includes this still
// loads when DovahMenu is not installed (RequestApi returns nullptr).
//
// DovahMenu is a hub: one key opens a list of every PrismaUI and Magelight UI mod it can find, and
// opens that mod's own menu from the list. Mods it finds by scanning Data\Magelight\<mod>\manifest.json
// and Data\PrismaUI\views\<mod>\ appear on their own; registering through this API is what makes
// "open" work for a PrismaUI mod (whose view only its own DLL can show) and lets any mod supply its
// display name, description, version and current hotkey.
//
//   #include "DovahMenu_API.h"
//
//   static void OpenMyMenu(void*) { MyMod::OpenSettings(); }   // game thread; the hub has closed
//
//   // at SKSE kPostPostLoad or kDataLoaded (every plugin DLL is loaded by then):
//   if (const auto* menu = DOVAHMENU_API::RequestApi()) {
//       DOVAHMENU_API::EntryDesc d{};
//       d.size        = sizeof(d);
//       d.id          = "MyMod";                 // the PrismaUI view folder / Magelight modId
//       d.name        = "My Mod";
//       d.description = "Settings and the quest log";
//       d.version     = "1.2.0";
//       d.hotkey      = "F3";                    // display only; keep it current with SetHotkey
//       d.framework   = DOVAHMENU_API::Framework::PrismaUI;
//       d.onOpen      = &OpenMyMenu;
//       menu->Register(&d, &g_menuEntry);
//   }
//
// C++17. Depends only on <windows.h> (for the loader; define DOVAHMENU_API_NO_LOADER to drop it)
// and the standard library. Include CommonLib (or your precompiled header) before this header.

#include <cstdint>

#ifndef DOVAHMENU_API_NO_LOADER
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace DOVAHMENU_API
{
    inline constexpr std::uint32_t kApiVersion1 = 1;

    using EntryId = std::uint32_t;   // 0 = invalid

    enum class Result : std::int32_t
    {
        Ok = 0,
        HostAbsent = 1,       // consumer-side only: RequestApi returned nullptr
        InvalidArgument = 2,  // nullptr desc, missing id/onOpen, bad size, id not a slug
        Duplicate = 3,        // an entry with that id (case-insensitive) is already registered
        InvalidEntry = 4,     // unknown or unregistered EntryId
        NotReady = 5,         // Open/Close before the hub page exists (Magelight UI absent or not loaded yet), or Open at the main menu
        Busy = 6,             // Open while another mod holds Magelight's UI mode
    };

    // Which framework draws the mod's menu. Shown as a badge; Other is fine for anything else
    // (Scaleform, ImGui, a Papyrus-driven Magelight page...).
    enum class Framework : std::uint8_t
    {
        Other = 0,
        PrismaUI = 1,
        Magelight = 2,
    };

    // Called on the game thread after the hub has closed and released UI mode. Open your menu here
    // exactly as your own hotkey would.
    using OpenFn = void (*)(void* user);

    struct EntryDesc
    {
        std::uint32_t size;       // = sizeof(EntryDesc) — forward-compat
        const char* id;           // slug [A-Za-z0-9_.-], 1..64 chars. Use your PrismaUI view folder name or
                                  // Magelight modId: the entry then replaces the one the scan found, instead of
                                  // appearing twice.
        const char* name;         // display name; nullptr = id
        const char* description;  // one line; optional
        const char* version;      // "1.2.3"; optional
        const char* hotkey;       // the key that opens your menu, for display ("F3", "N", "Ctrl+M"); optional
        Framework framework;
        OpenFn onOpen;            // required
        void* user;               // handed back to onOpen
    };

    struct DovahMenuApi1
    {
        std::uint32_t apiVersion;        // = kApiVersion1
        const char* hostVersion;         // "0.1.0"
        std::uint32_t hostVersionNumber; // MAJOR*10000 + MINOR*100 + PATCH

        // Strings are copied; the desc need not outlive the call. Idempotent per id: registering the
        // same id again returns Duplicate and leaves the first entry.
        Result (*Register)(const EntryDesc* desc, EntryId* outEntry);
        Result (*Unregister)(EntryId entry);
        // A disabled entry stays listed but greyed ("not available now"): e.g. your menu needs a loaded save.
        Result (*SetEnabled)(EntryId entry, bool enabled);
        // Update the displayed hotkey when the player rebinds yours.
        Result (*SetHotkey)(EntryId entry, const char* hotkey);
        // Update name/description/version (nullptr = keep).
        Result (*SetText)(EntryId entry, const char* name, const char* description, const char* version);

        bool (*IsOpen)();
        Result (*Open)();   // open the hub as its hotkey would
        Result (*Close)();

        // ---- appended in 0.2.3: gate on hostVersionNumber >= 203 ------------------------------------
        // Suite key awareness, so two mods never claim the same key. Game thread.
        //
        // Who opens with this key ("F3", "N", "Shift+N", "Ctrl+Alt+M", or a scancode number): "DovahMenu"
        // for the hub's own key, the id of the listed entry that opens with it (a registered hotkey, a
        // manifest or dovahmenu.json hotkey, or a relay key), or "" when nobody listed claims it, or for
        // text that is not a key. Left and right Shift/Ctrl/Alt count as one; N and Shift+N differ. It
        // rescans, so call it when the player picks a key, not every frame. The string lives until the
        // next call on the same thread.
        const char* (*FindKeyOwner)(const char* chord);
        // The hub's own key as a key name ("Home"). The string lives until the next call on the same thread.
        const char* (*GetHotkey)();
    };

    inline std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
    {
        return major * 10000 + minor * 100 + patch;
    }

#ifndef DOVAHMENU_API_NO_LOADER
    // nullptr when DovahMenu is not installed or does not serve this version.
    inline const void* RequestApiRaw(std::uint32_t version)
    {
        const HMODULE mod = GetModuleHandleW(L"DovahMenu.dll");
        if (!mod) {
            return nullptr;
        }
        using RequestFn = const void* (*)(std::uint32_t);
        const auto request = reinterpret_cast<RequestFn>(GetProcAddress(mod, "DovahMenu_RequestApi"));
        return request ? request(version) : nullptr;
    }

    inline const DovahMenuApi1* RequestApi()
    {
        return static_cast<const DovahMenuApi1*>(RequestApiRaw(kApiVersion1));
    }
#endif
}
