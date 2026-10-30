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

#include "History.h"
#include <stdexcept>
#include <string>

using namespace ChronicleHistory;

void Require(bool value, const char* what)
{
    if (!value) throw std::runtime_error(std::string("History regression: ") + what);
}

Line Make(std::int64_t timestamp, std::string channel = "dialogue", std::string message = "line")
{
    return { std::move(channel), "Lydia", std::move(message), timestamp };
}

int main()
{
    // At most 1000 lines; the oldest go first.
    List list;
    for (int i = 1; i <= 1005; ++i) list.Add(Make(i));
    Require(list.Size() == kMaxLines, "cap");
    auto page = list.Before(std::nullopt, 10);
    Require(page.lines.size() == 10 && page.lines.front().timestamp == 996 && page.lines.back().timestamp == 1005, "newest page");
    Require(page.more, "more after the newest page");
    page = list.Before(996, 10);
    Require(page.lines.size() == 10 && page.lines.front().timestamp == 986 && page.lines.back().timestamp == 995, "page before");
    page = list.Before(16, 200);
    Require(page.lines.size() == 10 && page.lines.front().timestamp == 6 && !page.more, "oldest page");
    page = list.Before(std::nullopt, 0);
    Require(page.lines.size() == 1, "a limit of 0 still returns a line");

    // Lines sharing the boundary timestamp stay together.
    List same;
    same.Add(Make(1));
    same.Add(Make(2, "dialogue", "a"));
    same.Add(Make(2, "dialogue", "b"));
    same.Add(Make(3));
    page = same.Before(std::nullopt, 2);
    Require(page.lines.size() == 3 && page.lines.front().message == "a" && page.more, "same-millisecond lines");
    page = same.Before(2, 5);
    Require(page.lines.size() == 1 && page.lines.front().timestamp == 1 && !page.more, "page under a shared timestamp");

    // Replace keeps the newest 1000.
    std::vector<Line> many;
    for (int i = 1; i <= 1200; ++i) many.push_back(Make(i));
    list.Replace(std::move(many));
    Require(list.Size() == kMaxLines && list.Before(std::nullopt, 1).lines.front().timestamp == 1200, "replace cap");
    list.Clear();
    Require(list.Empty(), "clear");

    // Channels the player turned off are not kept; the player's own lines always are.
    ChannelFilter filter;
    Require(filter.Records("dialogue") && filter.Records("system") && filter.Records("notification"), "default on");
    Require(!filter.Records("combat") && !filter.Records("console") && !filter.Records("skyrimnet"), "default off");
    Require(filter.Records("player"), "player lines with the channel off");
    Require(!filter.Records("all") && !filter.Records("unknown"), "unknown channels");
    filter.Apply(nlohmann::json::parse(R"({
        "dialogue": {"enabled": false, "colour": "#123456"},
        "combat": {"enabled": true, "colour": "red"},
        "player": {"enabled": false},
        "made_up": {"enabled": true, "colour": "#abcdef"}
    })"));
    Require(!filter.Records("dialogue") && filter.Records("combat") && filter.Records("player"), "settings apply");
    Require(filter.Records("system"), "unlisted channels keep their default");
    const auto colours = filter.Colours();
    Require(colours.size() == 1 && colours.at("dialogue") == "#123456", "only valid colours of known channels");

    // Lines read back from the file or the page are checked.
    const auto parsed = LinesFromJson(nlohmann::json::parse(R"([
        {"channel": "dialogue", "speaker": "Lydia", "message": "kept", "timestamp": 5},
        {"channel": "dialogue", "speaker": null, "message": "no speaker"},
        {"channel": "made_up", "message": "dropped"},
        {"channel": "system", "message": ""},
        {"channel": "system"},
        "not a line"
    ])"));
    Require(parsed.size() == 2 && parsed[0].timestamp == 5 && parsed[1].speaker.empty() && parsed[1].timestamp == 0, "file lines");
    nlohmann::json bad = { {"channel", "dialogue"}, {"message", std::string("caf\xC3", 4)} };
    const auto repaired = FromJson(bad);
    Require(repaired && repaired->message.find('\xC3') == std::string::npos, "invalid UTF-8 is replaced");
    Require(LinesToJson({ Make(7) }).dump() == R"([{"channel":"dialogue","message":"line","speaker":"Lydia","timestamp":7}])", "round trip");

    // Work from another character or world is not applied.
    Require((Generation{ 1, 2 } == Generation{ 1, 2 }) && !(Generation{ 1, 2 } == Generation{ 1, 3 }), "generations");
    Require(ShouldAdopt(true, false, 42, "42"), "adopt into an empty list");
    Require(!ShouldAdopt(false, false, 42, "42"), "never over native lines");
    Require(!ShouldAdopt(true, true, 42, "42"), "only once");
    Require(!ShouldAdopt(true, false, 42, "41"), "not another character's");
    Require(!ShouldAdopt(true, false, 0, "0"), "not before a character is loaded");
    return 0;
}
