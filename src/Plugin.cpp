#include "PCH.h"
#include "Chronicle.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // Determine log path. Fall back to game root if SKSE log dir is unavailable.
    std::filesystem::path logFilePath;
    if (auto logDir = SKSE::log::log_directory(); logDir) {
        logFilePath = *logDir / "Chronicle.log";
    } else {
        logFilePath = std::filesystem::current_path() / "Chronicle.log";
    }

    std::vector<spdlog::sink_ptr> sinks;
    try {
        sinks.push_back(
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));
    } catch (...) {
        // If the intended path fails, write next to the game EXE
        logFilePath = std::filesystem::current_path() / "Chronicle.log";
        sinks.push_back(
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));
    }
#ifndef NDEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    const auto level = spdlog::level::debug;
#else
    const auto level = spdlog::level::info;
#endif

    auto log = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
    log->set_level(level);
    log->flush_on(spdlog::level::trace);  // flush on every write — never lose a line to a crash
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("Chronicle loaded — log: {}", logFilePath.string());

    SKSE::Init(a_skse);

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            Chronicle::GetSingleton()->OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            Chronicle::GetSingleton()->OnPostLoadGame();
            break;
        default:
            break;
        }
    });

    return true;
}
