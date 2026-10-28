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

// PrismaUI backend: the page lives in Data/PrismaUI/views/Chronicle and the
// view is addressed by its PrismaUI handle.
namespace
{
    class PrismaHost final : public IUiHost
    {
    public:
        bool CreateView(DomReadyFn onDomReady) override
        {
            s_onDomReady = onDomReady;
            m_v2 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
            m_api = m_v2 ? m_v2 : PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
            if (!m_api) {
                spdlog::error("Chronicle: PrismaUI unavailable");
                return false;
            }
            s_view = m_api->CreateView("Chronicle/index.html", [](PrismaView view) {
                s_view = view;
                if (s_onDomReady) s_onDomReady();
            });
            if (s_view) m_api->Hide(s_view);
            spdlog::info("Chronicle: PrismaUI view {} created, API v{}", s_view.load(), ApiVersion());
            return true;
        }

        bool Available() const override { return m_api != nullptr; }
        bool ViewValid() const override { return m_api && s_view != 0; }

        void RegisterListener(const char* name, Listener fn) override
        {
            if (!ViewValid()) return;
            m_api->RegisterJSListener(s_view, name, fn);
            if (m_v2 && !m_consoleHooked) {
                m_consoleHooked = true;
                // This is WEB console output, not Skyrim console-command output.
                m_v2->RegisterConsoleCallback(s_view,
                    [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel, const char* text) {
                        if (text) spdlog::debug("Chronicle web: {}", text);
                    });
            }
        }

        void Invoke(const char* script) override
        {
            if (ViewValid()) m_api->Invoke(s_view, script);
        }

        void Show(bool visible) override
        {
            if (!ViewValid()) return;
            if (visible) m_api->Show(s_view);
            else m_api->Hide(s_view);
        }

        bool HasFocus() override { return ViewValid() && m_api->HasFocus(s_view); }
        bool AnyFocus() override { return m_api && m_api->HasAnyActiveFocus(); }
        bool Focus() override { return ViewValid() && m_api->Focus(s_view, false, false); }
        void Unfocus() override
        {
            if (ViewValid() && m_api->HasFocus(s_view)) m_api->Unfocus(s_view);
        }

        std::string FirstRunDefaults() override { return {}; }  // the page fetches config.json

        const char* Name() const override { return "PrismaUI"; }
        int ApiVersion() const override { return m_v2 ? 2 : 1; }
        std::uint64_t ViewHandle() const override { return s_view; }

    private:
        PRISMA_UI_API::IVPrismaUI1* m_api{ nullptr };
        PRISMA_UI_API::IVPrismaUI2* m_v2{ nullptr };  // real V2 only, never a V1 cast
        bool m_consoleHooked{ false };
        // Captureless PrismaUI callbacks reach these statics.
        static inline std::atomic<PrismaView> s_view{ 0 };
        static inline DomReadyFn s_onDomReady{ nullptr };
    };
}

IUiHost& UiHost()
{
    static PrismaHost host;
    return host;
}
