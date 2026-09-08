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

// ---- Menu Open/Close Sink -------------------------------------------------
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

// ---- SkyrimNet Mod Event Sink ---------------------------------------------
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
// channel useful even when no AI integration is installed.
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

// ---- Subtitle Poller ------------------------------------------------------
// SubtitleManager has no event dispatch so we poll it periodically.
//
// IMPORTANT: SKSE's AddTask queue is drained with while(!empty), so any task
// that re-enqueues itself causes an infinite loop and freezes the game.
// We therefore use a background thread for timing and fire one-shot AddTask
// calls that read SubtitleManager on the main thread without re-enqueueing.

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
    bool IsRunning() const { return m_running.load(); }
    std::string LastHudPath() const { return m_lastHudPath.empty() ? "not polled" : m_lastHudPath; }

private:
    SubtitlePoller() = default;

    // Called on the main thread (via AddTask) — reads SubtitleManager once.
    // Does NOT re-enqueue itself; the background thread schedules the next call.
    void PollOnce();

    std::string m_lastText;
    std::string m_lastNotificationText;
    std::vector<std::string> m_extraHudPaths;
    std::string m_lastHudPath;
    std::unordered_set<std::string> m_activeSubtitles;
    std::atomic<std::uint64_t> m_generation{ 0 };
    std::atomic<bool> m_pending{ false };
    std::atomic<bool> m_running{ false };
    // Declared last so it joins before the state it uses is destroyed.
    std::jthread m_worker;

    // How long to sleep between polls. 100ms = ~10 Hz.
    static constexpr auto kPollInterval = std::chrono::milliseconds(100);
};
