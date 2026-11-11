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
#include "Chronicle.h"
#include "DovahHubModule.h"
#include "DovahMenuModule.h"
#include "Events.h"
#include "GameThread.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // Log to Documents\My Games\Skyrim Special Edition\SKSE, not beside the DLL
    // (that would put logs under Data/SKSE/Plugins). A logging failure never
    // stops the plugin from loading.

    std::vector<spdlog::sink_ptr> sinks;

    auto tryAddFileSink = [&](const std::filesystem::path& path) {
        try {
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
        } catch (...) {}
    };

    if (auto logDir = ChronicleLogDirectory(); logDir) {
        tryAddFileSink(*logDir / "Chronicle.log");
    }

#ifndef NDEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    const auto level = spdlog::level::debug;
#else
    const auto level = spdlog::level::info;
#endif

    if (!sinks.empty()) {
        auto log = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
        log->set_level(level);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));
    }
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::info("Chronicle loaded ({} sink(s))", sinks.size());

    NotificationHook::Install();

    // Persist an opaque character identifier in the SKSE co-save. Histories
    // remain distinct even when two characters have the same display name.
    if (auto* serial = SKSE::GetSerializationInterface()) {
        serial->SetUniqueID(0x4348524E); // CHRN
        // PRFL: the character id. LINE (1.4.17): the stretches of play this save
        // went through, so the log follows the save (History.h Lineage).
        constexpr std::uint32_t kProfileRecord = 0x5052464C;  // PRFL
        constexpr std::uint32_t kLineageRecord = 0x4C494E45;  // LINE
        serial->SetRevertCallback([](SKSE::SerializationInterface*) {
            Chronicle::GetSingleton()->DropHistory();
            Chronicle::GetSingleton()->profileId = 0;
            Chronicle::GetSingleton()->OnLineageLoaded(std::nullopt);  // a save without LINE shows every line
        });
        serial->SetSaveCallback([](SKSE::SerializationInterface* data) {
            auto* chronicle = Chronicle::GetSingleton();
            const auto id = chronicle->profileId.load();
            if (!data->OpenRecord(kProfileRecord, 1) || !data->WriteRecordData(id)) {
                spdlog::error("Chronicle: unable to save character profile");
            }
            const auto ranges = chronicle->LineageForSave();
            const auto words = ChronicleHistory::Lineage::Pack(ranges);
            if (!data->OpenRecord(kLineageRecord, 1) ||
                !data->WriteRecordData(words.data(), static_cast<std::uint32_t>(words.size() * sizeof(std::int64_t)))) {
                spdlog::error("Chronicle: unable to save the history stretches");
            } else {
                spdlog::info("Chronicle: save keeps {} history stretch(es)", ranges.size());
            }
        });
        serial->SetLoadCallback([](SKSE::SerializationInterface* data) {
            std::uint32_t type, version, length;
            while (data->GetNextRecordInfo(type, version, length)) {
                std::uint64_t id = 0;
                if (type == kProfileRecord && version == 1 && length == sizeof(id) &&
                    data->ReadRecordData(id) == sizeof(id)) Chronicle::GetSingleton()->profileId = id;
                if (type == kLineageRecord && version == 1 && length % sizeof(std::int64_t) == 0 &&
                    length <= (1 + 2 * ChronicleHistory::Lineage::kMaxRanges) * sizeof(std::int64_t)) {
                    std::vector<std::int64_t> words(length / sizeof(std::int64_t));
                    const auto read = words.empty() ? 0 : data->ReadRecordData(words.data(), length);
                    auto ranges = read == length ? ChronicleHistory::Lineage::Unpack(words) : std::nullopt;
                    if (!ranges) spdlog::warn("Chronicle: the save's history stretches don't read; every line shows");
                    Chronicle::GetSingleton()->OnLineageLoaded(std::move(ranges));
                }
            }
        });
    }

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            // Every plugin is loaded now (DovahHub loads after Chronicle): use
            // DovahHub's game-thread pump, or Chronicle's own hook without it.
            DovahGameThread::Install("Chronicle");
            DovahHubModule::Listen();
            break;
        case SKSE::MessagingInterface::kPostPostLoad:
            DovahHubModule::Register();
            DovahMenuModule::Register();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            Chronicle::GetSingleton()->OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            Chronicle::GetSingleton()->OnPreLoadGame();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            Chronicle::GetSingleton()->OnPostLoadGame();
            break;
        case SKSE::MessagingInterface::kNewGame:
            Chronicle::GetSingleton()->OnPostLoadGame();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            Chronicle::GetSingleton()->SendMessage("system", "", Chronicle::GetSingleton()->Text(ChronicleRuntime::Phrase::GameSaved));
            break;
        default:
            break;
        }
    });

    return true;
}
