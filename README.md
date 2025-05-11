# R1Delta
Dedicated server framework for Titanfall 1 that allows hosting custom servers and loading mods.

### Disclaimer
**This is a development preview (a.k.a beta) where things may and WILL break over time and features that you might see in-game might not be available in the future.**

By reading beyond this disclaimer you hereby accept this and lose any right of complaining negatively about it.

# Installation
To get started, go to the [releases page](https://github.com/r1delta/r1delta/releases) and download the latest `Setup.exe`

If you're having issues please open an issue or join the [discord](https://discord.gg/zbFCcSM5t7)

For developers and contributors, keep reading below.

## Hosting a server
You can run "R1Delta Dedicated Server" on Windows in the start menu to run a dedicated server, or if you want to host a listen server you can just click "Create Server" from the main menu in R1Delta.

Instructions on how to run the dedicated server on Docker are [here](https://gist.github.com/quad-damage/3f4ad9c524b638510340cc31da86d3ff). This image is managed by one of our contributors ([@quadruple](https://github.com/quad-damage)) and not officially maintained by R1Delta, but we still endorse it.

The master server will not show your server if it can't be connected to publicly, so make sure to port-forward 27015 (and onwards if you're running multiple servers, 27016 etc) if you want it to be listed. 

If you want to make a friends-only server and hide it from the server browser you should put `hide_server 1` into the console, you can then give out your ip and port for a direct connect (in the style of ip:port) or use Discord game invites, just note that you'll still need to port-forward if your friends are going to connect over the internet. You can also set `sv_password` if you'd like to password protect your server but keep it on the server browser, just keep in mind this *will* leak your server's IP.

# Building From Source
1. [Install vcpkg and run vcpkg integrate install](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-msbuild)
2. Make your dev root directory somewhere (on the disk you want to build R1Delta on) like `C:\depot\r1delta`
3. Run the following commands in a command prompt (**not as Administrator**):
4. `subst S: C:\depot\r1delta` 
5. `git clone --recursive http://github.com/r1delta/r1delta S:\src`
6. `mkdir S:\game`
7. `git clone http://github.com/r1delta/CORE S:\game\r1delta`
8. Build S:\src\r1delta.sln (open Dll1.sln, restore the NuGet packages if it's not done automatically, and build the solution)
9. If you did this right, you should now have `S:\game\r1delta.exe` and it should boot properly.
10. It is recommended, though not required, that you put your original R1 install (or delta'd R1 content) at S:\content.
   
If you want to make the S: subst persistent across restarts, you need to import a registry key.
For example, this is for `C:\depot\r1delta`:
```
REGEDIT4 

[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\DOS Devices] 
"S:"="\\??\\C:\\depot\\r1delta"
```

# Credits
- [@r3muxd](https://github.com/r3muxd)
- [@sonny-tel](https://github.com/sonny-tel)
- [@mrsteyk](https://github.com/mrsteyk)
- [@PANCHO7532](https://github.com/PANCHO7532)
- [@quadruple](https://github.com/quad-damage)
- [@mv](https://github.com/mvoolt)
- [@bt](https://github.com/eepycats)
- [@dogecore](https://github.com/HappyDOGE)
- [@Allusive](https://github.com/AllusiveWheat)
- [@koutsie](https://github.com/koutsie)

## Third-Party Code Attribution

R1Delta incorporates code from several open-source projects. We are grateful to the developers of these projects for their contributions to the open-source community.

- **[R1Spectre/Spectre](https://github.com/R1Spectre/Spectre)**: GPLv3 License
- **[ValveSoftware/GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)**: BSD 3-Clause License
- **[R2Northstar/NorthstarLauncher](https://github.com/R2Northstar/NorthstarLauncher)**: MIT License
- **[ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013)**: Source 1 SDK License
- **[p0358/black_market_edition](https://github.com/p0358/black_market_edition)**: LGPL v2.1 License
- **[facebook/zstd](https://github.com/facebook/zstd)**: Dual-licensed under BSD and GPLv2
- **[TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)**: BSD 2-Clause License
- **[Mauler125/r5sdk](https://github.com/Mauler125/r5sdk)**: Source 1 SDK License
- **[IcePixelx/silver-bun](https://github.com/IcePixelx/silver-bun)**: TODO
- **[snake-biscuits/bsp_regen](https://github.com/snake-biscuits/bsp_regen)**: GPLv3 License
- **[nlohmann/json](https://github.com/nlohmann/json)**: MIT License

R1Delta itself is licensed under the GNU General Public License v3.0 (GPLv3). For full license texts and additional legal notices, please refer to the LICENSE file and thirdpartylegalnotices.txt in this repository.

# Bear
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
