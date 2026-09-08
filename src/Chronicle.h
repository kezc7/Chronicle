#pragma once

#include "PrismaUI.h"
#include "PCH.h"
#include "RuntimePolicy.h"

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
    void Bootstrap();
    void Diagnostics();
    void ExportHistory(std::string_view text);
    void OnHotkey(std::uint32_t scanCode, bool shift);
    std::atomic<std::uint64_t> profileId{ 0 };

    // Called by event sinks
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

    // PrismaUI view handle — set in CreateView DOM-ready callback
    PrismaView m_view{ 0 };

private:
    Chronicle() = default;

    void RegisterEventSinks();
    void InitPrismaUI();        // called on kDataLoaded — gets API + creates view
    void OnViewReady();         // called by DOM-ready callback — registers JS listeners
    void FlushPendingUI();
    void RequestFocus();
    void RequestUnfocus();
    void UpdateVisibility();
    void PublishRuntime();
    void ApplyNativeSettings(const nlohmann::json& settings);

    bool IsDuplicate(std::string_view a_channel, std::string_view a_speaker, std::string_view a_message);
    void InvokeUI(std::string_view a_js);

    PRISMA_UI_API::IVPrismaUI1* m_prismaUI{ nullptr };
    PRISMA_UI_API::IVPrismaUI2* m_prismaV2{ nullptr };
    bool m_hasV2{ false };  // true only when we got a real IVPrismaUI2, not a V1 cast

    std::atomic<bool> m_hidden{ true };
    std::atomic<bool> m_gameLoaded{ false };  // set true by OnPostLoadGame
    std::atomic<bool> m_hideInMenus{ true };
    std::atomic<bool> m_hideInPhotoMode{ true };
    std::atomic<bool> m_viewReady{ false };

    std::mutex m_uiMutex;
    std::deque<std::string> m_pendingUI;
    // Runtime state is only accessed on Skyrim's main thread.
    ChronicleRuntime::State m_state;
    std::atomic<std::uint64_t> m_focusRevision{ 0 };
    std::atomic<std::uint64_t> m_worldRevision{ 0 };
    bool m_hadFocus{ false };
    bool m_bootstrapped{ false };
    std::uint32_t m_interactKey{ VK_F8 };
    std::uint32_t m_settingsKey{ 'N' };
    std::uint32_t m_inputKey{ VK_RETURN };
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
