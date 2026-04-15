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

// Strip HTML tags from subtitle text (GFx returns htmlText with <font> etc.)
static std::string StripHtmlTags(const std::string& a_input)
{
    std::string out;
    out.reserve(a_input.size());
    bool inTag = false;
    for (char c : a_input) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            out += c;
        }
    }
    return out;
}

void SubtitlePoller::PollOnce()
{
    // This runs on the main game thread. Do NOT call AddTask here.
    if (!m_running) {
        return;
    }

    // --- Path 1: SubtitleManager ---
    // Populated during formal NPC→player dialogue (DialogueMenu open).
    // In AE 1.6.1170 ambient/idle follower lines do NOT go through this path.
    {
        auto* mgr = RE::SubtitleManager::GetSingleton();
        if (mgr && !mgr->subtitles.empty()) {
            const auto& info = mgr->subtitles[0];
            std::string text = info.subtitle.c_str();
            if (!text.empty() && text != m_lastText) {
                m_lastText = text;
                spdlog::debug("Chronicle: subtitle (mgr): '{}'", text);

                std::string speakerName;
                if (auto speakerRef = info.speaker.get()) {
                    if (auto* name = speakerRef->GetDisplayFullName()) {
                        speakerName = name;
                    }
                }

                Chronicle::GetSingleton()->SendMessage("dialogue", speakerName, text);
                return;
            }
        }
    }

    // --- Path 2: HUD Menu GFx movie ---
    // The HUD displays subtitles for ALL speech (ambient, idle, scene dialogue)
    // by writing directly into the Scaleform movie — bypassing SubtitleManager.
    // We read the live text field value to capture what the player sees.
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return;
        }

        auto hudMenuPtr = ui->GetMenu("HUD Menu");
        if (!hudMenuPtr || !hudMenuPtr->uiMovie) {
            return;
        }

        auto* movie = hudMenuPtr->uiMovie.get();

        // Try known ActionScript paths for the subtitle TextField.
        // Vanilla HUD uses the first; some replacer mods use alternate names.
        static constexpr const char* kPaths[] = {
            "_root.HUDMovieBaseInstance.SubtitleTextHolder.SubtitleText.htmlText",
            "_root.HUDMovieBaseInstance.SubtitleTextHolder.SubtitleText.text",
            "_root.HUDMovieBaseInstance.subtitleTextHolder.subtitleText.htmlText",
            "_root.HUDMovieBaseInstance.subtitleTextHolder.subtitleText.text",
        };

        bool pathResolved = false;
        for (auto* path : kPaths) {
            RE::GFxValue gfxVal;
            if (!movie->GetVariable(&gfxVal, path)) {
                continue;
            }
            if (!gfxVal.IsString()) {
                continue;
            }

            // We found the right path (GetVariable succeeded and returned a string).
            pathResolved = true;

            const char* raw = gfxVal.GetString();
            if (!raw || raw[0] == '\0') {
                // Subtitle field is empty — no subtitle on screen right now.
                // Clear m_lastText so we don't suppress the same line if it repeats.
                m_lastText.clear();
                break;
            }

            std::string text = StripHtmlTags(raw);
            if (text.empty() || text == m_lastText) {
                break;
            }

            m_lastText = text;
            spdlog::debug("Chronicle: subtitle (hud): '{}'", text);
            // No speaker name available from this path
            Chronicle::GetSingleton()->SendMessage("dialogue", "", text);
            return;
        }

        // If no GFx path resolved at all, log once at debug level so we can diagnose
        if (!pathResolved) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                spdlog::warn("Chronicle: HUD subtitle GFx paths not found — "
                             "subtitle capture may be incomplete");
            }
        }
    }
}
