An early build of the Fulmetry workbench: the desk rover project on the live 3D stage, its parts and checks, explode
and x-ray, the harness onboarding, and Simulate (the living-room drive test on MuJoCo).

The project chat runs on a stand-in agent for now: it answers, but it does not change the project yet.

## macOS (Apple silicon, macOS 15 or later)

1. Download `Fulmetry-<version>-macos-arm64.zip` and unzip it.
2. Move `Fulmetry.app` to Applications.
3. The app is not notarized yet, so the first time, right-click it and choose **Open**, then **Open** again. If
   macOS says it is damaged, run `xattr -dr com.apple.quarantine /Applications/Fulmetry.app` in Terminal.

The first launch compiles the stage's shaders, which takes a little while; later launches reuse them.

## Linux (x86_64)

Needs a Vulkan 1.3 driver and SDL2 (`sudo apt install libsdl2-2.0-0 mesa-vulkan-drivers` on Ubuntu).

```sh
tar -xzf fulmetry-<version>-linux-x86_64.tar.gz
./fulmetry-<version>-linux-x86_64/fulmetry
```
