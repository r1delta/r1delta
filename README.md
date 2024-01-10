![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)
<DO NOT REMOVE THE BEAR UNTIL WE CAN LOAD A MAP>

![image](https://github.com/r1delta/r1delta/assets/46062054/4ec5bdab-e6a2-4c71-a2c0-3f59c81ac0b3)
[![Build Status](https://dev.azure.com/ms/winget-cli/_apis/build/status/microsoft.winget-cli?branchName=master)](https://dev.azure.com/ms/winget-cli/_build/latest?definitionId=344&branchName=master)
![Open Source at Microsoft](https://github.com/microsoft/.github/blob/main/images/open-at-microsoft.png) 
[![Feature Requests](https://img.shields.io/github/issues/microsoft/vscode/feature-request.svg)](https://github.com/microsoft/vscode/issues?q=is%3Aopen+is%3Aissue+label%3Afeature-request+sort%3Areactions-%2B1-desc)
[![Bugs](https://img.shields.io/github/issues/microsoft/vscode/bug.svg)](https://github.com/microsoft/vscode/issues?utf8=✓&q=is%3Aissue+is%3Aopen+label%3Abug)
[![Gitter](https://img.shields.io/badge/chat-on%20gitter-yellow.svg)](https://gitter.im/Microsoft/vscode)
[![Python package](https://img.shields.io/pypi/v/semantic-kernel)](https://pypi.org/project/semantic-kernel/)
[![Nuget package](https://img.shields.io/nuget/vpre/Microsoft.SemanticKernel)](https://www.nuget.org/packages/Microsoft.SemanticKernel/)
[![dotnet Docker](https://github.com/microsoft/semantic-kernel/actions/workflows/dotnet-ci-docker.yml/badge.svg?branch=main)](https://github.com/microsoft/semantic-kernel/actions/workflows/dotnet-ci-docker.yml)
[![dotnet Windows](https://github.com/microsoft/semantic-kernel/actions/workflows/dotnet-ci-windows.yml/badge.svg?branch=main)](https://github.com/microsoft/semantic-kernel/actions/workflows/dotnet-ci-windows.yml)
[![License: MIT](https://img.shields.io/github/license/microsoft/semantic-kernel)](https://github.com/microsoft/semantic-kernel/blob/main/LICENSE)
[![Discord](https://img.shields.io/discord/1063152441819942922?label=Discord&logo=discord&logoColor=white&color=d82679)](https://aka.ms/SKDiscord)
![image](https://github.com/r1delta/r1delta/assets/46062054/07ef78b3-963d-41e2-b404-e3add21693b1)
![image](https://github.com/r1delta/r1delta/assets/46062054/06137be6-a5a8-480c-9c8d-82d79066bdab)

# r1delta

it dont work yet

shit thats broken:
- sendtables
- memory allocator
- convars/concommands
- scripts ( can theoretically just lift the scripts/vscripts/mp/ folder from this build, its included in OldTFOScripts )

# what
it's a port of the 2017-04-02 R1O server_local.dll to R1

# where

look behind you

# why
i hate myself and this is the perfect sisyphean endeavor to make sure i never get anything done

more seriously let's take a look at resource modding rn:

- R1O: will never release (tbf we wont either)

- R2: run by pedophiles

- R5R: i have no issues with amos actually i just dont like playing apex very much (no offense)

this is an attempt to "remaster" r1 and rebalance weapons and add more movement mechanics and recolor maps and stuff and maybe get more players (hey it's no titanfall 3 but most people have never played R1 so this should be new to them)

# whys it called r1delta
![https://gogle.com](https://img.shields.io/badge/optimized_for-Microsoft_Edge-green)
its called r1delta because the installer will (eventually) use xdelta3 patches to get the r1o files from the r1 versions

# when

r1delte release 2021 men🔥🔥🔥🔥🔥

# dev setup

-1. make sure you have titanfall installed to default location (C:\Program Files (x86)\Origin Games\Titanfall) 
0. its hard coded for some reason
1. rename "tier0.dll" to "tier0_r1.dll" in bin\x64_retail\
2. acquire OldTFOScripts.7z from [#1](https://github.com/r1delta/r1delta/issues/1) (you just need bin)
3. copy "gbclient.dll", "nmcogame64.dll" from there to bin\x64_retail\
4. copy "tier0_orig.dll", "server.dll", and "engine_r1o.dll" in this repo to bin\x64_retail\ 
5. build
6. pray (if you're not in the visual studio debug configuration, you need to specify -listenServer as a command line arg to load the listen server)
(a fork of SpectreLauncher by @barnabwhy has been provided for debugging convenience)

# current big issue
# convars
ConCommandBase has a (totally unused) new member in R1O

current plan: make the "wrapped" FindVar return a pointer offset by -8 bytes so it lines up with R1O's expectations since it probably only cares about the value and maintain our own map of convars and update the R1O convar copy whenever the r1 equivalent copy updates by using a global convar changed callback on FCVAR_GAMEDLL (none of these words are in the bible)

# Credits
## R1Delta - for Titanfall

# Thanks to the following individuals:
\<breathe in\> dogecore, kodoque, [redacted], MrSteyk, p0358, chateau, jackw, adderallzx, WellBehavedUser, VITALIZED, quadruple, nephacks, sizzukie, autismgaming, chinaman, Gooseman, invades, rexx, Amos, b!scuit, F1F7Y, BobTheBob9, Osvaldatore, para, CHOCOLATEMAN, gogo, foie, Phil, sashie, Coopyy, Gamma, Isla, Enron, doomfish, nanoman2525, hapaxant, mostly fireproof, Tech, rasmus \<and relax\>

...and all those I forgot.

TITANFALL™ is a trademark of Respawn Entertainment, LLC. 


<vitalised SMELLS>
