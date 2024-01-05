![bear](https://github.com/r1delta/r1delta/assets/37985788/41548f20-0878-4e1e-8538-e9be808fc363)

# r1delta

it dont work yet

shit thats broken:
- sendtables
- pebbles
- broomstick handle
- simple ibasefilesystem vftable (17 funcs)
- convars
- concommands
- scripts

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

its called r1delta because the installer will (eventually) use xdelta3 patches to get the r1o files from the r1 versions

# when

r1delte release 2021 men🔥🔥🔥🔥🔥

# dev setup

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

current plan: make the "wrapped" FindVar return a pointer offset by -8 bytes so it lines up with R1O's expectations since it probably only cares about the value and maintain our own map of convars and update the R1O convar copy whenever the r1 equivalent copy updates by using a global convar changed callback on FCVAR_GAMEDLL


# Credits
## R1Delta - for Titanfall

# Thanks to the following individuals:
\<breathe in\> dogecore, kodoque, [redacted], MrSteyk, p0358, chateau, jackw, adderallzx, WellBehavedUser, VITALIZED, quadruple, nephacks, sizzukie, autismgaming, chinaman, Gooseman, invades, rexx, Amos, b!scuit, F1F7Y, BobTheBob9, Osvaldatore, para, CHOCOLATEMAN, modeco80, gogo, foie, Phil, sashie, Coopyy, Gamma, Isla, Enron, doomfish, nanoman2525, hapaxant, mostly fireproof, Tech, rasmus \<and relax\>

...and all those I forgot.

...or neglected.

...or ignored.

...or lost.

TITANFALL™ is a trademark of Respawn Entertainment, LLC. 
