/*
 * Chronicle — Skyrim SE/AE chat and activity log
 * Copyright (C) 2026 kezc7
 *
 * This file is part of Chronicle.
 *
 * Chronicle is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Chronicle is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Chronicle. If not, see <https://www.gnu.org/licenses/>.
 */

#include "PCH.h"
#include "DovahMenuModule.h"
#include "Chronicle.h"
#include "UiHost.h"
#include "DovahMenu_API.h"

namespace
{
    const DOVAHMENU_API::DovahMenuApi1* g_menu = nullptr;
    DOVAHMENU_API::EntryId g_entry = 0;

    // Game thread; DovahMenu has closed and released UI mode.
    void OnOpen(void*)
    {
        Chronicle::GetSingleton()->OpenFromLauncher();
    }
}

namespace DovahMenuModule
{
    void Register()
    {
        g_menu = DOVAHMENU_API::RequestApi();
        if (!g_menu || !g_menu->Register) {
            g_menu = nullptr;
            spdlog::info("Chronicle: DovahMenu not found; no launcher entry");
            return;
        }
        const bool magelight = std::string_view(UiHost().Name()) == "Magelight";
        DOVAHMENU_API::EntryDesc desc{};
        desc.size = sizeof(desc);
        desc.id = "Chronicle";  // the PrismaUI view folder and the Magelight modId
        desc.name = "Chronicle";
        const auto description = Chronicle::GetSingleton()->Text(ChronicleRuntime::Phrase::MenuDescription);
        desc.description = description.c_str();
        desc.version = CHRONICLE_VERSION;
        desc.hotkey = "Enter";
        desc.framework = magelight ? DOVAHMENU_API::Framework::Magelight : DOVAHMENU_API::Framework::PrismaUI;
        desc.onOpen = &OnOpen;
        const auto result = g_menu->Register(&desc, &g_entry);
        if (result != DOVAHMENU_API::Result::Ok) g_entry = 0;
        spdlog::info("Chronicle: DovahMenu {} answered; launcher entry {} (result {})",
            g_menu->hostVersion ? g_menu->hostVersion : "?", g_entry ? "registered" : "NOT registered",
            static_cast<int>(result));
        SetEnabled(false);
    }

    void SetEnabled(bool a_enabled)
    {
        if (g_menu && g_entry && g_menu->SetEnabled) g_menu->SetEnabled(g_entry, a_enabled);
    }

    void SetDescription(std::string_view a_description)
    {
        if (!g_menu || !g_entry || !g_menu->SetText) return;
        const std::string text(a_description);
        g_menu->SetText(g_entry, nullptr, text.c_str(), nullptr);
    }

    void SetHotkey(std::string_view a_hotkey)
    {
        if (!g_menu || !g_entry || !g_menu->SetHotkey) return;
        const std::string text(a_hotkey.empty() ? std::string_view("Enter") : a_hotkey);
        g_menu->SetHotkey(g_entry, text.c_str());
    }
}
