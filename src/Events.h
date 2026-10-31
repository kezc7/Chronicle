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

#include "PCH.h"
#include <unordered_set>

class ChronicleInputSink final : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    static ChronicleInputSink& GetSingleton() { static ChronicleInputSink instance; return instance; }
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
        RE::BSTEventSource<RE::InputEvent*>*) override;
};

// Hides Chronicle during Photo Mode (and any other full-screen menu).
// Also provides a fallback trigger for subtitle polling after a save loads.

class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static MenuEventSink& GetSingleton()
    {
        static MenuEventSink instance;
        return instance;
    }

    MenuEventSink(const MenuEventSink&) = delete;
    MenuEventSink& operator=(const MenuEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

private:
    MenuEventSink() = default;
};

// Listens for SkyrimNet's Papyrus mod events via the SKSE mod event system.
// Event names: SkyrimNet_DialogueLine, SkyrimNet_NarratorLine, SkyrimNet_PlayerInput

class SkyrimNetEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
{
public:
    static SkyrimNetEventSink& GetSingleton()
    {
        static SkyrimNetEventSink instance;
        return instance;
    }

    SkyrimNetEventSink(const SkyrimNetEventSink&) = delete;
    SkyrimNetEventSink& operator=(const SkyrimNetEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>* a_eventSource) override;

private:
    SkyrimNetEventSink() = default;
};

// Native combat/death events involving the player. These keep the Combat
// channel useful even when no external dialogue integration is installed.
// This CommonLibSSE-NG only forward-declares the event (its source is in
// ScriptEventSourceHolder); the layout is newer CommonLib's. Only read, never copied.
namespace RE
{
    struct TESTopicInfoEvent
    {
        void* callback;                      // 00 BSTSmartPointer<REFREventCallbacks::IEventCallback>
        NiPointer<TESObjectREFR> speakerRef; // 08
        FormID topicInfoFormID;              // 10
        std::uint32_t type;                  // 14 0 = topic begins, 1 = ends
        std::uint16_t stage;                 // 18
    };
    static_assert(sizeof(TESTopicInfoEvent) == 0x20);
}

// Which topic info each actor is saying, so a subtitle can carry its topic and
// owner quest (Chronicle_API.h). Events may arrive on any thread: only the ids
// are copied there, and the map is used on the game thread.
class TopicInfoEventSink final : public RE::BSTEventSink<RE::TESTopicInfoEvent>
{
public:
    static TopicInfoEventSink& GetSingleton()
    {
        static TopicInfoEventSink instance;
        return instance;
    }

    TopicInfoEventSink(const TopicInfoEventSink&) = delete;
    TopicInfoEventSink& operator=(const TopicInfoEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESTopicInfoEvent* a_event,
        RE::BSTEventSource<RE::TESTopicInfoEvent>* a_eventSource) override;

    // Game thread. The topic info the speaker is saying now, 0 when none.
    RE::FormID Current(RE::FormID a_speaker) const;
    void Clear();

private:
    TopicInfoEventSink() = default;

    std::unordered_map<RE::FormID, RE::FormID> m_speaking;
    std::atomic<std::uint64_t> m_epoch{ 0 };  // Clear() drops updates queued before it
};

class CombatEventSink final : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
    static CombatEventSink& GetSingleton()
    {
        static CombatEventSink instance;
        return instance;
    }

    CombatEventSink(const CombatEventSink&) = delete;
    CombatEventSink& operator=(const CombatEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* a_event,
        RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

private:
    CombatEventSink() = default;
};

class DeathEventSink final : public RE::BSTEventSink<RE::TESDeathEvent>
{
public:
    static DeathEventSink& GetSingleton()
    {
        static DeathEventSink instance;
        return instance;
    }

    DeathEventSink(const DeathEventSink&) = delete;
    DeathEventSink& operator=(const DeathEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>* a_eventSource) override;

private:
    DeathEventSink() = default;
};

// Player level-ups feed the System channel.
class LevelUpEventSink final : public RE::BSTEventSink<RE::LevelIncrease::Event>
{
public:
    static LevelUpEventSink& GetSingleton()
    {
        static LevelUpEventSink instance;
        return instance;
    }

    LevelUpEventSink(const LevelUpEventSink&) = delete;
    LevelUpEventSink& operator=(const LevelUpEventSink&) = delete;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::LevelIncrease::Event* a_event,
        RE::BSTEventSource<RE::LevelIncrease::Event>* a_eventSource) override;

private:
    LevelUpEventSink() = default;
};

// Captures each notification (items, quests, skill increases) as the game
// queues it. Polling the queue missed short-lived entries and bursts.

namespace NotificationHook
{
    void Install();
}

// SubtitleManager has no event dispatch so we poll it periodically.
//
// IMPORTANT: SKSE's AddTask queue is drained with while(!empty), so any task
// that re-enqueues itself causes an infinite loop and freezes the game.
// We therefore use a background thread for timing and fire one-shot AddTask
// calls that read SubtitleManager on the main thread without re-enqueueing.
//
// The HUD movie is Scaleform and is only read from AddUITask: reading it from
// AddTask raced the HUD's own updates and crashed inside GetVariable with
// HUD-heavy setups. The UI task copies the text out; the next poll handles it.

class SubtitlePoller
{
public:
    static SubtitlePoller& GetSingleton()
    {
        static SubtitlePoller instance;
        return instance;
    }

    SubtitlePoller(const SubtitlePoller&) = delete;
    SubtitlePoller& operator=(const SubtitlePoller&) = delete;

    void Start();
    void Stop();
    void SetExtraHudPaths(std::vector<std::string> paths);
    // "hud_subtitle_capture": false in Chronicle.json stops every read of the
    // HUD movie; SubtitleManager and notification capture are unaffected.
    void SetHudCapture(bool enabled);
    bool IsRunning() const { return m_running.load(); }
    std::string LastHudPath() const { return m_lastHudPath.empty() ? "not polled" : m_lastHudPath; }

private:
    SubtitlePoller() = default;

    // Called on the main thread (via AddTask) — reads SubtitleManager once.
    // Does NOT re-enqueue itself; the background thread schedules the next call.
    void PollOnce();

    struct HudRead
    {
        enum class Status { kNoUi, kNoMovie, kNotFound, kFound };
        Status status = Status::kNoUi;
        std::string path;
        std::string text;
    };

    // Runs on SKSE's UI task queue; touches only the HUD movie.
    static HudRead ReadHudSubtitle(const std::vector<std::string>& a_paths);
    void RequestHudRead();
    void HandleHudRead();
    // Drops the stored result and any read still in flight.
    void ResetHudRead();

    std::string m_lastText;
    // NPC line being answered; only compared, never dereferenced.
    const RE::TESTopicInfo* m_lastTopicInfo = nullptr;
    std::vector<std::string> m_extraHudPaths;
    std::string m_lastHudPath;
    std::unordered_set<std::string> m_activeSubtitles;
    bool m_managerSubtitleActive = false;
    std::atomic<std::uint64_t> m_generation{ 0 };
    std::atomic<bool> m_pending{ false };
    std::atomic<bool> m_running{ false };
    std::mutex m_hudMutex;
    std::optional<HudRead> m_hudRead;  // latest UI-task result, guarded by m_hudMutex
    std::atomic<std::uint64_t> m_hudEpoch{ 0 };  // bumped by ResetHudRead; stale reads are dropped
    std::atomic<bool> m_hudCapture{ true };
    std::atomic<bool> m_hudPending{ false };
    // Declared last so it joins before the state it uses is destroyed.
    std::jthread m_worker;

    static constexpr auto kPollInterval = std::chrono::milliseconds(100);
};
