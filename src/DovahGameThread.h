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
// should be one), or frames stop reaching it, the log says why and the work waits: SKSE's UI tasks run it only
// when they find themselves on the game thread, and frames run it when they come again. Game work never runs
// on another thread, however long it waits. What waits is bounded; past that, new work is refused (said once in
// the log) and freed, never run. It never stops the game.
//
// Runtimes: Skyrim SE 1.5.97 and AE (1.6.1170 included) with their Address Libraries, and Skyrim VR 1.4.15 with
// the VR Address Library (keyed by SE's IDs) when CommonLibSSE-NG is built with VR. The call is looked for where
// it is in each runtime; if it isn't there, it's found in the main loop by its target (the one call to
// Main::Update), and if that fails too nothing is written: the log says why and the work falls back as above.
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
    // drop: frees ctx when the job is refused and will never run; nullptr when the caller keeps ctx then.
    struct Job
    {
        JobFn fn = nullptr;
        void* ctx = nullptr;
        const char* where = nullptr;
        JobFn drop = nullptr;
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

        // Push, unless a_max are queued already: then false and nothing is queued.
        bool PushBounded(const Job& a_job, std::size_t a_max)
        {
            const std::lock_guard lock{ mutex_ };
            if (jobs_.size() >= a_max) return false;
            jobs_.push_back(a_job);
            return true;
        }

        // Work taken earlier and handed back, ahead of anything queued since, in its order.
        void Return(std::vector<Job> a_jobs)
        {
            if (a_jobs.empty()) return;
            const std::lock_guard lock{ mutex_ };
            a_jobs.insert(a_jobs.end(), jobs_.begin(), jobs_.end());
            jobs_.swap(a_jobs);
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

    // How many jobs may wait for the game thread while no frame hook runs them. Past it, new work is refused.
    inline constexpr std::size_t kHeldMax = 4096;
    // A UI task that finds itself off the game thread looks again this many times, a frame apart, before
    // leaving it to the next post or the watch (every few seconds).
    inline constexpr int kQuickLooks = 8;

    // Whether a_me is the game thread: the thread frames run on once one has been seen (a_frame), before that
    // RE::Main's threadID (a_main). An unknown thread (0) is never the game thread.
    inline constexpr bool Verified(std::uint32_t a_me, std::uint32_t a_frame, std::uint32_t a_main)
    {
        return a_me != 0 && (a_frame != 0 ? a_me == a_frame : a_me == a_main);
    }

    // Work for the game thread while no frame hook runs it: held in posting order, bounded, until a pass on a
    // verified game thread runs it. Nothing here runs a job anywhere else, however long it waits. Each job runs
    // once (a pass takes what it runs), or, refused when full, never: then its drop frees it.
    class Held
    {
    public:
        explicit Held(std::size_t a_max = kHeldMax) : max_(a_max) {}

        // False when full: the job was refused, and its drop has run (when it has one; otherwise ctx is still
        // the caller's).
        bool Hold(const Job& a_job)
        {
            if (queue_.PushBounded(a_job, max_)) return true;
            refused_.fetch_add(1);
            if (a_job.drop) a_job.drop(a_job.ctx);
            return false;
        }

        // Work accepted elsewhere (the hook's queue, or a hand-over that failed): kept, ahead of what came since,
        // even past the bound, since it was already accepted.
        void Return(std::vector<Job> a_jobs) { queue_.Return(std::move(a_jobs)); }

        std::vector<Job> Take() { return queue_.Take(); }
        std::size_t Size() const { return queue_.Size(); }
        std::uint64_t Refused() const { return refused_.load(); }

        // One pass on thread a_me: everything held when it started runs if a_me is the game thread
        // (Verified), otherwise nothing does.
        Drained Pass(std::uint32_t a_me, std::uint32_t a_frame, std::uint32_t a_main)
        {
            if (!Verified(a_me, a_frame, a_main)) return {};
            return queue_.Drain();
        }

    private:
        Queue queue_;
        std::size_t max_;
        std::atomic<std::uint64_t> refused_{ 0 };
    };
}

namespace DovahGameThread::detail
{
    // The main loop's call to Main::Update, by address library ID: Skyrim SE 1.5.97, then AE (1.6.1170
    // included). VR's Address Library uses SE's IDs. Each is checked before anything is written.
    inline constexpr std::uint64_t kLoopIdSE = 35551, kLoopIdAE = 36544;
    inline constexpr std::uintptr_t kCallOffsetSE = 0x11F, kCallOffsetAE = 0x160;
    inline constexpr std::uint64_t kUpdateIdSE = 35565, kUpdateIdAE = 36564;
    // VR's main loop is built as SE's: this offset is tried first there, then the call is looked for.
    inline constexpr std::uintptr_t kCallOffsetVR = 0x11F;
    // How far into the main loop the call is looked for when it isn't at its offset.
    inline constexpr std::size_t kLoopScanBytes = 0x400;
    inline constexpr std::size_t kNoCall = static_cast<std::size_t>(-1);

    enum class Runtime { SE, AE, VR };
    struct Site
    {
        std::uint64_t loopId = 0;
        std::uint64_t updateId = 0;
        std::uintptr_t callOffset = 0;  // where the call is expected; tried first
    };

    // The IDs and the offset to try first, for each runtime.
    inline constexpr Site SiteFor(Runtime a_runtime)
    {
        return a_runtime == Runtime::AE ? Site{ kLoopIdAE, kUpdateIdAE, kCallOffsetAE } :
               a_runtime == Runtime::VR ? Site{ kLoopIdSE, kUpdateIdSE, kCallOffsetVR } :
                                          Site{ kLoopIdSE, kUpdateIdSE, kCallOffsetSE };
    }

    inline const char* RuntimeName(Runtime a_runtime)
    {
        return a_runtime == Runtime::AE ? "AE" : a_runtime == Runtime::VR ? "VR" : "SE";
    }

    // Where the 5-byte call (E8 rel32) at a_code[a_at] goes; a_codeAddress is the address of a_code[0].
    inline std::uintptr_t CallTarget(const std::uint8_t* a_code, std::uintptr_t a_codeAddress, std::size_t a_at)
    {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, a_code + a_at + 1, sizeof(displacement));
        return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(a_codeAddress + a_at + 5) + displacement);
    }

    // The main loop's call to Main::Update in a_code (its first a_size bytes, at a_codeAddress): the offset of the
    // one 5-byte call whose target is a_update. kNoCall when there's none, or more than one (then nothing is
    // certain, so nothing is written).
    inline std::size_t FindUpdateCall(const std::uint8_t* a_code, std::size_t a_size, std::uintptr_t a_codeAddress, std::uintptr_t a_update)
    {
        std::size_t found = kNoCall;
        for (std::size_t at = 0; at + 5 <= a_size; ++at) {
            if (a_code[at] != 0xE8 || CallTarget(a_code, a_codeAddress, at) != a_update) continue;
            if (found != kNoCall) return kNoCall;
            found = at;
        }
        return found;
    }
}

#ifndef DOVAH_GAME_THREAD_QUEUE_ONLY
#include "DovahHub_API.h"

namespace DovahGameThread
{
    namespace detail
    {
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
            kFallback,  // no hook: held until a verified game-thread pass (a UI task there, or a frame again)
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
            // The fallback (DHUB-123): held work, the one UI task looking for the game thread, said-once lines.
            Held held;
            std::atomic<bool> looking{ false };
            std::atomic<int> misses{ 0 };
            std::atomic<bool> fallbackTold{ false };
            std::atomic<bool> framesAgainTold{ false };
            std::atomic<bool> offThreadTold{ false };
            std::atomic<bool> noTasksTold{ false };
            std::atomic<bool> fullTold{ false };
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

        // RE::Main's threadID, or 0 when the address library lacks RE::Main's ID (asking would stop the game)
        // or the game hasn't made it yet.
        inline std::uint32_t MainThreadId()
        {
            static std::once_flag once;
            static bool known = false;
            std::call_once(once, []() {
                const std::uint64_t id = REL::Module::IsAE() ? 403449 : 516943;  // RE::Main's singleton; VR uses SE's
                known = HasIds({ id })[0];
                if (!known) {
                    SKSE::log::warn("{}: game-thread pump: the address library has no ID {} (RE::Main), so only a frame's thread counts as the game thread",
                                    S().name, id);
                }
            });
            if (!known) return 0;
            const auto* main = RE::Main::GetSingleton();
            return main ? static_cast<std::uint32_t>(main->threadID) : 0u;
        }

        // The thread frames run on, as the pump that runs this DLL's work sees it; 0 before the first frame.
        inline std::uint32_t FrameThread()
        {
            const auto& s = S();
            return s.hub ? s.hub->GameThreadId() : s.gameThread.load();
        }

        // Once per session: the fallback is in use, and why.
        inline void TellFallback(const std::string& a_why)
        {
            auto& s = S();
            if (s.fallbackTold.exchange(true)) return;
            SKSE::log::warn("{}: game-thread pump: fallback in use ({}): work waits until a UI task finds itself on the game thread, "
                            "or frames come again; it never runs on another thread (said once)",
                            s.name, a_why);
        }

        inline void Look();

        // On a UI task: runs what's held if this is the game thread; otherwise looks again a frame later, a few
        // times, then leaves it to the next post or the watch.
        inline void LookHere()
        {
            auto& s = S();
            s.looking = false;
            const auto me = static_cast<std::uint32_t>(::GetCurrentThreadId());
            const auto frame = FrameThread(), main = MainThreadId();
            if (Verified(me, frame, main)) {
                s.misses = 0;
                s.held.Pass(me, frame, main);
                return;
            }
            if (!s.offThreadTold.exchange(true)) {
                SKSE::log::warn("{}: game-thread pump: a UI task ran on thread {}, not the game thread ({}); waiting work stays queued until one runs there (said once)",
                                s.name, me, frame ? frame : main);
            }
            if (s.misses.fetch_add(1) < kQuickLooks) {
                std::thread([]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    Look();
                }).detach();
            }
        }

        // One UI task at a time looks for the game thread while work is held.
        inline void Look()
        {
            auto& s = S();
            if (!s.held.Size() || s.looking.exchange(true)) return;
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                s.looking = false;
                if (!s.noTasksTold.exchange(true)) {
                    SKSE::log::error("{}: game-thread pump: SKSE's task interface isn't there; waiting work runs when frames come again (said once)", s.name);
                }
                return;
            }
            tasks->AddUITask([]() { LookHere(); });
        }

        inline void Watch();

        // No hook runs this job now: hold it for the game thread. False when refused (the queue is full); then
        // its drop has run, or without one its ctx is still the caller's.
        inline bool Hold(const Job& a_job, const std::string& a_why)
        {
            auto& s = S();
            TellFallback(a_why);
            if (!s.held.Hold(a_job)) {
                if (!s.fullTold.exchange(true)) {
                    SKSE::log::error("{}: game-thread pump: {} jobs are already waiting for the game thread; work from {} was refused, not run (said once)",
                                     s.name, s.held.Size(), Where(a_job));
                }
                return false;
            }
            Watch();
            Look();
            return true;
        }

        // Accepted work moves to the fallback (the hook's queue when frames stop): never refused.
        inline void HoldAll(std::vector<Job> a_jobs, const std::string& a_why)
        {
            auto& s = S();
            TellFallback(a_why);
            s.held.Return(std::move(a_jobs));
            Watch();
            Look();
        }

        inline void ToFallback(const char* a_why)
        {
            auto& s = S();
            const std::lock_guard lock{ s.modeMutex };
            if (s.mode.load() != kHook) return;
            s.mode = kFallback;
            HoldAll(s.queue.Take(), a_why);
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
                    if (!s.framesAgainTold.exchange(true)) {
                        SKSE::log::info("{}: game-thread pump: frames again; the pump runs the work again (said once)", s.name);
                    }
                }
            }
            // Work held while frames were away runs first: it was posted earlier. Only on the frames' own thread.
            if (s.held.Size()) s.held.Pass(me, s.gameThread.load(), 0);
            const auto drained = s.queue.Drain();
            if (drained.ms > kSlowMs && !s.slowTold.exchange(true)) {
                SKSE::log::warn("{}: game-thread pump: one frame's work took {:.1f} ms ({} jobs); the slowest, {:.1f} ms, came from {} (said once)",
                                s.name, drained.ms, drained.ran, drained.slowestMs, Where(drained.slowest));
            }
            reinterpret_cast<void (*)(void*)>(s.original)(a_main);
        }

        inline void Hooked(void* a_main) { Frame(a_main); }

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
        // Executable throughout [a_address, a_address + a_size).
        inline bool ExecutableRange(std::uintptr_t a_address, std::size_t a_size)
        {
            for (std::uintptr_t at = a_address; at < a_address + a_size;) {
                if (!Executable(at)) return false;
                MEMORY_BASIC_INFORMATION info{};
                if (!::VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info))) return false;
                at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            }
            return true;
        }

        inline bool InstallHook(std::string& a_why)
        {
            auto& s = S();
            const auto runtime = REL::Module::IsVR() ? Runtime::VR : REL::Module::IsAE() ? Runtime::AE : Runtime::SE;
            // Once, before anything can fail: which game this is and which relocations the pump uses for it.
            const auto game = REL::Module::get().version();
            SKSE::log::info("{}: game-thread pump: game {}.{}.{}, relocations {}", s.name, game.major(), game.minor(), game.patch(), RuntimeName(runtime));
            const auto expected = SiteFor(runtime);
            const auto loopId = expected.loopId, updateId = expected.updateId;
            auto callOffset = expected.callOffset;
            const auto found = HasIds({ loopId, updateId });
            if (!found[0]) {
                a_why = std::format("the {} address library has no ID {} (the main loop)", RuntimeName(runtime), loopId);
                return false;
            }
            const auto base = REL::Module::get().base();
            const auto loop = base + AddressLibrary::get().id2offset(loopId);
            const auto update = found[1] ? base + AddressLibrary::get().id2offset(updateId) : 0;
            const auto text = REL::Module::get().segment(REL::Segment::textx);
            const auto inGame = [&](std::uintptr_t a_address) { return a_address >= text.address() && a_address < text.address() + text.size(); };
            auto site = loop + callOffset;
            if (!Executable(site)) {
                a_why = std::format("ID {} +0x{:X} isn't code", loopId, callOffset);
                return false;
            }
            // The call is at its offset when it calls Main::Update, or another mod's hook chained on it (outside the
            // game). Otherwise this runtime has it elsewhere: the one call to Main::Update in the main loop.
            const auto* code = reinterpret_cast<const std::uint8_t*>(site);
            const bool atOffset = code[0] == 0xE8 && (!inGame(CallTarget(code, site, 0)) || (update && CallTarget(code, site, 0) == update));
            if (!atOffset) {
                if (!update) {
                    a_why = std::format("ID {} +0x{:X} isn't the call, and the address library has no ID {} (Main::Update) to look for it by",
                                        loopId, callOffset, updateId);
                    return false;
                }
                if (!ExecutableRange(loop, kLoopScanBytes)) {
                    a_why = std::format("ID {} +0x{:X} isn't the call, and the main loop can't be read to look for it", loopId, callOffset);
                    return false;
                }
                const auto at = FindUpdateCall(reinterpret_cast<const std::uint8_t*>(loop), kLoopScanBytes, loop, update);
                if (at == kNoCall) {
                    a_why = std::format("ID {} +0x{:X} isn't the call, and the main loop's first 0x{:X} bytes hold no single call to Main::Update (ID {})",
                                        loopId, callOffset, kLoopScanBytes, updateId);
                    return false;
                }
                SKSE::log::info("{}: game-thread pump: on {} the call to Main::Update is at ID {} +0x{:X}, not +0x{:X}", s.name, RuntimeName(runtime),
                                loopId, at, callOffset);
                callOffset = at;
                site = loop + at;
                code = reinterpret_cast<const std::uint8_t*>(site);
            }
            const auto target = CallTarget(code, site, 0);
            if (!Executable(target)) {
                a_why = std::format("ID {} +0x{:X} calls 0x{:X}, which isn't code", loopId, callOffset, target);
                return false;
            }
            // Inside the game it must be Main::Update itself; outside, another mod's hook chained on it.
            const bool inGameTarget = inGame(target);
            if (inGameTarget && target != update) {
                a_why = std::format("ID {} +0x{:X} calls exe+0x{:X}, not Main::Update (ID {})", loopId, callOffset, target - base, updateId);
                return false;
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
            SKSE::log::info("{}: game-thread pump: hooked the main loop's call to Main::Update on {} (ID {} +0x{:X}, exe+0x{:X}); it called {}",
                            s.name, RuntimeName(runtime), loopId, callOffset, site - base,
                            inGameTarget ? std::format("Main::Update (ID {})", updateId) : std::format("another mod's hook in {}", ModuleOf(reinterpret_cast<const void*>(target))));
            return true;
        }

        // Held work offered to DovahHub's pump again, in order; what it still refuses stays held, ahead.
        inline void OfferHub()
        {
            auto& s = S();
            auto jobs = s.held.Take();
            std::vector<Job> back;
            for (const auto& job : jobs) {
                if (!back.empty() || s.hub->RunOnGameThread(job.fn, job.ctx) != DOVAHHUB_API::Result::Ok) back.push_back(job);
            }
            s.held.Return(std::move(back));
        }

        // Every few seconds, once started: frames stop reaching the hook only if another mod writes over the
        // call without chaining. While work is held, it's offered to DovahHub's pump again (when that's what
        // refused it) and a UI task looks for the game thread again.
        inline void Watch()
        {
            static std::once_flag once;
            std::call_once(once, []() {
                std::thread([]() {
                    auto& s = S();
                    for (;;) {
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        const auto now = NowMs();
                        if (s.mode.load() == kHook) {
                            if (s.frames.load() == 0 && s.installedAt.load() && now - s.installedAt.load() > 10 * 60 * 1000) {
                                ToFallback("no frame has reached the hook in 10 minutes");
                            } else if (s.frames.load() > 0 && now - s.lastFrameAt.load() > 60 * 1000) {
                                ToFallback("no frame has reached the hook for a minute");
                            }
                        }
                        if (s.held.Size()) {
                            if (s.mode.load() == kHub) OfferHub();
                            s.misses = 0;
                            Look();
                        }
                    }
                }).detach();
            });
        }

        // True when the job will run once on the game thread. False when it was refused: then its drop has run,
        // or without one its ctx is still the caller's.
        inline bool Send(const Job& a_job)
        {
            auto& s = S();
            auto mode = s.mode.load();
            if (mode == kPending) {
                const std::lock_guard lock{ s.modeMutex };
                mode = s.mode.load();
                if (mode == kPending) {
                    s.queue.Push(a_job);
                    return true;
                }
            }
            switch (mode) {
            case kHub:
                if (s.hub->RunOnGameThread(a_job.fn, a_job.ctx) == DOVAHHUB_API::Result::Ok) return true;
                return Hold(a_job, "DovahHub's pump refused the work");
            case kHook:
                s.queue.Push(a_job);
                return true;
            default:
                return Hold(a_job, "no frame hook runs the work");
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

        inline void DropBox(void* a_box) { delete static_cast<Box*>(a_box); }

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
                std::vector<detail::Job> refused;
                for (const auto& job : s.queue.Take()) {
                    if (!refused.empty() || hub->RunOnGameThread(job.fn, job.ctx) != DOVAHHUB_API::Result::Ok) refused.push_back(job);
                }
                if (!refused.empty()) detail::HoldAll(std::move(refused), "DovahHub's pump refused the work");
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
        SKSE::log::warn("{}: game-thread pump not installed: {}", s.name, why);
        detail::HoldAll(s.queue.Take(), "the hook isn't installed");
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
    // lives for the session, like a literal) names it in the log. Without a frame hook it waits for the game
    // thread; refused (the log says so, once) only when thousands are already waiting, and then never run.
    inline void Post(const char* a_where, std::function<void()> a_work)
    {
        auto* box = new detail::Box{ std::move(a_work), a_where };
        detail::Send({ &detail::RunBox, box, a_where, &detail::DropBox });
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
