// Example 03: memory allocation and thread use while stepping.
//
// It checks two things: that stepping a busy world makes no heap allocations,
// and that the physics work is spread across all worker threads.
//
//   Allocations are counted by this program, not the library. The global
//   operator new and delete are replaced below, so every allocation is counted,
//   whether it comes from ludifex, the standard library, or the physics
//   libraries.
//
//   Thread use is measured by the scheduler: each worker records the time it
//   spends running jobs, and the ratio to elapsed time shows how busy it was.
//
// It runs without a window or renderer, so the results show only the
// simulation. The timings it prints depend on the machine.

#include <ludifex/ludifex.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace
{

// Every allocation in the process, counted. Replacing these is allowed by the
// standard, and it is the only way to see allocations the library does not
// make itself, such as a vector growing inside a physics library or a string
// built for a log message.
std::atomic<uint64_t> g_Allocations{ 0 };
std::atomic<uint64_t> g_Frees{ 0 };
std::atomic<uint64_t> g_Bytes{ 0 };

int g_Failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("  [%s] %s\n", condition ? "pass" : "FAIL", description);
    if (!condition)
    {
        ++g_Failures;
    }
}

} // namespace

void* operator new(size_t size)
{
    g_Allocations.fetch_add(1, std::memory_order_relaxed);
    g_Bytes.fetch_add(size, std::memory_order_relaxed);

    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return memory;
}

void* operator new[](size_t size)
{
    return operator new(size);
}

void operator delete(void* memory) noexcept
{
    if (memory != nullptr)
    {
        g_Frees.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    operator delete(memory);
}

void operator delete(void* memory, size_t) noexcept
{
    operator delete(memory);
}

void operator delete[](void* memory, size_t) noexcept
{
    operator delete(memory);
}

int main()
{
    std::printf("\nAllocation and thread use\n");
    std::printf("=========================\n");

    // Quiet: a diagnostic would allocate a string and be counted as the
    // frame's allocation, which would be true but misleading.
    ludifex::SetMinimumLogLevel(ludifex::LogLevel::Error);

    // --- a world heavy enough to need every core ----------------------------

    ludifex::World3D world = ludifex::CreateWorld3D({
        .Gravity = { 0.0f, -10.0f, 0.0f },

        // Sleeping is what a real game wants and the opposite of what a
        // benchmark wants: a pile that settles and sleeps measures how fast
        // the solver does nothing.
        .EnableSleep = false,
        .WorkerCount = 0, // one per hardware thread
    });
    world.AddGround({ .Width = 200.0f, .Depth = 200.0f });

    // Four thousand bodies in a wide, shallow pile: enough islands for the
    // solver to spread across every worker.
    constexpr int Bodies = 4000;
    for (int index = 0; index < Bodies; ++index)
    {
        const int layer = index / 400;
        const int within = index % 400;
        world.AddBox({
            .Scale = { 0.5f, 0.5f, 0.5f },
            .Position = { static_cast<float>(within % 20) * 0.62f - 6.2f,
                          0.3f + static_cast<float>(layer) * 0.55f,
                          static_cast<float>(within / 20) * 0.62f - 6.2f },
            .Friction = 0.5f,
        });
    }
    world.ApplyPendingChanges();

    // Anything allocated once (the solver's islands, its contact and
    // constraint storage, and the scheduler's groups) is allocated during these
    // steps, so the measured steps start with everything already in place.
    std::vector<ludifex::Actor3D> bodies;
    bodies.reserve(Bodies);
    world.ForEachActor([&](ludifex::Actor3D& actor) {
        if (actor.GetName() != "Ground")
        {
            bodies.push_back(actor);
        }
    });

    // The pile is stirred during the warm-up so the measured steps start from
    // a busy state instead of a settled one. Stirring stops before the
    // measured windows, because it runs on this thread and would be counted
    // against the scheduler. Sleeping is off, so the solver still has the same
    // work every step.
    auto Stir = [&](int step) {
        const float phase = static_cast<float>(step) * 0.37f;
        for (size_t index = 0; index < bodies.size(); ++index)
        {
            const float wobble = std::sin(phase + static_cast<float>(index) * 0.011f);
            bodies[index].ApplyImpulse({ wobble * 0.02f, 0.06f, wobble * 0.015f });
        }
    };

    for (int step = 0; step < 180; ++step)
    {
        Stir(step);
        world.StepPhysics(1.0f / 60.0f);
    }

    std::printf("\n%d bodies across %u workers. Worker 0 is this thread. It runs\n"
                "jobs while it waits and also does the single-threaded parts of\n"
                "each step, so it reads lower than the pool threads.\n",
                Bodies + 1,
                static_cast<unsigned>(ludifex::GetWorkerUtilisation().size()));

    // --- allocation ---------------------------------------------------------

    constexpr int Frames = 240;

    // Two windows, because the question is whether a step allocates, not
    // whether anything was ever allocated. Something that grows once, such as a
    // pool reaching its final size, shows up only in the first window, while
    // something allocated every step shows up in both. The check below uses
    // the worse of the two.
    //
    // The counters are read inside the window and GetWorkerUtilisation outside
    // it, because it returns a vector, and that allocation would otherwise be
    // counted.
    std::vector<float> utilisation;
    uint64_t allocations = 0;
    uint64_t bytes = 0;
    double elapsed = 0.0;

    for (int window = 0; window < 2; ++window)
    {
        ludifex::GetWorkerUtilisation(); // opens the window

        const uint64_t allocationsBefore = g_Allocations.load();
        const uint64_t bytesBefore = g_Bytes.load();
        const auto started = std::chrono::steady_clock::now();

        for (int frame = 0; frame < Frames; ++frame)
        {
            world.StepPhysics(1.0f / 60.0f);
        }

        const auto ended = std::chrono::steady_clock::now();
        const uint64_t windowAllocations = g_Allocations.load() - allocationsBefore;
        const uint64_t windowBytes = g_Bytes.load() - bytesBefore;

        utilisation = ludifex::GetWorkerUtilisation();
        allocations = std::max(allocations, windowAllocations);
        bytes = std::max(bytes, windowBytes);
        elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(ended - started).count() / 1000.0;

        std::printf("\nwindow %d: %d steps in %.1f ms (%.2f ms a step), %llu allocations, %llu bytes\n",
                    window + 1, Frames, elapsed, elapsed / Frames,
                    static_cast<unsigned long long>(windowAllocations),
                    static_cast<unsigned long long>(windowBytes));
    }

    // --- cores --------------------------------------------------------------

    std::printf("\n");

    // Worker 0 is checked separately. It is the calling thread: it runs jobs
    // while it waits, but it also does the single-threaded parts of each step,
    // so it is always less busy than the pool threads, and including it would
    // make the spread look worse than it is.
    float total = 0.0f;
    float quietestPool = 1.0f;
    float busiestPool = 0.0f;
    float caller = 0.0f;

    for (size_t index = 0; index < utilisation.size(); ++index)
    {
        total += utilisation[index];
        if (index == 0)
        {
            caller = utilisation[index];
        }
        else
        {
            quietestPool = std::min(quietestPool, utilisation[index]);
            busiestPool = std::max(busiestPool, utilisation[index]);
        }
        std::printf("  %-16s %3.0f%%\n", index == 0 ? "calling thread" : "pool thread",
                    static_cast<double>(utilisation[index] * 100.0f));
    }

    const float average = utilisation.empty() ? 0.0f : total / static_cast<float>(utilisation.size());

    // How evenly the pool divided the work, which is the scheduler's job. The
    // absolute values depend on what else the machine is doing (with a
    // compiler running, every worker drops together), but the spread between
    // workers does not, so that is what the check uses.
    const float evenness = busiestPool > 0.0f ? quietestPool / busiestPool : 0.0f;

    std::printf("\naverage %.0f%%, pool evenness %.2f\n\n",
                static_cast<double>(average * 100.0f), static_cast<double>(evenness));

    Check(allocations == 0, "a steady frame allocates nothing at all");
    Check(utilisation.size() > 1, "the scheduler is running with more than one worker");
    Check(evenness > 0.85f, "the pool divides the work evenly rather than piling it on one");
    Check(quietestPool > 0.2f, "every pool thread is doing real work");
    Check(caller > 0.2f, "and the calling thread carries its share instead of waiting");

    // --- the profiler --------------------------------------------------------

    std::printf("\nWhere a frame goes\n");
    std::printf("------------------\n");

    ludifex::SetProfilingEnabled(true);
    for (int frame = 0; frame < 30; ++frame)
    {
        world.Update(1.0f / 60.0f);
    }

    const std::vector<ludifex::ProfileSection> profile = ludifex::GetProfile();
    for (const ludifex::ProfileSection& section : profile)
    {
        std::printf("  %-18s %6.2f ms  %d call%s\n", section.Name,
                    static_cast<double>(section.Milliseconds), section.Calls,
                    section.Calls == 1 ? "" : "s");
    }

    bool sawPhysics = false;
    bool sawUpdate = false;
    for (const ludifex::ProfileSection& section : profile)
    {
        sawPhysics = sawPhysics || std::string(section.Name) == "physics";
        sawUpdate = sawUpdate || std::string(section.Name) == "world update";
    }

    Check(!profile.empty() && sawUpdate && sawPhysics,
          "the profiler names the frame's own sections");

    ludifex::SetProfilingEnabled(false);
    Check(ludifex::GetProfile().empty(), "and says nothing once it is turned off");

    std::printf("\n%s\n\n", g_Failures == 0 ? "All checks passed." : "SOME CHECKS FAILED.");
    return g_Failures == 0 ? 0 : 1;
}
