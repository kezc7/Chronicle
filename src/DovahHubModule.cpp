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
#include "SelfTest.h"

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

    // Game thread: DovahHub's Diagnostics > Run self-test reached Chronicle.
    void StartSelfTest(const char* runJson, void*)
    {
        try {
            ChronicleSelfTestRun::StartFromHub(runJson ? runJson : "{}");
        } catch (...) {
            spdlog::error("Chronicle: the self-test did not start");
        }
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
    void Listen()
    {
        SKSE::GetMessagingInterface()->RegisterListener("DovahHub", [](SKSE::MessagingInterface::Message* a_msg) {
            if (!a_msg) return;
            if (a_msg->type == DOVAHHUB_API::kMsgOpened) Chronicle::GetSingleton()->OnDovahHubOpen(true);
            else if (a_msg->type == DOVAHHUB_API::kMsgClosed) Chronicle::GetSingleton()->OnDovahHubOpen(false);
        });
    }

    bool IsOpen()
    {
        return g_hub && g_hub->IsOpen && g_hub->IsOpen();
    }

    void Register()
    {
        g_hub = DOVAHHUB_API::RequestApi();
        if (!g_hub) {
            spdlog::info("Chronicle: DovahHub not found; no Chronicle tab");
            return;
        }
        DOVAHHUB_API::ModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.id = "chronicle";
        desc.tab = "Chronicle";
        desc.group = nullptr;
        desc.pageDir = "modules/chronicle/";
        desc.version = CHRONICLE_VERSION;
        desc.onRequest = &OnRequest;
        desc.onEvent = &OnEvent;
        const auto result = g_hub->RegisterModule(&desc, &g_module);
        if (result != DOVAHHUB_API::Result::Ok) g_module = 0;
        spdlog::info("Chronicle: DovahHub {} on {} answered; Chronicle tab {} (result {})",
            g_hub->hostVersion ? g_hub->hostVersion : "?", g_hub->host ? g_hub->host : "?",
            g_module ? "registered" : "NOT registered", static_cast<int>(result));
        if (g_module && DOVAHHUB_API::HasSelfTest(g_hub)) {
            const auto selfTest = g_hub->RegisterSelfTest(g_module, &StartSelfTest, nullptr);
            spdlog::info("Chronicle: self-test registered with DovahHub's Diagnostics (result {})", static_cast<int>(selfTest));
        }
    }

    void PushLine(const ChronicleHistory::Line& line)
    {
        Push({ { "line", ChronicleHistory::ToJson(line) } });
    }

    void PushReset()
    {
        Push({ { "reset", true } });
    }

    void ReportSelfTestStep(const ChronicleSelfTest::Entry& entry)
    {
        if (!g_module || !DOVAHHUB_API::HasSelfTest(g_hub)) return;
        // NotReady when the run is Chronicle's own (/selftest): nothing is recorded, by design.
        g_hub->ReportSelfTestStep(g_module, ChronicleSelfTest::StepJson(entry.step, entry.result, entry.detail).c_str());
    }

    void FinishSelfTest()
    {
        if (g_module && DOVAHHUB_API::HasSelfTest(g_hub)) g_hub->FinishSelfTest(g_module);
    }

    ChronicleSelfTest::Entry CheckModule(std::uint64_t historyProfile)
    {
        using ChronicleSelfTest::Result;
        constexpr const char* step = "dovahhub.module";
        if (!g_hub) return { step, Result::Skip, "DovahHub is not installed" };
        if (!g_module) return { step, Result::Fail, "DovahHub answered but the Chronicle tab is not registered" };
        const std::string version = g_hub->hostVersion ? g_hub->hostVersion : "?";
        // The same call DovahHub makes when the tab asks for its lines.
        const char* answer = OnRequest("history", R"({"limit":3})", nullptr);
        if (!answer) return { step, Result::Fail, "the tab's history request got no answer" };
        const auto reply = nlohmann::json::parse(answer, nullptr, false);
        if (!reply.is_object() || !reply.contains("lines") || !reply["lines"].is_array() || !reply.contains("profile")) {
            return { step, Result::Fail, "the history answer is not the tab's JSON" };
        }
        const std::string profile = reply["profile"].is_string() ? reply["profile"].get<std::string>() : std::string{};
        const std::string expected = historyProfile ? std::to_string(historyProfile) : std::string{};
        if (profile != expected) {
            return { step, Result::Fail, "the tab got character '" + profile + "', the history is '" + expected + "'" };
        }
        const auto lines = reply["lines"].size();
        if (lines > 3) return { step, Result::Fail, "asked for 3 lines, got " + std::to_string(lines) };
        if (OnRequest("selftest.unknown", "{}", nullptr)) return { step, Result::Fail, "an unknown request got an answer" };
        return { step, Result::Pass, "DovahHub " + version + "; the tab answered with " + std::to_string(lines) +
            " line(s) for character " + (expected.empty() ? std::string("(none loaded)") : expected) +
            (DOVAHHUB_API::HasSelfTest(g_hub) ? "; self-test registered" : "; this DovahHub has no self-test runner") };
    }
}
