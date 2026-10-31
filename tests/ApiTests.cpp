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

#include "ApiPolicy.h"
#define CHRONICLE_API_NO_LOADER
#include "api/Chronicle_API.h"
#include <stdexcept>
#include <string>

using namespace ChronicleApiPolicy;
using ChronicleHistory::Line;

// The header's Kind and the policy's Kind are the same values.
static_assert(static_cast<int>(CHRONICLE_API::Kind::Subtitle) == static_cast<int>(Kind::Subtitle));
static_assert(static_cast<int>(CHRONICLE_API::Kind::Mod) == static_cast<int>(Kind::Mod));
static_assert(static_cast<int>(CHRONICLE_API::Kind::SkyrimNet) == static_cast<int>(Kind::SkyrimNet));
static_assert(CHRONICLE_API::kMsgHistoryOpened == 0x43485201 && CHRONICLE_API::kMsgVisibility == 0x43485202);

void Require(bool value, const char* what)
{
    if (!value) throw std::runtime_error(std::string("API regression: ") + what);
}

Line Make(std::uint64_t id, std::string channel, std::uint32_t speaker = 0, std::string quest = {})
{
    Line line{ std::move(channel), "Lydia", "line " + std::to_string(id), static_cast<std::int64_t>(id) };
    line.id = id;
    line.speakerFormId = speaker;
    line.ownerQuest = std::move(quest);
    return line;
}

int main()
{
    // Chronicle's channels as other mods see them.
    Require(KindOf(Make(1, "dialogue")) == Kind::Subtitle && ChannelOf(Make(1, "dialogue")) == "npc", "dialogue is npc");
    Require(KindOf(Make(1, "skyrimnet")) == Kind::SkyrimNet && KindOf(Make(1, "console")) == Kind::Console, "kinds");
    auto mod = Make(1, "system");
    mod.source = "dworks";
    Require(KindOf(mod) == Kind::Mod && ChannelOf(mod) == "dworks", "a mod's line is in its own channel");
    Require(KindOf(Make(1, "system")) == Kind::System && ChannelOf(Make(1, "system")) == "system", "system");

    // Channel lists.
    Require(Channels(nullptr).All() && Channels("").All() && Channels(" , ").All(), "empty list = every channel");
    const Channels npc(" NPC , combat");
    Require(npc.Matches(Make(1, "dialogue")) && npc.Matches(Make(1, "combat")) && !npc.Matches(Make(1, "system")), "list");
    Require(Channels("dialogue").Matches(Make(1, "dialogue")), "dialogue is taken as npc");
    Require(Channels("dworks").Matches(mod) && !Channels("system").Matches(mod), "mod channel");

    // Queries: after sinceId, oldest first, filtered, limited.
    std::deque<Line> lines;
    for (std::uint64_t id = 1; id <= 3000; ++id) {
        lines.push_back(Make(id, id % 2 ? "dialogue" : "notification", id % 3 ? 0x1A2B : 0, id % 5 ? "" : "Skyrim.esm|0x000D4E"));
    }
    Filter all;
    all.limit = 0;
    auto selected = Select(lines, all);
    Require(selected.size() == kDefaultLimit && selected.front()->id == 1, "limit 0 is the page size");
    all.limit = 99999;
    Require(Select(lines, all).size() == kMaxLimit, "hard cap");
    Filter since;
    since.sinceId = 2990;
    selected = Select(lines, since);
    Require(selected.size() == 10 && selected.front()->id == 2991 && selected.back()->id == 3000, "since");
    Filter npcOnly;
    npcOnly.channels = Channels("npc");
    npcOnly.speakerFormId = 0x1A2B;
    npcOnly.ownerQuest = "skyrim.esm|0x000d4e";
    npcOnly.limit = 2000;
    selected = Select(lines, npcOnly);
    Require(!selected.empty(), "combined filter finds lines");
    for (const auto* line : selected) {
        Require(line->channel == "dialogue" && line->speakerFormId == 0x1A2B && line->ownerQuest == "Skyrim.esm|0x000D4E",
            "combined filter (quest matched without case)");
    }

    // A subscriber's own distance only narrows Chronicle's radius.
    Require(WithinDistance(0, 9000.0F) && WithinDistance(500, 499.0F) && !WithinDistance(500, 501.0F), "distance");
    Require(WithinDistance(500, -1.0F), "unknown distance passes");

    // Commands.
    auto command = ParseCommand("  /DWorks  dump all  ");
    Require(command && command->prefix == "dworks" && command->args == "dump all", "command with args");
    command = ParseCommand("/help");
    Require(command && command->prefix == "help" && command->args.empty(), "bare command");
    Require(!ParseCommand("hello /there") && !ParseCommand("/") && !ParseCommand("/ x") && !ParseCommand(""), "not commands");
    Require(IsSlug("dworks") && IsSlug("chronicle.echo") && IsSlug("d-f_c") && !IsSlug("d") && !IsSlug("DWorks") &&
        !IsSlug("has space") && !IsSlug(std::string(33, 'a')), "slugs");

    Require(VersionNumber("1.4.0") == 10400 && VersionNumber("1.2.39") == 10239 && VersionNumber("2.10.3") == 21003, "version number");
    return 0;
}
