#pragma once

#include "PCH.h"

// ---- Menu Open/Close Sink -------------------------------------------------
// Hides Chronicle during Photo Mode (and any other full-screen menu).
// Also drives subtitle polling: starts when DialogueMenu opens,
// stops when it closes.

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

// ---- Subtitle Poller ------------------------------------------------------
// SubtitleManager has no event dispatch. We poll its subtitle array on the
// main thread whenever the DialogueMenu is open and forward new lines to
// Chronicle. The MenuEventSink drives start/stop.

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

private:
    SubtitlePoller() = default;

    // Runs on the SKSE task thread — reads SubtitleManager and forwards
    // any new (speaker, text) pair to Chronicle.
    void Poll();

    std::string m_lastText;
    std::atomic<bool> m_running{ false };
    int m_skipCounter{ 0 };

    // Poll once every N frames. At 60fps, 6 = ~10Hz — fast enough to catch
    // any subtitle change, slow enough not to spam the task queue.
    static constexpr int kPollEveryNFrames = 6;
};
