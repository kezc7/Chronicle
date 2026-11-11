#pragma once
// DovahGameThread.h: game work on the game's main thread, for the Dovah suite's SKSE plugins. Header-only;
// needs CommonLibSSE-NG, and DovahHub_API.h beside it.
//
// SKSE's AddTask and AddUITask can't be trusted to reach the game's main thread: in some load orders either
// one runs its tasks on worker threads. What only ever runs on the game thread is the game's own frame
// update, so this hooks the main loop's call to it (Main::Update) and runs queued work there, just before
// each frame's update.
//
// With DovahHub 0.97.4 or later installed, the work goes to DovahHub's pump, one hook for the whole suite.
// Without it (or with an older DovahHub), this installs its own hook on the same call, chained through the
// call's old target, so any number of mods can share it.
//
//   // At kPostLoad, once every plugin is loaded:
//   DovahGameThread::Install("MiniPets");
//   DovahGameThread::Post("pet reacts", [] { ... });  // always later: at the next frame
//   DovahGameThread::Run("open tab", [] { ... });     // now if this is the game thread, otherwise Post
//   DovahGameThread::Note("event sink");              // logs once if this place runs off the game thread
//
// Work posted before Install, or before the first frame, waits for the first frame. Each frame runs what was
// queued when it started; work posted while it runs waits for the next frame, so nothing can chain within a
// frame. If the hook can't be installed (an ID missing from the address library, or no call where there
// should be one), the log says why and the work goes to SKSE's UI tasks instead, as before. It never stops
// the game.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace DovahGameThread::detail
{
    using JobFn = void (*)(void*);

    // where: a static string naming the place that posted it, or nullptr (then the log names fn's module).
    struct Job
    {
        JobFn fn = nullptr;
        void* ctx = nullptr;
        const char* where = nullptr;
    };

    struct Drained
    {
        std::size_t ran = 0;
        double ms = 0.0;
        double slowestMs = 0.0;
        Job slowest{};
    };

    // Jobs in posting order. Drain runs a snapshot: only what was queued when it started.
    class Queue
    {
    public:
        void Push(const Job& a_job)
        {
            const std::lock_guard lock{ mutex_ };
            jobs_.push_back(a_job);
        }

        std::size_t Size() const
        {
            const std::lock_guard lock{ mutex_ };
            return jobs_.size();
        }

        std::vector<Job> Take()
        {
            std::vector<Job> out;
            const std::lock_guard lock{ mutex_ };
            out.swap(jobs_);
            return out;
        }

        Drained Drain()
        {
            using Clock = std::chrono::steady_clock;
            const auto batch = Take();
            Drained result;
            const auto start = Clock::now();
            for (const auto& job : batch) {
                const auto at = Clock::now();
                job.fn(job.ctx);
                const double ms = std::chrono::duration<double, std::milli>(Clock::now() - at).count();
                if (ms >= result.slowestMs) {
                    result.slowestMs = ms;
                    result.slowest = job;
                }
                ++result.ran;
            }
            result.ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            return result;
        }

    private:
        mutable std::mutex mutex_;
        std::vector<Job> jobs_;
    };
}

#ifndef DOVAH_GAME_THREAD_QUEUE_ONLY
#include "DovahHub_API.h"

namespace DovahGameThread
{
    namespace detail
    {
        // The main loop's call to Main::Update, by address library ID: Skyrim SE 1.5.97, then AE (1.6.1170
        // included). Each is checked before anything is written.
        inline constexpr std::uint64_t kLoopIdSE = 35551, kLoopIdAE = 36544;
        inline constexpr std::uintptr_t kCallOffsetSE = 0x11F, kCallOffsetAE = 0x160;
        inline constexpr std::uint64_t kUpdateIdSE = 35565, kUpdateIdAE = 36564;
        inline constexpr double kSlowMs = 5.0;

        // The address library under either CommonLibSSE-NG's names: newer checkouts have REL::IDDB and
        // REL::Offset2ID (REL/IDDB.h), older ones REL::IDDatabase and REL::IDDatabase::Offset2ID. Kept in here,
        // so a mod that added its own aliases for an older copy of this header still builds.
#if __has_include(<REL/IDDB.h>)
        using AddressLibrary = REL::IDDB;
        using IdsByOffset = REL::Offset2ID;
#else
        using AddressLibrary = REL::IDDatabase;
        using IdsByOffset = REL::IDDatabase::Offset2ID;
#endif

        enum Mode : int
        {
            kPending,   // before Install: jobs wait in the queue
            kHub,       // DovahHub's pump runs them
            kHook,      // this DLL's own hook runs them
            kFallback,  // no hook: SKSE's UI tasks
        };

        struct State
        {
            Queue queue;
            std::mutex modeMutex;
            std::atomic<int> mode{ kPending };
            std::atomic<std::uint32_t> gameThread{ 0 };
            std::atomic<std::uint32_t> loadThread{ 0 };
            std::atomic<std::uint64_t> frames{ 0 };
            std::atomic<std::int64_t> installedAt{ 0 };
            std::atomic<std::int64_t> lastFrameAt{ 0 };
            std::atomic<bool> slowTold{ false };
            std::atomic<bool> strayTold{ false };
            const DOVAHHUB_API::DovahHubApi1* hub = nullptr;
            std::uintptr_t original = 0;
            std::string name = "DovahGameThread";
        };

        inline State& S()
        {
            static State state;
            return state;
        }

        inline std::int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        inline std::string ModuleOf(const void* a_address)
        {
            HMODULE module = nullptr;
            if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(a_address), &module)) {
                return "an unknown module";
            }
            char path[MAX_PATH]{};
            const auto length = ::GetModuleFileNameA(module, path, MAX_PATH);
            std::string name(path, length);
            const auto slash = name.find_last_of("\\/");
            return slash == std::string::npos ? name : name.substr(slash + 1);
        }

        inline std::string Where(const Job& a_job)
        {
            if (a_job.where && *a_job.where) {
                std::string where = a_job.where;
                const auto slash = where.find_last_of("\\/");
                return slash == std::string::npos ? where : where.substr(slash + 1);
            }
            return ModuleOf(reinterpret_cast<const void*>(a_job.fn));
        }

        // SKSE's UI tasks, checked against RE::Main's threadID: a task that finds itself elsewhere tries again a
        // frame later, a few times, then runs anyway so nothing is lost. What every mod did before the pump.
        inline void Fallback(const Job& a_job, int a_tries = 8)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                SKSE::log::error("{}: game-thread work from {} dropped: SKSE's task interface isn't there", S().name, Where(a_job));
                return;
            }
            tasks->AddUITask([a_job, a_tries]() {
                const auto* main = RE::Main::GetSingleton();
                if ((main && main->threadID == ::GetCurrentThreadId()) || a_tries <= 0) {
                    a_job.fn(a_job.ctx);
                    return;
                }
                std::thread([a_job, a_tries]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    Fallback(a_job, a_tries - 1);
                }).detach();
            });
        }

        inline void ToFallback(const char* a_why)
        {
            auto& s = S();
            const std::lock_guard lock{ s.modeMutex };
            if (s.mode.load() != kHook) return;
            s.mode = kFallback;
            SKSE::log::error("{}: game-thread pump: {}; SKSE's UI tasks run the work until frames come again", s.name, a_why);
            for (const auto& job : s.queue.Take()) Fallback(job);
        }

        inline void Frame(void* a_main)
        {
            auto& s = S();
            const auto me = static_cast<std::uint32_t>(::GetCurrentThreadId());
            if (s.frames.fetch_add(1) == 0) {
                s.gameThread = me;
                const auto* main = RE::Main::GetSingleton();
                const auto mainThread = main ? static_cast<std::uint32_t>(main->threadID) : 0u;
                SKSE::log::info("{}: game-thread pump: first frame on thread {}; RE::Main's threadID is {}, plugins loaded on thread {}{}",
                                s.name, me, mainThread, s.loadThread.load(),
                                me == mainThread ? "" : " (RE::Main's isn't the frame's thread: the frame's is used)");
            } else if (me != s.gameThread.load() && !s.strayTold.exchange(true)) {
                SKSE::log::warn("{}: game-thread pump: a frame update ran on thread {}, not {}", s.name, me, s.gameThread.load());
            }
            s.lastFrameAt = NowMs();
            if (s.mode.load() == kFallback) {
                const std::lock_guard lock{ s.modeMutex };
                if (s.mode.load() == kFallback) {
                    s.mode = kHook;
                    SKSE::log::info("{}: game-thread pump: frames again; the pump runs the work again", s.name);
                }
            }
            const auto drained = s.queue.Drain();
            if (drained.ms > kSlowMs && !s.slowTold.exchange(true)) {
                SKSE::log::warn("{}: game-thread pump: one frame's work took {:.1f} ms ({} jobs); the slowest, {:.1f} ms, came from {} (said once)",
                                s.name, drained.ms, drained.ran, drained.slowestMs, Where(drained.slowest));
            }
            reinterpret_cast<void (*)(void*)>(s.original)(a_main);
        }

        inline void Hooked(void* a_main) { Frame(a_main); }

        // Which of a_ids this game's address library has, asked without the lookup that stops the game when an
        // ID is missing.
        inline std::vector<bool> HasIds(std::initializer_list<std::uint64_t> a_ids)
        {
            std::vector<bool> found(a_ids.size(), false);
            try {
                const IdsByOffset ids;
                for (const auto& entry : ids) {
                    std::size_t i = 0;
                    for (const auto id : a_ids) {
                        if (entry.id == id) found[i] = true;
                        ++i;
                    }
                }
            } catch (...) {
            }
            return found;
        }

        inline bool Executable(std::uintptr_t a_address)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
            return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        }

        // Executable memory within a rel32 jump of a_site.
        inline void* AllocNear(std::uintptr_t a_site, std::size_t a_size)
        {
            SYSTEM_INFO system{};
            ::GetSystemInfo(&system);
            const std::uintptr_t grain = system.dwAllocationGranularity ? system.dwAllocationGranularity : 0x10000;
            constexpr std::uintptr_t kReach = 0x7FF00000;
            const std::uintptr_t low = a_site > kReach + grain ? a_site - kReach : grain;
            const std::uintptr_t high = a_site + kReach;
            const auto tryAt = [&](std::uintptr_t a_at) -> void* {
                return ::VirtualAlloc(reinterpret_cast<void*>(a_at), a_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            };
            for (std::uintptr_t at = a_site & ~(grain - 1); at > low;) {
                MEMORY_BASIC_INFORMATION info{};
                if (!::VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info))) break;
                if (info.State == MEM_FREE) {
                    if (auto* memory = tryAt(at)) return memory;
                }
                const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                if (base <= low + grain) break;
                at = (base - 1) & ~(grain - 1);
            }
            for (std::uintptr_t at = (a_site & ~(grain - 1)) + grain; at < high;) {
                MEMORY_BASIC_INFORMATION info{};
                if (!::VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info))) break;
                if (info.State == MEM_FREE) {
                    if (auto* memory = tryAt((at + grain - 1) & ~(grain - 1))) return memory;
                }
                at = (reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize + grain - 1) & ~(grain - 1);
            }
            return nullptr;
        }

        // Points the main loop's call to Main::Update at Hooked, keeping the call's old target (the game's, or
        // the mod that hooked it before) as the one Hooked calls. False with the reason when anything isn't as
        // expected; nothing is written then.
        inline bool InstallHook(std::string& a_why)
        {
            auto& s = S();
            if (REL::Module::IsVR()) {
                a_why = "Skyrim VR isn't supported yet";
                return false;
            }
            const bool ae = REL::Module::IsAE();
            const auto loopId = ae ? kLoopIdAE : kLoopIdSE;
            const auto updateId = ae ? kUpdateIdAE : kUpdateIdSE;
            const auto callOffset = ae ? kCallOffsetAE : kCallOffsetSE;
            const auto found = HasIds({ loopId, updateId });
            if (!found[0]) {
                a_why = std::format("the address library has no ID {} (the main loop)", loopId);
                return false;
            }
            const auto base = REL::Module::get().base();
            const auto site = base + AddressLibrary::get().id2offset(loopId) + callOffset;
            if (!Executable(site)) {
                a_why = std::format("ID {} +0x{:X} isn't code", loopId, callOffset);
                return false;
            }
            const auto* code = reinterpret_cast<const std::uint8_t*>(site);
            if (code[0] != 0xE8) {
                a_why = std::format("ID {} +0x{:X} holds {:02X}, not a call", loopId, callOffset, code[0]);
                return false;
            }
            std::int32_t displacement = 0;
            std::memcpy(&displacement, code + 1, sizeof(displacement));
            const auto target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(site + 5) + displacement);
            if (!Executable(target)) {
                a_why = std::format("ID {} +0x{:X} calls 0x{:X}, which isn't code", loopId, callOffset, target);
                return false;
            }
            // Inside the game it must be Main::Update itself; outside, another mod's hook chained on it.
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            const bool inGame = target >= text.address() && target < text.address() + text.size();
            if (inGame) {
                if (!found[1]) {
                    a_why = std::format("the address library has no ID {} (Main::Update)", updateId);
                    return false;
                }
                if (target != base + AddressLibrary::get().id2offset(updateId)) {
                    a_why = std::format("ID {} +0x{:X} calls SkyrimSE.exe+0x{:X}, not Main::Update (ID {})", loopId, callOffset, target - base, updateId);
                    return false;
                }
            }
            auto* stub = static_cast<std::uint8_t*>(AllocNear(site, 64));
            if (!stub) {
                a_why = "no memory within reach of the call for the hook";
                return false;
            }
            // jmp [rip+0] to Hooked, wherever this DLL loaded.
            const auto hooked = reinterpret_cast<std::uint64_t>(&Hooked);
            stub[0] = 0xFF;
            stub[1] = 0x25;
            std::memset(stub + 2, 0, 4);
            std::memcpy(stub + 6, &hooked, sizeof(hooked));
            const auto jump = reinterpret_cast<std::intptr_t>(stub) - static_cast<std::intptr_t>(site + 5);
            if (jump < INT32_MIN || jump > INT32_MAX) {
                ::VirtualFree(stub, 0, MEM_RELEASE);
                a_why = "the hook's memory is out of the call's reach";
                return false;
            }
            DWORD old = 0;
            if (!::VirtualProtect(reinterpret_cast<void*>(site + 1), 4, PAGE_EXECUTE_READWRITE, &old)) {
                ::VirtualFree(stub, 0, MEM_RELEASE);
                a_why = "the call couldn't be made writable";
                return false;
            }
            s.original = target;
            const auto rel = static_cast<std::int32_t>(jump);
            std::memcpy(reinterpret_cast<void*>(site + 1), &rel, sizeof(rel));
            ::VirtualProtect(reinterpret_cast<void*>(site + 1), 4, old, &old);
            ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(site), 5);
            SKSE::log::info("{}: game-thread pump: hooked the main loop's call to Main::Update (ID {} +0x{:X}, SkyrimSE.exe+0x{:X}); it called {}",
                            s.name, loopId, callOffset, site - base,
                            inGame ? std::format("Main::Update (ID {})", updateId) : std::format("another mod's hook in {}", ModuleOf(reinterpret_cast<const void*>(target))));
            return true;
        }

        // Frames stop reaching the hook only if another mod writes over the call without chaining.
        inline void Watch()
        {
            std::thread([]() {
                auto& s = S();
                for (;;) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    const auto now = NowMs();
                    const auto mode = s.mode.load();
                    if (mode == kHook) {
                        if (s.frames.load() == 0 && now - s.installedAt.load() > 10 * 60 * 1000) {
                            ToFallback("no frame has reached the hook in 10 minutes");
                        } else if (s.frames.load() > 0 && now - s.lastFrameAt.load() > 60 * 1000) {
                            ToFallback("no frame has reached the hook for a minute");
                        }
                    } else if (mode == kFallback && s.queue.Size()) {
                        for (const auto& job : s.queue.Take()) Fallback(job);
                    }
                }
            }).detach();
        }

        inline void Send(const Job& a_job)
        {
            auto& s = S();
            auto mode = s.mode.load();
            if (mode == kPending) {
                const std::lock_guard lock{ s.modeMutex };
                mode = s.mode.load();
                if (mode == kPending) {
                    s.queue.Push(a_job);
                    return;
                }
            }
            switch (mode) {
            case kHub:
                if (s.hub->RunOnGameThread(a_job.fn, a_job.ctx) != DOVAHHUB_API::Result::Ok) Fallback(a_job);
                break;
            case kHook:
                s.queue.Push(a_job);
                break;
            default:
                Fallback(a_job);
                break;
            }
        }

        // DovahHub's table, looked up here rather than with RequestApi so this header is the same whether or
        // not DovahHub_API.h's loader is compiled in.
        inline const DOVAHHUB_API::DovahHubApi1* FindHub()
        {
            const HMODULE module = ::GetModuleHandleW(L"DovahHub.dll");
            if (!module) return nullptr;
            using RequestFn = const void* (*)(std::uint32_t);
            const auto request = reinterpret_cast<RequestFn>(::GetProcAddress(module, "DovahHub_RequestApi"));
            return request ? static_cast<const DOVAHHUB_API::DovahHubApi1*>(request(DOVAHHUB_API::kApiVersion1)) : nullptr;
        }

        struct Box
        {
            std::function<void()> work;
            const char* where;
        };

        inline void RunBox(void* a_box)
        {
            const std::unique_ptr<Box> box(static_cast<Box*>(a_box));
            try {
                box->work();
            } catch (const std::exception& e) {
                SKSE::log::error("{}: game-thread work from {} failed: {}", S().name, box->where ? box->where : "?", e.what());
            } catch (...) {
                SKSE::log::error("{}: game-thread work from {} failed", S().name, box->where ? box->where : "?");
            }
        }
    }

    // Once, at kPostLoad (every plugin is loaded by then), with your mod's name for the log. a_host: DovahHub
    // itself, which is the pump the others use.
    inline void Install(const char* a_name, bool a_host = false)
    {
        auto& s = detail::S();
        const std::lock_guard lock{ s.modeMutex };
        if (s.mode.load() != detail::kPending) return;
        s.name = a_name ? a_name : "DovahGameThread";
        s.loadThread = static_cast<std::uint32_t>(::GetCurrentThreadId());
        if (!a_host) {
            const auto* hub = detail::FindHub();
            if (DOVAHHUB_API::HasPump(hub)) {
                s.hub = hub;
                s.mode = detail::kHub;
                SKSE::log::info("{}: game-thread work goes to DovahHub {}'s pump", s.name, hub->hostVersion);
                for (const auto& job : s.queue.Take()) detail::Send(job);
                return;
            }
        }
        std::string why;
        if (detail::InstallHook(why)) {
            s.installedAt = detail::NowMs();
            s.mode = detail::kHook;
            detail::Watch();
            return;
        }
        s.mode = detail::kFallback;
        SKSE::log::warn("{}: game-thread pump not installed: {}; SKSE's UI tasks run the work instead", s.name, why);
        for (const auto& job : s.queue.Take()) detail::Fallback(job);
    }

    // True while a frame hook (DovahHub's or this DLL's) runs the work.
    inline bool Pumping()
    {
        const auto mode = detail::S().mode.load();
        return mode == detail::kHub || mode == detail::kHook;
    }

    // The thread the game's frame update runs on; 0 before the first frame (or without a hook).
    inline std::uint32_t Id()
    {
        const auto& s = detail::S();
        if (s.mode.load() == detail::kHub) return s.hub->GameThreadId();
        return s.gameThread.load();
    }

    // True only on that thread.
    inline bool IsGameThread()
    {
        const auto id = Id();
        return id != 0 && id == static_cast<std::uint32_t>(::GetCurrentThreadId());
    }

    // Any thread: a_work runs on the game thread at the next frame, in posting order. a_where (a string that
    // lives for the session, like a literal) names it in the log.
    inline void Post(const char* a_where, std::function<void()> a_work)
    {
        auto* box = new detail::Box{ std::move(a_work), a_where };
        detail::Send({ &detail::RunBox, box, a_where });
    }

    // Now when this is the game thread, otherwise at the next frame.
    inline void Run(const char* a_where, std::function<void()> a_work)
    {
        if (IsGameThread()) {
            a_work();
            return;
        }
        Post(a_where, std::move(a_work));
    }

    // Logs once per place when game work runs off the game thread (judged against the frame's thread).
    inline void Note(const char* a_where)
    {
        const auto id = Id();
        if (id == 0 || IsGameThread()) return;
        static std::mutex mutex;
        static std::set<std::string> told;
        const std::lock_guard lock{ mutex };
        if (told.size() < 64 && told.insert(a_where ? a_where : "?").second) {
            SKSE::log::warn("{}: {} ran on thread {}, not the game thread {}", detail::S().name, a_where ? a_where : "?", ::GetCurrentThreadId(), id);
        }
    }
}
#endif
