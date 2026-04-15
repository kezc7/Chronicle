#include "Chronicle.h"
#include "Events.h"
#include "MessageBuilder.h"
#include "PrismaUI.h"

void Chronicle::OnDataLoaded()
{
    spdlog::info("Chronicle: OnDataLoaded — registering event sinks");
    RegisterEventSinks();
    LoadConfig();
}

void Chronicle::OnPostLoadGame()
{
    spdlog::info("Chronicle: OnPostLoadGame — registering JS listeners");
    RegisterJSListeners();
}

void Chronicle::RegisterEventSinks()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        spdlog::error("Chronicle: RE::UI singleton unavailable");
        return;
    }

    // Menu open/close — photo mode hiding + dialogue subtitle polling
    ui->AddEventSink<RE::MenuOpenCloseEvent>(&MenuEventSink::GetSingleton());

    // SkyrimNet mod events (SKSE::ModCallbackEvent, not RE::TESModCallbackEvent)
    if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
        modEvents->AddEventSink(&SkyrimNetEventSink::GetSingleton());
    } else {
        spdlog::warn("Chronicle: SKSE mod callback event source unavailable — SkyrimNet integration disabled");
    }

    spdlog::info("Chronicle: event sinks registered");
}

void Chronicle::RegisterJSListeners()
{
    auto* prisma = PrismaUI::GetSingleton();
    if (!prisma) {
        spdlog::error("Chronicle: PrismaUI not available");
        return;
    }

    m_view = prisma->GetView("Chronicle");
    if (!m_view) {
        spdlog::error("Chronicle: failed to get PrismaUI view 'Chronicle'");
        return;
    }

    // Player sends a message to SkyrimNet
    prisma->RegisterJSListener(m_view, "playerInput", [](const char* a_text) {
        Chronicle::GetSingleton()->OnPlayerInput(a_text);
    });

    // Console command typed in Console channel
    prisma->RegisterJSListener(m_view, "consoleCommand", [](const char* a_command) {
        Chronicle::GetSingleton()->OnConsoleCommand(a_command);
    });

    // Settings changed live from the settings panel
    prisma->RegisterJSListener(m_view, "settingsChanged", [](const char* a_json) {
        try {
            auto j = nlohmann::json::parse(a_json);
            Chronicle::GetSingleton()->OnSettingsChanged(j);
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("Chronicle: invalid settings JSON — {}", e.what());
        }
    });

    // Focus acquired/released by the input bar
    prisma->RegisterJSListener(m_view, "focusAcquired", [](const char*) {
        // Ensure the PrismaUI focus menu has kUsesCursor so the mouse doesn't get stuck
        auto focusMenu = RE::UI::GetSingleton()->GetMenu("PrismaUI_FocusMenu");
        if (focusMenu) {
            focusMenu->menuFlags.set(
                RE::UI_MENU_FLAGS::kUsesCursor,
                RE::UI_MENU_FLAGS::kAllowSaving);
        }
        PrismaUI::GetSingleton()->Focus(Chronicle::GetSingleton()->m_view);
    });

    prisma->RegisterJSListener(m_view, "focusReleased", [](const char*) {
        PrismaUI::GetSingleton()->Unfocus(Chronicle::GetSingleton()->m_view);
    });

    // Console output → Console channel
    prisma->RegisterConsoleCallback([](const char* a_output) {
        Chronicle::GetSingleton()->SendMessage("console", "", a_output);
    });

    spdlog::info("Chronicle: JS listeners registered");
}

void Chronicle::LoadConfig()
{
    // Config is owned and persisted by the web UI (config.json).
    // The UI sends an initial settingsChanged event on startup.
    spdlog::info("Chronicle: config will be loaded by the web UI on startup");
}

void Chronicle::SendMessage(std::string_view a_channel,
                            std::string_view a_speaker,
                            std::string_view a_message)
{
    if (m_hidden) {
        return;
    }

    if (IsDuplicate(a_channel, a_message)) {
        spdlog::debug("Chronicle: dropping duplicate on channel '{}'", a_channel);
        return;
    }

    auto json = MessageBuilder::Build(a_channel, a_speaker, a_message);
    InvokeUI(std::string("addMessage(") + json + ")");
}

void Chronicle::OnMenuOpenClose(std::string_view a_menuName, bool a_opening)
{
    if (a_menuName == "PhotoMenu") {
        m_hidden = a_opening;
        InvokeUI(a_opening ? "hideWindow()" : "showWindow()");
        spdlog::info("Chronicle: photo mode {} — window {}",
                     a_opening ? "opened" : "closed",
                     a_opening ? "hidden" : "shown");
    }
}

void Chronicle::OnPlayerInput(std::string_view a_text)
{
    // Echo locally to Player channel
    SendMessage("player", "Player", a_text);

    // Fire SkyrimNet_PlayerInput as an SKSE mod callback event
    auto* src = SKSE::GetModCallbackEventSource();
    if (src) {
        SKSE::ModCallbackEvent evt{
            RE::BSFixedString("SkyrimNet_PlayerInput"),
            RE::BSFixedString(std::string(a_text).c_str()),
            0.0f,
            nullptr,
        };
        src->SendEvent(&evt);
    }
}

void Chronicle::OnConsoleCommand(std::string_view a_command)
{
    std::string cmd(a_command);
    // Strip a leading '/' if the user typed one
    if (!cmd.empty() && cmd.front() == '/') {
        cmd = cmd.substr(1);
    }

    // Execute via RE::Script (the only way in CommonLibSSE-NG without raw trampolines)
    const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    if (!scriptFactory) {
        spdlog::error("Chronicle: could not get Script factory for console command");
        return;
    }
    const auto script = scriptFactory->Create();
    if (script) {
        script->SetCommand(cmd);
        script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
        delete script;
    }
}

void Chronicle::OnSettingsChanged(const nlohmann::json&)
{
    // Runtime settings are handled entirely in JS.
    // If we need to act on specific flags (e.g. hide_in_photo_mode),
    // pull them here and store in member variables.
    spdlog::info("Chronicle: settings updated from UI");
}

bool Chronicle::IsDuplicate(std::string_view a_channel, std::string_view a_message)
{
    std::lock_guard lock(m_dedupMutex);

    auto now = std::chrono::steady_clock::now();

    if (m_lastMessage.has_value()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - m_lastMessage->time)
                           .count();
        if (elapsed < 500 &&
            m_lastMessage->channel == a_channel &&
            m_lastMessage->message == a_message) {
            return true;
        }
    }

    m_lastMessage = LastMessage{ std::string(a_channel), std::string(a_message), now };
    return false;
}

void Chronicle::InvokeUI(std::string_view a_js)
{
    if (!m_view) {
        return;
    }
    if (auto* prisma = PrismaUI::GetSingleton()) {
        prisma->Invoke(m_view, a_js.data());
    }
}
