#include "Events.h"
#include "Chronicle.h"

RE::BSEventNotifyControl ChronicleInputSink::ProcessEvent(RE::InputEvent* const* events,
    RE::BSTEventSource<RE::InputEvent*>*)
{
    bool keyboardEvent = false;
    bool consume = false;
    if (events) for (auto* event = *events; event; event = event->next) {
        const auto* button = event->AsButtonEvent();
        if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
            keyboardEvent = true;
            auto* chronicle = Chronicle::GetSingleton();
            consume = chronicle->ShouldConsumeKeyboardInput() || consume;
            if (button->IsDown()) {
                consume = chronicle->OnHotkey(
                    button->GetIDCode(), (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) || consume;
            }
        }
    }
    return keyboardEvent && consume ? RE::BSEventNotifyControl::kStop :
        RE::BSEventNotifyControl::kContinue;
}

namespace
{
    constexpr std::array<const char*, 4> kDefaultHudSubtitlePaths = {
        "_root.HUDMovieBaseInstance.SubtitleTextHolder.SubtitleText.text",
        "_root.HUDMovieBaseInstance.SubtitleTextHolder.SubtitleText.htmlText",
        "_root.HUDMovieBaseInstance.subtitleTextHolder.subtitleText.text",
        "_root.HUDMovieBaseInstance.subtitleTextHolder.subtitleText.htmlText",
    };

    std::string DisplayName(RE::TESObjectREFR* a_ref, std::string_view a_fallback)
    {
        if (a_ref) {
            if (const auto* name = a_ref->GetDisplayFullName(); name && name[0] != '\0') {
                return name;
            }
        }
        return std::string(a_fallback);
    }
}

void SubtitlePoller::SetExtraHudPaths(std::vector<std::string> paths)
{
    m_extraHudPaths.clear();
    for (auto& path : paths) {
        if (path.empty() || path.size() > 256) continue;
        if (std::find(kDefaultHudSubtitlePaths.begin(), kDefaultHudSubtitlePaths.end(), path) !=
            kDefaultHudSubtitlePaths.end()) continue;
        if (std::find(m_extraHudPaths.begin(), m_extraHudPaths.end(), path) == m_extraHudPaths.end()) {
            m_extraHudPaths.push_back(std::move(path));
        }
        if (m_extraHudPaths.size() >= 32) break;
    }
    spdlog::info("Chronicle: {} additional HUD subtitle path(s) configured", m_extraHudPaths.size());
}

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

    // Keep a fallback start path for unusual load sequences. "HUD Menu" never
    // fires MenuOpenCloseEvent (it is always-open), so normal startup begins
    // from OnPostLoadGame instead.
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

    const bool isDialogueLine =
        eventName == "SkyrimNet_DialogueLine" ||
        eventName == "SkyrimNet_CharacterLine" ||
        eventName == "MinAI_DialogueLine" ||
        eventName == "Mantella_DialogueLine" ||
        eventName == "Chronicle_DialogueLine";
    const bool isNarratorLine =
        eventName == "SkyrimNet_NarratorLine" ||
        eventName == "MinAI_NarratorLine" ||
        eventName == "Mantella_NarratorLine" ||
        eventName == "Chronicle_NarratorLine";

    if (isDialogueLine) {
        // Expected format: "SpeakerName|DialogueText"
        auto sep = strArg.find('|');
        if (sep != std::string::npos) {
            Chronicle::GetSingleton()->SendMessage("skyrimnet", strArg.substr(0, sep), strArg.substr(sep + 1));
        } else {
            Chronicle::GetSingleton()->SendMessage("skyrimnet", "", strArg);
        }
    } else if (isNarratorLine) {
        // Narrator lines — never attributed to Player
        Chronicle::GetSingleton()->SendMessage("narrator", "Narrator", strArg);
    } else if (eventName == "SkyrimNet_Notification" ||
               eventName == "Chronicle_Notification") {
        Chronicle::GetSingleton()->SendMessage("notification", "", strArg);
    } else if (eventName == "SkyrimNet_Combat" ||
               eventName == "SkyrimNet_CombatEvent" ||
               eventName == "Chronicle_Combat") {
        Chronicle::GetSingleton()->SendMessage("combat", "Combat", strArg);
    } else if (eventName == "SkyrimNet_SpeechStarted" ||
               eventName == "SkyrimNet_SpeechComplete") {
        // Modern SkyrimNet builds send speech metadata as JSON. Accept both
        // the top-level payload and the nested { data: "..." } form.
        try {
            auto payload = nlohmann::json::parse(strArg);
            if (payload.contains("data") && payload["data"].is_string()) {
                payload = nlohmann::json::parse(payload["data"].get<std::string>());
            }

            auto text = payload.value("fullText", std::string{});
            if (text.empty()) {
                text = payload.value("text", std::string{});
            }
            auto speaker = payload.value("speakerName", std::string{});
            if (speaker.empty()) {
                speaker = payload.value("speaker", std::string{});
            }
            if (!text.empty()) {
                Chronicle::GetSingleton()->SendMessage("skyrimnet", speaker, text);
            }
        } catch (const std::exception& e) {
            spdlog::debug("Chronicle: ignored non-JSON SkyrimNet speech event: {}", e.what());
        }
    } else if (eventName == "SkyrimNet_PlayerInput") {
        Chronicle::GetSingleton()->SendMessage("player", "Player", strArg);
    }

    return RE::BSEventNotifyControl::kContinue;
}

// ---- Native combat/death sinks -------------------------------------------

RE::BSEventNotifyControl CombatEventSink::ProcessEvent(
    const RE::TESHitEvent* a_event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* target = a_event->target.get();
    auto* cause = a_event->cause.get();

    if (target == player && cause != player) {
        Chronicle::GetSingleton()->SendMessage(
            "combat", DisplayName(cause, "Enemy"), "hit Player");
    } else if (cause == player && target != player) {
        Chronicle::GetSingleton()->SendMessage(
            "combat", "Player", "hit " + DisplayName(target, "target"));
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl DeathEventSink::ProcessEvent(
    const RE::TESDeathEvent* a_event,
    RE::BSTEventSource<RE::TESDeathEvent>*)
{
    if (!a_event || !a_event->dead) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* dying = a_event->actorDying.get();
    auto* killer = a_event->actorKiller.get();

    if (dying == player) {
        Chronicle::GetSingleton()->SendMessage("combat", "Combat", "Player died");
    } else if (killer == player && dying) {
        Chronicle::GetSingleton()->SendMessage(
            "combat", "Player", "killed " + DisplayName(dying, "target"));
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LevelUpEventSink::ProcessEvent(
    const RE::LevelIncrease::Event* a_event,
    RE::BSTEventSource<RE::LevelIncrease::Event>*)
{
    if (a_event) {
        Chronicle::GetSingleton()->SendMessage(
            "system", "", "Reached level " + std::to_string(a_event->newLevel));
    }
    return RE::BSEventNotifyControl::kContinue;
}

// ---- HUD notification hook ------------------------------------------------

namespace
{
    struct HUDNotificationsProcessMessage
    {
        // Entries the original appends to the queue are exactly what the game
        // decided to show (duplicates it cancels are never added).
        static bool Thunk(RE::HUDNotifications* a_this, RE::UIMessage* a_message)
        {
            const auto before = a_this->queue.size();
            const bool result = Original(a_this, a_message);
            try {
                for (auto i = before; i < a_this->queue.size(); ++i) {
                    const auto& entry = a_this->queue[i];
                    auto text = ChronicleRuntime::NotificationText(entry.text.c_str(), entry.status.c_str());
                    if (!text.empty()) {
                        Chronicle::GetSingleton()->SendMessage(
                            ChronicleRuntime::NotificationChannel(entry.type), "", text);
                    }
                }
            } catch (...) {
                // Never let capture failures escape into the game's UI code.
            }
            return result;
        }

        static inline REL::Relocation<decltype(Thunk)> Original;
    };
}

void NotificationHook::Install()
{
    REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_HUDNotifications[0] };
    HUDNotificationsProcessMessage::Original = vtable.write_vfunc(0x2, HUDNotificationsProcessMessage::Thunk);
    spdlog::info("Chronicle: HUD notification hook installed");
}

// ---- SubtitlePoller -------------------------------------------------------

void SubtitlePoller::Start()
{
    if (m_running.exchange(true)) {
        spdlog::info("Chronicle: SubtitlePoller::Start called but already running");
        return;
    }

    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    const auto generation = ++m_generation;
    m_pending = false;
    m_activeSubtitles.clear();
    m_lastText.clear();
    spdlog::info("Chronicle: subtitle polling started");

    // Spawn a background thread that sleeps kPollInterval then schedules a
    // one-shot AddTask to read SubtitleManager on the main thread.
    // We never re-enqueue from inside the task — that would drain SKSE's
    // while(!empty) task queue infinitely and freeze the game.
    m_worker = std::jthread([this, generation](std::stop_token stop) {
        while (!stop.stop_requested() && m_running && generation == m_generation) {
            std::this_thread::sleep_for(kPollInterval);
            if (stop.stop_requested() || !m_running || generation != m_generation) break;

            auto* tasks = SKSE::GetTaskInterface();
            if (tasks && !m_pending.exchange(true)) {
                tasks->AddTask([this, generation]() {
                    if (generation != m_generation) return;
                    m_pending = false;
                    if (!m_running) return;
                    try {
                        Chronicle::GetSingleton()->Tick();
                        PollOnce();
                    } catch (const std::exception& e) {
                        spdlog::error("Chronicle: subtitle poll rejected: {}", e.what());
                    } catch (...) {
                        spdlog::error("Chronicle: subtitle poll rejected");
                    }
                });
            }
        }
        spdlog::info("Chronicle: subtitle polling thread exiting");
    });
}

void SubtitlePoller::Stop()
{
    m_running = false;
    ++m_generation;
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    m_pending = false;
    m_activeSubtitles.clear();
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
        std::unordered_set<std::string> current;
        if (mgr) for (const auto& info : mgr->subtitles) {
            std::string text = info.subtitle.c_str();
            if (!text.empty()) {
                std::string speakerName;
                if (auto speakerRef = info.speaker.get()) {
                    if (auto* name = speakerRef->GetDisplayFullName()) {
                        speakerName = name;
                    }
                }

                const auto key = speakerName + '\0' + text;
                current.insert(key);
                if (!m_activeSubtitles.contains(key)) {
                    Chronicle::GetSingleton()->SendMessage("dialogue", speakerName, text);
                }
                m_lastText = text;
            }
        }
        m_activeSubtitles = std::move(current);
    }

    // --- Path 2: HUD Menu GFx movie ---
    // The HUD displays subtitles for ALL speech (ambient, idle, scene dialogue)
    // by writing directly into the Scaleform movie — bypassing SubtitleManager.
    // We read the live text field value to capture what the player sees.
    {
        m_lastHudPath = m_activeSubtitles.empty() ? "none resolved" : "SubtitleManager";
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            m_lastHudPath = "HUD Menu unavailable";
            return;
        }

        // Notifications are captured by NotificationHook, not polled here.
        // Prefer the attributed manager entries while they are active.
        if (!m_activeSubtitles.empty()) return;
        auto hudMenuPtr = ui->GetMenu("HUD Menu");
        if (!hudMenuPtr || !hudMenuPtr->uiMovie) {
            return;
        }

        auto* movie = hudMenuPtr->uiMovie.get();

        // Try known ActionScript paths for the subtitle TextField.
        // Vanilla HUD uses the first; some replacer mods use alternate names.
        bool pathResolved = false;
        std::vector<std::string_view> paths;
        paths.reserve(kDefaultHudSubtitlePaths.size() + m_extraHudPaths.size());
        for (auto* path : kDefaultHudSubtitlePaths) paths.emplace_back(path);
        for (const auto& path : m_extraHudPaths) paths.emplace_back(path);

        for (const auto path : paths) {
            RE::GFxValue gfxVal;
            if (!movie->GetVariable(&gfxVal, path.data())) {
                continue;
            }
            if (!gfxVal.IsString()) {
                continue;
            }

            // We found the right path (GetVariable succeeded and returned a string).
            pathResolved = true;
            m_lastHudPath = std::string(path);

            const char* raw = gfxVal.GetString();
            if (!raw || raw[0] == '\0') {
                // Subtitle field is empty — no subtitle on screen right now.
                // Clear m_lastText so we don't suppress the same line if it repeats.
                m_lastText.clear();
                break;
            }

            std::string text = std::string_view(path).ends_with(".htmlText") ? StripHtmlTags(raw) : std::string(raw);
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
            m_lastHudPath = "none resolved";
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                spdlog::warn("Chronicle: HUD subtitle GFx paths not found — "
                             "subtitle capture may be incomplete");
            }
        }
    }
}
