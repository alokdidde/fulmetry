An early build of the Fulmetry workbench: the home screen (start a project from an idea), the coding-agent sign-in,
and the desk rover sample on the live 3D stage with its parts and checks, explode and x-ray, and Simulate (the
living-room drive test on MuJoCo).

The project chat and the sign-in run on a stand-in agent for now: it answers, but it does not change the project yet.

## macOS (Apple silicon)

The live 3D stage and Simulate need macOS 26 (Tahoe) or later: the renderer uses Metal 4. On macOS 15 the app runs
with a still picture of the rover in place of the live stage.

1. Download `Fulmetry-<version>-macos-arm64.zip` and unzip it.
2. Move `Fulmetry.app` to Applications.
3. The app is not notarized yet, so macOS says it "could not verify Fulmetry is free of malware". Either run
   `xattr -dr com.apple.quarantine /Applications/Fulmetry.app` in Terminal and open the app, or click **Done**, go
   to **System Settings > Privacy & Security**, click **Open Anyway** next to the Fulmetry line, and confirm.

To skip these steps for every future build, run this once in Terminal. It installs the newest build, opens it, and
from then on checks every 10 minutes and swaps in each new release (`--remove-agent` stops it):

```sh
curl -fsSL https://raw.githubusercontent.com/alokdidde/fulmetry/main/tools/mac/fulmetry-update.sh | bash -s -- --install-agent
```

The first launch opens the sign-in wizard for your coding agents, then the home screen, where you start a project from an
idea or open the desk rover sample.

## Linux (x86_64)

Needs a Vulkan 1.3 driver and SDL2 (`sudo apt install libsdl2-2.0-0 mesa-vulkan-drivers` on Ubuntu).

```sh
tar -xzf fulmetry-<version>-linux-x86_64.tar.gz
./fulmetry-<version>-linux-x86_64/fulmetry
```
