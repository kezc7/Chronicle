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

    auto* chronicle = Chronicle::GetSingleton();

    if (a_event->opening) {
        // Any menu opening: release Chronicle focus so it cannot hold input
        // hostage when another mod's menu or a game menu opens.
        chronicle->ForceCloseInput();

        // "HUD Menu" opening means the world is fully loaded and rendering —
        // safe point to start the subtitle poller.
        if (menuName == "HUD Menu") {
            SubtitlePoller::GetSingleton().Start();
        }
    }

    // Photo Mode: hide/show the whole Chronicle window
    if (menuName == "PhotoMenu") {
        chronicle->OnMenuOpenClose(menuName, a_event->opening);
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

    // Only do real work every kPollEveryNFrames frames (~10Hz at 60fps).
    // Re-enqueue every frame so we don't drift, but skip the subtitle read
    // on intermediate frames to keep task overhead negligible.
    if (++m_skipCounter < kPollEveryNFrames) {
        auto* tasks = SKSE::GetTaskInterface();
        if (tasks) {
            tasks->AddTask([this]() { Poll(); });
        }
        return;
    }
    m_skipCounter = 0;

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
