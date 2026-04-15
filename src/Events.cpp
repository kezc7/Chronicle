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
    }

    // Start the subtitle poller on the first MenuOpenCloseEvent after the game
    // has loaded a save. "HUD Menu" never fires MenuOpenCloseEvent (it is
    // always-open), so we use the first real menu event as the safe trigger.
    if (chronicle->IsGameLoaded() && !SubtitlePoller::GetSingleton().IsRunning()) {
        spdlog::info("Chronicle: first post-load menu event '{}' — starting subtitle poller",
                     menuName);
        SubtitlePoller::GetSingleton().Start();
    }

    // Route to OnMenuOpenClose — it filters to the known hide-list internally
    chronicle->OnMenuOpenClose(menuName, a_event->opening);

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
        spdlog::info("Chronicle: SubtitlePoller::Start called but already running");
        return;
    }

    m_lastText.clear();
    spdlog::info("Chronicle: subtitle polling started");

    // Spawn a background thread that sleeps kPollInterval then schedules a
    // one-shot AddTask to read SubtitleManager on the main thread.
    // We never re-enqueue from inside the task — that would drain SKSE's
    // while(!empty) task queue infinitely and freeze the game.
    std::thread([this]() {
        while (m_running) {
            std::this_thread::sleep_for(kPollInterval);
            if (!m_running) break;

            auto* tasks = SKSE::GetTaskInterface();
            if (tasks) {
                tasks->AddTask([this]() { PollOnce(); });
            }
        }
        spdlog::info("Chronicle: subtitle polling thread exiting");
    }).detach();
}

void SubtitlePoller::Stop()
{
    m_running = false;
    m_lastText.clear();
    spdlog::info("Chronicle: subtitle polling stopped");
}

void SubtitlePoller::PollOnce()
{
    // This runs on the main game thread. Do NOT call AddTask here.
    if (!m_running) {
        return;
    }

    auto* mgr = RE::SubtitleManager::GetSingleton();
    if (!mgr || mgr->subtitles.empty()) {
        return;
    }

    const auto& info = mgr->subtitles[0];
    std::string text = info.subtitle.c_str();
    if (text.empty() || text == m_lastText) {
        return;
    }

    m_lastText = text;
    spdlog::debug("Chronicle: subtitle: '{}'", text);

    std::string speakerName;
    if (auto speakerRef = info.speaker.get()) {
        if (auto* name = speakerRef->GetDisplayFullName()) {
            speakerName = name;
        }
    }

    Chronicle::GetSingleton()->SendMessage("dialogue", speakerName, text);
}
