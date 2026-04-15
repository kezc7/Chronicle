#include "PCH.h"
#include "Chronicle.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    // ---- Logging setup -------------------------------------------------------
    // Primary: resolve the DLL's own directory via its module handle.
    // This is always writable (same ACL as the DLL itself) and is independent
    // of CLibNG runtime detection, document folder redirection, etc.
    // Secondary: standard SKSE log directory, attempted after primary.
    // We never let a logging failure prevent the plugin from loading.

    std::vector<spdlog::sink_ptr> sinks;

    auto tryAddFileSink = [&](const std::filesystem::path& path) {
        try {
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
        } catch (...) {}
    };

    // Primary: next to the DLL
    {
        HMODULE hMod = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SKSEPlugin_Load), &hMod) && hMod) {
            wchar_t buf[MAX_PATH]{};
            if (GetModuleFileNameW(hMod, buf, MAX_PATH)) {
                tryAddFileSink(std::filesystem::path(buf).replace_extension(L".log"));
            }
        }
    }

    // Secondary: Documents\My Games\Skyrim Special Edition\SKSE\Chronicle.log
    if (auto logDir = SKSE::log::log_directory(); logDir) {
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

    // ---- Message listeners ---------------------------------------------------
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
