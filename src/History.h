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
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// The lines Chronicle has shown, per character, for Chronicle's DovahHub tab.
// Plain data only: the DLL owns one list on the game thread and hands copies
// to its writer thread.
namespace ChronicleHistory
{
    inline constexpr std::size_t kMaxLines = 1000;
    inline constexpr std::size_t kDefaultPage = 200;

    struct Line
    {
        std::string channel;
        std::string speaker;
        std::string message;
        std::int64_t timestamp{ 0 };  // ms since the epoch
        // For Chronicle_API.h: kept with the line and in the history file.
        std::uint64_t id{ 0 };        // per character, increasing; 0 = not kept in the history
        std::string source;           // the sending mod's slug for lines other mods Send ("" otherwise)
        std::uint32_t speakerFormId{ 0 };
        std::string speakerBase;      // FormKey of the speaker's base NPC
        std::string ownerQuest;       // FormKey of the dialogue topic's owner quest
        std::string topicKind;        // "conversation" | "quest" | "rumour" | "greeting" | "chatter" | "overheard" | ""
        double gameDays{ 0.0 };
        float gameHour{ 0.0F };
        std::string place;
    };

    // What capture knows about a line beyond its text.
    struct Attribution
    {
        std::uint32_t speakerFormId{ 0 };
        std::string speakerBase;
        std::string ownerQuest;
        std::string topicKind;
        std::string source;
        float distance{ -1.0F };  // speaker to player in game units, -1 when unknown
    };

    inline bool KnownChannel(std::string_view channel)
    {
        return channel == "dialogue" || channel == "system" || channel == "notification" ||
               channel == "skyrimnet" || channel == "narrator" || channel == "player" ||
               channel == "combat" || channel == "console";
    }

    // The page's DEFAULT_CONFIG: these are on until the settings say otherwise.
    inline bool DefaultEnabled(std::string_view channel)
    {
        return channel == "dialogue" || channel == "system" || channel == "notification";
    }

    // Channels the player turned off are not kept, as the chat window hides
    // them. The player's own typed lines always show in its All tab, so they
    // are kept with the Player channel off too.
    class ChannelFilter
    {
    public:
        void Apply(const nlohmann::json& channels)
        {
            m_enabled.clear();
            m_colours.clear();
            if (!channels.is_object()) return;
            for (const auto& [name, value] : channels.items()) {
                if (!value.is_object()) continue;
                if (const auto it = value.find("enabled"); it != value.end() && it->is_boolean()) {
                    m_enabled[name] = it->get<bool>();
                }
                if (const auto it = value.find("colour"); it != value.end() && it->is_string() && IsColour(it->get<std::string>())) {
                    m_colours[name] = it->get<std::string>();
                }
            }
        }

        // The player's channel colours (#rrggbb), for the tab to colour lines as the chat window does.
        nlohmann::json Colours() const
        {
            auto colours = nlohmann::json::object();
            for (const auto& [name, colour] : m_colours) {
                if (KnownChannel(name)) colours[name] = colour;
            }
            return colours;
        }

        bool Records(std::string_view channel) const
        {
            if (!KnownChannel(channel)) return false;
            if (channel == "player") return true;
            const auto it = m_enabled.find(std::string(channel));
            return it != m_enabled.end() ? it->second : DefaultEnabled(channel);
        }

    private:
        static bool IsColour(std::string_view text)
        {
            return text.size() == 7 && text[0] == '#' &&
                   std::all_of(text.begin() + 1, text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
        }

        std::unordered_map<std::string, bool> m_enabled;
        std::unordered_map<std::string, std::string> m_colours;
    };

    inline nlohmann::json ToJson(const Line& line)
    {
        nlohmann::json value = {
            {"channel", line.channel},
            {"speaker", line.speaker},
            {"message", line.message},
            {"timestamp", line.timestamp},
        };
        // Empty fields are left out, so 1.3 files and pages read the same.
        if (line.id) value["id"] = line.id;
        if (!line.source.empty()) value["source"] = line.source;
        if (line.speakerFormId) value["speakerFormId"] = line.speakerFormId;
        if (!line.speakerBase.empty()) value["speakerBase"] = line.speakerBase;
        if (!line.ownerQuest.empty()) value["ownerQuest"] = line.ownerQuest;
        if (!line.topicKind.empty()) value["topicKind"] = line.topicKind;
        if (line.gameDays != 0.0) value["gameDays"] = line.gameDays;
        if (line.gameHour != 0.0F) value["gameHour"] = line.gameHour;
        if (!line.place.empty()) value["place"] = line.place;
        return value;
    }

    // A line from the history file or the page's old localStorage: anything
    // malformed is skipped, text is re-checked for UTF-8.
    inline std::optional<Line> FromJson(const nlohmann::json& value)
    {
        if (!value.is_object()) return std::nullopt;
        const auto channel = value.find("channel");
        const auto message = value.find("message");
        if (channel == value.end() || !channel->is_string() || message == value.end() || !message->is_string()) {
            return std::nullopt;
        }
        Line line;
        line.channel = channel->get<std::string>();
        if (!KnownChannel(line.channel)) return std::nullopt;
        line.message = ChronicleUtf8::ReplaceInvalid(message->get<std::string>()).value;
        if (line.message.empty()) return std::nullopt;
        if (const auto speaker = value.find("speaker"); speaker != value.end() && speaker->is_string()) {
            line.speaker = ChronicleUtf8::ReplaceInvalid(speaker->get<std::string>()).value;
        }
        if (const auto timestamp = value.find("timestamp"); timestamp != value.end() && timestamp->is_number()) {
            line.timestamp = timestamp->get<std::int64_t>();
        }
        const auto text = [&value](const char* key, std::size_t max) {
            const auto it = value.find(key);
            if (it == value.end() || !it->is_string()) return std::string{};
            auto result = ChronicleUtf8::ReplaceInvalid(it->get<std::string>()).value;
            return result.size() <= max ? result : std::string{};
        };
        if (const auto id = value.find("id"); id != value.end() && id->is_number_unsigned()) line.id = id->get<std::uint64_t>();
        if (const auto form = value.find("speakerFormId"); form != value.end() && form->is_number_unsigned() &&
            form->get<std::uint64_t>() <= 0xFFFFFFFFu) {
            line.speakerFormId = form->get<std::uint32_t>();
        }
        line.source = text("source", 32);
        line.speakerBase = text("speakerBase", 300);
        line.ownerQuest = text("ownerQuest", 300);
        line.topicKind = text("topicKind", 32);
        line.place = text("place", 300);
        if (const auto days = value.find("gameDays"); days != value.end() && days->is_number()) {
            const auto number = days->get<double>();
            if (std::isfinite(number) && number >= 0.0) line.gameDays = number;
        }
        if (const auto hour = value.find("gameHour"); hour != value.end() && hour->is_number()) {
            const auto number = hour->get<double>();
            if (std::isfinite(number) && number >= 0.0 && number <= 24.0) line.gameHour = static_cast<float>(number);
        }
        return line;
    }

    inline std::vector<Line> LinesFromJson(const nlohmann::json& array)
    {
        std::vector<Line> lines;
        if (!array.is_array()) return lines;
        for (const auto& value : array) {
            if (auto line = FromJson(value)) lines.push_back(std::move(*line));
        }
        return lines;
    }

    inline nlohmann::json LinesToJson(const std::vector<Line>& lines)
    {
        auto array = nlohmann::json::array();
        for (const auto& line : lines) array.push_back(ToJson(line));
        return array;
    }

    struct Page
    {
        std::vector<Line> lines;  // oldest first
        bool more{ false };       // older lines remain
    };

    // Oldest first, at most kMaxLines. Every line gets the next id; ids never
    // go back while the list is open, so a reader can resume after its last id.
    class List
    {
    public:
        // Returns the id given to the line.
        std::uint64_t Add(Line line)
        {
            line.id = ++m_lastId;
            m_lines.push_back(std::move(line));
            while (m_lines.size() > kMaxLines) m_lines.pop_front();
            return m_lastId;
        }

        // Lines from the file keep their ids; missing or out-of-order ones (the
        // page's old history, 1.3 files) are numbered after the line before.
        void Replace(std::vector<Line> lines)
        {
            m_lines.clear();
            const auto skip = lines.size() > kMaxLines ? lines.size() - kMaxLines : 0;
            for (auto it = lines.begin() + static_cast<std::ptrdiff_t>(skip); it != lines.end(); ++it) {
                if (it->id <= m_lastId) it->id = m_lastId + 1;
                m_lastId = it->id;
                m_lines.push_back(std::move(*it));
            }
        }

        // Clear history: the lines go, the ids carry on.
        void Clear() { m_lines.clear(); }
        // Another character: ids start again from its own file.
        void Reset()
        {
            m_lines.clear();
            m_lastId = 0;
        }
        bool Empty() const { return m_lines.empty(); }
        std::size_t Size() const { return m_lines.size(); }
        std::uint64_t LastId() const { return m_lastId; }
        std::vector<Line> Copy() const { return { m_lines.begin(), m_lines.end() }; }
        const std::deque<Line>& Lines() const { return m_lines; }

        // Approximate memory held by the lines, for Diagnostics.
        std::size_t Bytes() const
        {
            std::size_t bytes = 0;
            for (const auto& line : m_lines) {
                bytes += sizeof(Line) + line.channel.size() + line.speaker.size() + line.message.size() +
                    line.source.size() + line.speakerBase.size() + line.ownerQuest.size() + line.topicKind.size() + line.place.size();
            }
            return bytes;
        }

        // The newest `limit` lines older than `before` (all lines without it).
        // Lines sharing the oldest returned timestamp come in the same page, so
        // the next page's `before` cannot split them. With `shown`, only the
        // lines it contains count: the loaded save's own (Lineage below).
        template<class Shown = std::nullptr_t>
        Page Before(std::optional<std::int64_t> before, std::size_t limit, const Shown& shown = nullptr) const
        {
            std::vector<const Line*> view;
            view.reserve(m_lines.size());
            for (const auto& line : m_lines) {
                if constexpr (std::is_same_v<Shown, std::nullptr_t>) view.push_back(&line);
                else if (shown.Contains(line.timestamp)) view.push_back(&line);
            }
            limit = std::clamp<std::size_t>(limit, 1, kMaxLines);
            std::size_t end = view.size();
            if (before) {
                while (end > 0 && view[end - 1]->timestamp >= *before) --end;
            }
            std::size_t start = end > limit ? end - limit : 0;
            while (start > 0 && start < end && view[start - 1]->timestamp == view[start]->timestamp) --start;
            Page page;
            for (auto i = start; i < end; ++i) page.lines.push_back(*view[i]);
            page.more = start > 0;
            return page;
        }

    private:
        std::deque<Line> m_lines;
        std::uint64_t m_lastId{ 0 };
    };

    // Which of a character's lines belong to the loaded save: the stretches of
    // real time (ms since the epoch, a line's timestamp) its play went through.
    // Every save keeps its own list in the co-save. Loading an older save shows
    // only its stretches, so lines from play after it (a branch it never saw)
    // are folded away, not deleted: loading the later save shows them again.
    //
    //   save A at t1           A: [0..t1]
    //   play on, save B at t2  B: [0..t2]
    //   load A at t3, play     now: [0..t1] + [t3..)   (t1..t3 folded)
    //   save C at t4           C: [0..t1] [t3..t4]
    //
    // A save from before this (no list) shows everything, as before: [0..).
    class Lineage
    {
    public:
        struct Range
        {
            std::int64_t from{ 0 };
            std::int64_t to{ 0 };  // inclusive
            bool operator==(const Range&) const = default;
        };
        static constexpr std::size_t kMaxRanges = 128;

        // Everything: a new character, or a save with no list.
        static Lineage All() { return {}; }

        // A save's list, then play from `nowMs` on. Bad ranges are dropped.
        static Lineage Loaded(std::vector<Range> ranges, std::int64_t nowMs)
        {
            Lineage lineage;
            lineage.m_closed = Normalized(std::move(ranges));
            lineage.m_openFrom = nowMs;
            return lineage;
        }

        bool Contains(std::int64_t timestamp) const
        {
            if (timestamp >= m_openFrom) return true;
            return std::any_of(m_closed.begin(), m_closed.end(),
                [timestamp](const Range& r) { return r.from <= timestamp && timestamp <= r.to; });
        }

        // The list a save made now keeps. The stretch played since the load
        // ends now. Stretches that end before `oldestMs` (no line is that
        // old any more) are dropped; past kMaxRanges the oldest go.
        std::vector<Range> ForSave(std::int64_t nowMs, std::int64_t oldestMs = 0) const
        {
            auto ranges = m_closed;
            ranges.push_back({ m_openFrom, std::max(m_openFrom, nowMs) });
            ranges = Normalized(std::move(ranges));
            std::erase_if(ranges, [oldestMs](const Range& r) { return r.to < oldestMs; });
            if (ranges.size() > kMaxRanges) ranges.erase(ranges.begin(), ranges.end() - kMaxRanges);
            return ranges;
        }

        // True for a save with no list (or none loaded yet): every line shows.
        bool ShowsAll() const { return m_closed.empty() && m_openFrom == 0; }
        const std::vector<Range>& Closed() const { return m_closed; }
        std::int64_t OpenFrom() const { return m_openFrom; }

        // For the chat window: [[from, to], ..., [openFrom, null]].
        nlohmann::json ToJson() const
        {
            auto out = nlohmann::json::array();
            for (const auto& r : m_closed) out.push_back({ r.from, r.to });
            out.push_back({ m_openFrom, nullptr });
            return out;
        }

        // Co-save bytes: a count, then from/to pairs.
        static std::vector<std::int64_t> Pack(const std::vector<Range>& ranges)
        {
            std::vector<std::int64_t> out;
            out.push_back(static_cast<std::int64_t>(ranges.size()));
            for (const auto& r : ranges) {
                out.push_back(r.from);
                out.push_back(r.to);
            }
            return out;
        }
        static std::optional<std::vector<Range>> Unpack(const std::vector<std::int64_t>& words)
        {
            if (words.empty() || words[0] < 0 || static_cast<std::size_t>(words[0]) > kMaxRanges ||
                words.size() != 1 + 2 * static_cast<std::size_t>(words[0])) return std::nullopt;
            std::vector<Range> ranges;
            for (std::size_t i = 1; i + 1 < words.size(); i += 2) ranges.push_back({ words[i], words[i + 1] });
            return ranges;
        }

    private:
        // Sorted, overlapping or touching stretches merged, reversed ones dropped.
        static std::vector<Range> Normalized(std::vector<Range> ranges)
        {
            std::erase_if(ranges, [](const Range& r) { return r.to < r.from || r.from < 0; });
            std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.from < b.from; });
            std::vector<Range> merged;
            for (const auto& r : ranges) {
                if (!merged.empty() && r.from <= merged.back().to + 1) merged.back().to = std::max(merged.back().to, r.to);
                else merged.push_back(r);
            }
            return merged;
        }

        std::vector<Range> m_closed;
        std::int64_t m_openFrom{ 0 };
    };

    // Work started for one character and world must not land in another: a
    // delayed save, or the page's old history arriving after a load.
    struct Generation
    {
        std::uint64_t profile{ 0 };
        std::uint64_t world{ 0 };

        bool operator==(const Generation&) const = default;
    };

    // The page's old localStorage lines are adopted once, into an empty list,
    // for the character they were saved under.
    inline bool ShouldAdopt(bool listEmpty, bool adoptedAlready, std::uint64_t currentProfile, std::string_view fromProfile)
    {
        return listEmpty && !adoptedAlready && currentProfile != 0 && fromProfile == std::to_string(currentProfile);
    }
}
