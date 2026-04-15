#pragma once

#include "PCH.h"
#include "PrismaUI.h"

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
    void OnPostLoadGame();

    // Called by event sinks
    void SendMessage(std::string_view a_channel,
                     std::string_view a_speaker,
                     std::string_view a_message);

    void OnMenuOpenClose(std::string_view a_menuName, bool a_opening);

    // Called by JS listener callbacks
    void OnPlayerInput(std::string_view a_text);
    void OnConsoleCommand(std::string_view a_command);
    void OnSettingsChanged(const nlohmann::json& a_settings);

    // PrismaUI view handle — exposed for lambda capture in RegisterJSListeners
    void* m_view{ nullptr };

private:
    Chronicle() = default;

    void RegisterEventSinks();
    void RegisterJSListeners();
    void LoadConfig();

    bool IsDuplicate(std::string_view a_channel, std::string_view a_message);

    void InvokeUI(std::string_view a_js);

    struct LastMessage
    {
        std::string channel;
        std::string message;
        std::chrono::steady_clock::time_point time;
    };
    std::optional<LastMessage> m_lastMessage;
    std::mutex m_dedupMutex;

    // Whether to suppress messages (e.g. photo mode active)
    std::atomic<bool> m_hidden{ false };
};
