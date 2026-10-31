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

#include "History.h"

// Chronicle's side of api/Chronicle_API.h: the subscriber and command
// registries other plugins fill, and the SKSE messages Chronicle sends them.
namespace ChronicleApi
{
    // Game thread. Calls the subscribers for a line just captured, after the dedup.
    void Deliver(const ChronicleHistory::Line& line, float distance);

    // Game thread. Handles a "/command" typed in the input bar; false when the
    // text is not a command and should go on to SkyrimNet as chat.
    bool RunCommand(std::string_view text);

    // Settings: the "chronicle.echo" test command behind "debug_commands".
    void SetEchoCommand(bool enabled);

    // Game thread. SKSE message to plugins listening to "Chronicle".
    void Dispatch(std::uint32_t type, const nlohmann::json& data);

    // For Diagnostics.
    std::size_t SubscriberCount();
    std::vector<std::string> CommandPrefixes();
}
