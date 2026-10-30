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
#include "HistoryWriter.h"

#include <condition_variable>
#include <fstream>
#include <map>

namespace
{
    const std::filesystem::path kHistoryDir = L"Data/SKSE/Plugins/Chronicle/history";
    constexpr std::uintmax_t kMaxFileSize = 4 * 1024 * 1024;

    // nullopt = remove the file.
    using Job = std::optional<std::vector<ChronicleHistory::Line>>;

    std::mutex g_mutex;
    std::condition_variable g_wake;
    std::map<std::filesystem::path, Job> g_jobs;
    bool g_started = false;

    void Write(const std::filesystem::path& path, const std::vector<ChronicleHistory::Line>& lines)
    {
        const auto text = ChronicleHistory::LinesToJson(lines).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::filesystem::create_directories(path.parent_path());
        auto temp = path;
        temp += L".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("cannot replace the file (Windows error " + std::to_string(GetLastError()) + ")");
        }
    }

    void Run()
    {
        for (;;) {
            std::map<std::filesystem::path, Job> jobs;
            {
                std::unique_lock lock(g_mutex);
                g_wake.wait(lock, [] { return !g_jobs.empty(); });
                jobs.swap(g_jobs);
            }
            for (const auto& [path, job] : jobs) {
                try {
                    if (job) Write(path, *job);
                    else std::filesystem::remove(path);
                } catch (const std::exception& e) {
                    spdlog::warn("Chronicle: history file '{}' not {}: {}", path.string(), job ? "saved" : "removed", e.what());
                }
            }
        }
    }

    void Queue(std::filesystem::path path, Job job)
    {
        {
            std::lock_guard lock(g_mutex);
            g_jobs[std::move(path)] = std::move(job);
            if (!g_started) {
                g_started = true;
                std::thread(Run).detach();
            }
        }
        g_wake.notify_one();
    }
}

namespace HistoryWriter
{
    std::filesystem::path PathFor(std::uint64_t profile)
    {
        return kHistoryDir / (std::to_string(profile) + ".json");
    }

    void Save(std::filesystem::path path, std::vector<ChronicleHistory::Line> lines)
    {
        Queue(std::move(path), std::move(lines));
    }

    void Remove(std::filesystem::path path)
    {
        Queue(std::move(path), std::nullopt);
    }

    std::vector<ChronicleHistory::Line> Load(const std::filesystem::path& path)
    {
        try {
            // A save still queued for this file is newer than the disk.
            {
                std::lock_guard lock(g_mutex);
                if (const auto it = g_jobs.find(path); it != g_jobs.end()) {
                    return it->second ? *it->second : std::vector<ChronicleHistory::Line>{};
                }
            }
            if (!std::filesystem::exists(path)) return {};
            if (std::filesystem::file_size(path) > kMaxFileSize) throw std::runtime_error("the file exceeds 4 MB");
            std::ifstream input(path, std::ios::binary);
            const auto json = nlohmann::json::parse(input, nullptr, false);
            if (json.is_discarded()) throw std::runtime_error("not valid JSON");
            return ChronicleHistory::LinesFromJson(json);
        } catch (const std::exception& e) {
            spdlog::warn("Chronicle: history file '{}' not read: {}", path.string(), e.what());
            return {};
        }
    }
}
