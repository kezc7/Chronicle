#include "PCH.h"
#include "Chronicle.h"
#include "Events.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // ---- Logging setup -------------------------------------------------------
    // Keep logs in SKSE's standard per-user directory:
    // Documents\My Games\Skyrim Special Edition\SKSE.
    // Do not write beside the DLL, since that puts logs under Data/SKSE/Plugins.
    // We never let a logging failure prevent the plugin from loading.

    std::vector<spdlog::sink_ptr> sinks;

    auto tryAddFileSink = [&](const std::filesystem::path& path) {
        try {
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
        } catch (...) {}
    };

    // Documents\My Games\Skyrim Special Edition\SKSE\Chronicle.log
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
        serial->SetRevertCallback([](SKSE::SerializationInterface*) {
            Chronicle::GetSingleton()->profileId = 0;
        });
        serial->SetSaveCallback([](SKSE::SerializationInterface* data) {
            const auto id = Chronicle::GetSingleton()->profileId.load();
            if (!data->OpenRecord(0x5052464C, 1) || !data->WriteRecordData(id)) {
                spdlog::error("Chronicle: unable to save character profile");
            }
        });
        serial->SetLoadCallback([](SKSE::SerializationInterface* data) {
            std::uint32_t type, version, length;
            while (data->GetNextRecordInfo(type, version, length)) {
                std::uint64_t id = 0;
                if (type == 0x5052464C && version == 1 && length == sizeof(id) &&
                    data->ReadRecordData(id) == sizeof(id)) Chronicle::GetSingleton()->profileId = id;
            }
        });
    }

    // ---- Message listeners ---------------------------------------------------
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
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
