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
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The lines Chronicle has shown, per character, for DovahHub's History tab.
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
        return {
            {"channel", line.channel},
            {"speaker", line.speaker},
            {"message", line.message},
            {"timestamp", line.timestamp},
        };
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

    // Oldest first, at most kMaxLines.
    class List
    {
    public:
        void Add(Line line)
        {
            m_lines.push_back(std::move(line));
            while (m_lines.size() > kMaxLines) m_lines.pop_front();
        }

        void Replace(std::vector<Line> lines)
        {
            m_lines.clear();
            const auto skip = lines.size() > kMaxLines ? lines.size() - kMaxLines : 0;
            for (auto it = lines.begin() + static_cast<std::ptrdiff_t>(skip); it != lines.end(); ++it) {
                m_lines.push_back(std::move(*it));
            }
        }

        void Clear() { m_lines.clear(); }
        bool Empty() const { return m_lines.empty(); }
        std::size_t Size() const { return m_lines.size(); }
        std::vector<Line> Copy() const { return { m_lines.begin(), m_lines.end() }; }

        // The newest `limit` lines older than `before` (all lines without it).
        // Lines sharing the oldest returned timestamp come in the same page, so
        // the next page's `before` cannot split them.
        Page Before(std::optional<std::int64_t> before, std::size_t limit) const
        {
            limit = std::clamp<std::size_t>(limit, 1, kMaxLines);
            std::size_t end = m_lines.size();
            if (before) {
                while (end > 0 && m_lines[end - 1].timestamp >= *before) --end;
            }
            std::size_t start = end > limit ? end - limit : 0;
            while (start > 0 && start < end && m_lines[start - 1].timestamp == m_lines[start].timestamp) --start;
            Page page;
            page.lines.assign(m_lines.begin() + static_cast<std::ptrdiff_t>(start),
                m_lines.begin() + static_cast<std::ptrdiff_t>(end));
            page.more = start > 0;
            return page;
        }

    private:
        std::deque<Line> m_lines;
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
