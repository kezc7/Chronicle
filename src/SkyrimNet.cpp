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
#include "SkyrimNet.h"
#include "Chronicle.h"
#include "GameThread.h"
#include "Utf8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    // How close a listener must be: SkyrimNet's own FindActorByName searches
    // 4000 units; an NPC last seen under the crosshair must still be nearby.
    constexpr float kNamedRange = 4000.0F;
    constexpr float kCrosshairRange = 1500.0F;
    constexpr auto kCrosshairMaxAge = std::chrono::seconds(30);

    // The last NPC the player looked at: opening Chronicle's input clears the
    // crosshair, so the last actor is kept. Crosshair events come on another
    // thread in some load orders: the reference is held by NiPointer there and
    // read on the game thread.
    class CrosshairSink final : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
    {
    public:
        static CrosshairSink& GetSingleton()
        {
            static CrosshairSink sink;
            return sink;
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event,
            RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
        {
            if (!a_event || !a_event->crosshairRef) return RE::BSEventNotifyControl::kContinue;
            GameThread::Post([this, ref = a_event->crosshairRef] { Look(ref.get()); });
            return RE::BSEventNotifyControl::kContinue;
        }

        // Game thread.
        void Look(RE::TESObjectREFR* a_ref)
        {
            GameThread::Check();
            auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
            if (actor && actor != RE::PlayerCharacter::GetSingleton() && !actor->IsDead()) {
                m_actor = actor->GetFormID();
                m_seen = Clock::now();
            }
        }

        RE::FormID Recent() const { return Clock::now() - m_seen <= kCrosshairMaxAge ? m_actor : 0; }
        void Clear() { m_actor = 0; }

    private:
        RE::FormID m_actor{ 0 };
        Clock::time_point m_seen{};
    };

    float DistanceToPlayer(RE::Actor* a_actor)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_actor || !player) return -1.0F;
        const auto a = a_actor->GetPosition();
        const auto b = player->GetPosition();
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    // A loaded, living NPC within range, or nullptr.
    RE::Actor* Listener(RE::FormID a_id, float a_range)
    {
        auto* actor = a_id ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr;
        if (!actor || actor == RE::PlayerCharacter::GetSingleton() || actor->IsDead() || !actor->Is3DLoaded()) return nullptr;
        const auto distance = DistanceToPlayer(actor);
        return distance >= 0.0F && distance <= a_range ? actor : nullptr;
    }

    std::string NameOf(RE::Actor* a_actor)
    {
        const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
        return name ? ChronicleUtf8::ReplaceInvalid(name).value : std::string{};
    }

    // The Papyrus call's return value: 0 is success for every function used here.
    class Reply final : public RE::BSScript::IStackCallbackFunctor
    {
    public:
        explicit Reply(const char* a_function) : m_function(a_function) {}

        // Papyrus thread.
        void operator()(RE::BSScript::Variable a_result) override
        {
            if (a_result.IsInt()) {
                const auto code = a_result.GetSInt();
                spdlog::info("Chronicle: SkyrimNet {} returned {}{}", m_function, code, code == 0 ? " (ok)" : " (failed)");
            } else {
                spdlog::warn("Chronicle: SkyrimNet {} returned no result", m_function);
            }
        }
        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

    private:
        const char* m_function;
    };

    // Game thread. The player's line as dialogue, to a listener when there is one.
    void Deliver(const std::string& a_text, RE::Actor* a_listener, const char* a_how)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        RE::Actor* speaker = RE::PlayerCharacter::GetSingleton();
        if (!vm || !speaker) {
            spdlog::warn("Chronicle: SkyrimNet handoff skipped: no script engine or player");
            return;
        }
        const char* function = ChronicleRuntime::SkyrimNetFunction(ChronicleRuntime::SkyrimNetSend::Dialogue, a_listener != nullptr);
        const auto listenerName = NameOf(a_listener);
        const auto listenerId = a_listener ? a_listener->GetFormID() : 0;
        RE::BSFixedString text(a_text.c_str());
        std::unique_ptr<RE::BSScript::IFunctionArguments> args;
        if (a_listener) args.reset(RE::MakeFunctionArguments(std::move(speaker), std::move(a_listener), std::move(text)));
        else args.reset(RE::MakeFunctionArguments(std::move(speaker), std::move(text)));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> reply{ new Reply(function) };
        if (vm->DispatchStaticCall("SkyrimNetApi", function, args.get(), reply)) {
            if (listenerId) {
                spdlog::info("Chronicle: SkyrimNet handoff: SkyrimNetApi.{} to {} [{:08X}] ({}, {} chars)", function,
                    listenerName, listenerId, a_how ? a_how : "?", a_text.size());
            } else {
                spdlog::info("Chronicle: SkyrimNet handoff: SkyrimNetApi.{}, no listener ({} chars)", function, a_text.size());
            }
        } else {
            spdlog::warn("Chronicle: SkyrimNet handoff failed: SkyrimNetApi.{} could not be called", function);
        }
    }

    // Game thread. The NPC under the crosshair recently, else no listener.
    void DeliverToCrosshair(const std::string& a_text, RE::FormID a_crosshair)
    {
        if (auto* actor = Listener(a_crosshair, kCrosshairRange)) Deliver(a_text, actor, "crosshair");
        else Deliver(a_text, nullptr, nullptr);
    }

    // SkyrimNetApi.FindActorByName's answer for a line that opens with a name.
    class NameReply final : public RE::BSScript::IStackCallbackFunctor
    {
    public:
        // a_world: the world the line was typed in. An answer that comes back
        // after a load is dropped, as Chronicle's own lines are (CHRON-39).
        NameReply(std::string a_text, std::string a_name, RE::FormID a_crosshair, std::uint64_t a_world) :
            m_text(std::move(a_text)), m_name(std::move(a_name)), m_crosshair(a_crosshair), m_world(a_world) {}

        // Papyrus thread: copy the form ID, then pick the listener on the game thread.
        void operator()(RE::BSScript::Variable a_result) override
        {
            RE::FormID found = 0;
            if (a_result.IsObject()) {
                if (auto* actor = a_result.Unpack<RE::Actor*>()) found = actor->GetFormID();
            }
            GameThread::Post([text = m_text, name = m_name, crosshair = m_crosshair, world = m_world, found] {
                auto* chronicle = Chronicle::GetSingleton();
                if (!chronicle->IsGameLoaded() || chronicle->WorldRevision() != world) {
                    spdlog::info("Chronicle: SkyrimNet line for '{}' dropped: a game was loaded since", name);
                    return;
                }
                if (auto* actor = Listener(found, kNamedRange)) {
                    Deliver(text, actor, "named in the line");
                    return;
                }
                spdlog::info("Chronicle: SkyrimNet found no NPC nearby named '{}'", name);
                DeliverToCrosshair(text, crosshair);
            });
        }
        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

    private:
        std::string m_text;
        std::string m_name;
        RE::FormID m_crosshair;
        std::uint64_t m_world;
    };

    // The NPC the player is in conversation with, if any.
    RE::Actor* ConversationPartner()
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) return nullptr;
        auto* topics = RE::MenuTopicManager::GetSingleton();
        if (!topics) return nullptr;
        const auto ref = topics->speaker.get();
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        return actor && actor != RE::PlayerCharacter::GetSingleton() ? actor : nullptr;
    }
}

namespace
{
    // CHRON-46: the living, loaded NPCs within earshot, nearest first.
    std::vector<std::pair<float, RE::Actor*>> NearbyActors(float a_range, std::size_t a_max)
    {
        std::vector<std::pair<float, RE::Actor*>> found;
        auto* lists = RE::ProcessLists::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!lists || !player) return found;
        lists->ForEachHighActor([&](RE::Actor& actor) {
            if (&actor != player && !actor.IsDead() && actor.Is3DLoaded()) {
                const auto distance = DistanceToPlayer(&actor);
                if (distance >= 0.0F && distance <= a_range && !NameOf(&actor).empty()) found.emplace_back(distance, &actor);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (found.size() > a_max) found.resize(a_max);
        return found;
    }
}

namespace ChronicleSkyrimNet
{
    std::string PreviewJson()
    {
        GameThread::Check();
        nlohmann::json out = { { "loaded", Loaded() }, { "nearby", nlohmann::json::array() } };
        if (!Loaded() || !RE::PlayerCharacter::GetSingleton()) return out.dump();
        const auto entry = [](RE::Actor* a_actor) {
            return nlohmann::json{ { "id", a_actor->GetFormID() }, { "name", NameOf(a_actor) } };
        };
        if (auto* partner = ConversationPartner()) {
            out["auto"] = entry(partner);
            out["auto"]["why"] = "conversation";
        } else if (auto* looked = Listener(CrosshairSink::GetSingleton().Recent(), kCrosshairRange)) {
            out["auto"] = entry(looked);
            out["auto"]["why"] = "looking";
        } else {
            out["auto"] = { { "why", "nearby" } };
        }
        for (const auto& [distance, actor] : NearbyActors(kCrosshairRange, 8)) out["nearby"].push_back(entry(actor));
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool Loaded()
    {
        return GetModuleHandleW(L"SkyrimNet.dll") != nullptr;
    }

    void RegisterSinks()
    {
        if (auto* source = SKSE::GetCrosshairRefEventSource()) source->AddEventSink(&CrosshairSink::GetSingleton());
    }

    void Clear()
    {
        CrosshairSink::GetSingleton().Clear();
    }

    void Send(std::string_view a_text, ChronicleRuntime::SkyrimNetSend a_mode, Pick a_pick)
    {
        if (a_mode == ChronicleRuntime::SkyrimNetSend::Off) {
            spdlog::info("Chronicle: SkyrimNet handoff off (skyrimnet_send)");
            return;
        }
        if (!Loaded()) {
            spdlog::info("Chronicle: SkyrimNet not loaded; the line stays in Chronicle");
            return;
        }
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm || !RE::PlayerCharacter::GetSingleton()) {
            spdlog::warn("Chronicle: SkyrimNet handoff skipped: no script engine or player");
            return;
        }
        std::string text(a_text);
        if (a_mode == ChronicleRuntime::SkyrimNetSend::Transform) {
            RE::BSFixedString line(text.c_str());
            std::unique_ptr<RE::BSScript::IFunctionArguments> args{ RE::MakeFunctionArguments(std::move(line)) };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> reply{ new Reply("TransformDialogue") };
            if (vm->DispatchStaticCall("SkyrimNetApi", "TransformDialogue", args.get(), reply)) {
                spdlog::info("Chronicle: SkyrimNet handoff: SkyrimNetApi.TransformDialogue ({} chars)", text.size());
            } else {
                spdlog::warn("Chronicle: SkyrimNet handoff failed: SkyrimNetApi.TransformDialogue could not be called");
            }
            return;
        }
        // The player's pick in the chat bar comes first (CHRON-46).
        if (a_pick.nearby) {
            Deliver(text, nullptr, nullptr);
            return;
        }
        if (a_pick.actor) {
            if (auto* picked = Listener(a_pick.actor, kNamedRange)) {
                Deliver(text, picked, "picked in the chat bar");
                return;
            }
            spdlog::info("Chronicle: the NPC picked in the chat bar [{:08X}] is gone; Chronicle chooses", a_pick.actor);
        }
        // Who the line is said to: the NPC in conversation, else one the line
        // names ("sally, ..."), else the NPC last under the crosshair, else no one.
        if (auto* partner = ConversationPartner()) {
            Deliver(text, partner, "dialogue menu");
            return;
        }
        const auto crosshair = CrosshairSink::GetSingleton().Recent();
        if (const auto name = ChronicleRuntime::AddressedName(text); !name.empty()) {
            RE::BSFixedString wanted(name.c_str());
            std::unique_ptr<RE::BSScript::IFunctionArguments> args{ RE::MakeFunctionArguments(std::move(wanted)) };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> reply{ new NameReply(text, name, crosshair, Chronicle::GetSingleton()->WorldRevision()) };
            if (vm->DispatchStaticCall("SkyrimNetApi", "FindActorByName", args.get(), reply)) {
                spdlog::info("Chronicle: SkyrimNet listener lookup: '{}'", name);
                return;
            }
            spdlog::warn("Chronicle: SkyrimNetApi.FindActorByName could not be called");
        }
        DeliverToCrosshair(text, crosshair);
    }
}
