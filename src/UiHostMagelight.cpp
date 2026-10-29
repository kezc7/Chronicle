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

#include "PCH.h"
#include "UiHost.h"

// CommonLib already brought in <Windows.h>; the header's own loader would
// include it again in a different configuration.
#define MAGELIGHT_API_NO_LOADER
#include "MagelightUI_API.h"

#include <deque>
#include <filesystem>
#include <fstream>

// Magelight UI backend (beta). The page lives in Data/Magelight/Chronicle/views
// and Chronicle registers itself as the "Chronicle" mod. Focus is Magelight's
// "UI mode": one view per process owns it, entry is queued and confirmed by
// an event, and the host leaves it on Escape or its own toggle key. Chronicle's
// focus watchdog (Tick) sees every exit through HasFocus().
//
// Magelight keeps an engine menu open while any interactive view is visible,
// and the game reads that as a menu over gameplay (the Dialogue Menu stops
// taking input, QuickLoot and the Journal stay shut). A view cannot switch
// between click-through and interactive, so the page runs twice: a
// click-through HUD copy that shows the log, and a hidden panel copy that is
// shown, and takes UI mode, only while the player interacts. Both receive every
// script; only the panel saves. When an interaction ends the panel's state is
// handed to the HUD copy before the HUD is shown again.
namespace
{
    using namespace MAGELIGHT_API;

    constexpr std::uint32_t kMinHost = PackVersion(0, 30, 1);      // PostGameTask
    constexpr std::uint32_t kCancelHost = PackVersion(1, 1, 0);    // CancelUIModeRequest
    const std::filesystem::path kViewDir = L"Data/Magelight/Chronicle/views";

    const MagelightApi4* RequestApi4()
    {
        const HMODULE module = GetModuleHandleW(L"Magelight.dll");
        if (!module) return nullptr;
        using RequestFn = void* (*)(std::uint32_t);
        const auto request = reinterpret_cast<RequestFn>(GetProcAddress(module, "Magelight_RequestApi"));
        return request ? static_cast<const MagelightApi4*>(request(kApiVersion4)) : nullptr;
    }

    std::uint32_t ParseVersion(const char* text)
    {
        unsigned major = 0, minor = 0, patch = 0;
        if (sscanf_s(text, "%u.%u.%u", &major, &minor, &patch) < 2) return 0;
        return PackVersion(major, minor, patch);
    }

    const char* ResultName(Result result)
    {
        switch (result) {
        case Result::Ok: return "Ok";
        case Result::HostAbsent: return "HostAbsent";
        case Result::HostTooOld: return "HostTooOld";
        case Result::NotReady: return "NotReady";
        case Result::RenderDead: return "RenderDead";
        case Result::InvalidView: return "InvalidView";
        case Result::InvalidMod: return "InvalidMod";
        case Result::FileNotFound: return "FileNotFound";
        case Result::Busy: return "Busy";
        case Result::Denied: return "Denied";
        case Result::Unsupported: return "Unsupported";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::Internal: return "Internal";
        }
        return "Unknown";
    }

    // A page->native call, copied off the render thread and run on the game thread.
    struct ListenerCall
    {
        IUiHost::Listener fn;
        std::string text;
    };

    class MagelightHost final : public IUiHost
    {
    public:
        static MagelightHost& Self()
        {
            static MagelightHost host;
            return host;
        }

        bool CreateView(DomReadyFn onDomReady) override
        {
            m_onDomReady = onDomReady;
            m_api = RequestApi4();
            if (!m_api) {
                spdlog::error("Chronicle: Magelight UI unavailable (Magelight.dll not loaded, or it serves no API v4)");
                return false;
            }
            spdlog::info("Chronicle: Magelight host {} ({})", m_api->hostVersion, m_api->hostVersionNumber);
            if (m_api->hostVersionNumber < kMinHost) {
                spdlog::error("Chronicle: Magelight UI {} is too old; 0.30.1 or newer is required", m_api->hostVersion);
                m_api = nullptr;
                return false;
            }

            ModDesc mod{};
            mod.size = sizeof(ModDesc);
            mod.modId = "Chronicle";
            mod.displayName = "Chronicle";
            mod.modVersion = ParseVersion(CHRONICLE_VERSION);
            mod.minHostVersion = kMinHost;
            mod.callbackThread = CallbackThread::GameThread;
            mod.onEvent = &OnEvent;
            mod.onLog = &OnLog;
            mod.user = this;
            mod.sessionName = nullptr;  // own storage jar: history/preferences stay Chronicle's
            if (const auto result = m_api->RegisterMod(&mod, &m_mod); result != Result::Ok || !m_mod) {
                spdlog::error("Chronicle: Magelight RegisterMod failed: {} {}", ResultName(result), LastError());
                m_mod = 0;
                m_api = nullptr;
                return false;
            }

            // Absolute path: the host resolves pages from its own runtime
            // directory otherwise. The game runs with Skyrim's root as the
            // working directory (the settings file relies on this too).
            std::error_code ec;
            const auto page = std::filesystem::absolute(kViewDir / L"index.html", ec);
            const auto pageText = page.string();
            // Hud/click-through views are refused UI mode; the panel never
            // shows unless it holds or is asking for it.
            const ViewId panel = Create(pageText, "panel", Layer::Panel, false);
            const ViewId hud = panel ? Create(pageText, "hud", Layer::Hud, true) : 0;
            if (!panel || !hud) {
                if (panel) m_api->DestroyView(panel);
                return false;
            }
            m_panel = panel;
            m_hud = hud;
            return true;
        }

        bool Available() const override { return m_api != nullptr; }
        bool ViewValid() const override { return m_api && m_panel != 0 && m_hud != 0 && !m_renderDead; }

        void RegisterListener(const char* name, Listener fn) override
        {
            if (!ViewValid()) return;
            m_listeners.push_back(fn);  // deque: stable address for the host's user pointer
            for (const ViewId view : { m_panel.load(), m_hud.load() }) {
                if (const auto result = m_api->RegisterJSListenerEx(view, name, &OnListener, &m_listeners.back());
                    result != Result::Ok) {
                    spdlog::error("Chronicle: Magelight listener '{}' not registered on view {}: {} {}",
                        name, view, ResultName(result), LastError());
                }
            }
        }

        // Both copies run every script, so the HUD keeps the live log while
        // the panel is up and the panel keeps saving while it is hidden.
        void Invoke(const char* script) override
        {
            if (!ViewValid()) return;
            m_api->InvokeJS(m_panel, script);
            m_api->InvokeJS(m_hud, script);
        }

        void Show(bool visible) override
        {
            m_visible = visible;
            ApplyShown();
        }

        bool HasFocus() override { return ViewValid() && m_api->GetUIModeOwner() == m_mod; }
        bool AnyFocus() override { return m_api && m_api->IsUIModeActive(); }

        bool Focus() override
        {
            if (!ViewValid()) return false;
            ++m_handover;  // an export still in flight must not swap the HUD back over the panel
            m_panelActive = true;
            ApplyShown();
            const auto result = m_api->RequestUIMode(m_panel, kUIModeFlagNone);
            if (result != Result::Ok) {
                spdlog::info("Chronicle: Magelight UI mode request -> {} {}", ResultName(result), LastError());
                ReturnToHud();
            }
            return result == Result::Ok;
        }

        void Unfocus() override
        {
            if (!ViewValid()) return;
            // A request still queued is withdrawn first; a release alone would
            // let the pending entry run after it.
            if (m_api->hostVersionNumber >= kCancelHost) m_api->CancelUIModeRequest(m_mod);
            if (m_api->GetUIModeOwner() == m_mod) m_api->ReleaseUIMode(m_mod);  // UIModeExited swaps back
            else ReturnToHud();
        }

        // Magelight's Content-Security-Policy stops the page from fetching its
        // own config.json; the native side hands the bundled defaults over.
        std::string FirstRunDefaults() override
        {
            try {
                const auto path = kViewDir / L"config.json";
                if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 65536) return {};
                std::ifstream input(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            } catch (...) {
                return {};
            }
        }

        const char* Name() const override { return "Magelight"; }
        const char* FocusMenu() const override { return "MagelightFocus"; }
        int ApiVersion() const override { return static_cast<int>(kApiVersion4); }
        std::uint64_t ViewHandle() const override { return m_panel; }

    private:
        MagelightHost() = default;

        // The game thread's handover generation, carried through EvalJS.
        struct Handover
        {
            MagelightHost* self;
            std::uint64_t generation;
        };

        ViewId Create(const std::string& page, const char* name, Layer layer, bool clickThrough)
        {
            ViewDesc view{};
            view.size = sizeof(ViewDesc);
            view.name = name;
            view.htmlPath = page.c_str();
            view.anchor = Anchor::TopLeft;
            view.fullscreen = true;
            view.clickThrough = clickThrough;
            view.startVisible = false;
            view.layer = layer;
            view.uiScale = 0.0F;
            view.onDomReady = nullptr;      // ViewDomReady arrives on the game thread instead
            ViewId created = 0;
            if (const auto result = m_api->CreateViewEx(m_mod, &view, &created); result != Result::Ok || !created) {
                spdlog::error("Chronicle: Magelight CreateViewEx failed for {} '{}': {} {}",
                    name, page, ResultName(result), LastError());
                return 0;
            }
            spdlog::info("Chronicle: Magelight {} view {} created from '{}'", name, created, page);
            return created;
        }

        // One copy is on screen at a time: the panel while it holds or asks
        // for UI mode, the HUD otherwise.
        void ApplyShown()
        {
            if (!ViewValid()) return;
            const bool panel = m_panelActive;
            m_api->ShowView(panel ? m_panel : m_hud, m_visible);
            m_api->ShowView(panel ? m_hud : m_panel, false);
        }

        // Game thread. Closing the panel's interaction first drops a settings
        // preview the player did not save, as Chronicle's own close does.
        void ReturnToHud()
        {
            if (!m_panelActive || !ViewValid()) return;
            auto* handover = new Handover{ this, ++m_handover };
            const auto result = m_api->EvalJS(m_panel, "closeInteraction(false); exportViewState()", &OnExported, handover);
            if (result != Result::Ok) {
                spdlog::warn("Chronicle: Magelight state handover failed: {} {}", ResultName(result), LastError());
                delete handover;
                m_panelActive = false;
                ApplyShown();
            }
        }

        // Callback thread (the game thread).
        static void OnExported(ViewId, const char* result, const char* exception, void* user)
        {
            std::unique_ptr<Handover> handover(static_cast<Handover*>(user));
            auto* self = handover ? handover->self : nullptr;
            if (!self || handover->generation != self->m_handover || !self->ViewValid()) return;
            if (exception && *exception) {
                spdlog::warn("Chronicle: Magelight panel state unreadable: {}", exception);
            } else if (result) {
                // Only well-formed JSON reaches the HUD page, re-serialised.
                const auto state = nlohmann::json::parse(result, nullptr, false);
                if (state.is_object()) {
                    const auto script = "importViewState(" + state.dump(-1, ' ', false,
                        nlohmann::json::error_handler_t::replace) + ")";
                    self->m_api->InvokeJS(self->m_hud, script.c_str());
                }
            }
            self->m_panelActive = false;
            self->ApplyShown();
        }

        std::string LastError() const
        {
            const char* text = m_api && m_mod ? m_api->GetLastErrorMessage(m_mod) : nullptr;
            return text ? text : "";
        }

        // Render thread, mid-frame: copy the argument and leave at once.
        static void OnListener(ViewId, const char* argument, void* user)
        {
            auto& self = Self();
            if (!self.m_api || !user) return;
            auto* call = new ListenerCall{ *static_cast<Listener*>(user), argument ? argument : "" };
            if (self.m_api->PostGameTask(&RunListener, call) != Result::Ok) delete call;
        }

        static void RunListener(void* user)
        {
            std::unique_ptr<ListenerCall> call(static_cast<ListenerCall*>(user));
            try { call->fn(call->text.c_str()); }
            catch (const std::exception& e) { spdlog::error("Chronicle: listener failed: {}", e.what()); }
            catch (...) { spdlog::error("Chronicle: listener failed"); }
        }

        // Game thread (ModDesc.callbackThread).
        static void OnEvent(const EventData* ev, void* user)
        {
            auto* self = static_cast<MagelightHost*>(user);
            if (!ev || !self) return;
            const std::string detail = ev->detail ? ev->detail : "";
            switch (ev->type) {
            case Event::ViewDomReady:
                if (ev->view == self->m_hud) {
                    // Before any listener exists, so before the handshake.
                    self->m_api->InvokeJS(self->m_hud, "setHostRole('hud')");
                    self->m_hudReady = true;
                } else if (ev->view == self->m_panel) {
                    self->m_api->InvokeJS(self->m_panel, "setHostRole('panel')");
                    self->m_panelReady = true;
                } else {
                    return;
                }
                spdlog::info("Chronicle: Magelight {} page ready", ev->view == self->m_hud ? "hud" : "panel");
                // Both copies must hear the native bootstrap.
                if (self->m_hudReady && self->m_panelReady && self->m_onDomReady) {
                    const auto ready = self->m_onDomReady;
                    self->m_onDomReady = nullptr;
                    ready();
                }
                break;
            case Event::ViewLoadFailed:
                spdlog::error("Chronicle: Magelight page failed to load: {}", detail);
                break;
            case Event::ViewReloaded:
                // Not expected: Chronicle never reloads, hibernates or rescales its views.
                spdlog::warn("Chronicle: Magelight page reloaded; the native handshake is not repeated");
                break;
            case Event::UIModeEntered:
                spdlog::info("Chronicle: Magelight UI mode entered {}", detail);
                break;
            case Event::UIModeExited:
                spdlog::info("Chronicle: Magelight UI mode exited");
                self->ReturnToHud();
                break;
            case Event::UIModeRefused:
                spdlog::info("Chronicle: Magelight UI mode refused: {}", detail);
                self->ReturnToHud();
                break;
            case Event::FocusDenied:
                spdlog::info("Chronicle: '{}' asked for UI mode while Chronicle holds it", detail);
                break;
            case Event::ConsoleMessage:
                // WEB console output, not Skyrim console-command output.
                spdlog::debug("Chronicle web: {}", detail);
                break;
            case Event::RenderDead:
                spdlog::error("Chronicle: Magelight overlay disabled itself after a render fault");
                self->m_renderDead = true;
                break;
            default:
                break;
            }
        }

        static void OnLog(int level, const char* message, void*)
        {
            const auto text = message ? message : "";
            if (level >= 2) spdlog::error("Chronicle: Magelight: {}", text);
            else if (level == 1) spdlog::warn("Chronicle: Magelight: {}", text);
            else spdlog::info("Chronicle: Magelight: {}", text);
        }

        const MagelightApi4* m_api{ nullptr };
        ModId m_mod{ 0 };
        std::atomic<ViewId> m_panel{ 0 };
        std::atomic<ViewId> m_hud{ 0 };
        std::atomic<bool> m_renderDead{ false };
        DomReadyFn m_onDomReady{ nullptr };
        std::deque<Listener> m_listeners;
        // Game thread only.
        bool m_hudReady{ false };
        bool m_panelReady{ false };
        bool m_visible{ false };
        bool m_panelActive{ false };
        std::uint64_t m_handover{ 0 };
    };
}

IUiHost& UiHost()
{
    return MagelightHost::Self();
}
