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

#include "SelfTestPolicy.h"
#include "RuntimePolicy.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void Require(bool value, const char* what)
    {
        if (!value) {
            std::cerr << "FAILED: " << what << "\n";
            ++g_failures;
        }
    }

    using namespace ChronicleSelfTest;

    void StepList()
    {
        std::set<std::string_view> seen;
        for (const auto step : kSteps) {
            Require(ValidStep(step), "every step name is one DovahHub accepts");
            Require(seen.insert(step).second, "step names are unique");
        }
        Require(kSteps.front() == "pump", "the pump is checked first: every other step runs on it");
        const auto at = [](std::string_view name) {
            return static_cast<std::size_t>(std::find(kSteps.begin(), kSteps.end(), name) - kSteps.begin());
        };
        Require(at("journal.write") < at("journal.read") && at("journal.read") < at("journal.remove"), "the history file is written, read, removed");
        Require(at("commands.dispatch") < at("commands.mods"), "Chronicle's own dispatch comes before the other mods' list");
        for (const auto name : { "saveleak.character", "saveleak.page", "saveleak.mainmenu", "saveleak.newgame",
                 "dovahhub.module", "dovahmenu.entry", "ui.view", "pad", "saveleak.lineage", "threads" }) {
            Require(at(name) < kSteps.size(), "the save-leak, module and view checks are in the list");
        }
        // "commands.mod." + a 32-letter prefix still fits.
        Require(ValidStep("commands.mod." + std::string(32, 'a')), "a per-command step fits in 48");
    }

    void StepNames()
    {
        Require(!ValidStep(""), "an empty step is refused");
        Require(!ValidStep(std::string(49, 'a')), "49 letters are refused");
        Require(ValidStep(std::string(48, 'a')), "48 letters are fine");
        Require(!ValidStep("two words") && !ValidStep("a/b") && !ValidStep("ü"), "spaces, slashes and non-ASCII are refused");
        Require(StepName("bad name/é") == "bad_name___", "a bad name is made safe byte by byte");
        Require(StepName("") == "step", "an empty name gets a placeholder");
        Require(StepName(std::string(60, 'x')).size() == kMaxStep, "a long name is cut to 48");
    }

    void Lines()
    {
        Require(Line("pump", Result::Pass, "ok") == "SELFTEST chronicle pump PASS ok", "a pass line");
        Require(Line("journal.read", Result::Fail, "") == "SELFTEST chronicle journal.read FAIL", "no trailing space without a detail");
        Require(Line("x", Result::Skip, "  two\nlines\r\tand tab  ") == "SELFTEST chronicle x SKIP two lines  and tab", "a detail is one line");
        // 199 ASCII bytes and then a two-byte letter: the cut keeps whole characters.
        const auto detail = Detail(std::string(199, 'a') + "é" + "tail");
        Require(detail.size() == 199, "a long detail is cut before a split character");
        Require(Detail(std::string(300, 'b')).size() == kMaxDetail, "a long detail is cut to 200");
        Require(Detail("bad \xFF byte") == "bad \xEF\xBF\xBD byte", "invalid UTF-8 is replaced");
        Require(Word(Result::Pass) == "PASS" && Word(Result::Fail) == "FAIL" && Word(Result::Skip) == "SKIP", "the log words");

        const auto json = nlohmann::json::parse(StepJson("dovahhub.module", Result::Skip, "DovahHub is not installed"));
        Require(json["step"] == "dovahhub.module" && json["result"] == "skip" && json["detail"] == "DovahHub is not installed",
            "the ReportSelfTestStep JSON");
        Require(nlohmann::json::parse(StepJson("a", Result::Pass, ""))["result"] == "pass", "pass in DovahHub's spelling");
        Require(nlohmann::json::parse(StepJson("a", Result::Fail, "line\nbreak"))["detail"] == "line break", "the JSON detail is one line too");

        Tally tally;
        tally.Add(Result::Pass);
        tally.Add(Result::Pass);
        tally.Add(Result::Fail);
        tally.Add(Result::Skip);
        Require(DoneLine(tally, 1234) == "SELFTEST chronicle DONE pass=2 fail=1 skip=1 ms=1234", "the DONE line");
        Require(DoneLine(Tally{}, -5) == "SELFTEST chronicle DONE pass=0 fail=0 skip=0 ms=0", "no negative time");
    }

    void RunnerWalk()
    {
        std::vector<Entry> got;
        int waits = 0;
        std::vector<Step> steps;
        steps.push_back({ "first", [](std::int64_t) { return Outcome::Done("first", Result::Pass, "fine"); } });
        steps.push_back({ "waits", [&waits](std::int64_t elapsed) {
            ++waits;
            if (elapsed < 50) return Outcome::Wait();
            return Outcome::Done("waits", Result::Pass, "after " + std::to_string(elapsed) + " ms");
        } });
        steps.push_back({ "many", [](std::int64_t) {
            Outcome outcome;
            outcome.entries.push_back({ "many", Result::Pass, "summary" });
            outcome.entries.push_back({ "many.one", Result::Skip, "" });
            outcome.entries.push_back({ "bad name", Result::Pass, "fixed" });
            return outcome;
        } });
        steps.push_back({ "throws", [](std::int64_t) -> Outcome { throw std::runtime_error("boom"); } });
        steps.push_back({ "silent", [](std::int64_t) { return Outcome{}; } });
        steps.push_back({ "stuck", [](std::int64_t) { return Outcome::Wait(); } });
        steps.push_back({ "last", [](std::int64_t) { return Outcome::Done("last", Result::Pass, ""); } });

        Runner runner(std::move(steps), [&got](const Entry& entry) { got.push_back(entry); }, 1000);
        Require(!runner.Advance(100), "a waiting step holds the run");
        Require(got.size() == 1 && got[0].step == "first", "the first step reported before the wait");
        Require(!runner.Advance(120), "still waiting");
        Require(runner.Advance(150) == false, "the stuck step waits now");
        Require(got.size() == 7, "waits, many (3 lines), throws and silent reported");
        Require(got[1].step == "waits" && got[1].detail == "after 50 ms", "a wait gets the time since its step began");
        Require(got[4].step == "bad_name", "a bad step name from a step is made safe");
        Require(got[5].step == "throws" && got[5].result == Result::Fail && got[5].detail == "threw: boom", "a throw is a fail");
        Require(got[6].step == "silent" && got[6].result == Result::Fail, "a step that reports nothing fails");
        Require(!runner.Advance(1000), "the stuck step has 1000 ms");
        Require(runner.Advance(1150), "then it times out and the run ends");
        Require(got[7].step == "stuck" && got[7].result == Result::Fail && got[7].detail == "timed out after 1 s", "the time-out line");
        Require(got[8].step == "last" && runner.Done(), "the run goes on after a time-out");
        Require(runner.Totals().pass == 5 && runner.Totals().fail == 3 && runner.Totals().skip == 1, "the totals");
        Require(runner.ElapsedMs() == 1050, "the run's time");
        Require(waits == 3, "a waiting step is asked once per Advance");
    }

    void RunnerAbort()
    {
        std::vector<Entry> got;
        std::vector<Step> steps;
        steps.push_back({ "a", [](std::int64_t) { return Outcome::Done("a", Result::Pass, ""); } });
        steps.push_back({ "b", [](std::int64_t) { return Outcome::Wait(); } });
        steps.push_back({ "c", [](std::int64_t) { return Outcome::Done("c", Result::Pass, ""); } });
        Runner runner(std::move(steps), [&got](const Entry& entry) { got.push_back(entry); });
        Require(!runner.Advance(10), "b waits");
        runner.Abort("a load started during the run", 30);
        Require(runner.Done(), "an abort ends the run");
        Require(got.size() == 2 && got[1].step == "b" && got[1].result == Result::Fail && got[1].detail == "a load started during the run",
            "one fail for the step it was on");
        Require(runner.Totals().fail == 1 && runner.Totals().pass == 1, "c never ran");
        runner.Abort("again", 40);
        Require(got.size() == 2, "a second abort adds nothing");
    }

    void Targets()
    {
        Require(TargetCommand("").empty() && TargetCommand("   ").empty() && TargetCommand("chronicle").empty(), "/selftest is Chronicle's own");
        Require(TargetCommand("Keepsakes") == "keepsakes.selftest", "/selftest Keepsakes runs keepsakes.selftest");
        Require(TargetCommand(" dfc ") == "dfc.selftest", "spaces around the name are dropped");
        Require(TargetCommand("dworks.selftest") == "dworks.selftest", "the full command name works too");
    }

    // The lines /selftest puts in the chat box, in every language.
    void Phrases()
    {
        using ChronicleRuntime::Lang;
        using ChronicleRuntime::Phrase;
        for (int lang = 0; lang <= static_cast<int>(Lang::Ko); ++lang) {
            const auto done = ChronicleRuntime::Phrased(static_cast<Lang>(lang), Phrase::SelfTestDone, "pass=1 fail=0 skip=0");
            Require(done.find("pass=1 fail=0 skip=0") != std::string::npos, "the counts go into the summary");
            Require(done.find("Chronicle.log") != std::string::npos, "the summary names the log");
            Require(!ChronicleRuntime::Phrased(static_cast<Lang>(lang), Phrase::SelfTestStarted).empty(), "a started line");
            Require(!ChronicleRuntime::Phrased(static_cast<Lang>(lang), Phrase::SelfTestBusy).empty(), "a busy line");
        }
        Require(ChronicleRuntime::Phrased(Lang::En, Phrase::SelfTestBusy) == "A self-test is already running", "the English busy line");
        Require(ChronicleRuntime::Phrased(Lang::De, Phrase::SelfTestDone, "pass=3 fail=0 skip=1") ==
            "Selbsttest beendet: pass=3 fail=0 skip=1. Jeder Schritt steht in Chronicle.log", "the German summary");
    }
}

int main()
{
    StepList();
    StepNames();
    Lines();
    RunnerWalk();
    RunnerAbort();
    Targets();
    Phrases();
    if (g_failures) {
        std::cerr << g_failures << " self-test policy check(s) failed\n";
        return 1;
    }
    std::cout << "self-test policy: all checks passed\n";
    return 0;
}
