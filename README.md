# ludifex examples

Example programs for [ludifex](https://github.com/cresmarmat-an/ludifex), the
2D and 3D world library for C++20. Each program links ludifex only. Two run
without a window, and one opens a window of its own.

The sounds, images, and models the programs use (including skins, morph
targets, and materials) are generated when they start, so the repository
contains no binary files.

Documentation: [ludifex](https://cresmarmat-an.github.io/ludifex/).

## Building and running

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --parallel
```

The programs are written to `build/bin`, with `SDL3.dll` copied next to them.

The first configure downloads ludifex 0.0.1 along with SDL3, Box2D, Box3D,
cgltf, stb, miniaudio, and Assimp, which takes a few minutes. Later configures
reuse the download. To build against your own copy of ludifex, add
`-DFETCHCONTENT_SOURCE_DIR_LUDIFEX=path/to/ludifex` when configuring.

You need CMake 3.22 or later, a C++20 compiler, and the Windows SDK, whose
`dxc` compiles the shaders for Direct3D 12. If the
[Vulkan SDK](https://vulkan.lunarg.com) is installed, the shaders are also
compiled for Vulkan.

## The programs

| | |
| --- | --- |
| [01-checks](#01-checks) | Automated checks of the library's behaviour, without a window |
| [02-run-standalone](#02-run-standalone) | `world.Run()` in its own window, without an interface library |
| [03-benchmark](#03-benchmark) | Memory allocation and thread use while stepping a large pile of boxes |

## 01-checks

Automated checks of the library, run without a window or a GPU. Each group
prints its checks as it goes:

1. **Mutability:** destroying actors while iterating over them is safe, and
   the actor count changes only when queued changes are applied.
2. **Handle safety:** a handle to a destroyed actor reports that it is
   invalid, and calls through it log a warning and do nothing.
3. **Determinism:** a 2D world stepped twice from the same start gives
   identical results.
4. **Validation:** invalid sizes and non-finite positions are rejected, and
   the world keeps working afterwards.
5. **Collision filtering:** overlapping boxes push apart normally and stay
   overlapped on excluded layers.
6. **Multithreaded stepping:** a pile of bodies settles the same way on one
   thread and on four.
7. **Joints:** ropes, motorised and limited hinges, welds, cone joints, and a
   ragdoll behave within their limits, and destroying an actor removes its
   joints.
8. **Sensors and triggers:** enter and leave events arrive once each, filters
   are respected, and handlers can create many actors while they run.
9. **2D parity:** impact speeds, triggers, filters, and rays in 2D.
10. **Shape queries, characters, and scale:** casts through a doorway,
    overlaps, a character against walls, steps, and ledges, and a stack of a
    thousand bricks that comes to rest.
11. **The transform hierarchy:** parenting, moving parents, detaching,
    refusing loops, and destroying a parent with its children.
12. **Saving and restoring:** a world saved to bytes comes back with the same
    positions, names, looks, and hierarchy, and bad data is refused.
13. **Many actors a second:** thousands of actors created and destroyed while
    stepping, many from inside trigger handlers.
14. **World audio:** the mix is pulled through manual output and checked for
    clicks, voice limits, occlusion, culling, and the Doppler shift.
15. **Skeletons and blending:** clips, crossfades, looping, separate poses per
    actor, several skins over one skeleton, and morph targets.
16. **Caching and reloading:** identical files are shared, and changed files
    reload while broken ones keep the last working version.
17. **2D joints, shape queries, and characters.**
18. **Model formats other than glTF:** OBJ with materials, FBX in metres and
    centimetres, Z-up FBX, skinned Collada, STL, and PLY.
19. **Graphics quality presets:** each preset's settings and how they apply
    to a world.

The program exits with 0 when every check passes. The warnings it prints are
expected; they come from the checks that test error handling.

Run it in Debug as well as Release. A Debug build checks container access and
fills freed memory, so memory errors that Release builds miss are caught
there. If the program crashes, it prints the call stack
(see `common/CrashReport.h`).

## 02-run-standalone

A world in its own window, with no interface library. `world.Run()` creates
the GPU device and the window, then steps and draws the world until the window
is closed.

## 03-benchmark

Runs without a window or renderer. It builds a large pile of boxes with
sleeping turned off, so the solver has work on every step, and steps it 240
times, twice.

It replaces the global `operator new` and `delete` to count every heap
allocation made during those steps, in ludifex, the standard library, and
Box3D. Two windows are measured because something that grows once, such as a
pool reaching its final size, shows up only in the first, while something
allocated every step shows up in both. The check expects zero allocations.

It also reads how busy each worker thread was with
`ludifex::GetWorkerUtilisation()` and checks that the work is spread evenly
across the pool threads. The first entry is the calling thread, which also
does the single-threaded parts of each step, so it is checked separately.

Finally it turns the profiler on for thirty frames, prints where the time
went, and checks that turning the profiler off clears it.

The timings and percentages it prints depend on your machine. There is
nothing to press: the program prints its results and exits with a non-zero
code if a check failed.

## License

MIT, copyright (c) 2026 Cresmar Mat-an. See [LICENSE](LICENSE).
