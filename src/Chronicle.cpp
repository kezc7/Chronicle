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

#include "Chronicle.h"
#include "Events.h"
#include "MessageBuilder.h"
#include <fstream>
#include <filesystem>

namespace
{
    const std::filesystem::path kSettingsPath = L"Data/SKSE/Plugins/Chronicle.json";

    // Own the callback payload before leaving PrismaUI's thread.
    template<class F> void MainTask(F task)
    {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([task = std::move(task)]() {
                try { task(); }
                catch (const std::exception& e) { spdlog::error("Chronicle: task failed: {}", e.what()); }
                catch (...) { spdlog::error("Chronicle: task failed"); }
            });
        }
    }

    // RE::Script::CompileAndRun uses AE ID 21890, which is absent from the
    // 1.6.1130+ address library; the lookup resolves to the neighbouring
    // function (21891) and crashes. The function is ID 441582 there, as
    // ConsoleUtilSSE NG also uses.
    void CompileAndRun(RE::Script* a_script, RE::TESObjectREFR* a_target)
    {
        using func_t = void(RE::Script*, RE::ScriptCompiler*, RE::COMPILER_NAME, RE::TESObjectREFR*);
        const auto id = REL::Module::IsAE() ?
            (REL::Module::get().version() >= REL::Version(1, 6, 1130) ? 441582 : 21890) : 21416;
        REL::Relocation<func_t*> func{ REL::ID(id) };
        RE::ScriptCompiler compiler;
        func(a_script, &compiler, RE::COMPILER_NAME::kSystemWindowCompiler, a_target);
    }

    std::string Json(const nlohmann::json& value)
    {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void AtomicWrite(const std::filesystem::path& path, std::string_view content)
    {
        std::filesystem::create_directories(path.parent_path());
        auto temp = path;
        temp += L".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.flush();
        }
        if (std::filesystem::exists(path)) {
            auto backup = path;
            backup += L".bak";
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace file (Windows error " + std::to_string(GetLastError()) + ")");
        }
    }
}

void Chronicle::OnDataLoaded()
{
    spdlog::info("Chronicle " CHRONICLE_VERSION ": OnDataLoaded");
    // "Auto" language follows Skyrim's sLanguage (e.g. ENGLISH, FRENCH, GERMAN).
    if (auto* ini = RE::INISettingCollection::GetSingleton()) {
        if (auto* setting = ini->GetSetting("sLanguage:General");
            setting && setting->GetType() == RE::Setting::Type::kString && setting->GetString()) {
            m_gameLanguage = setting->GetString();
        }
    }
    m_language = static_cast<int>(ChronicleRuntime::ResolveLanguage("auto", m_gameLanguage));
    spdlog::info("Chronicle: game language '{}'", m_gameLanguage);
    RegisterEventSinks();
    InitPrismaUI();
}

void Chronicle::OnPreLoadGame()
{
    ++m_worldRevision;
    m_gameLoaded = false;
    m_state.loaded = false;
    ForceCloseInput();
    SubtitlePoller::GetSingleton().Stop();
    {
        std::lock_guard lock(m_uiMutex);
        m_pendingUI.clear();
    }
    {
        std::lock_guard lock(m_dedupMutex);
        m_lastMessage.reset();
    }
    m_lastSubtitle.clear();
    m_messageCount = 0;
    UpdateVisibility();
}

void Chronicle::OnPostLoadGame()
{
    OnWorldReady();
}

void Chronicle::OnWorldReady()
{
    if (m_gameLoaded.exchange(true)) {
        UpdateVisibility();
        return;
    }
    m_state.loaded = true;
    if (!profileId.load()) {
        profileId = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    }
    spdlog::info("Chronicle: world ready; profile {}", profileId.load());
    PublishRuntime();
    UpdateVisibility();
    SubtitlePoller::GetSingleton().Start();
    FlushPendingUI();
}

void Chronicle::RegisterEventSinks()
{
    if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
        auto* sink = &ChronicleInputSink::GetSingleton();
        input->AddEventSink(sink);
        // Run first: AddEventSink appends, so kStop while typing would only hide
        // keys from mods registered after Chronicle, not their hotkeys before it.
        RE::BSSpinLockGuard locker(input->lock);
        auto& sinks = input->sinks;
        if (const auto it = std::find(sinks.begin(), sinks.end(), sink); !input->notifying && it != sinks.end()) {
            std::rotate(sinks.begin(), it, it + 1);
        }
    }
    if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink(&MenuEventSink::GetSingleton());
    if (auto* events = SKSE::GetModCallbackEventSource()) events->AddEventSink(&SkyrimNetEventSink::GetSingleton());
    if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
        events->AddEventSink<RE::TESHitEvent>(&CombatEventSink::GetSingleton());
        events->AddEventSink<RE::TESDeathEvent>(&DeathEventSink::GetSingleton());
    }
    if (auto* levels = RE::LevelIncrease::GetEventSource()) levels->AddEventSink(&LevelUpEventSink::GetSingleton());
}

void Chronicle::InitPrismaUI()
{
    m_prismaV2 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
    m_hasV2 = m_prismaV2 != nullptr;
    m_prismaUI = m_prismaV2 ? m_prismaV2 : PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
    if (!m_prismaUI) {
        spdlog::error("Chronicle: PrismaUI unavailable");
        return;
    }
    m_view = m_prismaUI->CreateView("Chronicle/index.html", [](PrismaView view) {
        MainTask([view]() {
            auto* c = Chronicle::GetSingleton();
            c->m_view = view;
            c->OnViewReady();
        });
    });
    if (m_view) m_prismaUI->Hide(m_view);
    spdlog::info("Chronicle: view {} created, API v{}", m_view, m_hasV2 ? 2 : 1);
}

void Chronicle::OnViewReady()
{
    if (!m_prismaUI || !m_view) return;
    m_prismaUI->RegisterJSListener(m_view, "playerInput", [](const char* text) {
        if (text) MainTask([payload = std::string(text)] { GetSingleton()->OnPlayerInput(payload); });
    });
    m_prismaUI->RegisterJSListener(m_view, "consoleCommand", [](const char* text) {
        if (text) MainTask([payload = std::string(text)] { GetSingleton()->OnConsoleCommand(payload); });
    });
    m_prismaUI->RegisterJSListener(m_view, "settingsChanged", [](const char* text) {
        if (text) MainTask([payload = std::string(text)] { GetSingleton()->OnSettingsChanged(payload); });
    });
    m_prismaUI->RegisterJSListener(m_view, "exportHistory", [](const char* text) {
        if (text) MainTask([payload = std::string(text)] { GetSingleton()->ExportHistory(payload); });
    });
    m_prismaUI->RegisterJSListener(m_view, "focusAcquired", [](const char*) { GetSingleton()->RequestFocus(); });
    m_prismaUI->RegisterJSListener(m_view, "focusReleased", [](const char*) { GetSingleton()->RequestUnfocus(); });
    m_prismaUI->RegisterJSListener(m_view, "uiReady", [](const char*) { MainTask([] { GetSingleton()->Bootstrap(); }); });
    m_prismaUI->RegisterJSListener(m_view, "diagnostics", [](const char*) { MainTask([] { GetSingleton()->Diagnostics(); }); });

    if (m_prismaV2) {
        // This is WEB console output, not Skyrim console-command output.
        m_prismaV2->RegisterConsoleCallback(m_view,
            [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel, const char* text) {
                if (text) spdlog::debug("Chronicle web: {}", text);
            });
    }
    m_viewReady = true;
    spdlog::info("Chronicle: listeners ready, waiting for UI handshake");
}

void Chronicle::Bootstrap()
{
    if (!m_viewReady || m_bootstrapped) return;
    nlohmann::json settings = nullptr;
    std::string warning;
    try {
        if (std::filesystem::exists(kSettingsPath)) {
            if (std::filesystem::file_size(kSettingsPath) > 65536) throw std::runtime_error("Settings file exceeds 64 KB");
            std::ifstream input(kSettingsPath);
            input >> settings;
            ApplyNativeSettings(settings);
        }
    } catch (const std::exception& e) {
        settings = nullptr;
        warning = std::string("Settings file could not be loaded: ") + e.what();
        spdlog::warn("Chronicle: {}", warning);
    }
    m_bootstrapped = true;
    m_state.ready = true;
    InvokeUI("nativeBootstrap(" + Json({{"settings", settings}, {"warning", warning}, {"version", CHRONICLE_VERSION}, {"nativeHotkeys", true}, {"gameLanguage", m_gameLanguage}}) + ")");
    PublishRuntime();
    UpdateVisibility();
    FlushPendingUI();
}

void Chronicle::UpdateVisibility()
{
    // Seed from live menu state: startup open events may predate our listener.
    if (auto* ui = RE::UI::GetSingleton()) {
        static constexpr std::string_view names[] = {
            "Main Menu", "Loading Menu", "LoadWaitSpinner", "Fader Menu", "SystemMenu", "TweenMenu",
            "InventoryMenu", "MagicMenu", "MapMenu", "ContainerMenu", "Journal Menu", "LevelUpMenu",
            "StatsMenu", "SleepWaitMenu", "FavoritesMenu", "BarterMenu",
            "CraftingMenu", "Crafting Menu", "SmithingMenu", "Smithing Menu",
            "AlchemyMenu", "Alchemy Menu", "EnchantingMenu", "Enchanting Menu",
            "CookingMenu", "Cooking Menu", "PhotoMenu", "MeridianUI_FocusMenu", "MagelightFocus", "Console",
            "RaceSex Menu", "Book Menu", "Lockpicking Menu", "Training Menu", "GiftMenu",
            "Tutorial Menu", "Credits Menu", "Mod Manager Menu", "CustomMenu"
        };
        for (auto name : names) {
            const bool open = ui->IsMenuOpen(name);
            auto flags = ChronicleRuntime::kInputMenuFlags;
            if (open) {
                if (const auto menu = ui->GetMenu(name)) flags = menu->menuFlags.underlying();
            }
            m_state.Menu(name, open, flags);
        }
    }
    const bool visible = m_state.Visible();
    if (!visible && !m_hidden) ForceCloseInput();
    if (m_prismaUI && m_view && m_viewReady) {
        InvokeUI(visible ? "setVisibility(true)" : "setVisibility(false)");
        if (visible) m_prismaUI->Show(m_view);
        else m_prismaUI->Hide(m_view);
    }
    m_hidden = !visible;
}

void Chronicle::PublishRuntime()
{
    if (!m_bootstrapped) return;
    std::string name;
    if (m_gameLoaded) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) name = player->GetName();
    }
    InvokeUI("setRuntime(" + Json({
        {"loaded", m_gameLoaded.load()}, {"profile", m_gameLoaded ? std::to_string(profileId.load()) : ""},
        {"character", ChronicleUtf8::ReplaceInvalid(name).value}
    }) + ")");
}

void Chronicle::Tick()
{
    if (!m_gameLoaded) return;
    PollMenuFramework();
    // Menus can unfocus a view without delivering an Escape key to JavaScript.
    if (!m_prismaUI || !m_view || !m_hadFocus) return;
    const bool focused = m_prismaUI->HasFocus(m_view);
    m_focusSeen = m_focusSeen || focused;
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_focusGrantedAt).count();
    if (ChronicleRuntime::FocusLost(focused, m_focusSeen, static_cast<int>(std::min<long long>(waited, 60000)))) {
        spdlog::info("Chronicle: focus {}", m_focusSeen ? "lost" : "never arrived");
        m_hadFocus = false;
        m_focusSeen = false;
        m_keyboardCapture = false;
        ++m_focusRevision;
        InvokeUI("closeInteraction(false)");
        InvokeUI("focusResult(false, 'focus.closed')");
    }
}

void Chronicle::PollMenuFramework()
{
    // SKSE Menu Framework 3 exports this; older versions and absent installs are skipped.
    using IsOpenFn = bool (*)();
    static const auto isOpen = [] {
        const auto module = GetModuleHandleW(L"SKSEMenuFramework.dll");
        return module ? reinterpret_cast<IsOpenFn>(GetProcAddress(module, "IsAnyBlockingWindowOpened")) : nullptr;
    }();
    if (!isOpen) return;
    const bool open = isOpen();
    if (open == m_menuFrameworkOpen) return;
    m_menuFrameworkOpen = open;
    OnMenuOpenClose(ChronicleRuntime::kMenuFrameworkMenu, open);
}

void Chronicle::OnMenuOpenClose(std::string_view name, bool opening)
{
    if (opening && name == "Main Menu") {
        OnPreLoadGame();
        m_state.Menu(name, true);
        PublishRuntime();
    }
    std::uint32_t flags = ChronicleRuntime::kInputMenuFlags;
    if (opening) {
        if (auto* ui = RE::UI::GetSingleton()) {
            if (const auto menu = ui->GetMenu(name)) flags = menu->menuFlags.underlying();
        }
        if (ChronicleRuntime::InterruptsFocus(name, flags)) {
            if (m_keyboardCapture || m_hadFocus) spdlog::info("Chronicle: '{}' opened, closing Chronicle input", name);
            ForceCloseInput();
        }
    }
    m_state.Menu(name, opening, flags);
    // One deferred reconciliation lets Skyrim finish updating its menu stack.
    MainTask([this, menu = std::string(name), opening] {
        if (!opening && (menu == "Loading Menu" || menu == "Fader Menu") && !m_gameLoaded) {
            auto* ui = RE::UI::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (ui && player && player->Is3DLoaded() && !ui->IsMenuOpen("Main Menu") &&
                !ui->IsMenuOpen("Loading Menu") && !ui->IsMenuOpen("Fader Menu")) OnWorldReady();
        }
        UpdateVisibility();
    });
}

void Chronicle::ForceCloseInput()
{
    ++m_focusRevision; // invalidate any focus request queued before this menu/load
    m_hadFocus = false;
    m_focusSeen = false;
    m_keyboardCapture = false;
    if (!m_prismaUI || !m_view || !m_viewReady) return;
    if (m_prismaUI->HasFocus(m_view)) m_prismaUI->Unfocus(m_view);
    InvokeUI("closeInteraction(false)");
}

void Chronicle::RequestFocus()
{
    const auto revision = ++m_focusRevision;
    MainTask([this, revision] {
        if (revision != m_focusRevision) return;
        if (!m_bootstrapped || m_hidden || !m_gameLoaded) {
            m_keyboardCapture = false;
            InvokeUI("focusResult(false, 'focus.unavailable')");
            return;
        }
        if (m_prismaUI->HasAnyActiveFocus() && !m_prismaUI->HasFocus(m_view)) {
            m_keyboardCapture = false;
            InvokeUI("focusResult(false, 'focus.otherMod')");
            return;
        }
        const bool focused = m_prismaUI->Focus(m_view, false, false);
        m_hadFocus = focused;
        m_focusSeen = focused && m_prismaUI->HasFocus(m_view);
        m_focusGrantedAt = std::chrono::steady_clock::now();
        m_keyboardCapture = focused;
        spdlog::info("Chronicle: focus request -> {}", focused ? "success" : "FAILED");
        InvokeUI(focused ? "focusResult(true, 'focus.active')" :
                          "focusResult(false, 'focus.failed')");
    });
}

void Chronicle::RequestUnfocus()
{
    const auto revision = ++m_focusRevision;
    MainTask([this, revision] {
        if (revision != m_focusRevision) return;
        ForceCloseInput();
    });
}

void Chronicle::SendMessage(std::string_view channel, std::string_view speaker, std::string_view message)
{
    // Always copy/sanitize before the borrowed strings leave the producer.
    // Every game event and UI delivery is then serialized on the main thread.
    try {
        const auto revision = m_worldRevision.load();
        auto payload = MessageBuilder::Build(channel, speaker, message);
        MainTask([this, revision, payload = std::move(payload)] {
            if (!m_gameLoaded || revision != m_worldRevision) return;
            const auto data = nlohmann::json::parse(payload);
            const auto channel = data.at("channel").get<std::string>();
            const auto speaker = data.at("speaker").is_null() ? std::string{} : data.at("speaker").get<std::string>();
            const auto message = data.at("message").get<std::string>();
            if (message.empty() || IsDuplicate(channel, speaker, message)) return;
            ++m_messageCount;
            if (channel == "dialogue") m_lastSubtitle = message;
            auto script = "addMessage(" + payload + ")";
            // A hidden view still receives messages, so menus don't erase history.
            if (m_bootstrapped) InvokeUI(script);
            else {
                std::lock_guard lock(m_uiMutex);
                if (m_pendingUI.size() >= 500) m_pendingUI.pop_front();
                m_pendingUI.push_back(std::move(script));
            }
        });
    } catch (const std::exception& e) {
        spdlog::error("Chronicle: message rejected: {}", e.what());
    } catch (...) {
        spdlog::error("Chronicle: message rejected");
    }
}

void Chronicle::OnPlayerInput(std::string_view text)
{
    if (!m_gameLoaded || m_hidden || text.size() > 8192) return;
    SendMessage("player", PlayerDisplayName(), text);
    if (auto* events = SKSE::GetModCallbackEventSource()) {
        SKSE::ModCallbackEvent event{
            RE::BSFixedString("SkyrimNet_PlayerInput"), RE::BSFixedString(std::string(text).c_str()), 0.0f, nullptr};
        events->SendEvent(&event);
    }
}

void Chronicle::OnConsoleCommand(std::string_view text)
{
    // Each outcome is logged so user logs show why a command did nothing.
    const auto shown = std::string(text.substr(0, 200));
    if (!m_gameLoaded || m_hidden || text.empty() || text.size() > 8192) {
        spdlog::info("Chronicle: console command '{}' ignored (loaded={}, hidden={}, length={})",
            shown, m_gameLoaded.load(), m_hidden.load(), text.size());
        return;
    }
    std::string command(text);
    if (command.front() == '/') command.erase(0, 1);
    const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    if (!factory) {
        spdlog::warn("Chronicle: console command '{}' failed: no script factory", shown);
        return;
    }
    std::unique_ptr<RE::Script> script(factory->Create());
    if (!script) {
        spdlog::warn("Chronicle: console command '{}' failed: could not create script", shown);
        return;
    }
    script->SetCommand(command);
    SendMessage("console", "", "> " + command);

    // Commands print synchronously into the console log; post what this one added.
    auto* log = RE::ConsoleLog::GetSingleton();
    const std::string before = log ? log->buffer.c_str() : "";
    CompileAndRun(script.get(), RE::PlayerCharacter::GetSingleton());
    const auto output = log ? ChronicleRuntime::ConsoleOutputSince(before, log->buffer.c_str()) : std::string{};
    const auto lines = ChronicleRuntime::ConsoleLines(output, 50, Language());
    for (const auto& line : lines) SendMessage("console", "", line);
    spdlog::info("Chronicle: console command '{}' run, {} output line(s)", shown, lines.size());
}

void Chronicle::ApplyNativeSettings(const nlohmann::json& settings)
{
    if (!settings.is_object() || !settings.at("general").is_object() ||
        !settings.at("window").is_object() || !settings.at("channels").is_object()) {
        throw std::runtime_error("Invalid settings structure");
    }
    const auto& general = settings.at("general");
    const bool hideMenus = general.value("hide_in_menus", true);
    const bool hidePhoto = general.value("hide_in_photo_mode", true);
    m_state.hideMenus = hideMenus;
    m_state.hidePhoto = hidePhoto;
    m_combatHits = general.value("combat_hits", true);
    m_dialogueRadius = ChronicleRuntime::NormalizeDialogueRadius(
        general.value("dialogue_radius", 0.0F));
    m_hideGameNotifications = general.value("hide_game_notifications", false);
    m_chatboxEnabled = general.value("enabled", true);
    m_language = static_cast<int>(ChronicleRuntime::ResolveLanguage(
        general.value("language", std::string("auto")), m_gameLanguage));
    const auto& channels = settings.at("channels");
    m_notificationChannel = !channels.contains("notification") || !channels.at("notification").is_object() ||
        channels.at("notification").value("enabled", true);
    std::vector<std::string> extraHudPaths;
    if (const auto it = settings.find("hud_subtitle_paths"); it != settings.end() && it->is_array()) {
        for (const auto& value : *it) {
            if (value.is_string()) extraHudPaths.push_back(value.get<std::string>());
        }
    }
    SubtitlePoller::GetSingleton().SetExtraHudPaths(std::move(extraHudPaths));
    SubtitlePoller::GetSingleton().SetHudCapture(settings.value("hud_subtitle_capture", true));
    auto key = [](std::string value, std::uint32_t fallback) -> std::uint32_t {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (value == "ENTER") return VK_RETURN;
        if (value.size() == 1 && std::isalnum(static_cast<unsigned char>(value[0]))) return static_cast<std::uint32_t>(value[0]);
        if (value.size() >= 2 && value[0] == 'F') {
            try { const auto n = std::stoi(value.substr(1)); if (n >= 1 && n <= 12) return VK_F1 + n - 1; } catch (...) {}
        }
        return fallback;
    };
    m_interactKey = key(general.value("interact_hotkey", std::string("F8")), VK_F8);
    m_settingsKey = key(general.value("config_hotkey", std::string("N")), 'N');
    m_inputKey = key(general.value("input_hotkey", std::string("Enter")), VK_RETURN);
    m_toggleKey = key(general.value("toggle_hotkey", std::string()), 0);  // 0 = no key
    // Remembered hidden state is restored from the settings file; otherwise
    // the current session's state is left as it is.
    m_rememberHidden = general.value("remember_hidden", true);
    if (m_rememberHidden) m_state.userHidden = general.value("user_hidden", false);
}

void Chronicle::ToggleUserHidden()
{
    m_state.userHidden = !m_state.userHidden;
    spdlog::info("Chronicle: {} with the show/hide hotkey", m_state.userHidden ? "hidden" : "shown");
    UpdateVisibility();
    // The UI saves the state when "Remember hidden state" is on.
    InvokeUI(m_state.userHidden ? "setUserHidden(true)" : "setUserHidden(false)");
}

bool Chronicle::OnHotkey(std::uint32_t scanCode, bool shift)
{
    // PrismaUI routes keys to JS once focused. Native keys acquire the focus
    // in the first place; otherwise a passive overlay cannot receive a hotkey.
    const auto key = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    // The show/hide key must work while Chronicle is hidden by it, but never
    // while typing (any focused view), in menus or in Skyrim's console.
    const bool canToggle = m_bootstrapped && m_gameLoaded && m_prismaUI &&
        !m_prismaUI->HasAnyActiveFocus() && m_state.menus.empty();
    if (canToggle && m_toggleKey && key == m_toggleKey && !shift) {
        MainTask([this] { ToggleUserHidden(); });
        return true;
    }
    // The settings hotkey also brings a hidden Chronicle back (and opens
    // settings), so a forgotten show/hide key cannot leave it lost. Tasks run
    // in order: the view is visible again before the settings request.
    if (canToggle && m_state.userHidden && shift && key == m_settingsKey) {
        m_keyboardCapture = true;
        MainTask([this] { if (m_state.userHidden) ToggleUserHidden(); });
        MainTask([this] { if (!m_hidden && m_gameLoaded) InvokeUI("nativeHotkey(\"settings\")"); });
        return true;
    }
    if (!m_bootstrapped || m_hidden || !m_gameLoaded || !m_prismaUI) return false;
    if (m_prismaUI->HasAnyActiveFocus()) {
        if (!m_prismaUI->HasFocus(m_view)) return false;
        // Release natively too: if the view never receives keys, the close
        // hotkey/Escape must still return control to Skyrim. Letter/Enter
        // hotkeys are left to JS so typing in the input bar cannot close it.
        const bool functionKey = key >= VK_F1 && key <= VK_F12;
        if (key == VK_ESCAPE || (functionKey && key == m_interactKey)) RequestUnfocus();
        return true;
    }
    if (!m_state.menus.empty()) return false;
    if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen("Console")) return false;
    const std::string action = shift && key == m_settingsKey ? "settings" :
        key == m_interactKey ? "interact" : key == m_inputKey ? "input" : "";
    if (action.empty()) return false;

    // Consume the opener immediately. Focus() is asynchronous, so without
    // this pending capture the same Enter/F8 key can reach Skyrim before the
    // view owns the keyboard, and subsequent typing can trigger game menus.
    m_keyboardCapture = true;
    MainTask([this, action] {
        if (!m_hidden && m_gameLoaded) InvokeUI("nativeHotkey(" + Json(action) + ")");
    });
    return true;
}

void Chronicle::OnSettingsChanged(std::string_view text)
{
    try {
        if (text.size() > 65536) throw std::runtime_error("Settings exceed 64 KB");
        const auto settings = nlohmann::json::parse(text);
        ApplyNativeSettings(settings);
        AtomicWrite(kSettingsPath, settings.dump(2));
        UpdateVisibility();
        InvokeUI("settingsSaveResult(true, 'settings.saved')");
    } catch (const std::exception& e) {
        spdlog::error("Chronicle: settings save failed: {}", e.what());
        InvokeUI("settingsSaveResult(false, 'settings.notSaved', " + Json(e.what()) + ")");
    }
}

void Chronicle::ExportHistory(std::string_view text)
{
    try {
        if (text.size() > 4 * 1024 * 1024) throw std::runtime_error("Export exceeds 4 MB");
        const auto base = ChronicleLogDirectory().value_or(kSettingsPath.parent_path());
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto path = base / "Chronicle-exports" / ("history-" + std::to_string(stamp) + ".txt");
        AtomicWrite(path, ChronicleUtf8::ReplaceInvalid(text).value);
        InvokeUI("saveResult(true, 'export.done', " + Json(path.string()) + ")");
    } catch (const std::exception& e) {
        InvokeUI("saveResult(false, 'export.failed', " + Json(e.what()) + ")");
    }
}

void Chronicle::Diagnostics()
{
    InvokeUI("diagnosticsResult(" + Json({
        {"version", CHRONICLE_VERSION}, {"bridge", m_bootstrapped}, {"gameLoaded", m_gameLoaded.load()},
        {"visible", !m_hidden}, {"focus", m_prismaUI && m_prismaUI->HasFocus(m_view)},
        {"poller", SubtitlePoller::GetSingleton().IsRunning()}, {"messages", m_messageCount},
        {"lastSubtitle", m_lastSubtitle}, {"api", m_hasV2 ? 2 : 1},
        {"hudPath", SubtitlePoller::GetSingleton().LastHudPath()},
        {"settingsFile", kSettingsPath.string()}
    }) + ")");
}

void Chronicle::FlushPendingUI()
{
    if (!m_bootstrapped) return;
    std::deque<std::string> pending;
    {
        std::lock_guard lock(m_uiMutex);
        pending.swap(m_pendingUI);
    }
    for (const auto& script : pending) InvokeUI(script);
}

bool Chronicle::IsDuplicate(std::string_view channel, std::string_view speaker, std::string_view message)
{
    std::lock_guard lock(m_dedupMutex);
    const auto now = std::chrono::steady_clock::now();
    if (m_lastMessage && now - m_lastMessage->time < std::chrono::milliseconds(500) &&
        m_lastMessage->channel == channel && m_lastMessage->speaker == speaker && m_lastMessage->message == message) return true;
    m_lastMessage = LastMessage{std::string(channel), std::string(speaker), std::string(message), now};
    return false;
}

void Chronicle::InvokeUI(std::string_view script)
{
    if (m_prismaUI && m_view && m_viewReady) {
        const std::string terminated(script);
        m_prismaUI->Invoke(m_view, terminated.c_str());
    }
}
