#include "Chronicle.h"
#include "Events.h"
#include "MessageBuilder.h"

// Forward declaration needed for SubtitlePoller::Start() in OnDataLoaded
// (definition is in Events.h which Chronicle.h pulls in via PCH)

void Chronicle::OnDataLoaded()
{
    spdlog::info("Chronicle: OnDataLoaded");
    RegisterEventSinks();
    InitPrismaUI();
    // Do NOT start SubtitlePoller here — kDataLoaded fires at the main menu before
    // any save is loaded. Starting a recurring AddTask loop at that point causes a
    // freeze. Polling starts in OnPostLoadGame instead.
}

void Chronicle::OnPostLoadGame()
{
    spdlog::info("Chronicle: OnPostLoadGame");
    // Show the view now that the game is fully loaded.
    if (m_prismaUI && m_view && m_hidden) {
        m_prismaUI->Show(m_view);
        m_hidden = false;
    }
    // SubtitlePoller is NOT started here. kPostLoadGame fires during the
    // loading→world transition before rendering is active; starting the
    // self-re-enqueueing AddTask loop at that point stalls the first frame.
    // The poller is started by MenuEventSink when "HUD Menu" opens, which
    // is a reliable signal that the world is fully rendered.
}

void Chronicle::RegisterEventSinks()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        spdlog::error("Chronicle: RE::UI unavailable");
        return;
    }

    ui->AddEventSink<RE::MenuOpenCloseEvent>(&MenuEventSink::GetSingleton());

    if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
        modEvents->AddEventSink(&SkyrimNetEventSink::GetSingleton());
    } else {
        spdlog::warn("Chronicle: SKSE mod callback source unavailable — SkyrimNet disabled");
    }

    spdlog::info("Chronicle: event sinks registered");
}

void Chronicle::InitPrismaUI()
{
    m_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
    if (m_prismaUI) {
        m_hasV2 = true;
    } else {
        // Try v1 fallback (no console callback support)
        spdlog::warn("Chronicle: PrismaUI v2 unavailable, trying v1");
        auto* v1 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
        m_prismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI2*>(v1);
        m_hasV2 = false;
    }

    if (!m_prismaUI) {
        spdlog::error("Chronicle: PrismaUI not found — UI will not render. "
                      "Is PrismaUI installed and loaded before Chronicle?");
        return;
    }

    spdlog::info("Chronicle: PrismaUI API acquired, creating view");

    // CreateView path is relative to Data/PrismaUI/views/
    m_view = m_prismaUI->CreateView("Chronicle/index.html", [](PrismaView) {
        Chronicle::GetSingleton()->OnViewReady();
    });

    if (!m_view) {
        spdlog::error("Chronicle: CreateView returned 0 — check that Chronicle/index.html exists");
    } else {
        spdlog::info("Chronicle: view created (handle={})", m_view);
        // Hide immediately — OnPostLoadGame will show it once the game is fully loaded.
        // Without this the view renders on the loading screen.
        m_prismaUI->Hide(m_view);
        m_hidden = true;
    }
}

void Chronicle::OnViewReady()
{
    spdlog::info("Chronicle: DOM ready — registering JS listeners");

    if (!m_prismaUI || !m_view) {
        return;
    }

    // Player sends a message to SkyrimNet
    m_prismaUI->RegisterJSListener(m_view, "playerInput", [](const char* a_text) {
        Chronicle::GetSingleton()->OnPlayerInput(a_text);
    });

    // Console command from the Console channel
    m_prismaUI->RegisterJSListener(m_view, "consoleCommand", [](const char* a_cmd) {
        Chronicle::GetSingleton()->OnConsoleCommand(a_cmd);
    });

    // Settings changed live from the settings panel
    m_prismaUI->RegisterJSListener(m_view, "settingsChanged", [](const char* a_json) {
        Chronicle::GetSingleton()->OnSettingsChanged(a_json);
    });

    // Focus acquired — kUsesCursor handled by PrismaUI's own Focus() call
    m_prismaUI->RegisterJSListener(m_view, "focusAcquired", [](const char*) {
        auto* c = Chronicle::GetSingleton();
        if (c->m_prismaUI) {
            c->m_prismaUI->Focus(c->m_view);
        }
    });

    m_prismaUI->RegisterJSListener(m_view, "focusReleased", [](const char*) {
        auto* c = Chronicle::GetSingleton();
        if (c->m_prismaUI) {
            c->m_prismaUI->Unfocus(c->m_view);
        }
    });

    // Console output → Console channel (v2 only)
    // Guard with m_hasV2: if we fell back to V1, m_prismaUI is a V1 pointer cast to V2*
    // and calling RegisterConsoleCallback would access an invalid vtable slot → crash.
    if (m_hasV2) {
        m_prismaUI->RegisterConsoleCallback(m_view,
            [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel, const char* a_msg) {
                Chronicle::GetSingleton()->SendMessage("console", "", a_msg);
            });
    } else {
        spdlog::info("Chronicle: skipping RegisterConsoleCallback (V1 API — no console channel)");
    }

    spdlog::info("Chronicle: JS listeners registered — UI ready");
}

void Chronicle::SendMessage(std::string_view a_channel,
                            std::string_view a_speaker,
                            std::string_view a_message)
{
    if (m_hidden || !m_prismaUI || !m_view) {
        return;
    }

    if (IsDuplicate(a_channel, a_message)) {
        spdlog::debug("Chronicle: dropping duplicate on '{}'", a_channel);
        return;
    }

    auto json = MessageBuilder::Build(a_channel, a_speaker, a_message);
    InvokeUI(std::string("addMessage(") + json + ")");
}

void Chronicle::ForceCloseInput()
{
    // Release PrismaUI focus and tell JS to close the input bar.
    // Guard on m_hidden: no-op while the view is hidden during loading so we
    // don't call into PrismaUI during the loading→world transition.
    if (m_hidden || !m_prismaUI || !m_view) {
        return;
    }
    m_prismaUI->Unfocus(m_view);
    InvokeUI("closeInput()");
}

void Chronicle::OnMenuOpenClose(std::string_view a_menuName, bool a_opening)
{
    if (a_menuName == "PhotoMenu") {
        m_hidden = a_opening;
        if (m_prismaUI && m_view) {
            if (a_opening) {
                m_prismaUI->Hide(m_view);
            } else {
                m_prismaUI->Show(m_view);
            }
        }
        spdlog::info("Chronicle: photo mode {} — window {}",
                     a_opening ? "opened" : "closed",
                     a_opening ? "hidden" : "shown");
    }
}

void Chronicle::OnPlayerInput(std::string_view a_text)
{
    SendMessage("player", "Player", a_text);

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
    if (!cmd.empty() && cmd.front() == '/') {
        cmd = cmd.substr(1);
    }

    const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    if (!scriptFactory) {
        spdlog::error("Chronicle: Script factory unavailable");
        return;
    }
    const auto script = scriptFactory->Create();
    if (script) {
        script->SetCommand(cmd);
        script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
        delete script;
    }
}

void Chronicle::OnSettingsChanged(std::string_view a_json)
{
    spdlog::info("Chronicle: settings updated from UI");
    // Settings are handled in JS; nothing for C++ to act on yet.
    (void)a_json;
}

bool Chronicle::IsDuplicate(std::string_view a_channel, std::string_view a_message)
{
    std::lock_guard lock(m_dedupMutex);
    auto now = std::chrono::steady_clock::now();

    if (m_lastMessage.has_value()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - m_lastMessage->time).count();
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
    if (m_prismaUI && m_view) {
        m_prismaUI->Invoke(m_view, a_js.data());
    }
}
