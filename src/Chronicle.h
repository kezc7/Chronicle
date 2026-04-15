#pragma once

#include "PrismaUI.h"
#include "PCH.h"

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

    // Called when any game menu opens so we can release focus and close the input bar.
    void ForceCloseInput();

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

    bool IsDuplicate(std::string_view a_channel, std::string_view a_message);
    void InvokeUI(std::string_view a_js);

    PRISMA_UI_API::IVPrismaUI2* m_prismaUI{ nullptr };
    bool m_hasV2{ false };  // true only when we got a real IVPrismaUI2, not a V1 cast

    std::atomic<bool> m_hidden{ false };

    struct LastMessage
    {
        std::string channel;
        std::string message;
        std::chrono::steady_clock::time_point time;
    };
    std::optional<LastMessage> m_lastMessage;
    std::mutex m_dedupMutex;
};
