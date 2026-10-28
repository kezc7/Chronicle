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
#include "RuntimePolicy.h"
#include "UiHost.h"

// Documents\My Games\Skyrim Special Edition\SKSE. SKSE::log::log_directory()
// alone resolves to My Games\Skyrim.INI\SKSE on 1.6.1170 (see MyGamesFolder).
inline std::optional<std::filesystem::path> ChronicleLogDirectory()
{
    auto dir = SKSE::log::log_directory();
    if (!dir) return dir;
    const auto myGames = dir->parent_path();
    return myGames.parent_path() / ChronicleRuntime::MyGamesFolder(myGames.filename().native()) / L"SKSE";
}

// The player's in-game name for player lines, matching combat lines. Main thread.
inline std::string PlayerDisplayName()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* name = player ? player->GetDisplayFullName() : nullptr;
    return name && name[0] != '\0' ? name : "Player";
}

class Chronicle
{
public:
    static Chronicle* GetSingleton()
    {
        static Chronicle instance;
        return &instance;
    }

    Chronicle(const Chronicle&) = delete;
    Chronicle& operator=(const Chronicle&) = delete;

    void OnDataLoaded();
    void OnPreLoadGame();
    void OnPostLoadGame();
    void OnWorldReady();
    void Tick();
    void PollMenuFramework();
    void Bootstrap();
    void Diagnostics();
    void ExportHistory(std::string_view text);
    // Returns true when the keyboard event belongs to Chronicle and should not
    // continue into Skyrim's gameplay/menu input pipeline.
    bool OnHotkey(std::uint32_t scanCode, bool shift);
    bool ShouldConsumeKeyboardInput() const { return m_keyboardCapture.load(); }
    bool ShowCombatHits() const { return m_combatHits.load(); }
    float DialogueRadius() const { return m_dialogueRadius.load(); }
    // A line Chronicle writes itself, in the chosen language (game text is already localized).
    std::string Text(ChronicleRuntime::Phrase a_phrase, std::string_view a_value = {}) const
    {
        return ChronicleRuntime::Phrased(Language(), a_phrase, a_value);
    }
    ChronicleRuntime::Lang Language() const { return static_cast<ChronicleRuntime::Lang>(m_language.load()); }
    // Hide the game's own notification only when Chronicle will visibly show it.
    bool ShouldHideGameNotification() const
    {
        return m_hideGameNotifications.load() && m_chatboxEnabled.load() &&
               m_notificationChannel.load() && m_gameLoaded.load() && !m_hidden.load();
    }
    std::atomic<std::uint64_t> profileId{ 0 };

    void SendMessage(std::string_view a_channel,
                     std::string_view a_speaker,
                     std::string_view a_message);

    void OnMenuOpenClose(std::string_view a_menuName, bool a_opening);

    // Called when any game menu opens so we can release focus and close the input bar.
    void ForceCloseInput();

    bool IsGameLoaded() const { return m_gameLoaded.load(); }

    // Called by JS listener callbacks (bare function pointers — no captures)
    void OnPlayerInput(std::string_view a_text);
    void OnConsoleCommand(std::string_view a_command);
    void OnSettingsChanged(std::string_view a_json);

private:
    Chronicle() = default;

    void RegisterEventSinks();
    void InitUiHost();          // called on kDataLoaded — resolves the host + creates the view
    void OnViewReady();         // called by DOM-ready callback — registers JS listeners
    void FlushPendingUI();
    void RequestFocus();
    void RequestUnfocus();
    void ToggleUserHidden();
    void UpdateVisibility();
    void PublishRuntime();
    void ApplyNativeSettings(const nlohmann::json& settings);

    bool IsDuplicate(std::string_view a_channel, std::string_view a_speaker, std::string_view a_message);
    void InvokeUI(std::string_view a_js);

    std::atomic<bool> m_hidden{ true };
    std::atomic<bool> m_gameLoaded{ false };  // set true by OnPostLoadGame
    std::atomic<bool> m_hideInMenus{ true };
    std::atomic<bool> m_hideInPhotoMode{ true };
    std::atomic<bool> m_viewReady{ false };

    std::mutex m_uiMutex;
    std::deque<std::string> m_pendingUI;
    // Runtime state is only accessed on Skyrim's main thread.
    ChronicleRuntime::State m_state;
    bool m_menuFrameworkOpen{ false };
    std::atomic<std::uint64_t> m_focusRevision{ 0 };
    std::atomic<bool> m_keyboardCapture{ false };
    std::atomic<bool> m_combatHits{ true };
    std::atomic<float> m_dialogueRadius{ 0.0F };
    std::atomic<int> m_language{ 0 };  // ChronicleRuntime::Lang
    std::string m_gameLanguage;        // Skyrim's sLanguage, read once at data load
    std::atomic<bool> m_hideGameNotifications{ false };
    std::atomic<bool> m_chatboxEnabled{ true };
    std::atomic<bool> m_notificationChannel{ true };
    std::atomic<std::uint64_t> m_worldRevision{ 0 };
    bool m_hadFocus{ false };   // Focus() succeeded for the current interaction
    bool m_focusSeen{ false };  // HasFocus() has reported it since then
    std::chrono::steady_clock::time_point m_focusGrantedAt;
    bool m_bootstrapped{ false };
    std::uint32_t m_interactKey{ VK_F8 };
    std::uint32_t m_settingsKey{ 'N' };
    std::uint32_t m_inputKey{ VK_RETURN };
    std::uint32_t m_toggleKey{ 0 };              // show/hide hotkey; 0 = none
    std::atomic<bool> m_rememberHidden{ true };
    std::string m_lastSubtitle;
    std::uint64_t m_messageCount{ 0 };

    struct LastMessage
    {
        std::string channel;
        std::string speaker;
        std::string message;
        std::chrono::steady_clock::time_point time;
    };
    std::optional<LastMessage> m_lastMessage;
    std::mutex m_dedupMutex;
};
