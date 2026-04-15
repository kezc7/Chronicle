#include "Events.h"
#include "Chronicle.h"

// ---- MenuEventSink --------------------------------------------------------

RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    std::string_view menuName = a_event->menuName.c_str();

    // Photo Mode: hide/show the whole Chronicle window
    if (menuName == "PhotoMenu") {
        Chronicle::GetSingleton()->OnMenuOpenClose(menuName, a_event->opening);
    }

    // DialogueMenu: drive subtitle polling
    if (menuName == "Dialogue Menu") {
        if (a_event->opening) {
            SubtitlePoller::GetSingleton().Start();
        } else {
            SubtitlePoller::GetSingleton().Stop();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

// ---- SkyrimNetEventSink ---------------------------------------------------

RE::BSEventNotifyControl SkyrimNetEventSink::ProcessEvent(
    const SKSE::ModCallbackEvent* a_event,
    RE::BSTEventSource<SKSE::ModCallbackEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    std::string_view eventName = a_event->eventName.c_str();
    std::string strArg = a_event->strArg.c_str();

    if (eventName == "SkyrimNet_DialogueLine") {
        // Expected format: "SpeakerName|DialogueText"
        auto sep = strArg.find('|');
        if (sep != std::string::npos) {
            Chronicle::GetSingleton()->SendMessage("skyrimnet", strArg.substr(0, sep), strArg.substr(sep + 1));
        } else {
            Chronicle::GetSingleton()->SendMessage("skyrimnet", "", strArg);
        }
    } else if (eventName == "SkyrimNet_NarratorLine") {
        // Narrator lines — never attributed to Player
        Chronicle::GetSingleton()->SendMessage("narrator", "Narrator", strArg);
    } else if (eventName == "SkyrimNet_PlayerInput") {
        Chronicle::GetSingleton()->SendMessage("player", "Player", strArg);
    }

    return RE::BSEventNotifyControl::kContinue;
}

// ---- SubtitlePoller -------------------------------------------------------

void SubtitlePoller::Start()
{
    if (m_running.exchange(true)) {
        return; // already running
    }

    m_lastText.clear();
    spdlog::debug("Chronicle: subtitle polling started");

    // Schedule a recurring main-thread task via SKSE::GetTaskInterface
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks) {
        spdlog::warn("Chronicle: SKSE task interface unavailable — subtitle polling disabled");
        m_running = false;
        return;
    }

    // Enqueue the first poll; each poll re-enqueues itself while running.
    tasks->AddTask([this]() { Poll(); });
}

void SubtitlePoller::Stop()
{
    m_running = false;
    m_lastText.clear();
    spdlog::debug("Chronicle: subtitle polling stopped");
}

void SubtitlePoller::Poll()
{
    if (!m_running) {
        return;
    }

    auto* mgr = RE::SubtitleManager::GetSingleton();
    if (mgr && !mgr->subtitles.empty()) {
        // Take the first subtitle — that's the active one
        const auto& info = mgr->subtitles[0];
        std::string text = info.subtitle.c_str();

        if (!text.empty() && text != m_lastText) {
            m_lastText = text;

            // Resolve speaker name from handle
            std::string speakerName;
            auto speakerRef = info.speaker.get();
            if (speakerRef) {
                auto* name = speakerRef->GetDisplayFullName();
                if (name) {
                    speakerName = name;
                }
            }

            Chronicle::GetSingleton()->SendMessage("dialogue", speakerName, text);
        }
    }

    // Re-enqueue for next frame if still active
    auto* tasks = SKSE::GetTaskInterface();
    if (tasks && m_running) {
        tasks->AddTask([this]() { Poll(); });
    }
}
