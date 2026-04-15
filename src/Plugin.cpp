#include "PCH.h"
#include "Chronicle.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
    const auto level = spdlog::level::trace;
    auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
    const auto level = spdlog::level::info;
    auto logPath = SKSE::log::log_directory();
    if (!logPath) {
        SKSE::stl::report_and_fail("Failed to find standard logging directory");
    }
    *logPath /= "Chronicle.log";
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath->string(), true);
#endif

    auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
    log->set_level(level);
    log->flush_on(level);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("Chronicle loaded");

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
