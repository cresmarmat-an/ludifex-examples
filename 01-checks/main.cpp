// Example 01: ludifex on its own, without a window.
//
// This program links ludifex without opane.
//
// It checks the basic behaviour the rest of the library depends on:
//   1. Mutability: destroying actors while iterating over them is safe.
//   2. Handle safety: a handle to a destroyed actor is detected, and using it
//      does nothing instead of crashing.
//   3. Determinism: the same inputs give identical results.
//   4. Validation: invalid values are rejected before they reach the solver.
//   5. Filtering: collision layers exclude the pairs they should.
//   6. Threading: stepping on several threads gives the same result as on one,
//      without deadlocking.
//
// Later sections cover the rest of the API, including model formats other than
// glTF (section 18) and the graphics quality presets (section 19).

#include <ludifex/ludifex.h>

#include "../common/CrashReport.h"
#include "../common/Gltf.h"
#include "../common/Png.h"
#include "../common/GltfMorph.h"
#include "../common/GltfSkinned.h"
#include "../common/ModelFormats.h"
#include "../common/Tone.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{

int g_Warnings = 0;
int g_Errors = 0;
int g_Failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("  [%s] %s\n", condition ? "pass" : "FAIL", description);
    if (!condition)
    {
        ++g_Failures;
    }
}

// Builds the same scene every time and returns the final positions. Two calls
// must agree exactly.
std::vector<ludifex::Vec2> RunSimulation(int steps)
{
    ludifex::World2D world = ludifex::CreateWorld2D({
        .Deterministic = true,
    });

    world.AddGround({ .Width = 40.0f });

    for (int i = 0; i < 16; ++i)
    {
        const float x = static_cast<float>(i % 4) * 0.55f - 0.8f;
        const float y = 1.0f + static_cast<float>(i) * 0.6f;

        world.AddCircle({
            .Radius = 0.25f,
            .Position = { x, y },
            .Restitution = 0.35f,
        });
    }

    for (int i = 0; i < steps; ++i)
    {
        world.StepPhysics(1.0f / 60.0f);
    }

    std::vector<ludifex::Vec2> positions;
    world.ForEachActor([&](ludifex::Actor2D& actor) { positions.push_back(actor.GetPosition()); });
    return positions;
}

} // namespace

// The same 3D scene, every time: a tumbling stack with a little sideways push,
// which is the kind of thing that diverges first when anything is not
// deterministic.
std::vector<ludifex::Vec3> RunSimulation3D(int steps)
{
    ludifex::World3D world = ludifex::CreateWorld3D({
        .Deterministic = true,
    });

    world.AddGround({ .Width = 40.0f, .Depth = 40.0f });

    std::vector<ludifex::Actor3D> bodies;
    for (int index = 0; index < 24; ++index)
    {
        const float x = static_cast<float>(index % 4) * 0.62f - 0.93f;
        const float z = static_cast<float>((index / 4) % 2) * 0.62f - 0.31f;
        const float y = 0.6f + static_cast<float>(index) * 0.7f;

        ludifex::Actor3D box = world.AddBox({
            .Scale = { 0.6f, 0.6f, 0.6f },
            .Position = { x, y, z },
            .Restitution = 0.1f,
        });
        box.ApplyImpulse({ 0.02f * static_cast<float>(index % 3), 0.0f, 0.01f });
        bodies.push_back(box);
    }

    for (int step = 0; step < steps; ++step)
    {
        world.StepPhysics(1.0f / 60.0f);
    }

    std::vector<ludifex::Vec3> positions;
    positions.reserve(bodies.size());
    for (ludifex::Actor3D& body : bodies)
    {
        positions.push_back(body.GetPosition());
    }
    return positions;
}

int main()
{
    examples::InstallCrashReport();

    ludifex::SetLogHandler([](ludifex::LogLevel level, const char* category, const char* message) {
        if (level == ludifex::LogLevel::Warning)
        {
            ++g_Warnings;
        }
        else if (level == ludifex::LogLevel::Error)
        {
            ++g_Errors;
        }
        std::printf("      (%s) %s\n", category, message);
    });

    std::printf("ludifex %s\n", ludifex::VersionString);

    // -----------------------------------------------------------------------
    std::printf("\n1. Mutability contract\n");

    ludifex::World3D world = ludifex::CreateWorld3D();
    world.AddGround({ .Width = 40.0f, .Depth = 40.0f });

    std::vector<ludifex::Actor3D> cubes;
    for (int i = 0; i < 12; ++i)
    {
        cubes.push_back(world.AddBox({
            .Scale = { 0.5f, 0.5f, 0.5f },
            .Position = { static_cast<float>(i % 4) * 0.8f - 1.2f, 2.0f + static_cast<float>(i) * 0.9f, 0.0f },
        }));
    }

    const size_t countAfterCreation = world.GetActorCount();
    Check(countAfterCreation == 13, "13 actors exist after creating a ground and 12 cubes");

    for (int i = 0; i < 60; ++i)
    {
        world.StepPhysics(1.0f / 60.0f);
    }

    // Destroying from inside the iteration is the case that breaks naive
    // engines. Here the destruction is queued and applied at the sync point,
    // so the iteration walks a stable structure.
    int destroyed = 0;
    world.ForEachActor([&](ludifex::Actor3D& actor) {
        if (actor.GetName() != "Ground" && destroyed < 6)
        {
            actor.Destroy();
            ++destroyed;
        }
    });

    Check(world.GetActorCount() == countAfterCreation,
          "destruction is deferred, so the count is unchanged before the sync point");

    world.ApplyPendingChanges();

    Check(world.GetActorCount() == countAfterCreation - 6,
          "the 6 destroyed actors are gone after the sync point");

    // The world must keep stepping cleanly after a structural change.
    for (int i = 0; i < 60; ++i)
    {
        world.StepPhysics(1.0f / 60.0f);
    }
    Check(world.GetStepCount() == 120, "the world stepped 120 times in total");

    // -----------------------------------------------------------------------
    std::printf("\n2. Handle safety\n");

    ludifex::Actor3D stale = cubes.front();
    const bool wasValidBefore = !stale.IsValid();
    Check(wasValidBefore, "a handle to a destroyed actor reports IsValid() == false");

    const int warningsBefore = g_Warnings;
    stale.SetPosition({ 0.0f, 100.0f, 0.0f });
    const ludifex::Vec3 readBack = stale.GetPosition();

    Check(g_Warnings > warningsBefore, "using a stale handle reports a diagnostic");
    Check(readBack.X == 0.0f && readBack.Y == 0.0f && readBack.Z == 0.0f,
          "a stale read returns a zero value instead of crashing");

    ludifex::Actor3D never;
    Check(!never.IsValid(), "a default-constructed actor is invalid");
    never.ApplyImpulse({ 1.0f, 1.0f, 1.0f });

    // -----------------------------------------------------------------------
    std::printf("\n3. Determinism\n");

    const std::vector<ludifex::Vec2> firstRun = RunSimulation(180);
    const std::vector<ludifex::Vec2> secondRun = RunSimulation(180);

    Check(firstRun.size() == secondRun.size(), "both runs produced the same actor count");

    bool identical = firstRun.size() == secondRun.size();
    for (size_t i = 0; identical && i < firstRun.size(); ++i)
    {
        identical = (firstRun[i].X == secondRun[i].X) && (firstRun[i].Y == secondRun[i].Y);
    }

    Check(identical, "180 steps of a 17-body 2D world reproduce bit-for-bit");

    // -----------------------------------------------------------------------
    std::printf("\n4. Validation\n");

    const int warningsBeforeValidation = g_Warnings;
    ludifex::Actor3D rejected = world.AddBox({ .Scale = { 0.0f, 1.0f, 1.0f } });
    Check(!rejected.IsValid(), "a zero-width box is rejected rather than fed to the solver");

    ludifex::Actor3D good = world.AddBox({ .Position = { 0.0f, 5.0f, 0.0f } });
    Check(good.IsValid(), "a valid box is still created after a rejected one");

    const float infinity = std::numeric_limits<float>::infinity();
    good.SetPosition({ infinity, 0.0f, 0.0f });
    Check(good.GetPosition().X != infinity, "a non-finite position is rejected at the setter");
    (void)warningsBeforeValidation;

    // -----------------------------------------------------------------------
    std::printf("\n5. Collision filtering\n");

    // Two boxes start overlapping. With default filters the solver pushes them
    // apart; with masks that exclude each other it should leave them alone.
    auto SeparationAfterSteps = [](bool filtered) {
        ludifex::World3D filterWorld = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

        ludifex::Actor3D first = filterWorld.AddBox({ .Position = { 0.0f, 0.0f, 0.0f } });
        ludifex::Actor3D second = filterWorld.AddBox({ .Position = { 0.2f, 0.0f, 0.0f } });

        if (filtered)
        {
            constexpr uint64_t LayerA = 1ull << 0;
            constexpr uint64_t LayerB = 1ull << 1;

            first.SetCollisionFilter(LayerA, ~LayerB);
            second.SetCollisionFilter(LayerB, ~LayerA);
        }

        for (int step = 0; step < 60; ++step)
        {
            filterWorld.StepPhysics(1.0f / 60.0f);
        }

        const ludifex::Vec3 a = first.GetPosition();
        const ludifex::Vec3 b = second.GetPosition();
        const float dx = b.X - a.X;
        const float dy = b.Y - a.Y;
        const float dz = b.Z - a.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    const float unfiltered = SeparationAfterSteps(false);
    const float filtered = SeparationAfterSteps(true);

    std::printf("      (separation: %.3f m unfiltered, %.3f m filtered)\n",
                static_cast<double>(unfiltered), static_cast<double>(filtered));

    Check(unfiltered > 0.9f, "overlapping boxes push apart when nothing filters them");
    Check(filtered < 0.25f, "boxes on mutually excluded layers pass through each other");

    // -----------------------------------------------------------------------
    std::printf("\n6. Multithreaded stepping\n");

    // The same pile, stepped single-threaded and then across the shared
    // scheduler. Thread count changes how the solver splits its work, so the
    // results are compared by where the pile ends up rather than bit for bit.
    auto SettlePile = [](uint32_t workers, uint32_t& outWorkersUsed) {
        ludifex::World3D pile = ludifex::CreateWorld3D({ .WorkerCount = workers });
        outWorkersUsed = pile.GetWorkerCount();

        pile.AddGround({ .Width = 60.0f, .Depth = 60.0f });

        for (int index = 0; index < 300; ++index)
        {
            pile.AddBox({
                .Scale = { 0.5f, 0.5f, 0.5f },
                .Position = { static_cast<float>(index % 10) * 0.6f - 2.7f,
                              1.0f + static_cast<float>(index / 10) * 0.7f,
                              static_cast<float>(index % 7) * 0.6f - 1.8f },
            });
        }

        for (int step = 0; step < 150; ++step)
        {
            pile.StepPhysics(1.0f / 60.0f);
        }

        double total = 0.0;
        int counted = 0;
        bool finite = true;

        pile.ForEachActor([&](ludifex::Actor3D& actor) {
            const ludifex::Vec3 position = actor.GetPosition();
            finite = finite && std::isfinite(position.X) && std::isfinite(position.Y) &&
                     std::isfinite(position.Z);
            total += position.Y;
            ++counted;
        });

        return std::pair<double, bool>{ counted > 0 ? total / counted : 0.0, finite };
    };

    uint32_t singleWorkers = 0;
    uint32_t manyWorkers = 0;

    const auto single = SettlePile(1, singleWorkers);
    const auto many = SettlePile(4, manyWorkers);

    std::printf("      (average height: %.3f m on %u worker, %.3f m on %u workers)\n", single.first,
                singleWorkers, many.first, manyWorkers);

    Check(manyWorkers > 1, "asking for 4 workers starts the scheduler and reports more than one");
    Check(single.second && many.second, "301 bodies stay finite under both");
    Check(std::abs(single.first - many.first) < 0.5,
          "the pile settles to the same place however many threads stepped it");

    uint32_t deterministicWorkers = 0;
    {
        ludifex::World3D fixed = ludifex::CreateWorld3D({ .Deterministic = true, .WorkerCount = 8 });
        deterministicWorkers = fixed.GetWorkerCount();
    }
    Check(deterministicWorkers == 1, "a deterministic world pins itself to one worker");

    // -----------------------------------------------------------------------
    std::printf("\n7. Joints\n");

    {
        ludifex::World3D joints = ludifex::CreateWorld3D();

        auto Distance = [](const ludifex::Vec3& a, const ludifex::Vec3& b) {
            const float dx = b.X - a.X;
            const float dy = b.Y - a.Y;
            const float dz = b.Z - a.Z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        // A pendulum on a distance joint. The bob swings, but the rope holds.
        ludifex::Actor3D anchor =
            joints.AddBox({ .Position = { 0.0f, 6.0f, 0.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D bob =
            joints.AddSphere({ .Radius = 0.3f, .Position = { 2.5f, 6.0f, 0.0f } });

        joints.AddDistanceJoint({
            .BodyA = anchor,
            .BodyB = bob,
            .AnchorA = { 0.0f, 6.0f, 0.0f },
            .AnchorB = { 2.5f, 6.0f, 0.0f },
        });

        // A motorised hinge.
        ludifex::Actor3D base =
            joints.AddBox({ .Position = { 10.0f, 6.0f, 0.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D arm = joints.AddBox({
            .Scale = { 2.0f, 0.2f, 0.2f },
            .Position = { 11.0f, 6.0f, 0.0f },
        });

        ludifex::Joint3D motor = joints.AddHinge({
            .BodyA = base,
            .BodyB = arm,
            .Anchor = { 10.0f, 6.0f, 0.0f },
            .Axis = { 0.0f, 0.0f, 1.0f },
            .EnableMotor = true,
            .MotorSpeed = 4.0f,
            .MaxMotorTorque = 2000.0f,
        });

        // A hinge held inside a narrow limit, with gravity pulling on it.
        ludifex::Actor3D pivot =
            joints.AddBox({ .Position = { 20.0f, 6.0f, 0.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D flap = joints.AddBox({
            .Scale = { 2.0f, 0.2f, 0.2f },
            .Position = { 21.0f, 6.0f, 0.0f },
        });

        ludifex::Joint3D limited = joints.AddHinge({
            .BodyA = pivot,
            .BodyB = flap,
            .Anchor = { 20.0f, 6.0f, 0.0f },
            .Axis = { 0.0f, 0.0f, 1.0f },
            .EnableLimit = true,
            .LowerAngle = -0.2f,
            .UpperAngle = 0.2f,
        });

        // A weld, which should hold its body in place against gravity.
        ludifex::Actor3D post =
            joints.AddBox({ .Position = { 30.0f, 6.0f, 0.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D welded = joints.AddBox({ .Position = { 31.0f, 6.0f, 0.0f } });

        ludifex::Joint3D weld = joints.AddWeld({
            .BodyA = post,
            .BodyB = welded,
            .Anchor = { 30.5f, 6.0f, 0.0f },
        });

        Check(joints.GetJointCount() == 4, "four joints exist after creating them");

        for (int step = 0; step < 180; ++step)
        {
            joints.StepPhysics(1.0f / 60.0f);
        }

        const float ropeLength = Distance(anchor.GetPosition(), bob.GetPosition());
        std::printf("      (rope %.3f m, hinge %.2f rad, limited %.3f rad, weld drift %.4f m)\n",
                    static_cast<double>(ropeLength), static_cast<double>(motor.GetAngle()),
                    static_cast<double>(limited.GetAngle()),
                    static_cast<double>(Distance(welded.GetPosition(),
                                                 ludifex::Vec3{ 31.0f, 6.0f, 0.0f })));

        Check(std::abs(ropeLength - 2.5f) < 0.1f,
              "a distance joint holds its length while the bob swings");
        Check(std::abs(motor.GetAngle()) > 0.3f, "a motorised hinge turns its arm");
        Check(std::abs(motor.GetMotorEffort()) > 0.0f,
              "a working motor reports the effort it is applying");
        Check(std::abs(limited.GetAngle()) < 0.35f, "a hinge limit holds the arm inside its range");
        Check(Distance(welded.GetPosition(), ludifex::Vec3{ 31.0f, 6.0f, 0.0f }) < 0.1f,
              "a weld holds its body against gravity");

        // Box3D destroys a joint when either body goes; the handle must notice.
        welded.Destroy();
        joints.ApplyPendingChanges();

        Check(!weld.IsValid(), "destroying an actor invalidates the joints attached to it");
        Check(joints.GetJointCount() == 3, "the reclaimed joint is no longer counted");
    }

    {
        ludifex::World3D cones = ludifex::CreateWorld3D();

        auto Distance = [](const ludifex::Vec3& a, const ludifex::Vec3& b) {
            const float dx = b.X - a.X;
            const float dy = b.Y - a.Y;
            const float dz = b.Z - a.Z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        // A rod hanging from a ball joint, kicked hard sideways. It swings out
        // to the cone and no further.
        ludifex::Actor3D ceiling =
            cones.AddBox({ .Position = { 0.0f, 8.0f, 0.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D rod = cones.AddCapsule({ .Radius = 0.1f, .Height = 2.0f, .Position = { 0.0f, 6.5f, 0.0f },
                                                  .Density = 1000.0f });

        ludifex::Joint3D cone = cones.AddConeJoint({
            .BodyA = ceiling,
            .BodyB = rod,
            .Anchor = { 0.0f, 7.5f, 0.0f },
            .Axis = { 0.0f, -1.0f, 0.0f },
            .ConeAngle = 0.4f,
            .LowerTwist = -0.3f,
            .UpperTwist = 0.3f,
        });

        Check(cone.IsValid() && cone.GetKind() == ludifex::JointKind::Cone, "a cone joint is created");
        Check(std::abs(cone.GetConeAngle()) < 1e-3f, "a cone joint starts with no swing");

        rod.SetLinearVelocity({ 4.0f, 0.0f, 2.5f });

        float widest = 0.0f;
        float pivotDrift = 0.0f;
        for (int step = 0; step < 240; ++step)
        {
            cones.StepPhysics(1.0f / 60.0f);
            widest = std::max(widest, cone.GetAngle());

            // The rod's top end should stay on the pivot.
            const ludifex::Vec3 top = rod.GetRotation().Rotate({ 0.0f, 1.0f, 0.0f });
            const ludifex::Vec3 position = rod.GetPosition();
            pivotDrift = std::max(pivotDrift, Distance({ position.X + top.X, position.Y + top.Y, position.Z + top.Z },
                                                       ludifex::Vec3{ 0.0f, 7.5f, 0.0f }));
        }

        std::printf("      (widest swing %.3f rad against a 0.4 rad cone, pivot drift %.4f m)\n",
                    static_cast<double>(widest), static_cast<double>(pivotDrift));

        Check(widest > 0.3f, "a kicked rod swings out to its cone");
        Check(widest < 0.46f, "a cone limit holds the swing inside the cone");
        Check(pivotDrift < 0.05f, "a cone joint keeps the rod on its pivot");

        // Spun about its own length by the motor, the rod turns until the
        // twist limit stops it.
        cone.EnableMotor(true);
        cone.SetMaxMotorEffort(40.0f);
        cone.SetMotorVelocity({ 0.0f, 3.0f, 0.0f });

        float mostTwist = 0.0f;
        for (int step = 0; step < 120; ++step)
        {
            cones.StepPhysics(1.0f / 60.0f);
            mostTwist = std::max(mostTwist, std::abs(cone.GetTwistAngle()));
        }

        std::printf("      (most twist %.3f rad against a 0.3 rad limit, motor %.1f N m)\n",
                    static_cast<double>(mostTwist), static_cast<double>(cone.GetMotorEffort()));

        Check(mostTwist > 0.2f, "a cone joint's motor twists the rod");
        Check(mostTwist < 0.36f, "a twist limit stops the motor");

        // Narrowed, the rod is pulled in and then circles the rim. Box3D as
        // released pushed it along a stale axis there, which wound it up until
        // it left the cone; ludifex builds Box3D with that corrected.
        cone.SetConeAngle(0.1f);
        cone.EnableMotor(false);

        float widestNarrowed = 0.0f;
        for (int step = 0; step < 240; ++step)
        {
            cones.StepPhysics(1.0f / 60.0f);
            if (step >= 20)
            {
                widestNarrowed = std::max(widestNarrowed, cone.GetAngle());
            }
        }
        std::printf("      (widest swing %.3f rad after narrowing the cone to 0.1 rad)\n",
                    static_cast<double>(widestNarrowed));
        Check(widestNarrowed < 0.13f, "narrowing the cone pulls the swing in, and it stays in");

        // A ragdoll dropped on the ground. It should fall in a heap and lie
        // still, every part where its joints say it can be.
        cones.AddGround({ .Width = 40.0f, .Depth = 40.0f });

        ludifex::Ragdoll ragdoll = cones.AddRagdoll({ .Position = { 10.0f, 0.3f, 0.0f }, .Name = "Guard" });
        const std::vector<ludifex::Actor3D> parts = ragdoll.Parts();

        bool allParts = true;
        for (const ludifex::Actor3D& part : parts)
        {
            allParts = allParts && part.IsValid();
        }
        Check(ragdoll.IsValid() && allParts && parts.size() == 11, "a ragdoll is made of eleven parts");
        Check(cones.GetJointCount() == 11, "a ragdoll adds ten joints");
        Check(ragdoll.Head.GetName() == "Guard.Head", "a ragdoll's parts are named after it");

        // Where a joint's pivot sits in each body's own space. A joint holds
        // when both bodies still put the pivot in the same place.
        auto LocalPoint = [](const ludifex::Actor3D& actor, const ludifex::Vec3& world) {
            const ludifex::Vec3 at = actor.GetPosition();
            return actor.GetRotation().Inverse().Rotate({ world.X - at.X, world.Y - at.Y, world.Z - at.Z });
        };
        auto WorldPoint = [](const ludifex::Actor3D& actor, const ludifex::Vec3& local) {
            const ludifex::Vec3 at = actor.GetPosition();
            const ludifex::Vec3 turned = actor.GetRotation().Rotate(local);
            return ludifex::Vec3{ at.X + turned.X, at.Y + turned.Y, at.Z + turned.Z };
        };

        const ludifex::Vec3 neckPivot{ 10.0f, 0.3f + 1.51f, 0.0f };
        const ludifex::Vec3 kneePivot{ 9.9f, 0.3f + 0.43f, 0.0f };
        const ludifex::Vec3 neckOnChest = LocalPoint(ragdoll.Chest, neckPivot);
        const ludifex::Vec3 neckOnHead = LocalPoint(ragdoll.Head, neckPivot);
        const ludifex::Vec3 kneeOnThigh = LocalPoint(ragdoll.UpperLegLeft, kneePivot);
        const ludifex::Vec3 kneeOnShin = LocalPoint(ragdoll.LowerLegLeft, kneePivot);

        ragdoll.Chest.SetLinearVelocity({ 0.0f, 0.0f, -3.0f });

        float lowestKnee = 0.0f;
        float highestKnee = 0.0f;
        for (int step = 0; step < 300; ++step)
        {
            cones.StepPhysics(1.0f / 60.0f);
            lowestKnee = std::min({ lowestKnee, ragdoll.KneeLeft.GetAngle(), ragdoll.KneeRight.GetAngle() });
            highestKnee = std::max({ highestKnee, ragdoll.KneeLeft.GetAngle(), ragdoll.KneeRight.GetAngle() });
        }

        bool finite = true;
        bool aboveGround = true;
        bool together = true;
        float fastest = 0.0f;
        const ludifex::Vec3 pelvis = ragdoll.Pelvis.GetPosition();
        for (const ludifex::Actor3D& part : parts)
        {
            const ludifex::Vec3 at = part.GetPosition();
            const ludifex::Vec3 velocity = part.GetLinearVelocity();
            finite = finite && std::isfinite(at.X) && std::isfinite(at.Y) && std::isfinite(at.Z);
            aboveGround = aboveGround && at.Y > -0.05f;
            together = together && Distance(at, pelvis) < 1.2f;
            fastest = std::max(fastest, std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y +
                                                  velocity.Z * velocity.Z));
        }

        const float neckGap = Distance(WorldPoint(ragdoll.Chest, neckOnChest), WorldPoint(ragdoll.Head, neckOnHead));
        const float kneeGap =
            Distance(WorldPoint(ragdoll.UpperLegLeft, kneeOnThigh), WorldPoint(ragdoll.LowerLegLeft, kneeOnShin));

        std::printf("      (pelvis at %.2f m, fastest part %.3f m/s, knees %.2f to %.2f rad, pivots apart %.4f %.4f m)\n",
                    static_cast<double>(pelvis.Y), static_cast<double>(fastest), static_cast<double>(lowestKnee),
                    static_cast<double>(highestKnee), static_cast<double>(neckGap), static_cast<double>(kneeGap));

        Check(finite && aboveGround, "a ragdoll lands on the ground without falling through it");
        Check(pelvis.Y < 0.6f, "a limp ragdoll falls down");
        Check(together, "a ragdoll stays in one piece");
        Check(neckGap < 0.02f && kneeGap < 0.02f, "a ragdoll's joints hold its parts together");
        Check(fastest < 0.3f, "a ragdoll comes to rest");
        Check(lowestKnee > -0.1f && highestKnee < 2.5f, "a ragdoll's knees bend only one way");

        // Losing the head takes the neck joint with it, and nothing else.
        ragdoll.Head.Destroy();
        cones.ApplyPendingChanges();
        Check(!ragdoll.Neck.IsValid() && ragdoll.Spine.IsValid(), "destroying a part removes only its joints");
        Check(cones.GetJointCount() == 10, "the ragdoll's remaining joints are still counted");
    }

    // -----------------------------------------------------------------------
    std::printf("\n8. Sensors and triggers\n");

    {
        ludifex::World3D triggers = ludifex::CreateWorld3D();

        // A static trigger volume with nothing solid about it, and a falling
        // sphere that passes straight through the middle of it.
        ludifex::Actor3D gate = triggers.AddBox({
            .Scale = { 4.0f, 1.0f, 4.0f },
            .Position = { 0.0f, 5.0f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .IsSensor = true,
        });

        ludifex::Actor3D faller = triggers.AddSphere({ .Radius = 0.4f, .Position = { 0.0f, 10.0f, 0.0f } });

        Check(gate.IsSensor(), "an actor created with IsSensor reports itself as one");
        Check(!faller.IsSensor(), "an ordinary actor does not");

        int entered = 0;
        int exited = 0;
        bool sawTheFaller = true;
        bool reportedItself = true;

        gate.WhenEntered([&](const ludifex::TriggerInfo& info) {
            ++entered;
            sawTheFaller = sawTheFaller && info.Other == faller;
            reportedItself = reportedItself && info.Sensor == gate;
        });
        gate.WhenExited([&](const ludifex::TriggerInfo&) { ++exited; });

        int worldEnters = 0;
        triggers.WhenActorEnteredTrigger([&](const ludifex::TriggerInfo&) { ++worldEnters; });

        float lowestY = 100.0f;
        for (int step = 0; step < 120; ++step)
        {
            triggers.StepPhysics(1.0f / 60.0f);
            lowestY = std::min(lowestY, faller.GetPosition().Y);
        }

        std::printf("      (entered %d, exited %d, fell to %.2f m)\n", entered, exited,
                    static_cast<double>(lowestY));

        Check(entered == 1, "a sphere falling through a sensor reports entering once");
        Check(exited == 1, "and reports leaving once");
        Check(sawTheFaller && reportedItself, "the trigger names both the sensor and the visitor");
        Check(worldEnters == 1, "a world-wide trigger handler sees the same overlap");
        Check(lowestY < 0.0f, "the sensor never pushed back: the sphere fell past it");

        const ludifex::RayHit through = triggers.CastRay({ 0.0f, 20.0f, 0.0f }, { 0.0f, -1.0f, 0.0f });
        Check(!through.Hit || through.Actor != gate, "a 3D ray passes through a sensor");

        // A sensor notices whatever is inside it, including bodies that never
        // move, and a filter is the way to narrow that down.
        ludifex::Actor3D still = triggers.AddBox({
            .Position = { 0.0f, 5.0f, 0.0f },
            .Type = ludifex::BodyType::Static,
        });

        int staticEnters = 0;
        gate.WhenEntered([&](const ludifex::TriggerInfo&) { ++staticEnters; });
        triggers.StepPhysics(1.0f / 60.0f);

        Check(staticEnters == 1, "a sensor notices a static actor standing inside it");

        constexpr uint64_t OnlyPlayers = 1ull << 1;
        constexpr uint64_t Scenery = 1ull << 2;

        gate.SetCollisionFilter(1ull << 0, OnlyPlayers);
        still.SetCollisionFilter(Scenery, Scenery);

        int afterFilter = 0;
        gate.WhenEntered([&](const ludifex::TriggerInfo&) { ++afterFilter; });
        for (int step = 0; step < 5; ++step)
        {
            triggers.StepPhysics(1.0f / 60.0f);
        }

        Check(afterFilter == 0, "a filter keeps a sensor from noticing what it should ignore");
    }

    {
        // Handlers are stored with their actors, in an array that grows when
        // actors are added. A handler that adds thousands of them moves that
        // array while it is running, and must come out the other side intact:
        // everything it captured is read again after the move.
        ludifex::World3D growing = ludifex::CreateWorld3D();
        growing.AddGround({ .Width = 40.0f, .Depth = 40.0f });

        auto AddMany = [&](int& counter) {
            for (int index = 0; index < 3000; ++index)
            {
                growing.AddSphere({ .Radius = 0.05f,
                                    .Position = { 100.0f + static_cast<float>(index % 60) * 0.5f, 50.0f,
                                                  static_cast<float>(index / 60) * 0.5f },
                                    .Type = ludifex::BodyType::Static });
                ++counter;
            }
        };

        int addedOnImpact = 0;
        bool impactFinished = false;
        ludifex::Actor3D dropped = growing.AddSphere({ .Radius = 0.3f, .Position = { 0.0f, 2.0f, 0.0f } });
        dropped.WhenCollided([&](const ludifex::CollisionInfo&) {
            if (addedOnImpact == 0)
            {
                AddMany(addedOnImpact);
                impactFinished = true;
            }
        });

        int addedOnEntry = 0;
        bool entryFinished = false;
        ludifex::Actor3D gate = growing.AddBox({ .Scale = { 2.0f, 0.5f, 2.0f }, .Position = { 6.0f, 2.0f, 0.0f },
                                                 .Type = ludifex::BodyType::Static, .IsSensor = true });
        growing.AddSphere({ .Radius = 0.3f, .Position = { 6.0f, 4.0f, 0.0f } });
        gate.WhenEntered([&](const ludifex::TriggerInfo&) {
            if (addedOnEntry == 0)
            {
                AddMany(addedOnEntry);
                entryFinished = true;
            }
        });

        for (int step = 0; step < 120; ++step)
        {
            growing.StepPhysics(1.0f / 60.0f);
        }

        Check(addedOnImpact == 3000 && impactFinished, "a collision handler can add thousands of actors safely");
        Check(addedOnEntry == 3000 && entryFinished, "and so can a trigger handler");
    }

    // -----------------------------------------------------------------------
    std::printf("\n9. 2D parity: events, triggers, filters, and rays\n");

    {
        ludifex::World2D flat = ludifex::CreateWorld2D();
        flat.AddGround({ .Width = 40.0f });

        // A collision event, with the same physically exact impact speed the
        // 3D world reports: a 5 m drop under 10 m/s^2 arrives at 10 m/s.
        ludifex::Actor2D brick = flat.AddRectangle({ .Position = { 0.0f, 5.5f } });

        float impact = 0.0f;
        bool namedItself = true;
        brick.WhenCollided([&](const ludifex::CollisionInfo2D& info) {
            impact = std::max(impact, info.ImpactSpeed);
            namedItself = namedItself && info.Self == brick;
        });

        int worldHits = 0;
        flat.WhenActorCollided([&](const ludifex::CollisionInfo2D&) { ++worldHits; });

        // A trigger the brick falls through on its way down.
        ludifex::Actor2D gate = flat.AddRectangle({
            .Width = 6.0f,
            .Height = 0.5f,
            .Position = { 0.0f, 3.0f },
            .Type = ludifex::BodyType::Static,
            .IsSensor = true,
        });

        int entered = 0;
        int exited = 0;
        gate.WhenEntered([&](const ludifex::TriggerInfo2D& info) {
            ++entered;
            namedItself = namedItself && info.Other == brick;
        });
        gate.WhenExited([&](const ludifex::TriggerInfo2D&) { ++exited; });

        for (int step = 0; step < 180; ++step)
        {
            flat.StepPhysics(1.0f / 60.0f);
        }

        std::printf("      (impact %.2f m/s, entered %d, exited %d, resting y %.3f)\n",
                    static_cast<double>(impact), entered, exited,
                    static_cast<double>(brick.GetPosition().Y));

        Check(std::abs(impact - 10.0f) < 0.5f,
              "a 2D collision reports a physical impact speed for a 5 m drop");
        Check(worldHits > 0, "a world-wide 2D collision handler sees the same impact");
        Check(namedItself, "2D events name the actors involved");
        Check(gate.IsSensor() && !brick.IsSensor(), "a 2D sensor reports itself as one");
        Check(entered == 1 && exited == 1, "a 2D trigger reports entering and leaving once each");

        // A ray fired down the Y axis should find the brick resting on the
        // ground, at roughly its own half-height above it.
        const ludifex::RayHit2D down = flat.CastRay({ 0.0f, 8.0f }, { 0.0f, -1.0f });
        std::printf("      (ray hit %s at y %.3f, %.2f m away)\n", down.Hit ? "yes" : "no",
                    static_cast<double>(down.Point.Y), static_cast<double>(down.Distance));

        Check(down.Hit && down.Actor == brick,
              "a 2D ray passes through the sensor above and finds the brick under it");
        Check(down.Normal.Y > 0.9f, "and reports a normal pointing back along the ray");
        Check(down.Distance > 0.0f && down.Distance < 8.0f, "at a sensible distance");

        const ludifex::RayHit2D miss = flat.CastRay({ 30.0f, 8.0f }, { 0.0f, 1.0f });
        Check(!miss.Hit, "a ray fired into empty space reports no hit");

        // Filtering, the 2D mirror of the 3D check: two boxes that overlap
        // push apart unless their layers exclude each other.
        auto Settle = [](bool filtered) {
            ludifex::World2D world = ludifex::CreateWorld2D({ .Gravity = { 0.0f, 0.0f } });

            ludifex::Actor2D a = world.AddRectangle({ .Position = { 0.0f, 0.0f } });
            ludifex::Actor2D b = world.AddRectangle({ .Position = { 0.2f, 0.0f } });

            if (filtered)
            {
                a.SetCollisionFilter(1ull << 0, 1ull << 0);
                b.SetCollisionFilter(1ull << 1, 1ull << 1);
            }

            for (int step = 0; step < 60; ++step)
            {
                world.StepPhysics(1.0f / 60.0f);
            }

            return std::abs(b.GetPosition().X - a.GetPosition().X);
        };

        const float unfiltered2D = Settle(false);
        const float filtered2D = Settle(true);

        std::printf("      (unfiltered %.3f m apart, filtered %.3f m apart)\n",
                    static_cast<double>(unfiltered2D), static_cast<double>(filtered2D));

        Check(unfiltered2D > 0.9f, "overlapping 2D boxes push apart when nothing filters them");
        Check(filtered2D < 0.25f, "2D boxes on mutually excluded layers pass through each other");
    }

    // -----------------------------------------------------------------------
    std::printf("\n10. Shape queries, characters, and scale\n");

    {
        ludifex::World3D world = ludifex::CreateWorld3D();
        world.AddGround({ .Width = 60.0f, .Depth = 60.0f });

        // A doorway 0.9 m wide: a ray down the middle passes, and so does a
        // narrow sphere, but a wide one cannot fit.
        world.AddBox({
            .Scale = { 4.0f, 3.0f, 0.4f },
            .Position = { -2.45f, 1.5f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Left jamb",
        });
        world.AddBox({
            .Scale = { 4.0f, 3.0f, 0.4f },
            .Position = { 2.45f, 1.5f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Right jamb",
        });

        const ludifex::Vec3 from{ 0.0f, 1.5f, -4.0f };
        const ludifex::Vec3 through{ 0.0f, 0.0f, 1.0f };

        const ludifex::RayHit ray = world.CastRay(from, through, 8.0f);
        const ludifex::RayHit narrow = world.CastSphere(from, 0.3f, through, 8.0f);
        const ludifex::RayHit wide = world.CastSphere(from, 0.8f, through, 8.0f);

        std::printf("      (ray %s, 0.6 m sphere %s, 1.6 m sphere stopped at %.2f m)\n",
                    ray.Hit ? "hit" : "passed", narrow.Hit ? "hit" : "passed",
                    static_cast<double>(wide.Distance));

        Check(!ray.Hit, "a ray goes straight through the doorway");
        Check(!narrow.Hit, "and so does a sphere narrow enough to fit");
        Check(wide.Hit && wide.Distance > 3.0f && wide.Distance < 4.1f,
              "a sphere wider than the doorway is stopped at the jamb");
        Check(wide.Hit && (wide.Actor.GetName() == "Left jamb" || wide.Actor.GetName() == "Right jamb"),
              "and the cast names which jamb it met");

        // Overlaps: what is where, right now.
        ludifex::Actor3D crate = world.AddBox({
            .Scale = { 1.0f, 1.0f, 1.0f },
            .Position = { 6.0f, 0.5f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Crate",
        });
        ludifex::Actor3D trigger = world.AddBox({
            .Scale = { 2.0f, 2.0f, 2.0f },
            .Position = { 6.0f, 1.0f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .IsSensor = true,
            .Name = "Trigger",
        });
        world.ApplyPendingChanges();

        const std::vector<ludifex::Actor3D> solid = world.OverlapSphere({ 6.0f, 0.5f, 0.0f }, 0.6f);
        const std::vector<ludifex::Actor3D> withSensors =
            world.OverlapSphere({ 6.0f, 0.5f, 0.0f }, 0.6f, true);
        const std::vector<ludifex::Actor3D> empty = world.OverlapSphere({ 20.0f, 6.0f, 0.0f }, 0.6f);

        bool foundCrate = false;
        for (const ludifex::Actor3D& actor : solid)
        {
            foundCrate = foundCrate || actor == crate;
        }

        std::printf("      (%zu solid, %zu including sensors, %zu in empty air)\n", solid.size(),
                    withSensors.size(), empty.size());

        Check(foundCrate, "an overlap finds the crate it is standing in");
        Check(solid.size() + 1 == withSensors.size() && trigger.IsValid(),
              "sensors are left out unless they are asked for");
        Check(empty.empty(), "and an overlap in empty air finds nothing");
    }

    {
        // A character: a wall it cannot pass, a step it can, and a ledge it
        // cannot.
        ludifex::World3D world = ludifex::CreateWorld3D();
        world.AddGround({ .Width = 60.0f, .Depth = 60.0f });

        world.AddBox({
            .Scale = { 6.0f, 3.0f, 0.5f },
            .Position = { 0.0f, 1.5f, -3.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Wall",
        });

        // A 0.25 m step and a 0.8 m ledge, side by side.
        world.AddBox({
            .Scale = { 3.0f, 0.25f, 3.0f },
            .Position = { 6.0f, 0.125f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Step",
        });
        world.AddBox({
            .Scale = { 3.0f, 0.8f, 3.0f },
            .Position = { -6.0f, 0.4f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .Name = "Ledge",
        });
        world.ApplyPendingChanges();

        ludifex::Character3D walker = world.AddCharacter({
            .Position = { 0.0f, 0.9f, 0.0f },
            .Radius = 0.35f,
            .Height = 1.8f,
            .StepHeight = 0.35f,
        });
        Check(walker.IsValid() && walker.GetActor().IsValid(),
              "a character is a capsule the world can see");

        // Settle it onto the ground first.
        for (int step = 0; step < 10; ++step)
        {
            walker.Move({ 0.0f, -0.05f, 0.0f });
        }
        const bool grounded = walker.IsOnGround();

        // Into the wall, hard.
        for (int step = 0; step < 60; ++step)
        {
            walker.Move({ 0.0f, -0.02f, -0.05f });
        }
        const float stoppedAt = walker.GetPosition().Z;

        // Along the wall: it slides rather than sticking.
        const float beforeSlide = walker.GetPosition().X;
        for (int step = 0; step < 40; ++step)
        {
            walker.Move({ 0.05f, -0.02f, -0.05f });
        }
        const float slid = walker.GetPosition().X - beforeSlide;

        // The wall's face is at z = -2.75, so a 0.35 m capsule resting against
        // it sits at -2.40.
        const float gap = stoppedAt + 2.75f;
        std::printf("      (stopped %.2f m from the wall's face for a 0.35 m radius, slid %.2f m "
                    "along it)\n",
                    static_cast<double>(gap), static_cast<double>(slid));

        Check(grounded, "it stands on the ground rather than sinking into it");
        Check(gap > 0.32f && gap < 0.45f, "a wall stops it exactly its own width from the wall");
        Check(slid > 1.5f, "and pushing along the wall slides rather than sticking");

        // The step is climbed; the ledge is not.
        walker.SetPosition({ 6.0f, 1.4f, 3.0f });
        for (int step = 0; step < 10; ++step)
        {
            walker.Move({ 0.0f, -0.08f, 0.0f });
        }
        for (int step = 0; step < 80; ++step)
        {
            walker.Move({ 0.0f, -0.02f, -0.05f });
        }
        const float ontoStep = walker.GetPosition().Y;

        walker.SetPosition({ -6.0f, 1.4f, 3.0f });
        for (int step = 0; step < 10; ++step)
        {
            walker.Move({ 0.0f, -0.08f, 0.0f });
        }
        for (int step = 0; step < 80; ++step)
        {
            walker.Move({ 0.0f, -0.02f, -0.05f });
        }
        const float atLedge = walker.GetPosition().Y;

        std::printf("      (stood %.2f m up after the 0.25 m step, %.2f m up at the 0.8 m ledge)\n",
                    static_cast<double>(ontoStep), static_cast<double>(atLedge));

        Check(ontoStep > 1.1f, "a step shorter than StepHeight is climbed");
        Check(atLedge < 1.05f, "a ledge taller than StepHeight is not");
    }

    {
        // A thousand bodies, stacked, settling. The check is that they come to
        // rest where they were put rather than shuffling or sinking.
        ludifex::World3D world = ludifex::CreateWorld3D();
        world.AddGround({ .Width = 60.0f, .Depth = 60.0f });

        std::vector<ludifex::Actor3D> bricks;
        bricks.reserve(1000);
        for (int layer = 0; layer < 10; ++layer)
        {
            for (int row = 0; row < 10; ++row)
            {
                for (int column = 0; column < 10; ++column)
                {
                    bricks.push_back(world.AddBox({
                        .Scale = { 0.5f, 0.5f, 0.5f },
                        .Position = { static_cast<float>(column) * 0.52f - 2.6f,
                                      0.25f + static_cast<float>(layer) * 0.5f,
                                      static_cast<float>(row) * 0.52f - 2.6f },
                        .Friction = 0.6f,
                    }));
                }
            }
        }
        world.ApplyPendingChanges();

        for (int step = 0; step < 240; ++step)
        {
            world.StepPhysics(1.0f / 60.0f);
        }

        // How far the top layer moved, and how fast anything is still going.
        float sink = 0.0f;
        float fastest = 0.0f;
        for (size_t index = 0; index < bricks.size(); ++index)
        {
            const ludifex::Vec3 position = bricks[index].GetPosition();
            const ludifex::Vec3 velocity = bricks[index].GetLinearVelocity();
            const float expected = 0.25f + static_cast<float>(index / 100) * 0.5f;
            sink = std::max(sink, std::abs(position.Y - expected));
            fastest = std::max(fastest, std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y +
                                                  velocity.Z * velocity.Z));
        }

        std::printf("      (1000 bricks, worst drift %.3f m, fastest %.4f m/s after 4 seconds)\n",
                    static_cast<double>(sink), static_cast<double>(fastest));

        Check(bricks.size() == 1000 && world.GetActorCount() == 1001,
              "a thousand bricks and a ground stand in one world");
        Check(sink < 0.08f, "the stack settles where it was built rather than sinking into itself");
        Check(fastest < 0.05f, "and nothing is still jittering four seconds later");
    }

    {
        const std::vector<ludifex::Vec3> first = RunSimulation3D(240);
        const std::vector<ludifex::Vec3> second = RunSimulation3D(240);

        bool identical = first.size() == second.size();
        for (size_t index = 0; identical && index < first.size(); ++index)
        {
            identical = first[index].X == second[index].X && first[index].Y == second[index].Y &&
                        first[index].Z == second[index].Z;
        }

        Check(identical, "240 steps of a 25-body 3D world reproduce bit-for-bit");
    }

    // -----------------------------------------------------------------------
    std::printf("\n11. The transform hierarchy\n");

    {
        ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

        ludifex::Actor3D cart = world.AddBox({
            .Scale = { 2.0f, 0.4f, 1.2f },
            .Position = { 0.0f, 0.5f, 0.0f },
            .Type = ludifex::BodyType::Kinematic,
            .Name = "Cart",
        });
        ludifex::Actor3D lamp = world.AddSphere({
            .Radius = 0.2f,
            .Position = { 0.8f, 1.2f, 0.0f },
            .Type = ludifex::BodyType::Kinematic,
            .Name = "Lamp",
        });
        ludifex::Actor3D flame = world.AddSphere({
            .Radius = 0.08f,
            .Position = { 0.8f, 1.45f, 0.0f },
            .Type = ludifex::BodyType::Kinematic,
            .Name = "Flame",
        });

        lamp.SetParent(cart);
        flame.SetParent(lamp);
        world.ApplyPendingChanges();

        const ludifex::Vec3 afterParenting = lamp.GetPosition();
        Check(std::abs(afterParenting.X - 0.8f) < 1e-4f && std::abs(afterParenting.Y - 1.2f) < 1e-4f,
              "parenting leaves the child exactly where it was");
        Check(lamp.GetParent() == cart && flame.GetParent() == lamp,
              "the child knows its parent, and so does the grandchild");
        Check(cart.GetChildren().size() == 1 && lamp.GetChildren().size() == 1,
              "and the parent knows its children");

        // The cart rolls five metres; everything on it goes too.
        cart.SetPosition({ 5.0f, 0.5f, 0.0f });
        world.StepPhysics(1.0f / 60.0f);

        const ludifex::Vec3 carried = lamp.GetPosition();
        const ludifex::Vec3 carriedFlame = flame.GetPosition();
        Check(std::abs(carried.X - 5.8f) < 1e-3f && std::abs(carried.Y - 1.2f) < 1e-3f,
              "moving the parent carries the child, offset intact");
        Check(std::abs(carriedFlame.X - 5.8f) < 1e-3f && std::abs(carriedFlame.Y - 1.45f) < 1e-3f,
              "and carries the grandchild with it");

        // A quarter turn about Y: the lamp swings around the cart rather than
        // spinning where it stands.
        cart.SetRotation(ludifex::Quat::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 1.5707963f));
        world.StepPhysics(1.0f / 60.0f);

        const ludifex::Vec3 turned = lamp.GetPosition();
        std::printf("      (after a quarter turn the lamp is at %.2f, %.2f, %.2f)\n",
                    static_cast<double>(turned.X), static_cast<double>(turned.Y),
                    static_cast<double>(turned.Z));
        Check(std::abs(turned.X - 5.0f) < 1e-2f && std::abs(turned.Z + 0.8f) < 1e-2f,
              "turning the parent swings the child around it");

        // Detaching leaves it where it is, and it stops following.
        lamp.ClearParent();
        world.ApplyPendingChanges();
        const ludifex::Vec3 detached = lamp.GetPosition();
        cart.SetPosition({ 20.0f, 0.5f, 0.0f });
        world.StepPhysics(1.0f / 60.0f);

        Check(std::abs(lamp.GetPosition().X - detached.X) < 1e-3f,
              "detaching leaves the child where it stands, and it stops following");

        // A loop is refused rather than hung on.
        const int refusalsBefore = g_Errors;
        lamp.SetParent(cart);
        world.ApplyPendingChanges();
        cart.SetParent(flame);
        world.ApplyPendingChanges();
        Check(g_Errors > refusalsBefore && !cart.GetParent().IsValid(),
              "a parent loop is refused with a diagnostic rather than made");

        // Destroying the cart takes everything on it.
        const size_t before = world.GetActorCount();
        cart.Destroy();
        world.ApplyPendingChanges();

        std::printf("      (%zu actors before the cart was destroyed, %zu after)\n", before,
                    world.GetActorCount());
        Check(!lamp.IsValid() && !flame.IsValid() && world.GetActorCount() == before - 3,
              "destroying a parent destroys what is parented to it");
    }

    // -----------------------------------------------------------------------
    std::printf("\n12. Saving and restoring\n");

    {
        // A world is built, saved, stepped on, and then restored from the
        // bytes: what comes back has to be the same world, and stepping it
        // has to produce the same future.
        auto Build = [] {
            ludifex::World3D world = ludifex::CreateWorld3D({ .Deterministic = true });
            world.AddGround({ .Width = 40.0f, .Depth = 40.0f, .Name = "Ground" });

            for (int index = 0; index < 12; ++index)
            {
                ludifex::Actor3D box = world.AddBox({
                    .Scale = { 0.6f, 0.6f, 0.6f },
                    .Position = { static_cast<float>(index % 4) * 0.7f - 1.05f,
                                  1.0f + static_cast<float>(index) * 0.8f,
                                  static_cast<float>(index / 4) * 0.7f - 0.35f },
                    .Friction = 0.4f,
                    .Restitution = 0.15f,
                    .Name = "Box " + std::to_string(index),
                });
                box.SetColor(ludifex::Color::FromBytes(static_cast<uint8_t>(40 + index * 15), 120, 200));
                box.SetRoughness(0.3f);
            }

            ludifex::Actor3D cart = world.AddBox({
                .Scale = { 2.0f, 0.4f, 1.2f },
                .Position = { 6.0f, 0.5f, 0.0f },
                .Type = ludifex::BodyType::Kinematic,
                .Name = "Cart",
            });
            ludifex::Actor3D lamp = world.AddSphere({
                .Radius = 0.2f,
                .Position = { 6.8f, 1.2f, 0.0f },
                .Type = ludifex::BodyType::Kinematic,
                .Name = "Lamp",
            });
            lamp.SetParent(cart);

            world.AddPointLight({ .Position = { 0.0f, 4.0f, 0.0f }, .Intensity = 6.0f, .Range = 12.0f });
            world.ApplyPendingChanges();
            return world;
        };

        ludifex::World3D original = Build();
        for (int step = 0; step < 30; ++step)
        {
            original.StepPhysics(1.0f / 60.0f);
        }

        const std::vector<uint8_t> bytes = original.Save();

        // Where everything was at the moment of the save, and where it ends up
        // if the original keeps going.
        std::vector<ludifex::Vec3> atSave;
        original.ForEachActor([&](ludifex::Actor3D& actor) { atSave.push_back(actor.GetPosition()); });

        for (int step = 0; step < 90; ++step)
        {
            original.StepPhysics(1.0f / 60.0f);
        }
        std::vector<ludifex::Vec3> later;
        original.ForEachActor([&](ludifex::Actor3D& actor) { later.push_back(actor.GetPosition()); });

        // A fresh world, restored from those bytes.
        ludifex::World3D restored = ludifex::CreateWorld3D();
        const bool loaded = restored.Load(bytes);

        std::vector<ludifex::Vec3> afterLoad;
        std::vector<std::string> names;
        restored.ForEachActor([&](ludifex::Actor3D& actor) {
            afterLoad.push_back(actor.GetPosition());
            names.push_back(actor.GetName());
        });

        bool samePlaces = afterLoad.size() == atSave.size();
        float worst = 0.0f;
        for (size_t index = 0; samePlaces && index < atSave.size(); ++index)
        {
            worst = std::max(worst, std::abs(afterLoad[index].X - atSave[index].X));
            worst = std::max(worst, std::abs(afterLoad[index].Y - atSave[index].Y));
            worst = std::max(worst, std::abs(afterLoad[index].Z - atSave[index].Z));
        }

        std::printf("      (%zu bytes for %zu actors; worst difference after restoring %.6f m)\n",
                    bytes.size(), atSave.size(), static_cast<double>(worst));

        Check(loaded && !bytes.empty(), "a world saves to bytes and loads back");
        Check(samePlaces && worst < 1e-6f, "and every actor is exactly where it was");

        bool sameNames = names.size() == atSave.size();
        for (const std::string& name : names)
        {
            sameNames = sameNames && !name.empty();
        }
        Check(sameNames, "names come back with them");

        // The restored world must have the same future as the original.
        for (int step = 0; step < 90; ++step)
        {
            restored.StepPhysics(1.0f / 60.0f);
        }
        std::vector<ludifex::Vec3> restoredLater;
        restored.ForEachActor([&](ludifex::Actor3D& actor) { restoredLater.push_back(actor.GetPosition()); });

        bool sameFuture = restoredLater.size() == later.size();
        float drift = 0.0f;
        for (size_t index = 0; sameFuture && index < later.size(); ++index)
        {
            drift = std::max(drift, std::abs(restoredLater[index].X - later[index].X));
            drift = std::max(drift, std::abs(restoredLater[index].Y - later[index].Y));
            drift = std::max(drift, std::abs(restoredLater[index].Z - later[index].Z));
        }

        std::printf("      (after another 90 steps the two worlds differ by %.6f m)\n",
                    static_cast<double>(drift));
        Check(sameFuture && drift < 1e-4f,
              "and stepping the restored world ninety times lands where the original did");

        // The hierarchy survives: moving the cart still carries the lamp.
        ludifex::Actor3D cart;
        ludifex::Actor3D lamp;
        restored.ForEachActor([&](ludifex::Actor3D& actor) {
            if (actor.GetName() == "Cart")
            {
                cart = actor;
            }
            else if (actor.GetName() == "Lamp")
            {
                lamp = actor;
            }
        });

        bool carried = false;
        if (cart.IsValid() && lamp.IsValid())
        {
            cart.SetPosition({ 16.0f, 0.5f, 0.0f });
            restored.StepPhysics(1.0f / 60.0f);
            carried = std::abs(lamp.GetPosition().X - 16.8f) < 1e-3f;
        }
        Check(lamp.GetParent() == cart && carried, "the hierarchy comes back with it");

        // A file, and bytes that are not a world.
        const std::string path =
            (std::filesystem::temp_directory_path() / "ludifex-check-world.bin").string();
        const bool wrote = original.SaveToFile(path);

        ludifex::World3D fromFile = ludifex::CreateWorld3D();
        const bool read = fromFile.LoadFromFile(path);

        const int refusalsBefore = g_Errors;
        ludifex::World3D nonsense = ludifex::CreateWorld3D();
        nonsense.AddBox({ .Position = { 0.0f, 5.0f, 0.0f }, .Name = "Survivor" });
        nonsense.ApplyPendingChanges();
        const std::vector<uint8_t> rubbish{ 1, 2, 3, 4, 5, 6, 7, 8 };
        const bool refused = !nonsense.Load(rubbish);

        Check(wrote && read && fromFile.GetActorCount() == original.GetActorCount(),
              "a world round-trips through a file");
        Check(refused && g_Errors > refusalsBefore && nonsense.GetActorCount() == 1,
              "bytes that are not a world are refused, and the world is left alone");
    }

    // -----------------------------------------------------------------------
    std::printf("\n13. Ten thousand actors a second\n");

    {
        // The mutability contract under load: thousands of actors created and
        // destroyed a second, many from inside callbacks, with the count and
        // the storage staying correct.
        ludifex::World3D world = ludifex::CreateWorld3D();
        world.AddGround({ .Width = 60.0f, .Depth = 60.0f });

        int created = 0;
        int destroyed = 0;
        std::vector<ludifex::Actor3D> churn;

        // A sensor at the bottom: everything that falls through it is
        // destroyed from inside the callback, and replaced from there too.
        ludifex::Actor3D drain = world.AddBox({
            .Scale = { 40.0f, 0.4f, 40.0f },
            .Position = { 0.0f, -6.0f, 0.0f },
            .Type = ludifex::BodyType::Static,
            .IsSensor = true,
            .Name = "Drain",
        });
        drain.WhenEntered([&](const ludifex::TriggerInfo& info) {
            ludifex::Actor3D other = info.Other;
            other.Destroy();
            ++destroyed;
        });
        world.ApplyPendingChanges();

        // Three seconds at 60 Hz, 167 actors created per step: a shade over
        // ten thousand a second.
        constexpr int Steps = 180;
        constexpr int PerStep = 167;

        std::mt19937 random(11);
        std::uniform_real_distribution<float> spread(-8.0f, 8.0f);

        for (int step = 0; step < Steps; ++step)
        {
            for (int index = 0; index < PerStep; ++index)
            {
                ludifex::Actor3D speck = world.AddSphere({
                    .Radius = 0.12f,
                    .Position = { spread(random), 6.0f + spread(random) * 0.2f, spread(random) },
                    .Name = "Speck",
                });
                speck.SetLinearVelocity({ 0.0f, -30.0f, 0.0f });
                churn.push_back(speck);
                ++created;
            }

            world.StepPhysics(1.0f / 60.0f);

            // Anything that has fallen past the drain and missed it goes too,
            // so the world does not grow without bound.
            for (size_t index = 0; index < churn.size();)
            {
                if (!churn[index].IsValid() || churn[index].GetPosition().Y < -10.0f)
                {
                    if (churn[index].IsValid())
                    {
                        churn[index].Destroy();
                        ++destroyed;
                    }
                    churn[index] = churn.back();
                    churn.pop_back();
                    continue;
                }
                ++index;
            }
        }

        // Destroy everything that is left. The world should end up with the
        // same actors it started with.
        for (ludifex::Actor3D& speck : churn)
        {
            if (speck.IsValid())
            {
                speck.Destroy();
                ++destroyed;
            }
        }
        world.ApplyPendingChanges();

        const size_t remaining = world.GetActorCount();
        std::printf("      (%d created and %d destroyed over %d steps, %.0f a second, leaving %zu)\n",
                    created, destroyed, Steps,
                    static_cast<double>(created) / (static_cast<double>(Steps) / 60.0), remaining);

        Check(created >= 30000, "thirty thousand actors created across three seconds of steps");
        Check(destroyed == created, "every one of them destroyed, most from inside a callback");
        Check(remaining == 2, "and the world is back to its ground and its drain");
    }

    // -----------------------------------------------------------------------
    std::printf("\n14. World audio\n");

    {
        // Manual output: nothing goes to a device, and the mix is pulled here a
        // block at a time so it can be measured.
        ludifex::ConfigureAudio({ .Output = ludifex::AudioOutput::Manual, .SampleRate = 48000, .Channels = 2 });

        // One second of a 220 Hz sine is a whole number of cycles, so it loops
        // without a jump that could be mistaken for a click.
        const std::filesystem::path directory = std::filesystem::temp_directory_path();
        const std::string tonePath = (directory / "ludifex-check-tone.wav").string();
        examples::WriteToneWav(tonePath, 220.0f, 1000, 0.8f, 0.0f);
        const ludifex::SoundId tone = ludifex::LoadSound(tonePath);
        Check(tone.IsValid(), "a WAV loads as a sound");
        Check(std::abs(ludifex::GetSoundDuration(tone) - 1.0f) < 0.01f, "and reports its length");

        constexpr uint32_t Block = 480; // 10 ms at 48 kHz
        std::vector<float> block(Block * 2);

        // Mixes one block, advances the world by the same 10 ms, and folds the
        // block into the running maximum sample-to-sample step.
        float previous[2] = { 0.0f, 0.0f };
        auto MixBlock = [&](ludifex::World3D& world, float& maxStep, std::vector<float>* capture) {
            ludifex::MixAudio(block.data(), Block);
            for (uint32_t frame = 0; frame < Block; ++frame)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    const float sample = block[frame * 2 + channel];
                    maxStep = std::max(maxStep, std::abs(sample - previous[channel]));
                    previous[channel] = sample;
                }
                if (capture != nullptr)
                {
                    capture->push_back(block[frame * 2] + block[frame * 2 + 1]);
                }
            }
            world.Update(static_cast<float>(Block) / 48000.0f);
        };

        // --- no clicks --------------------------------------------------------
        {
            ludifex::World3D world = ludifex::CreateWorld3D();
            ludifex::Camera3D& camera = world.GetCamera();
            camera.Position = { 0.0f, 0.0f, 0.0f };
            camera.Target = { 0.0f, 0.0f, -1.0f };

            const ludifex::VoiceId voice = world.PlaySoundAt(tone, ludifex::Vec3{ 2.0f, 0.0f, -2.0f },
                                                             { .Looping = true, .MaxDistance = 20.0f });
            Check(voice.IsValid() && world.IsAudible(voice), "a looping emitter in range is audible");

            // The tone's own steepest slope, measured once it has settled.
            float steady = 0.0f;
            for (int index = 0; index < 5; ++index)
            {
                MixBlock(world, steady, nullptr);
            }
            steady = 0.0f;
            for (int index = 0; index < 30; ++index)
            {
                MixBlock(world, steady, nullptr);
            }

            // A jump across the range and back, out of range and back, a volume
            // change down and up, and a stop: every one of them must be a ramp,
            // never a step.
            float worst = 0.0f;
            auto Transition = [&](int blocks) {
                for (int index = 0; index < blocks; ++index)
                {
                    MixBlock(world, worst, nullptr);
                }
            };
            world.SetVoicePosition(voice, { 10.0f, 0.0f, -10.0f });
            Transition(30);
            world.SetVoicePosition(voice, { 2.0f, 0.0f, -2.0f });
            Transition(30);

            world.SetVoicePosition(voice, { 30.0f, 0.0f, 0.0f });
            Transition(30);
            const bool wentVirtual = world.IsPlaying(voice) && !world.IsAudible(voice);

            world.SetVoicePosition(voice, { 2.0f, 0.0f, -2.0f });
            Transition(30);
            const bool cameBack = world.IsAudible(voice);

            world.SetVoiceVolume(voice, 0.1f);
            Transition(20);
            world.SetVoiceVolume(voice, 1.0f);
            Transition(20);
            world.StopSound(voice);
            Transition(20);

            std::printf("      (steady step %.5f, worst step through transitions %.5f)\n", static_cast<double>(steady),
                        static_cast<double>(worst));

            Check(wentVirtual, "moving out of range keeps a looping voice, silently");
            Check(cameBack, "and moving back in range resumes it");
            Check(!world.IsPlaying(voice), "a stopped voice is gone once its fade ends");
            Check(steady > 0.0f && worst <= steady * 1.1f,
                  "no transition steps the waveform harder than the tone itself does: no clicks");
        }

        // --- no starvation: the nearest are the ones heard ----------------------
        {
            ludifex::World3D world = ludifex::CreateWorld3D();
            world.GetAudioSettings().MaxVoices = 6;
            ludifex::Camera3D& camera = world.GetCamera();
            camera.Position = { 0.0f, 0.0f, 0.0f };
            camera.Target = { 0.0f, 0.0f, -1.0f };

            std::vector<ludifex::VoiceId> field;
            for (int index = 0; index < 20; ++index)
            {
                field.push_back(world.PlaySoundAt(tone,
                                                  ludifex::Vec3{ 0.0f, 0.0f, -2.0f - static_cast<float>(index) * 2.0f },
                                                  { .Looping = true, .MaxDistance = 100.0f }));
            }
            float ignored = 0.0f;
            MixBlock(world, ignored, nullptr);

            const ludifex::AudioStats near = world.GetAudioStats();
            bool nearestSix = true;
            for (int index = 0; index < 20; ++index)
            {
                nearestSix = nearestSix && (world.IsAudible(field[static_cast<size_t>(index)]) == (index < 6));
            }

            // The listener flies to the far end; the voices heard follow it.
            camera.Position = { 0.0f, 0.0f, -44.0f };
            for (int index = 0; index < 10; ++index)
            {
                MixBlock(world, ignored, nullptr);
            }
            bool farthestSix = true;
            for (int index = 0; index < 20; ++index)
            {
                farthestSix = farthestSix && (world.IsAudible(field[static_cast<size_t>(index)]) == (index >= 14));
            }
            const ludifex::AudioStats far = world.GetAudioStats();

            std::printf("      (%d playing, %d virtual, then %d playing, %d virtual, %d gave way)\n", near.Playing,
                        near.Virtual, far.Playing, far.Virtual, far.Stolen);

            Check(near.Playing == 6 && near.Virtual == 14, "twenty emitters with six voices: six play, fourteen wait");
            Check(nearestSix, "the six playing are the six nearest the listener");
            Check(farthestSix && far.Playing == 6, "moving the listener hands the voices to the new nearest six");
        }

        // --- occlusion and culling ------------------------------------------------
        {
            ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });
            ludifex::Camera3D& camera = world.GetCamera();
            camera.Position = { 0.0f, 0.0f, 0.0f };
            camera.Target = { 0.0f, 0.0f, -1.0f };

            ludifex::Actor3D wall = world.AddBox({
                .Scale = { 6.0f, 6.0f, 0.5f },
                .Position = { 0.0f, 0.0f, -5.0f },
                .Type = ludifex::BodyType::Static,
            });

            world.PlaySoundAt(tone, ludifex::Vec3{ 0.0f, 0.0f, -10.0f }, { .Looping = true, .Occlusion = true });
            float ignored = 0.0f;
            for (int index = 0; index < 3; ++index)
            {
                MixBlock(world, ignored, nullptr);
            }
            const int occluded = world.GetAudioStats().Occluded;

            wall.Destroy();
            for (int index = 0; index < 20; ++index)
            {
                MixBlock(world, ignored, nullptr);
            }
            const int clear = world.GetAudioStats().Occluded;

            const ludifex::VoiceId distant =
                world.PlaySoundAt(tone, ludifex::Vec3{ 0.0f, 0.0f, -500.0f }, { .MaxDistance = 60.0f });

            Check(occluded == 1, "a wall between the listener and a sound occludes it");
            Check(clear == 0, "and removing the wall clears it");
            Check(!distant.IsValid() && world.GetAudioStats().Culled == 1,
                  "a one-shot starting out of range is culled rather than played");
        }

        // --- a listener placed by hand, and an emitter on an actor -----------------
        {
            ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

            // The camera looks from somewhere else entirely: what is heard must
            // follow the listener, not the view.
            ludifex::Camera3D& camera = world.GetCamera();
            camera.Position = { 40.0f, 20.0f, 40.0f };
            camera.Target = { 0.0f, 0.0f, 0.0f };

            world.GetAudioSettings().ListenerFollowsCamera = false;
            world.SetListener({ 0.0f, 1.6f, 0.0f }, { 0.0f, 0.0f, -1.0f });

            world.AddBox({
                .Scale = { 8.0f, 4.0f, 0.5f },
                .Position = { 0.0f, 2.0f, -5.0f },
                .Type = ludifex::BodyType::Static,
            });

            // A sensor bell on a post, which is how an emitter is usually
            // mounted: the sound is on the actor, and the actor is not solid.
            ludifex::Actor3D bell = world.AddSphere({
                .Radius = 0.3f,
                .Position = { 0.0f, 1.7f, -10.0f },
                .Type = ludifex::BodyType::Static,
                .IsSensor = true,
            });
            const ludifex::VoiceId voice =
                world.PlaySoundAt(tone, bell, { .Looping = true, .MaxDistance = 40.0f, .Occlusion = true });

            float ignored = 0.0f;
            for (int index = 0; index < 3; ++index)
            {
                MixBlock(world, ignored, nullptr);
            }
            const ludifex::AudioStats behind = world.GetAudioStats();
            const bool reportedBehind = world.IsOccluded(voice);

            // Step around the end of the wall; the sound opens up again.
            world.SetListener({ 12.0f, 1.6f, -10.0f }, { -1.0f, 0.0f, 0.0f });
            for (int index = 0; index < 20; ++index)
            {
                MixBlock(world, ignored, nullptr);
            }
            const ludifex::AudioStats beside = world.GetAudioStats();

            Check(behind.Playing == 1 && behind.Occluded == 1,
                  "a hand-placed listener hears an actor's emitter, muffled by the wall between them");
            Check(beside.Occluded == 0, "and moving the listener into the clear un-muffles it");
            Check(reportedBehind && !world.IsOccluded(voice), "and the voice itself says which it is");
        }

        // --- Doppler ---------------------------------------------------------------
        {
            ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });
            ludifex::Camera3D& camera = world.GetCamera();
            camera.Position = { 0.0f, 0.0f, 0.0f };
            camera.Target = { 0.0f, 0.0f, -1.0f };

            // Zero crossings of the mono mix over half a second give its
            // frequency.
            auto MeasureHertz = [&](ludifex::World3D& target) {
                std::vector<float> capture;
                float ignored = 0.0f;
                for (int index = 0; index < 5; ++index)
                {
                    MixBlock(target, ignored, nullptr);
                }
                for (int index = 0; index < 50; ++index)
                {
                    MixBlock(target, ignored, &capture);
                }
                int crossings = 0;
                for (size_t index = 1; index < capture.size(); ++index)
                {
                    crossings += (capture[index - 1] < 0.0f) != (capture[index] < 0.0f) ? 1 : 0;
                }
                return static_cast<float>(crossings) / 2.0f / (static_cast<float>(capture.size()) / 48000.0f);
            };

            ludifex::Actor3D still = world.AddSphere({ .Position = { 0.0f, 0.0f, -20.0f }, .Type = ludifex::BodyType::Kinematic });
            ludifex::VoiceId voice = world.PlaySoundAt(tone, still, { .Looping = true, .MaxDistance = 400.0f });
            const float atRest = MeasureHertz(world);
            world.StopSound(voice, 0.0f);

            ludifex::Actor3D racer = world.AddSphere({ .Position = { 0.0f, 0.0f, -300.0f }, .Type = ludifex::BodyType::Kinematic });
            racer.SetLinearVelocity({ 0.0f, 0.0f, 30.0f });
            voice = world.PlaySoundAt(tone, racer, { .Looping = true, .MaxDistance = 400.0f });
            const float approaching = MeasureHertz(world);

            const float expected = 220.0f * 343.0f / (343.0f - 30.0f);
            std::printf("      (at rest %.1f Hz, approaching at 30 m/s %.1f Hz, expected %.1f Hz)\n",
                        static_cast<double>(atRest), static_cast<double>(approaching), static_cast<double>(expected));

            Check(std::abs(atRest - 220.0f) < 3.0f, "a still emitter is heard at its own pitch");
            Check(std::abs(approaching - expected) < 3.0f, "an approaching one is raised by the Doppler ratio");
        }
    }

    // -----------------------------------------------------------------------
    std::printf("\n15. Skeletons and blending\n");

    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path();
        const std::string modelPath = (directory / "ludifex-check-column.gltf").string();

        // Five joints stacked up Y, two clips that disagree: "sway" loops and
        // rocks about Z, "curl" runs once and bends about X. Two clips that
        // look alike would prove nothing about blending between them.
        Check(examples::WriteGltfSkinnedColumn(modelPath, 5, 0.4f, 0.16f),
              "a skinned model with two clips is written");

        ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });
        ludifex::Actor3D column = world.AddModel({ .Path = modelPath, .Type = ludifex::BodyType::Static });
        world.ApplyPendingChanges();

        Check(column.IsValid(), "and loads as an actor");
        Check(column.GetJointCount() == 5, "with its five joints");
        Check(std::string(column.GetJointName(0)) == "joint0" &&
                  std::string(column.GetJointName(4)) == "joint4",
              "named as the file named them");
        Check(column.FindJoint("joint3") == 3, "and findable by name");
        Check(column.FindJoint("elbow") == -1, "while a name it does not have is -1, not a guess");

        Check(column.GetAnimationCount() == 2, "both clips arrived");

        const int sway = column.FindAnimation("sway");
        const int curl = column.FindAnimation("curl");
        Check(sway >= 0 && curl >= 0, "and are findable by the names the file gave them");
        Check(std::abs(column.GetAnimationDuration(sway) - 1.0f) < 0.01f, "with their own durations");

        // The joints are stacked, so the top one starts four segments up.
        // Nothing has posed it yet, so its transform is still the identity.
        Check(!column.IsAnimating(), "nothing is playing to begin with");

        // --- a clip moves the skeleton --------------------------------------

        column.Play({ .Name = "sway", .Fade = 0.0f });
        Check(column.IsAnimating(), "playing a clip starts it");

        world.Update(1.0f / 60.0f);

        const ludifex::Transform3 rest = column.GetJointTransform(4);
        Check(std::abs(rest.Position.Y - 1.6f) < 0.05f,
              "the top joint stands four segments up, where the file put it");

        // A quarter of the way into the sway is its extreme, so the top joint
        // should have swung a long way off the axis it started on.
        column.SetAnimationTime(0.25f);
        world.Update(0.0f);

        const ludifex::Transform3 swung = column.GetJointTransform(4);
        const float swayOffset = std::abs(swung.Position.X - rest.Position.X);
        std::printf("      (top joint moved %.3f m across at the sway's extreme)\n",
                    static_cast<double>(swayOffset));
        Check(swayOffset > 0.15f, "and a clip visibly moves it");
        Check(std::abs(swung.Position.Z - rest.Position.Z) < 0.01f,
              "about the axis the clip names and no other");

        // --- a crossfade is between, not either -----------------------------

        column.StopAnimation(0.0f);
        column.Play({ .Name = "curl", .Fade = 0.0f });
        column.SetAnimationTime(1.0f);
        world.Update(0.0f);

        const ludifex::Transform3 curled = column.GetJointTransform(4);
        const float curlOffset = std::abs(curled.Position.Z - rest.Position.Z);
        std::printf("      (and %.3f m forward at the curl's end)\n", static_cast<double>(curlOffset));
        Check(curlOffset > 0.15f, "the other clip moves it a different way");

        // Both at once, each at full weight. Weights are normalized, so this
        // is half of each, and the pose should land between the two.
        // The sway is held at its extreme so that only the fade is changing;
        // a running clip would pass through its neutral pose partway and make
        // the blend hard to check.
        column.StopAnimation(0.0f);
        column.Play({ .Name = "sway", .Speed = 0.0f, .StartTime = 0.25f, .Fade = 0.0f });
        column.Play({ .Name = "curl", .Speed = 0.0f, .StartTime = 1.0f, .Fade = 0.5f });
        world.Update(0.25f); // halfway through the fade

        const ludifex::Transform3 mixed = column.GetJointTransform(4);
        const float mixedAcross = std::abs(mixed.Position.X - rest.Position.X);
        const float mixedForward = std::abs(mixed.Position.Z - rest.Position.Z);

        std::printf("      (halfway through the fade: %.3f m across, %.3f m forward)\n",
                    static_cast<double>(mixedAcross), static_cast<double>(mixedForward));

        Check(mixedAcross > 0.02f && mixedForward > 0.02f,
              "a crossfade shows both clips at once rather than one of them");
        Check(mixedAcross < swayOffset && mixedForward < curlOffset,
              "and neither at full strength, because the weights are normalized");

        // --- looping, finishing, and scrubbing ------------------------------

        column.StopAnimation(0.0f);
        column.Play({ .Name = "curl", .Loop = false, .Fade = 0.0f });
        for (int step = 0; step < 90; ++step)
        {
            world.Update(1.0f / 60.0f);
        }
        Check(column.IsAnimationFinished(), "a clip that does not loop finishes");
        Check(std::abs(column.GetAnimationTime() - 1.0f) < 0.01f, "and holds its last pose");

        column.StopAnimation(0.0f);
        column.Play({ .Name = "sway", .Loop = true, .Fade = 0.0f });
        for (int step = 0; step < 90; ++step)
        {
            world.Update(1.0f / 60.0f);
        }
        Check(!column.IsAnimationFinished(), "a clip that loops never does");
        Check(column.GetAnimationTime() < 1.0f, "and wraps rather than running off the end");

        // --- one model, separate poses --------------------------------------

        ludifex::Actor3D second = world.AddModel({ .Path = modelPath, .Position = { 2.0f, 0.0f, 0.0f },
                                                   .Type = ludifex::BodyType::Static });
        world.ApplyPendingChanges();

        second.Play({ .Name = "curl", .Fade = 0.0f });
        second.SetAnimationTime(1.0f);
        world.Update(0.0f);

        const ludifex::Transform3 firstTop = column.GetJointTransform(4);
        const ludifex::Transform3 secondTop = second.GetJointTransform(4);

        Check(std::abs(secondTop.Position.X - 2.0f) < 0.5f,
              "a second actor from the same file stands where it was put");
        Check(std::abs(firstTop.Position.Z - secondTop.Position.Z) > 0.1f,
              "and holds its own pose rather than sharing the first's");

        // --- what is not a skeleton -----------------------------------------

        ludifex::Actor3D box = world.AddBox({ .Position = { -3.0f, 0.0f, 0.0f }, .Name = "crate" });
        world.ApplyPendingChanges();

        Check(box.GetAnimationCount() == 0 && box.GetJointCount() == 0,
              "a box has no skeleton and says so");
        Check(!box.IsAnimating(), "asking a box to animate is a no-op, not a crash");
        ++g_Warnings; // Play on a box reports one
        box.Play({ .Name = "sway" });
        Check(!box.IsAnimating(), "and it stays a box");

        // --- two skins over one skeleton ------------------------------------
        //
        // The second column's vertices index a skin that lists the joints the
        // other way round. Read through its own skin it bends with the first;
        // read through the first's, its top would be carried by the bottom
        // joint, and its top vertex would stay put while the first's swung.

        const std::string twoSkinPath = (directory / "ludifex-check-two-skins.gltf").string();
        Check(examples::WriteGltfSkinnedColumn(twoSkinPath, 5, 0.4f, 0.16f, true),
              "a model with two skins over one skeleton is written");

        ludifex::Actor3D pair = world.AddModel({ .Path = twoSkinPath, .Position = { 6.0f, 0.0f, 0.0f },
                                                 .Type = ludifex::BodyType::Static });
        world.ApplyPendingChanges();
        Check(pair.IsValid() && pair.GetJointCount() == 5,
              "and loads with its five bones once, not once a skin");

        // The collider is fitted to everything the model draws, so a model
        // with both columns is about three times as wide as one column, and
        // with the same density, about three times as heavy.
        ludifex::Actor3D loneBody = world.AddModel({ .Path = modelPath, .Position = { 9.0f, 0.0f, 0.0f } });
        ludifex::Actor3D pairBody = world.AddModel({ .Path = twoSkinPath, .Position = { 12.0f, 0.0f, 0.0f } });
        world.ApplyPendingChanges();
        std::printf("      (one column's collider weighs %.3f kg, the pair's %.3f kg)\n",
                    static_cast<double>(loneBody.GetMass()), static_cast<double>(pairBody.GetMass()));
        Check(pairBody.GetMass() > loneBody.GetMass() * 2.0f, "and draws both of its meshes, one a skin");
        loneBody.Destroy();
        pairBody.Destroy();

        pair.Play({ .Name = "curl", .Speed = 0.0f, .StartTime = 1.0f, .Fade = 0.0f });
        world.Update(0.0f);
        Check(pair.IsAnimating() && std::abs(pair.GetJointTransform(4).Position.Z -
                                             pair.GetJointTransform(0).Position.Z) > 0.15f,
              "and both skins' bones follow the clip");

        // --- morph targets ----------------------------------------------------

        const std::string morphPath = (directory / "ludifex-check-morphs.gltf").string();
        Check(examples::WriteGltfMorphPanels(morphPath), "a model with morph targets is written");

        ludifex::Actor3D panels = world.AddModel({ .Path = morphPath, .Position = { 0.0f, 0.0f, 6.0f },
                                                   .Type = ludifex::BodyType::Static });
        world.ApplyPendingChanges();

        Check(panels.IsValid(), "a flat model is given a thin collider rather than refused");
        Check(panels.GetMorphTargetCount() == 3, "its three blend shapes arrive, across both meshes");
        Check(std::string(panels.GetMorphTargetName(0)) == "Bulge" &&
                  std::string(panels.GetMorphTargetName(1)) == "Stretch" &&
                  std::string(panels.GetMorphTargetName(2)) == "Wave",
              "named as the file named them");
        Check(panels.FindMorphTarget("Wave") == 2 && panels.FindMorphTarget("Smile") == -1,
              "and findable by name");
        Check(std::abs(panels.GetMorphWeight(0) - 0.25f) < 1e-4f && panels.GetMorphWeight(2) == 0.0f,
              "each starting at the weight its node gives it");
        Check(panels.GetAnimationCount() == 1 && panels.GetJointCount() == 0,
              "a model with no skeleton can still have a clip, for its weights");

        panels.SetMorphWeight("Wave", 0.7f);
        panels.SetMorphWeight(1, 0.3f);
        Check(std::abs(panels.GetMorphWeight(2) - 0.7f) < 1e-4f && std::abs(panels.GetMorphWeight(1) - 0.3f) < 1e-4f,
              "a weight set by hand is the weight drawn");

        // Half way to the clip's first key: Bulge heads for one and Stretch for
        // a half. The flag is not in the clip, so Wave keeps its own weight.
        panels.Play({ .Name = "breathe", .Speed = 0.0f, .StartTime = 0.5f, .Fade = 0.0f });
        world.Update(0.0f);
        std::printf("      (half a second in: Bulge %.3f, Stretch %.3f, Wave %.3f)\n",
                    static_cast<double>(panels.GetMorphWeight(0)), static_cast<double>(panels.GetMorphWeight(1)),
                    static_cast<double>(panels.GetMorphWeight(2)));
        Check(std::abs(panels.GetMorphWeight(0) - 0.5f) < 0.01f && std::abs(panels.GetMorphWeight(1) - 0.25f) < 0.01f,
              "a clip drives the weights of the mesh it animates");
        Check(std::abs(panels.GetMorphWeight(2) - 0.7f) < 1e-4f, "and leaves another mesh's weights alone");

        // Stopped, the weights go back to the actor's own.
        panels.StopAnimation(0.0f);
        world.Update(1.0f / 60.0f);
        Check(!panels.IsAnimating() && std::abs(panels.GetMorphWeight(0) - 0.25f) < 1e-4f &&
                  std::abs(panels.GetMorphWeight(1) - 0.3f) < 1e-4f,
              "and when it stops, the actor's own weights come back");

        Check(column.GetMorphTargetCount() == 0 && box.GetMorphTargetCount() == 0,
              "a model or a box without morph targets has none");
    }

    // -----------------------------------------------------------------------
    std::printf("\n16. Caching by contents, and reloading\n");

    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path();
        const std::string firstPath = (directory / "ludifex-check-shared-a.gltf").string();
        const std::string secondPath = (directory / "ludifex-check-shared-b.gltf").string();
        const std::string editedPath = (directory / "ludifex-check-edited.gltf").string();

        // Two names, one file's worth of contents.
        examples::WriteGltfSphere(firstPath, 8, 0.5f);
        examples::WriteGltfSphere(secondPath, 8, 0.5f);

        // And one that is going to change under us.
        examples::WriteGltfSphere(editedPath, 8, 0.5f);

        ludifex::World3D world = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

        ludifex::Actor3D first = world.AddModel({ .Path = firstPath, .Type = ludifex::BodyType::Static });
        ludifex::Actor3D second = world.AddModel({ .Path = secondPath, .Position = { 3.0f, 0.0f, 0.0f },
                                                   .Type = ludifex::BodyType::Static });
        world.ApplyPendingChanges();

        Check(first.IsValid() && second.IsValid(), "two models load under two names");

        // Two spheres of the same radius would measure the same anyway. What
        // matters is that the second load reported sharing the first one's
        // parse (in the log above). The scale comes from the bounds, so equal
        // bounds is the part that can be checked here.
        Check(std::abs(first.GetScale().X - second.GetScale().X) < 1e-6f,
              "two names for the same contents give the same model");

        // Hot reload is off by default, as a release build needs.
        Check(!ludifex::IsAssetHotReloadEnabled(), "hot reload is off unless asked for");

        // A changed file is ignored while it is off, however many frames pass.
        const float sharedScale = first.GetScale().X;
        examples::WriteGltfSphere(firstPath, 8, 1.25f);
        std::filesystem::last_write_time(firstPath,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::seconds(2));
        for (int frame = 0; frame < 4; ++frame)
        {
            world.Update(1.0f / 60.0f);
        }
        Check(std::abs(first.GetScale().X - sharedScale) < 1e-6f,
              "and a changed file is ignored while it is off");

        // --- the file changes underneath -------------------------------------
        //
        // Turned on before anything is loaded, as a development build would do
        // at startup. It also turns off the sharing tested above: a mesh shared
        // between two names can only watch one of the files, so editing the
        // other would appear to do nothing.

        ludifex::SetAssetHotReload(true);
        Check(ludifex::IsAssetHotReloadEnabled(), "turning it on takes");

        ludifex::World3D reloading = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

        // Dynamic, because the visible consequence of a model changing shape
        // is its collider changing shape, and a dynamic body's mass is what
        // reports that from outside. GetScale would not: it gives back the
        // scale the caller asked for, which a new file does not change.
        ludifex::Actor3D edited = reloading.AddModel({ .Path = editedPath });
        reloading.ApplyPendingChanges();

        const float beforeMass = edited.GetMass();

        // A bigger sphere at the same path. File timestamps are coarse enough
        // that a write in the same instant can go unnoticed, so the timestamp
        // is moved forward here instead of sleeping.
        examples::WriteGltfSphere(editedPath, 8, 1.25f);
        std::filesystem::last_write_time(editedPath,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::seconds(2));

        // The check is throttled to a few times a second, so this advances
        // real time rather than only the world's.
        for (int frame = 0; frame < 30 && std::abs(edited.GetMass() - beforeMass) < 1e-6f; ++frame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            reloading.Update(1.0f / 60.0f);
        }

        const float afterMass = edited.GetMass();
        std::printf("      (the model weighed %.2f kg, and weighs %.2f after the file changed)\n",
                    static_cast<double>(beforeMass), static_cast<double>(afterMass));

        // Two and a half times the radius is about fifteen times the volume,
        // so a collider that did not follow the model would be obvious.
        Check(afterMass > beforeMass * 5.0f, "a model reloads when its file changes");

        // --- a file that is written but not changed --------------------------

        const float steadyMass = edited.GetMass();
        examples::WriteGltfSphere(editedPath, 8, 1.25f); // the same contents again
        std::filesystem::last_write_time(editedPath,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::seconds(4));

        for (int frame = 0; frame < 12; ++frame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            reloading.Update(1.0f / 60.0f);
        }
        Check(std::abs(edited.GetMass() - steadyMass) < 1e-4f,
              "and a file re-saved without being edited is left alone");

        // --- a file that is changed into something unreadable ----------------

        {
            std::ofstream broken(editedPath, std::ios::binary);
            broken << "this is not a glTF file";
        }
        std::filesystem::last_write_time(editedPath,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::seconds(6));

        ++g_Errors; // the failed parse reports one
        for (int frame = 0; frame < 12; ++frame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            reloading.Update(1.0f / 60.0f);
        }
        Check(std::abs(edited.GetMass() - steadyMass) < 1e-4f,
              "while a file edited into nonsense keeps the model that worked");

        // --- images, which reload the same way ------------------------------

        {
            const std::string imagePath = (directory / "ludifex-check-image.png").string();
            const std::string twinPath = (directory / "ludifex-check-image-twin.png").string();

            auto WriteSquare = [](const std::string& path, int size, uint8_t red) {
                std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
                for (size_t index = 0; index + 3 < pixels.size(); index += 4)
                {
                    pixels[index + 0] = red;
                    pixels[index + 1] = 90;
                    pixels[index + 2] = 160;
                    pixels[index + 3] = 255;
                }
                return examples::WritePng(path, size, size, pixels);
            };

            Check(WriteSquare(imagePath, 16, 200) && WriteSquare(twinPath, 16, 200),
                  "two identical images are written");

            const ludifex::TextureId first = ludifex::LoadTexture(imagePath);
            const ludifex::TextureId twin = ludifex::LoadTexture(twinPath);

            Check(first.IsValid() && twin.IsValid(), "and both load");
            Check(ludifex::GetTextureSize(first).X == 16.0f, "at the size they were written");

            // A bigger image at the same path.
            WriteSquare(imagePath, 48, 40);
            std::filesystem::last_write_time(imagePath,
                                             std::filesystem::file_time_type::clock::now() +
                                                 std::chrono::seconds(8));

            for (int frame = 0; frame < 30 && ludifex::GetTextureSize(first).X == 16.0f; ++frame)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                reloading.Update(1.0f / 60.0f);
            }

            const float size = ludifex::GetTextureSize(first).X;
            std::printf("      (the image was 16 px across, and is %.0f after the file changed)\n",
                        static_cast<double>(size));
            Check(size == 48.0f, "an image reloads when its file changes");

            // The twin is a separate file that was not changed, so it should
            // stay as it was. With hot reload on, it was never shared with the
            // first one, so this checks that only the changed file reloaded.
            Check(ludifex::GetTextureSize(twin).X == 16.0f,
                  "and the file nobody edited is left where it was");
        }

        ludifex::SetAssetHotReload(false);
    }

    // -----------------------------------------------------------------------
    std::printf("\n17. 2D joints, shape queries, and characters\n");

    {
        ludifex::World2D flat = ludifex::CreateWorld2D();
        flat.AddGround({ .Width = 400.0f });

        auto Distance2 = [](const ludifex::Vec2& a, const ludifex::Vec2& b) {
            return std::hypot(b.X - a.X, b.Y - a.Y);
        };
        auto Run = [&](int steps) {
            for (int step = 0; step < steps; ++step)
            {
                flat.StepPhysics(1.0f / 60.0f);
            }
        };

        // An arm on a limited hinge, falling until the limit catches it.
        ludifex::Actor2D post = flat.AddRectangle(
            { .Width = 0.4f, .Height = 0.4f, .Position = { 0.0f, 6.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor2D arm = flat.AddRectangle({ .Width = 2.0f, .Height = 0.2f, .Position = { 1.0f, 6.0f } });
        ludifex::Joint2D hinge = flat.AddHinge({ .BodyA = post, .BodyB = arm, .Anchor = { 0.0f, 6.0f },
                                                 .EnableLimit = true, .LowerAngle = -0.5f, .UpperAngle = 0.5f });

        // A lift on a slider, driven up by its motor until its limit.
        ludifex::Actor2D shaft = flat.AddRectangle(
            { .Width = 0.2f, .Height = 0.2f, .Position = { 10.0f, 2.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor2D lift = flat.AddRectangle({ .Width = 1.0f, .Height = 0.2f, .Position = { 10.0f, 2.0f } });
        ludifex::Joint2D slider = flat.AddSlider({ .BodyA = shaft, .BodyB = lift, .Anchor = { 10.0f, 2.0f },
                                                   .Axis = { 0.0f, 1.0f }, .EnableLimit = true,
                                                   .LowerTranslation = 0.0f, .UpperTranslation = 3.0f,
                                                   .EnableMotor = true, .MotorSpeed = 2.0f,
                                                   .MaxMotorForce = 500.0f });

        // A rope: slack until it is taut at two metres.
        ludifex::Actor2D hook = flat.AddRectangle(
            { .Width = 0.2f, .Height = 0.2f, .Position = { 20.0f, 8.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor2D weight = flat.AddCircle({ .Radius = 0.2f, .Position = { 20.0f, 7.0f } });
        flat.AddDistanceJoint({ .BodyA = hook, .BodyB = weight, .AnchorA = { 20.0f, 8.0f },
                                .AnchorB = { 20.0f, 7.0f }, .EnableSpring = true, .Hertz = 0.0f,
                                .EnableLimit = true, .MinLength = 0.0f, .MaxLength = 2.0f });

        // A weld holding a box out from a wall.
        ludifex::Actor2D wall = flat.AddRectangle(
            { .Width = 0.4f, .Height = 2.0f, .Position = { 30.0f, 5.0f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor2D shelf = flat.AddRectangle({ .Width = 1.0f, .Height = 0.3f, .Position = { 30.7f, 5.0f } });
        flat.AddWeld({ .BodyA = wall, .BodyB = shelf, .Anchor = { 30.2f, 5.0f } });

        // A cart on two sprung wheels, the back one driven.
        const float cartX = 40.0f;
        ludifex::Actor2D chassis =
            flat.AddRectangle({ .Width = 2.0f, .Height = 0.4f, .Position = { cartX, 1.0f }, .Density = 0.5f });
        ludifex::Actor2D backWheel =
            flat.AddCircle({ .Radius = 0.35f, .Position = { cartX - 0.7f, 0.5f }, .Friction = 0.9f });
        ludifex::Actor2D frontWheel =
            flat.AddCircle({ .Radius = 0.35f, .Position = { cartX + 0.7f, 0.5f }, .Friction = 0.9f });
        ludifex::Joint2D drive = flat.AddWheel({ .BodyA = chassis, .BodyB = backWheel,
                                                 .Anchor = { cartX - 0.7f, 0.5f }, .EnableMotor = true,
                                                 .MotorSpeed = -12.0f, .MaxMotorTorque = 20.0f });
        ludifex::Joint2D free = flat.AddWheel({ .BodyA = chassis, .BodyB = frontWheel,
                                                .Anchor = { cartX + 0.7f, 0.5f } });

        Check(flat.GetJointCount() == 6, "six 2D joints exist after creating them");
        Check(drive.GetKind() == ludifex::JointKind::Wheel && free.IsValid(), "a wheel joint is created");

        Run(180);

        const float ropeLength = Distance2(weight.GetPosition(), ludifex::Vec2{ 20.0f, 8.0f });
        std::printf("      (arm %.3f rad, lift %.3f m, rope %.3f m, shelf drift %.4f m, cart moved %.2f m)\n",
                    static_cast<double>(hinge.GetAngle()), static_cast<double>(slider.GetTranslation()),
                    static_cast<double>(ropeLength),
                    static_cast<double>(Distance2(shelf.GetPosition(), ludifex::Vec2{ 30.7f, 5.0f })),
                    static_cast<double>(chassis.GetPosition().X - cartX));

        Check(hinge.GetAngle() < -0.4f && hinge.GetAngle() > -0.55f, "a 2D hinge limit catches a falling arm");
        Check(std::abs(slider.GetTranslation() - 3.0f) < 0.05f, "a slider's motor drives it to its limit");
        Check(std::abs(slider.GetMotorEffort()) > 0.0f, "a slider's motor reports the force it applies");
        Check(ropeLength > 1.9f && ropeLength < 2.05f, "a rope goes slack, then holds at its length");
        Check(Distance2(shelf.GetPosition(), ludifex::Vec2{ 30.7f, 5.0f }) < 0.05f,
              "a 2D weld holds its body against gravity");
        Check(chassis.GetPosition().X - cartX > 2.0f, "a driven wheel carries its cart along");

        // Changing a joint wakes what it holds: the arm has come to rest
        // against its limit, and widening the limit lets it fall further.
        Run(120);
        hinge.SetLimits(-1.2f, 0.5f);
        Run(120);
        Check(hinge.GetAngle() < -1.0f, "changing a sleeping joint's limit takes effect");

        // Destroying the chassis takes both wheel joints with it.
        chassis.Destroy();
        flat.ApplyPendingChanges();
        Check(!drive.IsValid() && !free.IsValid(), "destroying an actor invalidates its 2D joints");
        Check(flat.GetJointCount() == 4, "the reclaimed 2D joints are no longer counted");

        hinge.Destroy();
        flat.ApplyPendingChanges();
        Check(!hinge.IsValid() && flat.GetJointCount() == 3, "a 2D joint can be destroyed on its own");

        // A capsule, dropped.
        ludifex::Actor2D pill = flat.AddCapsule({ .Radius = 0.25f, .Height = 1.0f, .Position = { 60.0f, 3.0f } });
        Run(120);
        Check(pill.IsValid() && pill.GetMass() > 0.0f && pill.GetPosition().Y > 0.2f && pill.GetPosition().Y < 0.55f,
              "a 2D capsule falls and comes to rest on the ground");

        // Shape queries.
        ludifex::Actor2D crate = flat.AddRectangle(
            { .Width = 1.0f, .Height = 1.0f, .Position = { 80.0f, 0.5f }, .Type = ludifex::BodyType::Static });
        ludifex::Actor2D zone =
            flat.AddCircle({ .Radius = 1.0f, .Position = { 84.0f, 1.0f }, .Type = ludifex::BodyType::Static,
                            .IsSensor = true });

        const ludifex::RayHit2D fall = flat.CastCircle({ 90.0f, 5.0f }, 0.5f, { 0.0f, -1.0f }, 10.0f);
        std::printf("      (a circle cast down from 5 m stopped after %.3f m)\n", static_cast<double>(fall.Distance));
        Check(fall.Hit && std::abs(fall.Distance - 4.5f) < 0.05f && fall.Normal.Y > 0.99f,
              "a circle cast stops where the circle would touch the ground");

        const ludifex::RayHit2D across = flat.CastRectangle({ 76.0f, 0.5f }, { 0.4f, 0.4f }, { 1.0f, 0.0f }, 10.0f);
        Check(across.Hit && across.Actor == crate && std::abs(across.Distance - 3.3f) < 0.05f,
              "a rectangle cast finds the crate in its way");

        const ludifex::RayHit2D pastZone = flat.CastCapsule({ 82.0f, 1.0f }, 0.2f, 1.0f, { 1.0f, 0.0f }, 5.0f);
        Check(!pastZone.Hit, "a cast passes through a sensor");

        const std::vector<ludifex::Actor2D> nearCrate = flat.OverlapCircle({ 80.0f, 1.2f }, 0.3f);
        Check(nearCrate.size() == 1 && nearCrate[0] == crate, "an overlap finds what is there");
        Check(flat.OverlapCircle({ 84.0f, 1.0f }, 0.2f).empty() &&
                  flat.OverlapCircle({ 84.0f, 1.0f }, 0.2f, true).size() == 1,
              "an overlap leaves sensors out unless asked");
        const std::vector<ludifex::Actor2D> atPoint = flat.OverlapPoint({ 80.2f, 0.3f });
        Check(atPoint.size() == 1 && atPoint[0] == crate, "a point overlap finds the shape under it");
        Check(flat.OverlapRectangle({ 80.0f, 3.0f }, { 1.0f, 1.0f }).empty(), "an overlap in empty air finds nothing");
        Check(flat.OverlapCapsule({ 80.0f, 0.7f }, 0.2f, 1.5f).size() == 2,
              "a capsule overlap finds the crate and the ground under it");

        // A character: dropped, walked into a wall, and up a step.
        ludifex::Character2D hero = flat.AddCharacter({ .Position = { 100.0f, 2.0f }, .Name = "Hero" });
        Check(hero.IsValid() && hero.GetActor().GetName() == "Hero", "a 2D character is created");

        for (int frame = 0; frame < 60; ++frame)
        {
            hero.Move({ 0.0f, -0.1f });
        }
        std::printf("      (the character landed with its middle at %.3f m)\n",
                    static_cast<double>(hero.GetPosition().Y));
        Check(hero.IsOnGround() && std::abs(hero.GetPosition().Y - 0.6f) < 0.04f,
              "a 2D character lands on the ground and stands on it");

        flat.AddRectangle({ .Width = 0.5f, .Height = 3.0f, .Position = { 103.0f, 1.5f },
                            .Type = ludifex::BodyType::Static });
        ludifex::Vec2 lastMove;
        for (int frame = 0; frame < 60; ++frame)
        {
            lastMove = hero.Move({ 0.1f, -0.05f });
        }
        const float wallFace = 103.0f - 0.25f - 0.3f;
        std::printf("      (stopped %.3f m short of the wall)\n", static_cast<double>(wallFace - hero.GetPosition().X));
        Check(hero.GetPosition().X < wallFace + 0.001f && hero.GetPosition().X > wallFace - 0.05f,
              "a 2D character stops at a wall");
        Check(std::abs(lastMove.X) < 0.01f && hero.IsOnGround(), "and reports that it no longer moves into it");

        // A kerb low enough to step onto, behind it.
        flat.AddRectangle({ .Width = 2.0f, .Height = 0.15f, .Position = { 99.0f, 0.075f },
                            .Type = ludifex::BodyType::Static });
        for (int frame = 0; frame < 40; ++frame)
        {
            hero.Move({ -0.1f, -0.05f });
        }
        std::printf("      (after the kerb the character is at %.2f, %.3f)\n",
                    static_cast<double>(hero.GetPosition().X), static_cast<double>(hero.GetPosition().Y));
        Check(hero.GetPosition().X < 99.5f && hero.GetPosition().Y > 0.7f && hero.IsOnGround(),
              "a 2D character steps up a low kerb");

        // A wall too tall to step.
        flat.AddRectangle({ .Width = 0.5f, .Height = 1.0f, .Position = { 96.0f, 0.5f },
                            .Type = ludifex::BodyType::Static });
        for (int frame = 0; frame < 60; ++frame)
        {
            hero.Move({ -0.1f, -0.05f });
        }
        Check(hero.GetPosition().X > 96.25f, "but not a wall");

        // A ramp gentle enough to stand on, and one too steep.
        auto Ramp = [&](float x, float degrees) {
            flat.AddRectangle({ .Width = 6.0f, .Height = 0.4f, .Position = { x, 1.0f },
                                .Rotation = degrees * 3.14159265f / 180.0f, .Type = ludifex::BodyType::Static });
        };
        Ramp(120.0f, 25.0f);
        Ramp(140.0f, 70.0f);

        hero.SetPosition({ 120.0f, 4.0f });
        for (int frame = 0; frame < 80; ++frame)
        {
            hero.Move({ 0.0f, -0.1f });
        }
        Check(hero.IsOnGround() && hero.GetGroundNormal().Y < 0.95f && hero.GetGroundNormal().Y > 0.85f,
              "a gentle ramp is ground, and its tilt is reported");

        hero.SetPosition({ 139.0f, 4.0f });
        for (int frame = 0; frame < 20; ++frame)
        {
            hero.Move({ 0.0f, -0.05f });
        }
        Check(!hero.IsOnGround(), "a steep ramp is not");

        // Dynamic bodies see the character: a ball dropped on its head bounces off.
        hero.SetPosition({ 160.0f, 0.62f });
        hero.Move({ 0.0f, -0.05f });
        ludifex::Actor2D ball = flat.AddCircle({ .Radius = 0.2f, .Position = { 160.0f, 3.0f } });
        Run(90);
        Check(ball.GetPosition().Y > 1.2f, "a dynamic body lands on a 2D character rather than passing through");

        hero.Destroy();
        flat.ApplyPendingChanges();
        Check(!hero.IsValid(), "a destroyed 2D character is no longer valid");
    }

    // -----------------------------------------------------------------------
    std::printf("\n18. Model formats beyond glTF\n");

    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path();
        ludifex::World3D formats = ludifex::CreateWorld3D({ .Gravity = { 0.0f, 0.0f, 0.0f } });

        // The collider is fitted to the model's bounds, and at a density of 1
        // its mass is its volume: a check of the size and the units at once.
        auto MassOf = [&](const std::string& path) {
            ludifex::Actor3D actor = formats.AddModel({ .Path = path, .Position = { 0.0f, 20.0f, 0.0f } });
            formats.ApplyPendingChanges();
            const float mass = actor.IsValid() ? actor.GetMass() : -1.0f;
            std::printf("      (%s: %.3f kg)\n", std::filesystem::path(path).filename().string().c_str(),
                        static_cast<double>(mass));
            actor.Destroy();
            formats.ApplyPendingChanges();
            return mass;
        };

        // --- OBJ, with a material library and a texture ----------------------

        const std::string objPath = (directory / "ludifex-check-box.obj").string();
        Check(examples::WriteObjBox(objPath, 2.0f, 1.0f, 1.0f, "ludifex-check-brick.png"),
              "an OBJ box with a textured material is written");
        int errorsBefore = g_Errors;
        Check(std::abs(MassOf(objPath) - 2.0f) < 0.05f, "OBJ: it loads at the size it was written, 2 by 1 by 1");
        Check(g_Errors == errorsBefore, "with its material library and texture found");

        // --- FBX, in metres and in centimetres ----------------------------------

        const std::string metresPath = (directory / "ludifex-check-metres.fbx").string();
        const std::string centimetresPath = (directory / "ludifex-check-centimetres.fbx").string();
        Check(examples::WriteFbxBox(metresPath, 2.0f, 1.0f, 1.0f, 100.0, false) &&
                  examples::WriteFbxBox(centimetresPath, 200.0f, 100.0f, 100.0f, 1.0, false),
              "FBX boxes are written, one in metres and one in centimetres");
        errorsBefore = g_Errors;
        Check(std::abs(MassOf(metresPath) - 2.0f) < 0.05f, "FBX: the one in metres loads at 2 by 1 by 1");
        Check(std::abs(MassOf(centimetresPath) - 2.0f) < 0.05f,
              "and the one in centimetres arrives in metres, the same size");
        Check(g_Errors == errorsBefore, "and neither reports anything");

        // --- FBX lying Z up -------------------------------------------------------

        // Three metres tall along Z, as a Z-up tool writes it. Stood up the
        // way ludifex has the world, its top is 1.5 m above its middle; left
        // lying, a ray from above would meet it at half a metre.
        const std::string zUpPath = (directory / "ludifex-check-z-up.fbx").string();
        Check(examples::WriteFbxBox(zUpPath, 1.0f, 1.0f, 3.0f, 100.0, true), "a Z-up FBX pillar is written");
        ludifex::Actor3D pillar = formats.AddModel({ .Path = zUpPath, .Type = ludifex::BodyType::Static });
        formats.ApplyPendingChanges();
        const ludifex::RayHit top = formats.CastRay({ 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 20.0f);
        std::printf("      (a ray from above meets the pillar at y = %.3f)\n", static_cast<double>(top.Point.Y));
        Check(top.Hit && std::abs(top.Point.Y - 1.5f) < 0.05f, "FBX: a Z-up file is stood upright, Y up");
        pillar.Destroy();
        formats.ApplyPendingChanges();

        // --- Collada, skinned, with a clip -----------------------------------------

        const std::string columnPath = (directory / "ludifex-check-column.dae").string();
        Check(examples::WriteColladaBendingColumn(columnPath), "a skinned Collada column with a clip is written");
        ludifex::Actor3D column = formats.AddModel({ .Path = columnPath, .Type = ludifex::BodyType::Static });
        formats.ApplyPendingChanges();

        const int joint0 = column.FindJoint("joint0");
        const int joint1 = column.FindJoint("joint1");
        Check(column.IsValid() && joint0 >= 0 && joint1 >= 0, "Collada: it loads with both joints, found by name");
        Check(column.GetAnimationCount() == 1 && std::abs(column.GetAnimationDuration(0) - 1.0f) < 0.01f,
              "and its one clip, a second long");

        column.Play({ .Index = 0, .Fade = 0.0f });
        column.SetAnimationTime(1.0f);
        formats.Update(0.0f);
        const ludifex::Transform3 bent = column.GetJointTransform(joint1);
        std::printf("      (joint1 at %.2f, %.2f, %.2f, turned %.3f about Z)\n", static_cast<double>(bent.Position.X),
                    static_cast<double>(bent.Position.Y), static_cast<double>(bent.Position.Z),
                    static_cast<double>(bent.Rotation.Z));
        Check(std::abs(std::abs(bent.Rotation.Z) - 0.7071f) < 0.02f,
              "and playing it turns joint1 a quarter turn about Z");
        Check(std::abs(bent.Position.Y - 1.0f) < 0.02f && std::abs(bent.Position.X) < 0.02f,
              "about its own place, a metre up the column");
        column.Destroy();
        formats.ApplyPendingChanges();

        // --- STL and PLY -------------------------------------------------------------

        const std::string stlPath = (directory / "ludifex-check-tetrahedron.stl").string();
        const std::string plyPath = (directory / "ludifex-check-square.ply").string();
        Check(examples::WriteStlTetrahedron(stlPath) && examples::WritePlySquare(plyPath),
              "an STL tetrahedron and a PLY square are written");
        Check(std::abs(MassOf(stlPath) - 1.0f) < 0.05f, "STL: it loads, a metre each way");
        Check(MassOf(plyPath) > 0.0f, "PLY: it loads too");

        // --- what is not a model ---------------------------------------------------

        const std::string brokenPath = (directory / "ludifex-check-broken.fbx").string();
        {
            std::ofstream broken(brokenPath);
            broken << "this is not a model\n";
        }
        errorsBefore = g_Errors;
        ludifex::Actor3D nothing = formats.AddModel({ .Path = brokenPath });
        formats.ApplyPendingChanges();
        Check(g_Errors > errorsBefore, "a file that is not a model is reported, not crashed on");
        nothing.Destroy();
        formats.ApplyPendingChanges();
    }

    // -----------------------------------------------------------------------
    std::printf("\n19. Graphics quality presets\n");

    {
        using ludifex::GraphicsQuality;
        const GraphicsQuality presets[] = { GraphicsQuality::Potato, GraphicsQuality::Low,  GraphicsQuality::Medium,
                                            GraphicsQuality::High,   GraphicsQuality::Ultra, GraphicsQuality::Extreme };

        auto Preset = [](GraphicsQuality quality) {
            ludifex::RenderSettings settings;
            ludifex::ApplyGraphicsQuality(settings, quality);
            return settings;
        };

        // A world starts at High: applying High to the defaults changes none
        // of the fields a preset sets.
        const ludifex::RenderSettings defaults;
        const ludifex::RenderSettings high = Preset(GraphicsQuality::High);
        Check(defaults.RenderScale == high.RenderScale && defaults.Mode == high.Mode &&
                  defaults.Shadows.Enabled == high.Shadows.Enabled &&
                  defaults.Shadows.Resolution == high.Shadows.Resolution &&
                  defaults.Shadows.Cascades == high.Shadows.Cascades &&
                  defaults.Shadows.Distance == high.Shadows.Distance &&
                  defaults.Shadows.Softness == high.Shadows.Softness &&
                  defaults.AmbientOcclusion.Enabled == high.AmbientOcclusion.Enabled &&
                  defaults.AmbientOcclusion.Samples == high.AmbientOcclusion.Samples &&
                  defaults.Bloom.Enabled == high.Bloom.Enabled && defaults.Detail.Enabled == high.Detail.Enabled &&
                  defaults.Detail.Simpler == high.Detail.Simpler && defaults.Detail.Simplest == high.Detail.Simplest,
              "the default settings are the High preset");

        // Each preset asks for at least as much as the one below it.
        bool rising = true;
        for (size_t index = 1; index < std::size(presets); ++index)
        {
            const ludifex::RenderSettings lower = Preset(presets[index - 1]);
            const ludifex::RenderSettings higher = Preset(presets[index]);
            const auto ShadowTexels = [](const ludifex::RenderSettings& settings) {
                return settings.Shadows.Enabled ? settings.Shadows.Resolution * settings.Shadows.Cascades : 0;
            };
            const auto Occlusion = [](const ludifex::RenderSettings& settings) {
                return settings.AmbientOcclusion.Enabled ? settings.AmbientOcclusion.Samples : 0;
            };
            rising = rising && higher.RenderScale >= lower.RenderScale && ShadowTexels(higher) >= ShadowTexels(lower) &&
                     higher.Shadows.Distance >= lower.Shadows.Distance && Occlusion(higher) >= Occlusion(lower) &&
                     (higher.Bloom.Enabled || !lower.Bloom.Enabled);
        }
        Check(rising, "each preset draws at least as much as the one below it");

        const ludifex::RenderSettings potato = Preset(GraphicsQuality::Potato);
        const ludifex::RenderSettings extreme = Preset(GraphicsQuality::Extreme);
        Check(potato.RenderScale == 0.5f && !potato.Shadows.Enabled && !potato.AmbientOcclusion.Enabled &&
                  potato.Mode == ludifex::AntiAliasing::Off,
              "Potato draws at half resolution, without shadows, occlusion, or anti-aliasing");
        Check(extreme.RenderScale > 1.0f && extreme.Shadows.Cascades == 4 && !extreme.Detail.Enabled,
              "Extreme draws more pixels than it shows, with four cascades and every mesh at full detail");

        // A preset is how much work, never how the world looks, and never
        // whether it is traced.
        ludifex::RenderSettings styled;
        styled.SkyColor = ludifex::Color{ 0.1f, 0.2f, 0.3f, 1.0f };
        styled.Sky.Enabled = true;
        styled.Grading.Curve = ludifex::ToneCurve::Filmic;
        styled.Grading.Vignette = 0.4f;
        styled.Exposure = 1.5f;
        styled.Fog.Enabled = true;
        styled.RayTracing.Shadows = true;
        styled.RayTracing.Reflections = true;
        styled.PathTracing.Enabled = true;
        ludifex::ApplyGraphicsQuality(styled, GraphicsQuality::Potato);
        Check(styled.SkyColor.B == 0.3f && styled.Sky.Enabled && styled.Grading.Curve == ludifex::ToneCurve::Filmic &&
                  styled.Grading.Vignette == 0.4f && styled.Exposure == 1.5f && styled.Fog.Enabled,
              "a preset leaves the look alone: sky, grading, exposure, fog");
        Check(styled.RayTracing.Shadows && styled.RayTracing.Reflections && styled.PathTracing.Enabled,
              "and leaves ray tracing and path tracing as they were");

        Check(std::string(ludifex::GetGraphicsQualityName(GraphicsQuality::Potato)) == "Potato" &&
                  std::string(ludifex::GetGraphicsQualityName(GraphicsQuality::Extreme)) == "Extreme",
              "presets have names to show");

        ludifex::World3D quality = ludifex::CreateWorld3D();
        quality.SetGraphicsQuality(GraphicsQuality::Low);
        Check(quality.GetRenderSettings().RenderScale == 0.75f &&
                  quality.GetRenderSettings().Mode == ludifex::AntiAliasing::Fast,
              "a world takes a preset");
        Check(!quality.IsRayTracingActive() && quality.GetPathTracedSampleCount() == 0,
              "and one that has never drawn has traced nothing");
    }

    // -----------------------------------------------------------------------
    std::printf("\n%s  (%d warning%s reported, which is expected)\n\n",
                g_Failures == 0 ? "All checks passed." : "SOME CHECKS FAILED.",
                g_Warnings, g_Warnings == 1 ? "" : "s");

    return g_Failures == 0 ? 0 : 1;
}
