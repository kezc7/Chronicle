#include "PCH.h"
#include "Chronicle.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    std::vector<spdlog::sink_ptr> sinks;

    // --- Sink 1: next to the DLL (Data/SKSE/Plugins/Chronicle.log) --------------
    // We resolve the DLL's own path via its module handle so this works regardless
    // of where Documents is or whether the game directory is write-protected.
    {
        HMODULE hMod = nullptr;
        // Use the address of this function as an anchor into our own module
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&SKSEPlugin_Load),
            &hMod);
        if (hMod) {
            wchar_t buf[MAX_PATH]{};
            GetModuleFileNameW(hMod, buf, MAX_PATH);
            auto dllLog = std::filesystem::path(buf).replace_extension(L".log");
            try {
                sinks.push_back(
                    std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                        dllLog.string(), true));
            } catch (...) {}
        }
    }

    // --- Sink 2: standard SKSE log directory ------------------------------------
    // (Documents\My Games\Skyrim Special Edition\SKSE\Chronicle.log)
    if (auto logDir = SKSE::log::log_directory(); logDir) {
        auto skseLog = *logDir / "Chronicle.log";
        try {
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    skseLog.string(), true));
        } catch (...) {}
    }

#ifndef NDEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    const auto level = spdlog::level::debug;
#else
    const auto level = spdlog::level::info;
#endif

    if (sinks.empty()) {
        // Last resort — write to the game root directory
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                "Chronicle.log", true));
        } catch (...) {
            SKSE::stl::report_and_fail("Chronicle: could not open any log file");
        }
    }

    auto log = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
    log->set_level(level);
    log->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("Chronicle loaded ({} log sink(s) active)", sinks.size());

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
