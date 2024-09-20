# R1Delta
Private servers for Titanfall 1

# Disclaimer
This is a development preview (a.k.a beta) where things may and WILL break over time and features that you might see in-game might not be available in the future.

By reading beyond this disclaimer you hereby accept this and lose any right of complaining negatively about it.

# Requirements
- A computer that runs
- Windows 10 (preferably)
- A copy of Titanfall (it doesn't matter if its not the Deluxe Edition, the only thing you need is Titanfall installed.)
- If you have BME installed (Black Market Edition, a mod for Titanfall 1), uninstall it or back up the `bin` folder for your game, **BME IS INCOMPATIBLE WITH R1DELTA AND WILL BREAK YOUR INSTALL**
- The files `GBClient.dll`, `nmcogame64.dll`, `engine.dll`, `server_local.dll` and `tier0.dll` downloaded from [here](https://github.com/r1delta/r1delta/files/13839300/Bin_2.zip).
- The file `engine_ds.dll` downloaded from [here](https://github.com/user-attachments/files/16607639/engine_ds.zip).
- Read the rest of system requirements for Titanfall by clicking [here](https://www.ea.com/games/titanfall/titanfall).

# Instructions
It's recommended if you don't know what you're doing to use the [R1Delta Installer](https://github.com/r1delta/r1delta_installer/releases).

You can install the project yourself by [compiling the code](https://github.com/r1delta/r1delta/tree/main#installing-r1delta-by-compiling-the-code) or [downloading from releases](https://github.com/r1delta/r1delta/tree/main#installing-r1delta-via-releases).

If you're having issues please open an issue or join the [discord](https://discord.gg/zbFCcSM5t7)

# Installing R1Delta via Releases
You can find the latest release of R1Delta by clicking [here](https://github.com/r1delta/r1delta/releases/). You need to download `r1delta_full-build.zip`, everything you need is already packed into the .zip file for your comfort.

You are also provided with the `r1delta_minimal-build.zip` if you don't want 3rd party .dlls included (you'll need to apply these manually later on.), in such case, click [here](#instructions-for-the-minimal-zip-release) for the instructions.

If you aren't using Origin, you'll also want to download the `Titanfall.exe` provided and replace the one in your Titanfall installation with it and then run the game with the launch arguments `-noorigin`.

Once you downloaded the .zip file, follow these instructions:
1) Locate your Titanfall installation directory, this is usually one of the following paths:
    - `C:\Program Files\EA Games\Titanfall`
    - `C:\Program Files (x86)\Origin Games\Titanfall`
    - `C:\Program Files (x86)\Steam\steamapps\common\Titanfall`
    
    If `Titanfall.exe` is inside in one of those directories, that's the game directory.
2) Decompress all the .zip contents inside the game directory, replace all files when prompted.
3) Double-click r1delta.bat for launching R1Delta, or, add `-game r1delta -listenserver -multiple +fatal_script_errors 0` as launch parameters in your Origin/EA App/Steam client.
4) Success!

## Instructions for the minimal .zip release
If you decided to download the minimal .zip file instead of the full one, follow these instructions instead:

1) Locate your Titanfall installation directory, this is usually one of the following paths:
    - `C:\Program Files\EA Games\Titanfall`
    - `C:\Program Files (x86)\Origin Games\Titanfall`
    - `C:\Program Files (x86)\Steam\steamapps\common\Titanfall`
    
    If `Titanfall.exe` is inside in one of those directories, that's the game directory.
2) Move the .dll files `GBClient.dll` and `nmcogame64.dll` that you downloaded earlier (check the [Requirements](#requirements) section for more info) inside the `bin\x64_retail` folder located within `Titanfall.exe`
3) Rename the `tier0.dll` that you downloaded earlier to `tier0_orig.dll` and move it into `bin\x64_retail` on your game directory
4) Go to your game directory, inside `bin\x64_retail` and rename `tier0.dll` to `tier0_r1.dll`
5) If you had BME installed, rename `launcher.dll` to `launcher.bme.dll` and rename `launcher.org.dll` to `launcher.dll`
6) Decompress all the minimal .zip contents inside the game directory, if you're prompted to replace any files, abort, verify game files and go again from step 1
7) Rename the `engine.dll` that you downloaded earlier to `engine_r1o.dll` and move it into `r1delta\bin\x64_delta` on your game directory
8) Move the `engine_ds.dll` that you downloaded earlier into `r1delta\bin\x64_delta` on your game directory
9) Rename the `server_local.dll` that you downloaded earlier to `server.dll` and move it into `r1delta\bin\x64_delta` on your game directory
9) Double-click r1delta.bat for launching R1Delta, or, add `-game r1delta -listenserver -multiple +fatal_script_errors 0` as launch parameters in your Origin/EA App/Steam client.
10) Success!

# Installing R1Delta by Compiling the code

1) Locate your Titanfall installation directory, this is usually one of the following paths:
    - `C:\Program Files\EA Games\Titanfall`
    - `C:\Program Files (x86)\Origin Games\Titanfall`
    - `C:\Program Files (x86)\Steam\steamapps\common\Titanfall`
    
    If `Titanfall.exe` is inside in one of those directories, that's the game directory.
2) Move the .dll files `GBClient.dll` and `nmcogame64.dll` that you downloaded earlier (check the [Requirements](#requirements) section for more info) inside the `bin\x64_retail` folder located within `Titanfall.exe`
3) Rename the `tier0.dll` that you downloaded earlier to `tier0_orig.dll` and move it into `bin\x64_retail` on your game directory
4) Go to your game directory, inside `bin\x64_retail` and rename `tier0.dll` to `tier0_r1.dll`
5) If you had BME installed, rename `launcher.dll` to `launcher.bme.dll` and rename `launcher.org.dll` to `launcher.dll`
6) Install Visual Studio 2022, if you're unsure on how to do it, watch [this](https://youtu.be/1OsGXuNA5cc?t=98) video.
7) Download r1delta source code by clicking [here](https://github.com/r1delta/r1delta/archive/main.zip)
8) Download r1delta scripts by clicking [here](https://github.com/r1delta/core/archive/master.zip)
9) Uncompress main.zip and open `Dll1.sln` that's inside `r1delta-main`
10) (Optional) Right-click the `tier0` project on Solution Explorer and then click on "Properties", change the Output Directory to the `bin\x64_retail` folder of your game directory, if you installed the game via EA App, this step is unnecesary.
11) Click on `Build > Build Solution` to build r1delta binaries
12) Uncompress `master.zip` and rename `core-master` folder to `r1delta`, then move it into your game directory.
13) Add `-game r1delta -listenserver -multiple +fatal_script_errors 0` as launch parameters in your Origin/EA App/Steam client.
14) Success!

If you aren't using Origin, you'll also want to download the `Titanfall.exe` provided in the releases, just drag and replace the one inside your Titanfall installation and run it with the launch arguments `-noorigin`.

# Additional Resources:
- Download DLC Maps from here if you don't have the Deluxe Edition: [https://archive.org/details/r1titanfall_org](https://archive.org/details/r1titanfall_org) the DLC maps are the .zip files that weigh ~1.5GB
- Download the ported Titanfall Online maps ready to be used on Titanfall 1 with R1Delta (M.I.A, Nest and Box) from here: [https://icedrive.net/s/Sbt9huiYFCbtykiBVzVtzu6Twi6C](https://icedrive.net/s/Sbt9huiYFCbtykiBVzVtzu6Twi6C)

# Credits
- [@r3muxd](https://github.com/r3muxd)
- [@VITALISED](https://github.com/VITALISED)
- [@mrsteyk](https://github.com/mrsteyk)
- [@PANCHO7532](https://github.com/PANCHO7532)
- [@quadruple](https://github.com/quad-damage)
- [@mv](https://github.com/mvoolt)
- [@bt](https://github.com/caatge)
- [@dogecore](https://github.com/HappyDOGE)

(list to be expanded in the future as we speak)

## Third-Party Code Attribution

R1Delta incorporates code from several open-source projects. We are grateful to the developers of these projects for their contributions to the open-source community.

- **[R1Spectre/Spectre](https://github.com/R1Spectre/Spectre)**: GPLv3 License
- **[ValveSoftware/GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)**: BSD 3-Clause License
- **[R2Northstar/NorthstarLauncher](https://github.com/R2Northstar/NorthstarLauncher)**: MIT License
- **[ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013)**: Source 1 SDK License
- **[p0358/black_market_edition](https://github.com/p0358/black_market_edition)**: LGPL v2.1 License
- **[facebook/zstd](https://github.com/facebook/zstd)**: Dual-licensed under BSD and GPLv2
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)**: BSD 2-Clause License

R1Delta itself is licensed under the GNU General Public License v3.0 (GPLv3). For full license texts and additional legal notices, please refer to the LICENSE file and thirdpartylegalnotices.txt in this repository.

# Bear
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
