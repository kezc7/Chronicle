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

// DovahGameThread.h and DovahHub_API.h are copied unchanged from DovahHub; the
// pump header picks the address library's names for either CommonLibSSE-NG.
#include "DovahGameThread.h"

#include <filesystem>
#include <source_location>
#include <unordered_set>

// Game work runs on the game thread: the thread the game's frame update runs
// on. Neither of SKSE's task queues can be trusted to get there (in some load
// orders both run on worker threads), so posts go to the suite's game-thread
// pump (DovahGameThread.h): DovahHub's when it is installed, otherwise
// Chronicle's own hook on the same call. Code that touches game objects calls
// Check, which logs once per place when it finds itself on another thread.
namespace GameThread
{
    // The frame's thread from the pump; 0 before its first frame. Without a
    // hook (the pump's fallback, or before Install), RE::Main's threadID.
    inline std::uint32_t Id()
    {
        if (DovahGameThread::Pumping()) return DovahGameThread::Id();
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

    // Logged at kDataLoaded and by the first posted task: the game thread as
    // Chronicle judges it, and the thread this runs on.
    inline void LogThreads(std::string_view where)
    {
        const auto* main = RE::Main::GetSingleton();
        spdlog::info("Chronicle: game thread {} ({}); RE::Main's threadID {}; {} on thread {}", Id(),
            DovahGameThread::Pumping() ? "the pump's frame thread" : "no pump: RE::Main's",
            main ? main->threadID : 0, where, GetCurrentThreadId());
    }

    inline std::atomic<bool> g_firstTask{ true };

    // Runs task once on the game thread, at the start of the next frame, in
    // posting order (DovahGameThread::Post). Work posted while a frame runs the
    // queue waits for the next frame, so a task that posts again can't chain
    // within a frame; it still needs a flag or a timer to stay bounded.
    template<class F>
    bool Post(F task, std::source_location where = std::source_location::current())
    {
        DovahGameThread::Post(where.function_name(), [task = std::move(task), where]() mutable {
            if (g_firstTask.exchange(false)) LogThreads("first task ran");
            Check(where);
            task();
        });
        return true;
    }
}
