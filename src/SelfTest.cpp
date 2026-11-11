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
#include "SelfTest.h"
#include "Api.h"
#include "Chronicle.h"
#include "DovahHubModule.h"
#include "DovahHub_API.h"
#include "DovahMenuModule.h"
#include "GameThread.h"
#include "HistoryWriter.h"
#include "SelfTestPolicy.h"
#include "UiHost.h"
#include "api/Chronicle_API.h"

namespace
{
    using ChronicleSelfTest::Entry;
    using ChronicleSelfTest::Outcome;
    using ChronicleSelfTest::Result;
    using ChronicleSelfTest::Step;

    std::int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Filled on whichever thread the pump runs the probe on.
    struct PumpProbe
    {
        std::atomic<bool> ran{ false };
        std::atomic<bool> onGameThread{ false };
        std::atomic<std::uint32_t> thread{ 0 };
    };

    struct PageReply
    {
        std::uint64_t token{ 0 };
        std::string profile;
        bool loaded{ false };
        std::size_t lines{ 0 };
        std::string search;
        std::string speaker;
        bool padApi{ false };          // the page has navigator.getGamepads
        std::size_t padsConnected{ 0 };
        std::string padId;
    };

    // One run at a time, owned by the game thread.
    struct Run
    {
        bool fromHub{ false };
        std::uint64_t world{ 0 };
        std::unique_ptr<ChronicleSelfTest::Runner> runner;
        std::vector<std::string> failures;

        std::shared_ptr<PumpProbe> probe;
        bool journalSaved{ false };
        bool journalRemoved{ false };
        std::vector<ChronicleHistory::Line> written;
        std::uint64_t pageToken{ 0 };
        std::optional<PageReply> page;
    };

    std::unique_ptr<Run> g_run;
    std::uint64_t g_nextToken = 0;

    // ---- commands ---------------------------------------------------------------------------------

    constexpr const char* kProbePrefix = "chronicle.selftest-probe";

    struct ProbeCalls
    {
        int calls{ 0 };
        std::string prefix;
        std::string args;
    } g_probeCalls;

    bool ProbeCommand(const char* prefix, const char* args, void*)
    {
        ++g_probeCalls.calls;
        g_probeCalls.prefix = prefix ? prefix : "";
        g_probeCalls.args = args ? args : "";
        return true;
    }

    bool Listed(std::string_view prefix)
    {
        const auto prefixes = ChronicleApi::CommandPrefixes();
        return std::find(prefixes.begin(), prefixes.end(), prefix) != prefixes.end();
    }

    // Registers a throwaway command the way another mod would (through the
    // exported API table), dispatches typed text to it and unregisters it.
    Outcome CommandsDispatch(std::int64_t)
    {
        constexpr const char* step = "commands.dispatch";
        namespace Api = CHRONICLE_API;
        using ChronicleApi::Dispatched;
        const auto* api = Api::RequestApi();
        if (!api) return Outcome::Done(step, Result::Fail, "Chronicle_RequestApi(1) answered nothing, as another mod would see it");

        g_probeCalls = {};
        Api::CommandDesc desc{};
        desc.size = sizeof(desc);
        desc.prefix = kProbePrefix;
        desc.help = "self-test probe";
        desc.onCommand = &ProbeCommand;
        Api::CommandId id = 0;
        if (const auto result = api->RegisterCommand(&desc, &id); result != Api::Result::Ok || !id) {
            return Outcome::Done(step, Result::Fail, "registering /" + std::string(kProbePrefix) + " answered " +
                std::to_string(static_cast<int>(result)));
        }
        std::string failure;
        const auto expect = [&failure](bool ok, std::string what) {
            if (!ok && failure.empty()) failure = std::move(what);
        };
        expect(Listed(kProbePrefix), "the registered command is not listed");
        expect(ChronicleApi::DispatchQuiet("  /Chronicle.SelfTest-Probe   ping pong  ") == Dispatched::Handled,
            "'/Chronicle.SelfTest-Probe ping pong' was not dispatched");
        expect(g_probeCalls.calls == 1 && g_probeCalls.prefix == kProbePrefix && g_probeCalls.args == "ping pong",
            "the handler got '" + g_probeCalls.prefix + "' / '" + g_probeCalls.args + "' (" + std::to_string(g_probeCalls.calls) + " call(s))");
        expect(ChronicleApi::DispatchQuiet("hello /chronicle.selftest-probe") == Dispatched::NotCommand && g_probeCalls.calls == 1,
            "plain text was taken as a command");
        expect(ChronicleApi::DispatchQuiet("/chronicle.no-such-command") == Dispatched::Unhandled, "an unknown command was handled");
        expect(ChronicleApi::DispatchQuiet("/help") == Dispatched::BuiltIn && ChronicleApi::DispatchQuiet("/selftest") == Dispatched::BuiltIn,
            "/help or /selftest is not a built-in");
        Api::CommandId spare = 0;
        expect(api->RegisterCommand(&desc, &spare) == Api::Result::Duplicate, "the same prefix registered twice");
        for (const char* reserved : { "help", "selftest" }) {
            desc.prefix = reserved;
            expect(api->RegisterCommand(&desc, &spare) == Api::Result::Duplicate, std::string("/") + reserved + " could be taken by a mod");
        }
        desc.prefix = "Not A Slug!";
        expect(api->RegisterCommand(&desc, &spare) == Api::Result::InvalidArgument, "a prefix that isn't a slug was accepted");
        const auto removed = api->UnregisterCommand(id);
        expect(removed == Api::Result::Ok && !Listed(kProbePrefix), "unregistering left the command listed");
        expect(ChronicleApi::DispatchQuiet("/chronicle.selftest-probe") == Dispatched::Unhandled && g_probeCalls.calls == 1,
            "the command still ran after unregistering");
        if (removed != Api::Result::Ok) api->UnregisterCommand(id);
        if (!failure.empty()) return Outcome::Done(step, Result::Fail, failure);
        return Outcome::Done(step, Result::Pass, "registered through Chronicle_RequestApi; typed text parsed and dispatched with its "
            "arguments; plain text, unknown and built-in commands not dispatched; reserved and duplicate prefixes refused; unregistered");
    }

    // Other mods' commands: listed with a handler. They are not called: the
    // command API has no dry run, and a real call could change the game.
    Outcome CommandsMods(std::int64_t)
    {
        constexpr const char* step = "commands.mods";
        std::vector<std::string> others;
        for (auto& prefix : ChronicleApi::CommandPrefixes()) {
            if (prefix != "chronicle.echo" && prefix != kProbePrefix) others.push_back(std::move(prefix));
        }
        if (others.empty()) return Outcome::Done(step, Result::Skip, "no other mod has registered a command");
        Outcome outcome;
        std::string list;
        std::string selfTests;
        for (const auto& prefix : others) {
            list += (list.empty() ? "/" : ", /") + prefix;
            if (prefix.ends_with(".selftest")) selfTests += (selfTests.empty() ? "/" : ", /") + prefix;
        }
        outcome.entries.push_back({ step, Result::Pass, std::to_string(others.size()) + " command(s): " + list +
            (selfTests.empty() ? std::string{} : "; self-tests: " + selfTests) });
        for (const auto& prefix : others) {
            outcome.entries.push_back({ "commands.mod." + prefix, Result::Pass, "registered with a handler; not called (commands have no dry run)" });
        }
        return outcome;
    }

    // ---- the history file -------------------------------------------------------------------------

    std::vector<ChronicleHistory::Line> JournalLines()
    {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::vector<ChronicleHistory::Line> lines(3);
        lines[0].channel = "system";
        lines[0].speaker = "Chronicle";
        lines[0].message = "Self-test line 1";
        lines[0].timestamp = now;
        lines[0].id = 1;
        lines[0].gameDays = 12.5;
        lines[0].gameHour = 13.25F;
        lines[0].place = "Whiterun";
        // Text from other languages must come back byte for byte.
        lines[1].channel = "dialogue";
        lines[1].speaker = "NPC 2";
        lines[1].message = "Ærlig talt — 日本語 — 한국어 — Żółć ✓";
        lines[1].timestamp = now + 1;
        lines[1].id = 2;
        lines[1].speakerFormId = 0x000A2C94;
        lines[1].speakerBase = "Skyrim.esm|0xA2C8E";
        lines[1].ownerQuest = "Skyrim.esm|0x3372B";
        lines[1].topicKind = "conversation";
        lines[2].channel = "console";
        lines[2].message = "> /help";
        lines[2].timestamp = now + 2;
        lines[2].id = 3;
        lines[2].source = "chronicle";
        return lines;
    }

    std::string Differs(const ChronicleHistory::Line& a, const ChronicleHistory::Line& b)
    {
        if (a.channel != b.channel) return "channel";
        if (a.speaker != b.speaker) return "speaker";
        if (a.message != b.message) return "message";
        if (a.timestamp != b.timestamp) return "timestamp";
        if (a.id != b.id) return "id";
        if (a.source != b.source) return "source";
        if (a.speakerFormId != b.speakerFormId) return "speakerFormId";
        if (a.speakerBase != b.speakerBase) return "speakerBase";
        if (a.ownerQuest != b.ownerQuest) return "ownerQuest";
        if (a.topicKind != b.topicKind) return "topicKind";
        if (a.gameDays != b.gameDays) return "gameDays";
        if (a.gameHour != b.gameHour) return "gameHour";
        if (a.place != b.place) return "place";
        return {};
    }

    std::string ShownPath()
    {
        return HistoryWriter::SelfTestPath().generic_string();
    }

    // ---- the run ----------------------------------------------------------------------------------

    std::vector<Step> Steps(Run& run)
    {
        auto* chronicle = Chronicle::GetSingleton();
        std::vector<Step> steps;

        steps.push_back({ "pump", [&run](std::int64_t) {
            constexpr const char* step = "pump";
            if (!run.probe) {
                run.probe = std::make_shared<PumpProbe>();
                GameThread::Post([probe = run.probe] {
                    probe->thread = GetCurrentThreadId();
                    probe->onGameThread = DovahGameThread::IsGameThread();
                    probe->ran = true;
                });
                return Outcome::Wait();
            }
            if (!run.probe->ran) return Outcome::Wait();
            const auto thread = std::to_string(run.probe->thread.load());
            if (!DovahGameThread::Pumping()) {
                return Outcome::Done(step, Result::Fail, "no frame hook: SKSE's UI tasks ran the work, on thread " + thread);
            }
            const std::string pump = DOVAHHUB_API::HasPump(DOVAHHUB_API::RequestApi()) ? "DovahHub's pump" : "Chronicle's own frame hook";
            if (!run.probe->onGameThread) {
                return Outcome::Done(step, Result::Fail, pump + " ran the work on thread " + thread + ", not the frame thread " +
                    std::to_string(DovahGameThread::Id()));
            }
            return Outcome::Done(step, Result::Pass, pump + " ran the work on the frame thread " + thread);
        } });

        steps.push_back({ "commands.dispatch", &CommandsDispatch });
        steps.push_back({ "commands.mods", &CommandsMods });

        steps.push_back({ "journal.write", [&run](std::int64_t) {
            constexpr const char* step = "journal.write";
            const auto path = HistoryWriter::SelfTestPath();
            if (!run.journalSaved) {
                run.written = JournalLines();
                HistoryWriter::Save(path, run.written);
                run.journalSaved = true;
                return Outcome::Wait();
            }
            if (HistoryWriter::Pending(path)) return Outcome::Wait();
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return Outcome::Done(step, Result::Fail, "the writer finished but " + ShownPath() + " isn't there (see the warning above)");
            return Outcome::Done(step, Result::Pass, std::to_string(run.written.size()) + " lines, " + std::to_string(size) +
                " bytes, written to " + ShownPath() + " by the history writer");
        } });

        steps.push_back({ "journal.read", [&run](std::int64_t) {
            constexpr const char* step = "journal.read";
            const auto read = HistoryWriter::Load(HistoryWriter::SelfTestPath());
            if (read.size() != run.written.size()) {
                return Outcome::Done(step, Result::Fail, "read back " + std::to_string(read.size()) + " of " + std::to_string(run.written.size()) + " lines");
            }
            for (std::size_t i = 0; i < read.size(); ++i) {
                if (const auto field = Differs(read[i], run.written[i]); !field.empty()) {
                    return Outcome::Done(step, Result::Fail, "line " + std::to_string(i + 1) + " came back with a different " + field);
                }
            }
            return Outcome::Done(step, Result::Pass, "read back from disk unchanged, every field, the UTF-8 line byte for byte");
        } });

        steps.push_back({ "journal.remove", [&run](std::int64_t) {
            constexpr const char* step = "journal.remove";
            const auto path = HistoryWriter::SelfTestPath();
            if (!run.journalRemoved) {
                HistoryWriter::Remove(path);
                run.journalRemoved = true;
                return Outcome::Wait();
            }
            if (HistoryWriter::Pending(path)) return Outcome::Wait();
            std::error_code error;
            if (std::filesystem::exists(path, error)) return Outcome::Done(step, Result::Fail, ShownPath() + " is still there");
            return Outcome::Done(step, Result::Pass, "the history writer removed " + ShownPath());
        } });

        steps.push_back({ "ui.view", [chronicle](std::int64_t) {
            constexpr const char* step = "ui.view";
            const std::string host = UiHost().Name();
            if (!UiHost().ViewValid()) return Outcome::Done(step, Result::Fail, "the " + host + " view is not valid");
            if (!chronicle->ListenersReady()) return Outcome::Done(step, Result::Fail, "the page's listeners were never registered");
            if (!chronicle->Bootstrapped()) return Outcome::Done(step, Result::Fail, "the page never finished its handshake (uiReady)");
            return Outcome::Done(step, Result::Pass, host + " view valid, listeners registered, handshake done (host API " +
                std::to_string(UiHost().ApiVersion()) + ")");
        } });

        steps.push_back({ "saveleak.character", [chronicle](std::int64_t) {
            constexpr const char* step = "saveleak.character";
            if (!chronicle->IsGameLoaded()) return Outcome::Done(step, Result::Skip, "no game loaded");
            const auto profile = chronicle->profileId.load();
            if (!profile) return Outcome::Done(step, Result::Fail, "a game is loaded but there is no character id");
            if (chronicle->HistoryProfile() != profile) {
                return Outcome::Done(step, Result::Fail, "the history belongs to character " + std::to_string(chronicle->HistoryProfile()) +
                    ", the loaded character is " + std::to_string(profile));
            }
            const auto& history = chronicle->History();
            return Outcome::Done(step, Result::Pass, "character " + std::to_string(profile) + "; the history is this character's, " +
                std::to_string(history.Size()) + " line(s), last id " + std::to_string(history.LastId()) +
                (chronicle->KeepsHistory() ? ", kept in " + std::to_string(profile) + ".json" : ", Keep history is off"));
        } });

        steps.push_back({ "saveleak.page", [&run, chronicle](std::int64_t) {
            constexpr const char* step = "saveleak.page";
            if (!chronicle->IsGameLoaded()) return Outcome::Done(step, Result::Skip, "no game loaded");
            if (!chronicle->Bootstrapped()) return Outcome::Done(step, Result::Skip, "the page isn't ready (see ui.view)");
            if (!run.pageToken) {
                run.pageToken = ++g_nextToken;
                run.page.reset();
                chronicle->AskPageSelfTest(run.pageToken);
                return Outcome::Wait();
            }
            if (!run.page) return Outcome::Wait();
            const auto expected = std::to_string(chronicle->profileId.load());
            if (run.page->profile != expected) {
                return Outcome::Done(step, Result::Fail, "the chat window shows character '" + run.page->profile + "', the game has " + expected);
            }
            if (!run.page->loaded) return Outcome::Done(step, Result::Fail, "the chat window thinks no game is loaded");
            return Outcome::Done(step, Result::Pass, "the chat window shows character " + expected + " with " + std::to_string(run.page->lines) +
                " line(s); search '" + run.page->search + "', speaker filter '" + run.page->speaker + "'");
        } });

        steps.push_back({ "saveleak.mainmenu", [](std::int64_t) {
            return Outcome::Done("saveleak.mainmenu", Result::Skip,
                "needs a quit to the Main Menu and a load (save-leak in-game check 2)");
        } });

        steps.push_back({ "saveleak.newgame", [](std::int64_t) {
            return Outcome::Done("saveleak.newgame", Result::Skip, "needs a New Game (save-leak in-game check 3)");
        } });

        // The controller (CHRON-17): the combo setting, and how the pad reaches the page.
        // Pressing buttons is left to the player: the step only checks the path is there.
        steps.push_back({ "pad", [&run, chronicle](std::int64_t) {
            constexpr const char* step = "pad";
            const auto combo = "combo " + chronicle->PadComboName();
            const auto pad = UiHost().Gamepad();
            if (pad.path == IUiHost::PadPath::HostKeys) {
                if (!pad.connected) return Outcome::Done(step, Result::Skip, "no controller connected; " + pad.detail + "; " + combo);
                return Outcome::Done(step, Result::Pass, pad.detail + ", a controller is connected; " + combo);
            }
            if (pad.path == IUiHost::PadPath::None) return Outcome::Done(step, Result::Skip, pad.detail + "; " + combo);
            // The page reads the pad itself: saveleak.page asked it.
            if (!run.page) return Outcome::Done(step, Result::Skip, "the chat window didn't answer (see saveleak.page); " + combo);
            if (!run.page->padApi) return Outcome::Done(step, Result::Fail, "the chat window has no gamepad API; " + combo);
            if (!run.page->padsConnected) return Outcome::Done(step, Result::Skip, "no controller connected; " + combo);
            return Outcome::Done(step, Result::Pass, "the chat window sees '" + run.page->padId + "'; " + combo);
        } });

        steps.push_back({ "dovahhub.module", [chronicle](std::int64_t) {
            return Outcome{ false, { DovahHubModule::CheckModule(chronicle->HistoryProfile()) } };
        } });

        steps.push_back({ "dovahmenu.entry", [chronicle](std::int64_t) {
            return Outcome{ false, { DovahMenuModule::CheckEntry(chronicle->LauncherHotkey(), chronicle->IsGameLoaded()) } };
        } });

        return steps;
    }

    void Finish()
    {
        auto run = std::move(g_run);
        if (!run) return;
        const auto& totals = run->runner->Totals();
        spdlog::info("{}", ChronicleSelfTest::DoneLine(totals, run->runner->ElapsedMs()));
        if (run->fromHub) {
            DovahHubModule::FinishSelfTest();
            return;
        }
        auto* chronicle = Chronicle::GetSingleton();
        for (const auto& failure : run->failures) chronicle->SendMessage("console", "", failure);
        chronicle->SendMessage("console", "", chronicle->Text(ChronicleRuntime::Phrase::SelfTestDone, totals.Counts()));
    }

    // Once per frame on the pump until the run is over.
    void Tick()
    {
        if (!g_run) return;
        bool done = false;
        if (Chronicle::GetSingleton()->WorldRevision() != g_run->world) {
            g_run->runner->Abort("a load started during the run", NowMs());
            done = true;
        } else {
            done = g_run->runner->Advance(NowMs());
        }
        if (done) Finish();
        else GameThread::Post([] { Tick(); });
    }

    void Start(bool fromHub, std::string_view origin)
    {
        g_run = std::make_unique<Run>();
        g_run->fromHub = fromHub;
        g_run->world = Chronicle::GetSingleton()->WorldRevision();
        auto* run = g_run.get();
        g_run->runner = std::make_unique<ChronicleSelfTest::Runner>(Steps(*run), [run](const Entry& entry) {
            const auto line = ChronicleSelfTest::Line(entry.step, entry.result, entry.detail);
            spdlog::info("{}", line);
            if (entry.result == Result::Fail) run->failures.push_back(line);
            if (run->fromHub) DovahHubModule::ReportSelfTestStep(entry);
        });
        spdlog::info("Chronicle {}: self-test started from {}", CHRONICLE_VERSION, origin);
        GameThread::Post([] { Tick(); });
    }
}

namespace ChronicleSelfTestRun
{
    void StartFromChat()
    {
        GameThread::Check();
        auto* chronicle = Chronicle::GetSingleton();
        if (g_run) {
            chronicle->SendMessage("console", "", chronicle->Text(ChronicleRuntime::Phrase::SelfTestBusy));
            return;
        }
        chronicle->SendMessage("console", "", chronicle->Text(ChronicleRuntime::Phrase::SelfTestStarted));
        Start(false, "the chat box (/selftest)");
    }

    void StartFromHub(std::string_view runJson)
    {
        GameThread::Check();
        const auto run = nlohmann::json::parse(runJson, nullptr, false);
        std::string runId = "?";
        if (run.is_object() && run.contains("runId") && run["runId"].is_string()) runId = run["runId"].get<std::string>().substr(0, 40);
        if (g_run) {
            const Entry busy{ "start", Result::Fail, "a /selftest run was already going; its lines are in Chronicle.log" };
            spdlog::info("{}", ChronicleSelfTest::Line(busy.step, busy.result, busy.detail));
            DovahHubModule::ReportSelfTestStep(busy);
            DovahHubModule::FinishSelfTest();
            return;
        }
        Start(true, "DovahHub's Diagnostics, run " + ChronicleSelfTest::Detail(runId));
    }

    void OnPageReply(std::string_view json)
    {
        if (!g_run || !g_run->pageToken) return;
        const auto data = nlohmann::json::parse(json, nullptr, false);
        if (!data.is_object()) return;
        const auto text = [&data](const char* key) {
            const auto it = data.find(key);
            return it != data.end() && it->is_string() ? ChronicleUtf8::ReplaceInvalid(it->get<std::string>()).value.substr(0, 100) : std::string{};
        };
        if (text("token") != std::to_string(g_run->pageToken)) return;  // an older run's answer
        PageReply reply;
        reply.token = g_run->pageToken;
        reply.profile = text("profile");
        reply.search = text("search");
        reply.speaker = text("speaker");
        if (const auto it = data.find("loaded"); it != data.end() && it->is_boolean()) reply.loaded = it->get<bool>();
        if (const auto it = data.find("lines"); it != data.end() && it->is_number_unsigned()) reply.lines = it->get<std::size_t>();
        if (const auto pad = data.find("pad"); pad != data.end() && pad->is_object()) {
            if (const auto it = pad->find("api"); it != pad->end() && it->is_boolean()) reply.padApi = it->get<bool>();
            if (const auto it = pad->find("connected"); it != pad->end() && it->is_number_unsigned()) reply.padsConnected = it->get<std::size_t>();
            if (const auto it = pad->find("id"); it != pad->end() && it->is_string()) {
                reply.padId = ChronicleSelfTest::Detail(ChronicleUtf8::ReplaceInvalid(it->get<std::string>()).value.substr(0, 80));
            }
        }
        g_run->page = std::move(reply);
    }
}
