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
#include "Utf8.h"

#include <memory>

namespace
{
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

namespace ChronicleSkyrimNet
{
    bool Loaded()
    {
        return GetModuleHandleW(L"SkyrimNet.dll") != nullptr;
    }

    void Send(std::string_view a_text, ChronicleRuntime::SkyrimNetSend a_mode)
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
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!vm || !player) {
            spdlog::warn("Chronicle: SkyrimNet handoff skipped: no script engine or player");
            return;
        }
        auto* partner = a_mode == ChronicleRuntime::SkyrimNetSend::Dialogue ? ConversationPartner() : nullptr;
        const char* function = ChronicleRuntime::SkyrimNetFunction(a_mode, partner != nullptr);
        RE::BSFixedString text(std::string(a_text).c_str());
        RE::Actor* speaker = player;
        std::unique_ptr<RE::BSScript::IFunctionArguments> args;
        if (a_mode == ChronicleRuntime::SkyrimNetSend::Transform) {
            args.reset(RE::MakeFunctionArguments(std::move(text)));
        } else if (partner) {
            args.reset(RE::MakeFunctionArguments(std::move(speaker), std::move(partner), std::move(text)));
        } else {
            args.reset(RE::MakeFunctionArguments(std::move(speaker), std::move(text)));
        }
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> reply{ new Reply(function) };
        const char* name = partner ? partner->GetDisplayFullName() : nullptr;
        const std::string target = name ? ChronicleUtf8::ReplaceInvalid(name).value : std::string{};
        if (vm->DispatchStaticCall("SkyrimNetApi", function, args.get(), reply)) {
            spdlog::info("Chronicle: SkyrimNet handoff: SkyrimNetApi.{}{}{} ({} chars)", function,
                partner ? " to " : "", target, a_text.size());
        } else {
            spdlog::warn("Chronicle: SkyrimNet handoff failed: SkyrimNetApi.{} could not be called", function);
        }
    }
}
