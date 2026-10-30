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

#include <filesystem>
#include <optional>
#include <vector>

// Writes history files on its own thread. The game thread hands it a copy of
// the lines (or a removal); the latest job per file wins.
namespace HistoryWriter
{
    // Data/SKSE/Plugins/Chronicle/history/<profileId>.json; MO2 puts it in Overwrite.
    std::filesystem::path PathFor(std::uint64_t profile);

    void Save(std::filesystem::path path, std::vector<ChronicleHistory::Line> lines);
    void Remove(std::filesystem::path path);

    // Game thread. Empty when there is no file or it cannot be read.
    std::vector<ChronicleHistory::Line> Load(const std::filesystem::path& path);
}
