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

#include <filesystem>
#include <source_location>
#include <unordered_set>

// The game thread is RE::Main's thread. SKSE's AddTask queue can be drained on
// other threads in some load orders, racing the game's own update; its
// AddUITask queue runs on the game thread. All game work therefore goes
// through Post (AddUITask), and code that touches game objects calls Check,
// which logs once per place when it finds itself on another thread.
namespace GameThread
{
    // 0 until RE::Main exists.
    inline std::uint32_t Id()
    {
        const auto* main = RE::Main::GetSingleton();
        return main ? main->threadID : 0;
    }

    inline bool IsCurrent()
    {
        const auto id = Id();
        return id == 0 || id == GetCurrentThreadId();
    }

    inline void Check(const std::source_location& where = std::source_location::current())
    {
        if (IsCurrent()) return;
        static std::mutex mutex;
        static std::unordered_set<std::string> warned;
        std::string place = std::filesystem::path(where.file_name()).filename().string() + ":" + std::to_string(where.line());
        std::lock_guard lock(mutex);
        if (warned.size() >= 64 || !warned.insert(place).second) return;
        spdlog::warn("Chronicle: game work off the game thread at {} ({}): thread {}, game thread {}",
            place, where.function_name(), GetCurrentThreadId(), Id());
    }

    // Logged once at kDataLoaded: the game thread and the thread this runs on.
    inline void LogThreads(std::string_view where)
    {
        spdlog::info("Chronicle: game thread {}; {} on thread {}", Id(), where, GetCurrentThreadId());
    }

    inline std::atomic<bool> g_firstTask{ true };

    // Runs task once on the game thread (SKSE's AddUITask). A task that posts
    // again must be bounded by a flag or a timer, never a chain of posts.
    template<class F>
    bool Post(F task, std::source_location where = std::source_location::current())
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) return false;
        tasks->AddUITask([task = std::move(task), where]() mutable {
            if (g_firstTask.exchange(false)) LogThreads("first task ran");
            Check(where);
            task();
        });
        return true;
    }
}
