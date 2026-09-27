// Example 02: a world without an interface library.
//
// This program links ludifex only. With no host to join, world.Run() creates a
// GPU device and a window, steps and draws the world every frame, and returns
// when the window is closed.

#include <ludifex/ludifex.h>

int main()
{
    ludifex::World3D world = ludifex::CreateWorld3D();
    world.AddGround();

    for (int index = 0; index < 20; ++index)
    {
        ludifex::Actor3D box = world.AddBox({
            .Scale = { 0.8f, 0.8f, 0.8f },
            .Position = { -1.5f + static_cast<float>(index % 4) * 0.9f, 1.0f + static_cast<float>(index) * 0.9f,
                          static_cast<float>(index % 3) * 0.5f },
        });
        box.SetColor(ludifex::Color::FromBytes(static_cast<uint8_t>(120 + index * 6), 150, 230));
    }

    world.Run();
    return 0;
}
