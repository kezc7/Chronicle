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
#include "DovahHubModule.h"
#include "Chronicle.h"
#include "DovahHub_API.h"

#include <cstring>

namespace
{
    const DOVAHHUB_API::DovahHubApi1* g_hub = nullptr;
    DOVAHHUB_API::ModuleId g_module = 0;
    std::atomic<bool> g_shown{ false };

    std::string Dump(const nlohmann::json& value)
    {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void Push(const nlohmann::json& value)
    {
        if (!g_hub || !g_module || !g_shown) return;
        // NotReady: the hub page is gone; the next tabShown reloads the list anyway.
        g_hub->PushJson(g_module, Dump(value).c_str());
    }

    // Game thread. {"before": <timestamp>, "limit": <n>} -> {"profile", "lines", "more"}.
    const char* OnRequest(const char* request, const char* json, void*)
    {
        if (!request || std::strcmp(request, "history") != 0) return nullptr;
        thread_local std::string reply;
        const auto payload = nlohmann::json::parse(json ? json : "{}", nullptr, false);
        std::optional<std::int64_t> before;
        std::size_t limit = ChronicleHistory::kDefaultPage;
        if (payload.is_object()) {
            if (const auto it = payload.find("before"); it != payload.end() && it->is_number()) {
                before = it->get<std::int64_t>();
            }
            if (const auto it = payload.find("limit"); it != payload.end() && it->is_number_integer() && it->get<std::int64_t>() > 0) {
                limit = static_cast<std::size_t>(it->get<std::int64_t>());
            }
        }
        reply = Chronicle::GetSingleton()->HistoryPageJson(before, limit);
        return reply.c_str();
    }

    void OnEvent(const char* event, void*)
    {
        if (!event) return;
        if (std::strcmp(event, "tabShown") == 0) g_shown = true;
        else if (std::strcmp(event, "tabHidden") == 0 || std::strcmp(event, "closed") == 0) g_shown = false;
    }
}

namespace DovahHubModule
{
    void Register()
    {
        g_hub = DOVAHHUB_API::RequestApi();
        if (!g_hub) {
            spdlog::info("Chronicle: DovahHub not found; no History tab");
            return;
        }
        DOVAHHUB_API::ModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.id = "chronicle";
        desc.tab = "History";
        desc.group = "Journey";
        desc.pageDir = "modules/chronicle/";
        desc.version = CHRONICLE_VERSION;
        desc.onRequest = &OnRequest;
        desc.onEvent = &OnEvent;
        const auto result = g_hub->RegisterModule(&desc, &g_module);
        if (result != DOVAHHUB_API::Result::Ok) g_module = 0;
        spdlog::info("Chronicle: DovahHub {} on {} answered; History module {} (result {})",
            g_hub->hostVersion ? g_hub->hostVersion : "?", g_hub->host ? g_hub->host : "?",
            g_module ? "registered" : "NOT registered", static_cast<int>(result));
    }

    void PushLine(const ChronicleHistory::Line& line)
    {
        Push({ { "line", ChronicleHistory::ToJson(line) } });
    }

    void PushReset()
    {
        Push({ { "reset", true } });
    }
}
