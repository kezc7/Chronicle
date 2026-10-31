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
#include "Api.h"
#include "ApiPolicy.h"
#include "Chronicle.h"
#include "api/Chronicle_API.h"

#include <cstring>

#ifndef CHRONICLE_HOST_NAME
#    define CHRONICLE_HOST_NAME "PrismaUI"
#endif

namespace
{
    namespace Api = CHRONICLE_API;
    namespace Policy = ChronicleApiPolicy;

    struct Subscriber
    {
        Api::SubscriptionId id{ 0 };
        Policy::Channels channels;
        std::uint32_t maxDistance{ 0 };
        Api::MessageFn fn{ nullptr };
        void* user{ nullptr };
    };

    struct Command
    {
        Api::CommandId id{ 0 };
        std::string prefix;
        std::string help;
        Api::CommandFn fn{ nullptr };
        void* user{ nullptr };
    };

    // Registration is meant for the game thread; the lock only keeps a
    // misbehaving caller from corrupting the lists. It is never held while
    // calling out.
    std::mutex g_mutex;
    std::vector<Subscriber> g_subscribers;
    std::vector<Command> g_commands;
    std::uint32_t g_nextId = 1;
    Api::CommandId g_echoCommand = 0;

    // The channel name as the API reports it, null-terminated for Message.
    const char* ChannelName(const ChronicleHistory::Line& line)
    {
        if (!line.source.empty()) return line.source.c_str();
        if (line.channel == "dialogue") return "npc";
        return line.channel.c_str();
    }

    Api::Message ToMessage(const ChronicleHistory::Line& line)
    {
        Api::Message message{};
        message.size = sizeof(message);
        message.id = line.id;
        message.kind = static_cast<Api::Kind>(Policy::KindOf(line));
        message.channel = ChannelName(line);
        message.speaker = line.speaker.c_str();
        message.speakerFormId = line.speakerFormId;
        message.speakerBase = line.speakerBase.c_str();
        message.text = line.message.c_str();
        message.ownerQuest = line.ownerQuest.c_str();
        message.topicKind = line.topicKind.c_str();
        message.gameDays = line.gameDays;
        message.gameHour = line.gameHour;
        message.realTimeMs = line.timestamp;
        message.place = line.place.c_str();
        return message;
    }

    std::uint32_t GetMessages(const Api::Query* query, Api::VisitFn visit, void* user)
    {
        auto* chronicle = Chronicle::GetSingleton();
        if (!query || !visit || query->size < sizeof(Api::Query) || !chronicle->HistoryOpen()) return 0;
        Policy::Filter filter;
        filter.sinceId = query->sinceId;
        filter.channels = Policy::Channels(query->channels);
        filter.speakerFormId = query->speakerFormId;
        filter.ownerQuest = query->ownerQuest ? query->ownerQuest : "";
        filter.limit = query->limit;
        std::uint32_t visited = 0;
        for (const auto* line : Policy::Select(chronicle->History().Lines(), filter)) {
            const auto message = ToMessage(*line);
            ++visited;
            if (!visit(&message, user)) break;
        }
        return visited;
    }

    std::uint64_t GetLastId()
    {
        return Chronicle::GetSingleton()->History().LastId();
    }

    Api::Result Subscribe(const Api::SubscribeDesc* desc, Api::SubscriptionId* out)
    {
        if (!desc || desc->size < sizeof(Api::SubscribeDesc) || !desc->onMessage) return Api::Result::InvalidArgument;
        std::lock_guard lock(g_mutex);
        Subscriber subscriber{ g_nextId++, Policy::Channels(desc->channels), desc->minSpeakerDistance, desc->onMessage, desc->user };
        if (out) *out = subscriber.id;
        spdlog::info("Chronicle API: subscriber {} added ({})", subscriber.id, desc->channels && *desc->channels ? desc->channels : "every channel");
        g_subscribers.push_back(std::move(subscriber));
        return Api::Result::Ok;
    }

    Api::Result Unsubscribe(Api::SubscriptionId id)
    {
        std::lock_guard lock(g_mutex);
        const auto it = std::find_if(g_subscribers.begin(), g_subscribers.end(), [id](const auto& s) { return s.id == id; });
        if (!id || it == g_subscribers.end()) return Api::Result::InvalidId;
        g_subscribers.erase(it);
        return Api::Result::Ok;
    }

    Api::Result Send(Api::Kind kind, const char* sender, const char* speaker, const char* text)
    {
        if (!text) return Api::Result::InvalidArgument;
        const auto length = std::strlen(text);
        if (length == 0 || length > Policy::kMaxSendText) return Api::Result::InvalidArgument;
        auto* chronicle = Chronicle::GetSingleton();
        switch (kind) {
        case Api::Kind::Mod:
            {
                const auto slug = sender ? Policy::Lower(sender) : std::string{};
                if (!Policy::IsSlug(slug)) return Api::Result::InvalidArgument;
                if (!chronicle->IsGameLoaded()) return Api::Result::NotReady;
                ChronicleHistory::Attribution attribution;
                attribution.source = slug;
                // Shown in the chat window's System tab; the API reports channel "<sender>".
                chronicle->SendMessage("system", speaker && *speaker ? std::string_view(speaker) : std::string_view(slug),
                    std::string_view(text, length), std::move(attribution));
                return Api::Result::Ok;
            }
        case Api::Kind::Player:
            {
                if (!GetModuleHandleW(L"SkyrimNet.dll")) return Api::Result::NoTarget;
                if (!chronicle->IsGameLoaded()) return Api::Result::NotReady;
                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) return Api::Result::NotReady;
                tasks->AddTask([copy = std::string(text, length)] { Chronicle::GetSingleton()->SubmitChat(copy); });
                return Api::Result::Ok;
            }
        default:
            return Api::Result::InvalidArgument;
        }
    }

    Api::Result RegisterCommand(const Api::CommandDesc* desc, Api::CommandId* out)
    {
        if (!desc || desc->size < sizeof(Api::CommandDesc) || !desc->prefix || !desc->onCommand) return Api::Result::InvalidArgument;
        const auto prefix = Policy::Lower(desc->prefix);
        if (!Policy::IsSlug(prefix)) return Api::Result::InvalidArgument;
        std::lock_guard lock(g_mutex);
        if (prefix == "help" || std::any_of(g_commands.begin(), g_commands.end(), [&](const auto& c) { return c.prefix == prefix; })) {
            return Api::Result::Duplicate;
        }
        Command command{ g_nextId++, prefix, desc->help ? ChronicleUtf8::ReplaceInvalid(desc->help).value.substr(0, 200) : std::string{},
            desc->onCommand, desc->user };
        if (out) *out = command.id;
        spdlog::info("Chronicle API: command /{} registered", command.prefix);
        g_commands.push_back(std::move(command));
        return Api::Result::Ok;
    }

    Api::Result UnregisterCommand(Api::CommandId id)
    {
        std::lock_guard lock(g_mutex);
        const auto it = std::find_if(g_commands.begin(), g_commands.end(), [id](const auto& c) { return c.id == id; });
        if (!id || it == g_commands.end()) return Api::Result::InvalidId;
        g_commands.erase(it);
        return Api::Result::Ok;
    }

    bool IsVisible()
    {
        return Chronicle::GetSingleton()->IsVisible();
    }

    Api::Result SetVisible(bool visible)
    {
        Chronicle::GetSingleton()->SetApiHidden(!visible);
        return Api::Result::Ok;
    }

    // "/chronicle.echo <text>": a reply through Send, to check the command path in game.
    bool Echo(const char*, const char* args, void*)
    {
        Send(Api::Kind::Mod, "chronicle.echo", nullptr, args && *args ? args : "(nothing)");
        return true;
    }

    const Api::ChronicleApi1 g_api = [] {
        Api::ChronicleApi1 api{};
        api.apiVersion = Api::kApiVersion1;
        api.hostVersion = CHRONICLE_VERSION;
        api.hostVersionNumber = Policy::VersionNumber(CHRONICLE_VERSION);
        api.host = CHRONICLE_HOST_NAME;
        api.GetMessages = &GetMessages;
        api.GetLastId = &GetLastId;
        api.Subscribe = &Subscribe;
        api.Unsubscribe = &Unsubscribe;
        api.Send = &Send;
        api.RegisterCommand = &RegisterCommand;
        api.UnregisterCommand = &UnregisterCommand;
        api.IsVisible = &IsVisible;
        api.SetVisible = &SetVisible;
        return api;
    }();
}

extern "C" __declspec(dllexport) const void* Chronicle_RequestApi(std::uint32_t version)
{
    return version == Api::kApiVersion1 ? &g_api : nullptr;
}

namespace ChronicleApi
{
    void Deliver(const ChronicleHistory::Line& line, float distance)
    {
        std::vector<Subscriber> subscribers;
        {
            std::lock_guard lock(g_mutex);
            if (g_subscribers.empty()) return;
            subscribers = g_subscribers;
        }
        const auto message = ToMessage(line);
        for (const auto& subscriber : subscribers) {
            if (!subscriber.channels.Matches(line) || !Policy::WithinDistance(subscriber.maxDistance, distance)) continue;
            try {
                subscriber.fn(&message, subscriber.user);
            } catch (...) {
                spdlog::error("Chronicle API: subscriber {} threw", subscriber.id);
            }
        }
    }

    bool RunCommand(std::string_view text)
    {
        const auto parsed = Policy::ParseCommand(text);
        if (!parsed) return false;
        auto* chronicle = Chronicle::GetSingleton();
        using ChronicleRuntime::Phrase;
        // Echoed first, so a handler's replies (Send) follow it in the box.
        chronicle->SendMessage("console", "", "> /" + parsed->prefix + (parsed->args.empty() ? "" : " " + parsed->args));
        std::vector<Command> commands;
        {
            std::lock_guard lock(g_mutex);
            commands = g_commands;
        }
        if (parsed->prefix == "help") {
            if (commands.empty()) {
                chronicle->SendMessage("console", "", chronicle->Text(Phrase::NoCommands));
                return true;
            }
            std::string list = "/help";
            for (const auto& command : commands) list += ", /" + command.prefix;
            chronicle->SendMessage("console", "", chronicle->Text(Phrase::Commands, list));
            for (const auto& command : commands) {
                if (!command.help.empty()) chronicle->SendMessage("console", "", "/" + command.prefix + ": " + command.help);
            }
            return true;
        }
        const auto it = std::find_if(commands.begin(), commands.end(), [&](const auto& c) { return c.prefix == parsed->prefix; });
        bool handled = false;
        if (it != commands.end()) {
            try {
                handled = it->fn(it->prefix.c_str(), parsed->args.c_str(), it->user);
            } catch (...) {
                spdlog::error("Chronicle API: command /{} threw", it->prefix);
            }
        }
        if (!handled) chronicle->SendMessage("console", "", chronicle->Text(Phrase::UnknownCommand, "/" + parsed->prefix));
        return true;
    }

    void SetEchoCommand(bool enabled)
    {
        if (enabled == (g_echoCommand != 0)) return;
        if (!enabled) {
            UnregisterCommand(g_echoCommand);
            g_echoCommand = 0;
            return;
        }
        Api::CommandDesc desc{};
        desc.size = sizeof(desc);
        desc.prefix = "chronicle.echo";
        desc.help = "chronicle.echo <text>: replies with the text (debug_commands)";
        desc.onCommand = &Echo;
        if (RegisterCommand(&desc, &g_echoCommand) != Api::Result::Ok) g_echoCommand = 0;
    }

    void Dispatch(std::uint32_t type, const nlohmann::json& data)
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) return;
        const auto text = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        messaging->Dispatch(type, const_cast<char*>(text.c_str()), static_cast<std::uint32_t>(text.size() + 1), nullptr);
    }

    std::size_t SubscriberCount()
    {
        std::lock_guard lock(g_mutex);
        return g_subscribers.size();
    }

    std::vector<std::string> CommandPrefixes()
    {
        std::lock_guard lock(g_mutex);
        std::vector<std::string> prefixes;
        for (const auto& command : g_commands) prefixes.push_back(command.prefix);
        return prefixes;
    }
}
