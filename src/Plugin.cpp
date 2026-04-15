#include "PCH.h"
#include "Chronicle.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // Init SKSE first — CLibNG needs it to resolve the runtime (SSE/AE/VR)
    // before log_directory() can return the correct path.
    SKSE::Init(a_skse);

    auto logDir = SKSE::log::log_directory();
    if (!logDir) {
        SKSE::stl::report_and_fail("Chronicle: SKSE log directory unavailable");
    }
    auto logPath = *logDir / "Chronicle.log";

#ifndef NDEBUG
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
    auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    auto log = std::make_shared<spdlog::logger>("global",
        spdlog::sinks_init_list{ fileSink, msvcSink });
    log->set_level(spdlog::level::debug);
#else
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
    auto log = std::make_shared<spdlog::logger>("global",
        spdlog::sinks_init_list{ fileSink });
    log->set_level(spdlog::level::info);
#endif

    log->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("Chronicle loaded");

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
