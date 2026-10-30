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

// Chronicle's tab in DovahHub (its history page), through DovahHub's module
// host (DovahHub_API.h). A soft dependency: without DovahHub.dll every
// call here does nothing.
namespace DovahHubModule
{
    // At kPostLoad: DovahHub's open/close messages hide and show Chronicle.
    void Listen();

    // Game thread, at kPostPostLoad.
    void Register();

    // True while the DovahHub page is on screen.
    bool IsOpen();

    // Game thread. Sent to the page only while the tab is on screen.
    void PushLine(const ChronicleHistory::Line& line);
    void PushReset();
}
