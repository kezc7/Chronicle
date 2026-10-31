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

    // Ids: each added line gets the next one; lines read back keep theirs, and
    // missing or out-of-order ids are numbered after the line before.
    List ids;
    Require(ids.Add(Make(1)) == 1 && ids.Add(Make(2)) == 2 && ids.LastId() == 2, "ids increase");
    ids.Clear();
    Require(ids.Add(Make(3)) == 3, "Clear history keeps the ids going");
    ids.Reset();
    std::vector<Line> file;
    for (const std::uint64_t id : { 0ull, 7ull, 5ull, 0ull, 20ull }) {
        auto line = Make(static_cast<std::int64_t>(id));
        line.id = id;
        file.push_back(line);
    }
    ids.Replace(std::move(file));
    const auto read = ids.Copy();
    Require(read[0].id == 1 && read[1].id == 7 && read[2].id == 8 && read[3].id == 9 && read[4].id == 20, "ids repaired on load");
    Require(ids.Add(Make(21)) == 21 && ids.Bytes() > 0, "ids carry on after a load");

    // The API fields round-trip through the file; 1.3 lines have none of them.
    Line full = Make(9, "dialogue", "Hello");
    full.id = 42;
    full.speakerFormId = 0xA2C94;
    full.speakerBase = "Skyrim.esm|0x0A2C8E";
    full.ownerQuest = "Skyrim.esm|0x000D4E";
    full.topicKind = "quest";
    full.gameDays = 12.5;
    full.gameHour = 13.25F;
    full.place = "Whiterun";
    full.source = "dworks";
    const auto back = FromJson(ToJson(full));
    Require(back && back->id == 42 && back->speakerFormId == 0xA2C94 && back->speakerBase == full.speakerBase &&
        back->ownerQuest == full.ownerQuest && back->topicKind == "quest" && back->gameDays == 12.5 &&
        back->gameHour == 13.25F && back->place == "Whiterun" && back->source == "dworks", "API fields round trip");
    const auto old = FromJson(nlohmann::json::parse(R"({"channel":"dialogue","speaker":"Lydia","message":"old","timestamp":1,"gameHour":99})"));
    Require(old && old->id == 0 && old->speakerFormId == 0 && old->ownerQuest.empty() && old->gameHour == 0.0F, "1.3 lines, bad hour");

    // Work from another character or world is not applied.
    Require((Generation{ 1, 2 } == Generation{ 1, 2 }) && !(Generation{ 1, 2 } == Generation{ 1, 3 }), "generations");
    Require(ShouldAdopt(true, false, 42, "42"), "adopt into an empty list");
    Require(!ShouldAdopt(false, false, 42, "42"), "never over native lines");
    Require(!ShouldAdopt(true, true, 42, "42"), "only once");
    Require(!ShouldAdopt(true, false, 42, "41"), "not another character's");
    Require(!ShouldAdopt(true, false, 0, "0"), "not before a character is loaded");
    return 0;
}
