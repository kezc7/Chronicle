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

#include "RuntimePolicy.h"

#include <string_view>

// Typed chat to SkyrimNet through its Papyrus API (SkyrimNetApi.psc). SkyrimNet
// does not listen for Chronicle's "SkyrimNet_PlayerInput" mod event, so the
// line is handed over with a script call. A soft dependency: without
// SkyrimNet.dll nothing is called. Every handover and its result is logged.
namespace ChronicleSkyrimNet
{
    bool Loaded();

    // Game thread. SKSE's crosshair event, for the NPC a line is said to.
    void RegisterSinks();
    void Clear();  // load boundary

    // CHRON-46: who the player picked in the chat bar. Neither set: Chronicle
    // chooses, as before (conversation, a name in the line, the crosshair).
    struct Pick
    {
        bool nearby{ false };   // said to no one in particular
        RE::FormID actor{ 0 };  // said to this NPC
    };

    // Game thread.
    void Send(std::string_view a_text, ChronicleRuntime::SkyrimNetSend a_mode, Pick a_pick = {});

    // Game thread. What the chat bar shows: {"loaded", "auto": {"id", "name",
    // "why"}, "nearby": [{"id", "name"}]}. Native data only, never Papyrus, so a
    // script backlog can't make it late. A name typed in the line isn't known here.
    std::string PreviewJson();
}
