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
- The files `GBClient.dll`, `nmcogame64.dll`, `engine.dll`, `server_local.dll` and `tier0.dll` downloaded from [here](https://archive.org/download/titanfall-online_202201) by clicking "View Contents" on the .rar file (this is not necessary if you're downloading the full .zip from the Releases section and not the minimal one), you can verify the hashes below if you want to make sure you're downloading the right ones.
- The file `engine_ds.dll` downloaded from [here](https://archive.org/download/titanfall-beta) by clicking "View Contents" on the .rar file, this is for the dedicated server, you can verify the hashes for this file below if you want to verify you're downloading the right ones.
- Read the rest of system requirements for Titanfall by clicking [here](https://www.ea.com/games/titanfall/titanfall)

## File hashes for Titanfall Online files (SHA256)
- `GBClient.dll` - 426e1811fb9b8dfefaed33b946448d5d8d3dc667401caf34b30350794fd319dd
- `nmcogame64.dll` - f0f0a4efeb78f77e39f88616dd666997b85140cf1ea7d3c2a834b425852a6989
- `tier0.dll` - 957cc35d0f9444dcb29b8a993de2a0da50fc555d8f0eb3e51f0d41747171c818
- `engine.dll` - 7d85f8201f2a951a35ddb57dc8162169a1b3fa730b7a94b0caec002dcc569999
- `server_local.dll` - 50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4
## File hashes for Titanfall Beta files (SHA256)
- `engine_ds.dll` - bc27e3ef243e87bfcf9a0b4d4059ff9dcaeec4a7bc1dcd653255a0b9ad34699a

# Instructions
Currently there are two ways of getting R1Delta:

Via [releases](#installing-r1delta-via-releases) or by [compiling the code](#installing-r1delta-by-compiling-the-code)

# Installing R1Delta via Releases
You can find the latest release of R1Delta by clicking [here](https://github.com/r1delta/r1delta/releases/). You need to download `r1delta_full-build.zip`, everything you need is already packed into the .zip file for your comfort.

You are also provided with the `r1delta_minimal-build.zip` if you don't want 3rd party .dlls included (you'll need to apply these manually later on.), in such case, click [here](#instructions-for-the-minimal-zip-release) for the instructions.

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

# Additional Resources:
- Download DLC Maps from here if you don't have the Deluxe Edition: [https://archive.org/details/r1titanfall_org](https://archive.org/details/r1titanfall_org) the DLC maps are the .zip files that weigh ~1.5GB
- Download the ported Titanfall Online maps ready to be used on Titanfall 1 with R1Delta (M.I.A, Nest and Box) from here: [https://icedrive.net/s/Sbt9huiYFCbtykiBVzVtzu6Twi6C](https://icedrive.net/s/Sbt9huiYFCbtykiBVzVtzu6Twi6C)

# Credits
- [@r3muxd](https://github.com/r3muxd)
- [@VITALISED](https://github.com/VITALISED)
- [@mrsteyk](https://github.com/mrsteyk)
- [@nephacks](https://github.com/nephacks)
- [@PANCHO7532](https://github.com/PANCHO7532)

(list to be expanded in the future as we speak)
# Bear
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)