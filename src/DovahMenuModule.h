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

#pragma once

#include <string_view>

// Chronicle's entry in DovahMenu's launcher list (DovahMenu_API.h), the same
// on either UI host. A soft dependency: without DovahMenu.dll every call here
// does nothing. Opening the entry shows the chat box and opens its input bar.
namespace DovahMenuModule
{
    // Game thread, at kPostPostLoad.
    void Register();

    // Game thread. Greyed in the list until a game is loaded.
    void SetEnabled(bool a_enabled);
    // Game thread. The input hotkey's name, as the settings file spells it.
    void SetHotkey(std::string_view a_hotkey);
}
