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

    bool IsWithinDialogueRadius(RE::TESObjectREFR* a_speaker, float a_radius)
    {
        if (a_radius <= 0.0F || !a_speaker) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            // If Skyrim is between player instances, keep the line rather
            // than dropping game text because the distance cannot be read.
            return true;
        }

        const auto speakerPosition = a_speaker->GetPosition();
        const auto playerPosition = player->GetPosition();
        const auto dx = speakerPosition.x - playerPosition.x;
        const auto dy = speakerPosition.y - playerPosition.y;
        const auto dz = speakerPosition.z - playerPosition.z;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        return ChronicleRuntime::WithinDialogueRadius(distance, a_radius);
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
        Chronicle::GetSingleton()->SendMessage("combat", Chronicle::GetSingleton()->Text(ChronicleRuntime::Phrase::Combat), strArg);
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
        // Same speaker as OnPlayerInput, so the echo of our own event is deduplicated.
        Chronicle::GetSingleton()->SendMessage("player", PlayerDisplayName(), strArg);
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

    using ChronicleRuntime::Phrase;
    auto* chronicle = Chronicle::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !chronicle->ShowCombatHits()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* target = a_event->target.get();
    auto* cause = a_event->cause.get();

    // Hits stay player-only; follower hits would flood the channel.
    if (target == player && cause != player) {
        chronicle->SendMessage(
            "combat", DisplayName(cause, chronicle->Text(Phrase::Enemy)),
            chronicle->Text(Phrase::Hit, DisplayName(player, chronicle->Text(Phrase::Player))));
    } else if (cause == player && target != player) {
        chronicle->SendMessage(
            "combat", DisplayName(player, chronicle->Text(Phrase::Player)),
            chronicle->Text(Phrase::Hit, DisplayName(target, chronicle->Text(Phrase::Target))));
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
    if (!dying) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto side = [player](RE::TESObjectREFR* a_ref) {
        using ChronicleRuntime::Side;
        auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
        if (!actor) return Side::Other;
        if (actor == player) return Side::Player;
        if (actor->IsPlayerTeammate()) return Side::Ally;
        if (actor->IsCommandedActor() && actor->GetCommandingActor().get() == player) return Side::Ally;
        return Side::Other;
    };
    if (!ChronicleRuntime::ReportDeath(side(dying), side(killer))) {
        return RE::BSEventNotifyControl::kContinue;
    }

    using ChronicleRuntime::Phrase;
    auto* chronicle = Chronicle::GetSingleton();
    if (killer && killer != dying) {
        chronicle->SendMessage(
            "combat", DisplayName(killer, chronicle->Text(Phrase::Someone)),
            chronicle->Text(Phrase::Killed, DisplayName(dying, chronicle->Text(Phrase::Target))));
    } else {
        chronicle->SendMessage(
            "combat", chronicle->Text(Phrase::Combat),
            chronicle->Text(Phrase::Died, DisplayName(dying, chronicle->Text(Phrase::Someone))));
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LevelUpEventSink::ProcessEvent(
    const RE::LevelIncrease::Event* a_event,
    RE::BSTEventSource<RE::LevelIncrease::Event>*)
{
    if (a_event) {
        Chronicle::GetSingleton()->SendMessage(
            "system", "", Chronicle::GetSingleton()->Text(ChronicleRuntime::Phrase::ReachedLevel, std::to_string(a_event->newLevel)));
    }
    return RE::BSEventNotifyControl::kContinue;
}

// ---- HUD notification hook ------------------------------------------------

namespace
{
    // Resolve a translation key through the game's Scaleform translator, which
    // already holds every Interface/Translations file. Unknown keys stay as-is.
    std::string TranslateKey(const std::string& a_text)
    {
        if (!ChronicleRuntime::IsTranslationKey(a_text)) return a_text;
        const auto* manager = RE::BSScaleformManager::GetSingleton();
        if (!manager || !manager->loader) return a_text;
        auto* translator = manager->loader->GetStateAddRef<RE::GFxTranslator>(RE::GFxState::StateType::kTranslator);
        if (!translator) return a_text;

        std::string translated;
        if (const auto key = SKSE::stl::utf8_to_utf16(a_text)) {
            RE::GFxWStringBuffer result;
            RE::GFxTranslator::TranslateInfo info;
            info.key = key->c_str();
            info.result = std::addressof(result);
            translator->Translate(std::addressof(info));
            if (info.flags.all(RE::GFxTranslator::TranslateInfo::kTranslated) && !result.empty()) {
                translated = SKSE::stl::utf16_to_utf8(std::wstring_view(result)).value_or(std::string{});
            }
        }
        translator->Release();
        translated = ChronicleRuntime::StripHtml(translated);
        return translated.empty() ? a_text : translated;
    }

    // HUDMenu receives each HUD message (HUDData) before it is displayed. On
    // 1.6.1170 notifications never reach HUDNotifications::ProcessMessage (the
    // menu queues them itself), so capture them here from the message data.
    struct HUDMenuProcessMessage
    {
        // Returns true when the game's own display of this notification should be
        // suppressed ("notifications only in Chronicle").
        static bool Capture(const RE::UIMessage& a_message)
        {
            if (a_message.type.get() != RE::UI_MESSAGE_TYPE::kUpdate || !a_message.data) return false;
            // Identify HUDData exactly by its vtable before reading any of its fields.
            static const auto hudDataVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_HUDData[0] }.address();
            if (*reinterpret_cast<const std::uintptr_t*>(a_message.data) != hudDataVtable) return false;

            const auto* hud = static_cast<const RE::HUDData*>(a_message.data);
            const auto type = static_cast<std::uint32_t>(hud->type.get());
            const std::string text = TranslateKey(ChronicleRuntime::StripHtml(hud->text.c_str()));
            if (!ChronicleRuntime::IsNotificationType(type) || text.empty()) return false;

            auto* chronicle = Chronicle::GetSingleton();
            chronicle->SendMessage(ChronicleRuntime::NotificationChannel(type), "",
                ChronicleRuntime::NotificationText(chronicle->Language(), type, text));
            // Only plain notifications (type 1); quest and skill banners stay on screen.
            return type == 1 && chronicle->ShouldHideGameNotification();
        }

        static RE::UI_MESSAGE_RESULTS Thunk(RE::HUDMenu* a_this, RE::UIMessage& a_message)
        {
            bool suppress = false;
            try {
                suppress = Capture(a_message);
            } catch (...) {
                // Never let capture failures escape into the game's UI code.
            }
            // The UI system still releases the message; the HUD just never shows it.
            if (suppress) return RE::UI_MESSAGE_RESULTS::kHandled;
            return Original(a_this, a_message);
        }

        static inline REL::Relocation<decltype(Thunk)> Original;
    };
}

void NotificationHook::Install()
{
    REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_HUDMenu[0] };
    HUDMenuProcessMessage::Original = vtable.write_vfunc(0x4, HUDMenuProcessMessage::Thunk);
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
    m_managerSubtitleActive = false;
    m_lastText.clear();
    m_lastTopicInfo = nullptr;
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
    m_managerSubtitleActive = false;
    m_lastText.clear();
    m_lastTopicInfo = nullptr;
    spdlog::info("Chronicle: subtitle polling stopped");
}

void SubtitlePoller::PollOnce()
{
    // This runs on the main game thread. Do NOT call AddTask here.
    if (!m_running) {
        return;
    }

    // --- Player dialogue choices ---
    // The player's lines are silent menu choices, never subtitles. When the NPC
    // starts answering, log the option the player picked first.
    {
        auto* ui = RE::UI::GetSingleton();
        auto* topics = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ? RE::MenuTopicManager::GetSingleton() : nullptr;
        auto* info = topics ? topics->currentTopicInfo : nullptr;
        if (info != m_lastTopicInfo) {
            m_lastTopicInfo = info;
            const auto* chosen = info ? topics->lastSelectedDialogue : nullptr;
            if (chosen && ChronicleRuntime::AnswersChoice(info, info->parentTopic, chosen->parentTopicInfo, chosen->parentTopic)) {
                const auto text = ChronicleRuntime::StripHtml(chosen->topicText.c_str());
                if (!text.empty()) {
                    Chronicle::GetSingleton()->SendMessage("dialogue", PlayerDisplayName(), text);
                }
            }
        }
    }

    // --- Path 1: SubtitleManager ---
    // Populated during formal NPC→player dialogue (DialogueMenu open).
    // In AE 1.6.1170 ambient/idle follower lines do NOT go through this path.
    {
        auto* mgr = RE::SubtitleManager::GetSingleton();
        std::unordered_set<std::string> current;
        bool managerSubtitleActive = false;
        const auto dialogueRadius = Chronicle::GetSingleton()->DialogueRadius();
        if (mgr) for (const auto& info : mgr->subtitles) {
            std::string text = info.subtitle.c_str();
            if (!text.empty()) {
                managerSubtitleActive = true;
                std::string speakerName;
                auto speakerRef = info.speaker.get();
                if (speakerRef) {
                    if (auto* name = speakerRef->GetDisplayFullName()) {
                        speakerName = name;
                    }
                }

                const auto key = speakerName + '\0' + text;
                const bool withinRadius = IsWithinDialogueRadius(speakerRef.get(), dialogueRadius);
                if (withinRadius) {
                    current.insert(key);
                }
                if (withinRadius && !m_activeSubtitles.contains(key)) {
                    Chronicle::GetSingleton()->SendMessage("dialogue", speakerName, text);
                }
                m_lastText = text;
            }
        }
        m_activeSubtitles = std::move(current);
        m_managerSubtitleActive = managerSubtitleActive;
    }

    // --- Path 2: HUD Menu GFx movie ---
    // The HUD displays subtitles for ALL speech (ambient, idle, scene dialogue)
    // by writing directly into the Scaleform movie — bypassing SubtitleManager.
    // We read the live text field value to capture what the player sees.
    {
        m_lastHudPath = m_managerSubtitleActive ? "SubtitleManager" : "none resolved";
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            m_lastHudPath = "HUD Menu unavailable";
            return;
        }

        // Notifications are captured by NotificationHook, not polled here.
        // Prefer the attributed manager entries while they are active.
        if (m_managerSubtitleActive) return;
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

            std::string text = std::string_view(path).ends_with(".htmlText") ? ChronicleRuntime::StripHtml(raw) : std::string(raw);
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
