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

#include "History.h"

#include <set>

// The rules behind Chronicle_API.h that need no game: channel names as other
// mods see them, filters, slugs and "/command" parsing.
namespace ChronicleApiPolicy
{
    inline constexpr std::size_t kMaxSendText = 2000;
    inline constexpr std::uint32_t kDefaultLimit = 200;
    inline constexpr std::uint32_t kMaxLimit = 2000;

    // CHRONICLE_API::Kind, by value.
    enum class Kind : std::uint8_t
    {
        Subtitle = 0, Notification = 1, System = 2, Narrator = 3, Player = 4,
        Combat = 5, Console = 6, Mod = 7, SkyrimNet = 8
    };

    inline Kind KindOf(const ChronicleHistory::Line& line)
    {
        if (!line.source.empty()) return Kind::Mod;
        const std::string_view channel = line.channel;
        if (channel == "dialogue") return Kind::Subtitle;
        if (channel == "notification") return Kind::Notification;
        if (channel == "narrator") return Kind::Narrator;
        if (channel == "player") return Kind::Player;
        if (channel == "combat") return Kind::Combat;
        if (channel == "console") return Kind::Console;
        if (channel == "skyrimnet") return Kind::SkyrimNet;
        return Kind::System;
    }

    // The chat window's "dialogue" channel is "npc" in the API; a mod's line is
    // in the channel named after the mod.
    inline std::string_view ChannelOf(const ChronicleHistory::Line& line)
    {
        if (!line.source.empty()) return line.source;
        if (line.channel == "dialogue") return "npc";
        return line.channel;
    }

    // [a-z0-9_.-], 2..32 characters.
    inline bool IsSlug(std::string_view text)
    {
        return text.size() >= 2 && text.size() <= 32 && std::all_of(text.begin(), text.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        });
    }

    inline std::string Lower(std::string_view text)
    {
        std::string out(text);
        for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    // A comma list ("npc, quest"); empty = every channel. "dialogue" is taken
    // as "npc", the chat window's name for it.
    class Channels
    {
    public:
        Channels() = default;
        explicit Channels(const char* list)
        {
            if (!list) return;
            std::string_view rest = list;
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                auto name = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
                if (name.empty()) continue;
                auto lower = Lower(name);
                m_names.insert(lower == "dialogue" ? std::string("npc") : std::move(lower));
            }
        }

        bool All() const { return m_names.empty(); }
        bool Has(std::string_view channel) const { return All() || m_names.contains(std::string(channel)); }
        bool Matches(const ChronicleHistory::Line& line) const { return Has(ChannelOf(line)); }

    private:
        std::set<std::string> m_names;
    };

    struct Filter
    {
        std::uint64_t sinceId{ 0 };
        Channels channels;
        std::uint32_t speakerFormId{ 0 };
        std::string ownerQuest;   // "" = any
        std::uint32_t limit{ kDefaultLimit };

        bool Matches(const ChronicleHistory::Line& line) const
        {
            return line.id > sinceId && channels.Matches(line) &&
                (!speakerFormId || line.speakerFormId == speakerFormId) &&
                (ownerQuest.empty() || Lower(line.ownerQuest) == Lower(ownerQuest));
        }
    };

    inline std::uint32_t ClampLimit(std::uint32_t limit)
    {
        return limit == 0 ? kDefaultLimit : std::min(limit, kMaxLimit);
    }

    // The lines a query visits, oldest first: the first `limit` after sinceId.
    template<class Lines>
    std::vector<const ChronicleHistory::Line*> Select(const Lines& lines, const Filter& filter)
    {
        std::vector<const ChronicleHistory::Line*> out;
        const auto limit = ClampLimit(filter.limit);
        for (const auto& line : lines) {
            if (out.size() >= limit) break;
            if (filter.Matches(line)) out.push_back(&line);
        }
        return out;
    }

    // A subscriber's minSpeakerDistance: 0 keeps Chronicle's own radius (the
    // line was already captured within it); otherwise the speaker must also be
    // that close. Lines with no known distance pass.
    inline bool WithinDistance(std::uint32_t maxDistance, float distance)
    {
        return maxDistance == 0 || distance < 0.0F || distance <= static_cast<float>(maxDistance);
    }

    // "/dworks dump all" -> {"dworks", "dump all"}. Not a command: nullopt.
    struct Command
    {
        std::string prefix;  // lower case
        std::string args;
    };

    inline std::optional<Command> ParseCommand(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
        if (text.size() < 2 || text.front() != '/') return std::nullopt;
        text.remove_prefix(1);
        const auto space = text.find_first_of(" \t");
        Command command{ Lower(text.substr(0, space)), {} };
        if (command.prefix.empty()) return std::nullopt;
        if (space != std::string_view::npos) {
            auto args = text.substr(space + 1);
            while (!args.empty() && std::isspace(static_cast<unsigned char>(args.front()))) args.remove_prefix(1);
            while (!args.empty() && std::isspace(static_cast<unsigned char>(args.back()))) args.remove_suffix(1);
            command.args = std::string(args);
        }
        return command;
    }

    // MAJOR*10000 + MINOR*100 + PATCH from "1.4.0".
    inline std::uint32_t VersionNumber(std::string_view version)
    {
        std::uint32_t parts[3] = { 0, 0, 0 };
        std::size_t index = 0;
        for (const char c : version) {
            if (c == '.') {
                if (++index >= 3) break;
            } else if (c >= '0' && c <= '9') {
                parts[index] = parts[index] * 10 + static_cast<std::uint32_t>(c - '0');
            } else {
                break;
            }
        }
        return parts[0] * 10000 + parts[1] * 100 + parts[2];
    }
}
