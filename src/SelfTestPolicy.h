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

#pragma once

#include "Utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The suite self-test's plain parts: the step list, the shared line format
// and the runner that walks the steps one frame at a time. No game code, so
// the unit tests cover it; SelfTest.cpp supplies the steps.
namespace ChronicleSelfTest
{
    inline constexpr std::string_view kMod = "chronicle";
    inline constexpr std::size_t kMaxStep = 48;
    inline constexpr std::size_t kMaxDetail = 200;
    // A step that waits (the history writer, the page) fails after this long.
    inline constexpr std::int64_t kStepTimeoutMs = 10000;

    enum class Result
    {
        Pass,
        Fail,
        Skip,
    };

    // The fixed steps, in run order. "commands.mod.<prefix>" lines are added
    // after "commands.mods" for each other mod's command.
    inline constexpr std::array<std::string_view, 15> kSteps = {
        "pump",
        "commands.dispatch",
        "commands.mods",
        "journal.write",
        "journal.read",
        "journal.remove",
        "ui.view",
        "saveleak.character",
        "saveleak.page",
        "saveleak.lineage",
        "saveleak.mainmenu",
        "saveleak.newgame",
        "pad",
        "dovahhub.module",
        "dovahmenu.entry",
    };

    inline std::string_view Word(Result result)
    {
        switch (result) {
        case Result::Pass: return "PASS";
        case Result::Fail: return "FAIL";
        case Result::Skip: return "SKIP";
        }
        return "FAIL";
    }

    // DovahHub's ReportSelfTestStep spelling.
    inline std::string_view JsonWord(Result result)
    {
        switch (result) {
        case Result::Pass: return "pass";
        case Result::Fail: return "fail";
        case Result::Skip: return "skip";
        }
        return "fail";
    }

    // 1 to 48 of letters, digits, '-', '_' or '.', as DovahHub accepts.
    inline bool ValidStep(std::string_view step)
    {
        return !step.empty() && step.size() <= kMaxStep && std::all_of(step.begin(), step.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        });
    }

    // A step name made from outside text (another mod's command prefix):
    // anything DovahHub would refuse becomes '_', and it is cut to 48.
    inline std::string StepName(std::string_view text)
    {
        std::string step;
        for (const char c : text.substr(0, kMaxStep)) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            step += ok ? c : '_';
        }
        return step.empty() ? std::string("step") : step;
    }

    // One line of valid UTF-8, at most 200 bytes, cut on a whole character.
    inline std::string Detail(std::string_view text)
    {
        auto clean = ChronicleUtf8::ReplaceInvalid(text).value;
        for (auto& c : clean) {
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        }
        const auto first = clean.find_first_not_of(' ');
        if (first == std::string::npos) return {};
        clean = clean.substr(first, clean.find_last_not_of(' ') - first + 1);
        if (clean.size() <= kMaxDetail) return clean;
        std::size_t cut = kMaxDetail;
        while (cut > 0 && ChronicleUtf8::IsContinuation(static_cast<std::uint8_t>(clean[cut]))) --cut;
        return clean.substr(0, cut);
    }

    // SELFTEST <mod> <step> PASS|FAIL|SKIP <detail>
    inline std::string Line(std::string_view step, Result result, std::string_view detail)
    {
        std::string line = "SELFTEST " + std::string(kMod) + " " + std::string(step) + " " + std::string(Word(result));
        const auto clean = Detail(detail);
        if (!clean.empty()) line += " " + clean;
        return line;
    }

    // {"step":"...","result":"pass","detail":"..."} for ReportSelfTestStep.
    inline std::string StepJson(std::string_view step, Result result, std::string_view detail)
    {
        const nlohmann::json json = { { "step", std::string(step) }, { "result", std::string(JsonWord(result)) },
            { "detail", Detail(detail) } };
        return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    struct Tally
    {
        std::uint32_t pass{ 0 };
        std::uint32_t fail{ 0 };
        std::uint32_t skip{ 0 };

        void Add(Result result)
        {
            switch (result) {
            case Result::Pass: ++pass; break;
            case Result::Fail: ++fail; break;
            case Result::Skip: ++skip; break;
            }
        }

        // "pass=N fail=N skip=N"
        std::string Counts() const
        {
            return "pass=" + std::to_string(pass) + " fail=" + std::to_string(fail) + " skip=" + std::to_string(skip);
        }
    };

    // SELFTEST <mod> DONE pass=N fail=N skip=N ms=N
    inline std::string DoneLine(const Tally& tally, std::int64_t ms)
    {
        return "SELFTEST " + std::string(kMod) + " DONE " + tally.Counts() + " ms=" + std::to_string(std::max<std::int64_t>(ms, 0));
    }

    struct Entry
    {
        std::string step;
        Result result{ Result::Fail };
        std::string detail;
    };

    // What a step answers each time it is asked: its lines, or "not yet"
    // (it waits on another thread, the page or the next frame).
    struct Outcome
    {
        bool wait{ false };
        std::vector<Entry> entries;

        static Outcome Wait() { return { true, {} }; }
        static Outcome Done(std::string step, Result result, std::string detail)
        {
            return { false, { Entry{ std::move(step), result, std::move(detail) } } };
        }
    };

    struct Step
    {
        std::string name;
        // Called with the ms since the step started; called again while it answers Wait.
        std::function<Outcome(std::int64_t elapsedMs)> run;
    };

    // Runs the steps in order, one call to Advance per frame. A step that
    // still waits after kStepTimeoutMs fails "timed out". Every line goes to
    // the sink as it is decided; Advance answers true once the run is over.
    class Runner
    {
    public:
        using Sink = std::function<void(const Entry&)>;

        Runner(std::vector<Step> steps, Sink sink, std::int64_t timeoutMs = kStepTimeoutMs) :
            m_steps(std::move(steps)), m_sink(std::move(sink)), m_timeoutMs(timeoutMs)
        {}

        bool Advance(std::int64_t nowMs)
        {
            if (m_started < 0) m_started = nowMs;
            while (m_next < m_steps.size()) {
                auto& step = m_steps[m_next];
                if (m_stepStarted < 0) m_stepStarted = nowMs;
                const auto elapsed = nowMs - m_stepStarted;
                Outcome outcome;
                try {
                    outcome = step.run(elapsed);
                } catch (const std::exception& e) {
                    outcome = Outcome::Done(step.name, Result::Fail, std::string("threw: ") + e.what());
                } catch (...) {
                    outcome = Outcome::Done(step.name, Result::Fail, "threw");
                }
                if (outcome.wait) {
                    if (elapsed < m_timeoutMs) return false;
                    outcome = Outcome::Done(step.name, Result::Fail, "timed out after " + std::to_string(m_timeoutMs / 1000) + " s");
                }
                if (outcome.entries.empty()) outcome = Outcome::Done(step.name, Result::Fail, "the step reported nothing");
                for (auto& entry : outcome.entries) Emit(std::move(entry));
                ++m_next;
                m_stepStarted = -1;
            }
            m_ended = nowMs;
            return true;
        }

        // Ends the run early (a load started): one FAIL for the step it was on.
        void Abort(std::string_view why, std::int64_t nowMs)
        {
            if (Done()) return;
            Emit({ m_steps[m_next].name, Result::Fail, std::string(why) });
            m_next = m_steps.size();
            m_ended = nowMs;
        }

        bool Done() const { return m_next >= m_steps.size(); }
        const Tally& Totals() const { return m_tally; }
        std::int64_t ElapsedMs() const { return m_started < 0 ? 0 : m_ended - m_started; }

    private:
        void Emit(Entry entry)
        {
            if (!ValidStep(entry.step)) entry.step = StepName(entry.step);
            entry.detail = Detail(entry.detail);
            m_tally.Add(entry.result);
            if (m_sink) m_sink(entry);
        }

        std::vector<Step> m_steps;
        Sink m_sink;
        std::int64_t m_timeoutMs;
        std::size_t m_next{ 0 };
        std::int64_t m_started{ -1 };
        std::int64_t m_stepStarted{ -1 };
        std::int64_t m_ended{ 0 };
        Tally m_tally;
    };

    // "/selftest" -> Chronicle's own run; "/selftest keepsakes" -> the
    // command "keepsakes.selftest" that mod registered. "" for Chronicle.
    inline std::string TargetCommand(std::string_view args)
    {
        while (!args.empty() && args.front() == ' ') args.remove_prefix(1);
        while (!args.empty() && args.back() == ' ') args.remove_suffix(1);
        if (args.empty() || args == "chronicle") return {};
        std::string mod;
        for (const char c : args) mod += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        constexpr std::string_view suffix = ".selftest";
        if (mod.size() > suffix.size() && mod.ends_with(suffix)) return mod;
        return mod + std::string(suffix);
    }
}
