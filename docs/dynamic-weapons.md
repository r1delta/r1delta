# Dynamic weapons

R1Delta can register weapon classes that the retail R1 `client.dll` and the TFO `server.dll` do not
know about. A mod (or CORE itself) lists the weapon classes in `scripts/weapon_manifest.txt`, ships the
weapon definitions and scripts, and the native module `shared/dynamic_weapons.cpp` registers the classes
on both sides before the class lists are published. No manifest means stock behaviour: nothing is hooked
into the registries and no check is made.

CORE uses it for the five weapons that used to be stored in the engine's `mp_weapon_mega1..5` slots:

| Old slot | Weapon class |
| --- | --- |
| `mp_weapon_mega1` | `mp_weapon_sr_valkyrie` |
| `mp_weapon_mega2` | `mp_weapon_twinb_shotgun` |
| `mp_weapon_mega3` | `mp_titanweapon_minigun` (Thunderbolt) |
| `mp_weapon_mega4` | `mp_titanweapon_charge_cannon` |
| `mp_weapon_mega5` | `mp_weapon_throwing_knife` |

## Manifest format

`scripts/weapon_manifest.txt`, found through the GAME search path (loose file, addon or VPK):

```
// Weapons registered at startup on top of the classes built into client.dll and server.dll.
weaponscripts
{
	"file"	"mp_weapon_sr_valkyrie"
	"file"	"mp_weapon_my_rifle"
}
```

- The root key name is free. Each entry is `"file" "<class>"`. Bare or quoted tokens, `//` line comments.
- An entry is a class name. A folder and extension are tolerated and dropped
  (`scripts/weapons/Mp_Weapon_X.txt` is `mp_weapon_x`). Names are lower cased and may only contain
  `a-z 0-9 _`, at most 39 characters.
- Every class needs `scripts/weapons/<class>.txt` (or `scripts/<class>.txt`, `.ctx`) in the GAME path.
- A class the engine already registers (every stock weapon) is skipped: the native identity wins.
- The names are sorted and de-duplicated, so entry order does not matter.
- Anything else (unknown key, nested block, missing brace, empty list, bad name, missing definition)
  is fatal at startup with a message naming the problem. A typo can never silently drop a weapon.

Only one manifest is read: the first `scripts/weapon_manifest.txt` on the GAME search path. A mod that
ships its own manifest therefore replaces CORE's file and must repeat the CORE entries it still wants.

A class also needs its scripts, exactly like a stock weapon: `scripts/weapons/<class>.txt` (definition),
`scripts/vscripts/weapons/<class>.nut` (callbacks), a `damageSourceStrings` entry, and menu or loadout
registration if it is selectable. Burn card weapons are weapon mods, not classes, and need no manifest.

## What is registered, and where

| Mode | client.dll | server.dll | Manifest read |
| --- | --- | --- | --- |
| Client / listen | yes | yes | at the first class request of each module |
| Legacy dedicated (`engine_ds.dll`) | no | yes | at `ServerGameDLL::DLLInit` (the engine's first class request comes before the file system is handed over; that call passes through untouched) |
| R1O fake-dedicated (`-r1o_dedi`) | no | yes | at the first class request |

`server.dll` is the same file in all three modes, so one implementation serves them. Both modules append
the new names to their native weapon-name registry in sorted order. The shipped `r1delta.wpatch` removes the
TFO-only registrations from `server.dll`, so the stock server and client registries are identical (82 names)
and a weapon ID means the same thing on both sides. The module checks this before it registers anything
and fails loudly when it is not true.

Four of the five CORE weapons (Valkyrie, Twin-B, Thunderbolt, charge cannon) still have an entity factory
in `server.dll` whose weapon ID and ServerClass initializers the wpatch removes. A manifest entry for
exactly those four names replaces the orphaned factory; any other name that collides with an existing
entity is fatal.

Pinned builds (checked by timestamp, image size and entry prologues; anything else is fatal):

| Module | SHA-256 | TimeDateStamp | SizeOfImage |
| --- | --- | --- | --- |
| `client.dll` (retail R1) | `5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e` | `0x55038DB8` | `0x39A4000` |
| `server.dll` (TFO) | `50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4` | `0x58E93EDF` | `0xFB5000` |

## Connect gating

The client sends its roster as the `delta_weapon_roster` userinfo convar: `none`, or
`<count>:<fnv1a64>` over the sorted class names. `HookedCBaseClientConnect` (`engine/auth.cpp`) compares it
with the server's token and refuses the connection on a mismatch:

> Weapon manifest mismatch: the server registers N custom weapons, you have M. Install the same mods / weapon_manifest.txt as the server.

The check does not depend on `delta_skip_version_check`. `delta_weapon_roster_check 0` on the server turns
it off (class IDs then desync; only for debugging). Membership is fixed for the life of the process;
changing a manifest needs a restart.

## Persistent data

Profiles written before the rename store the old names (`mp_weapon_mega1..4`) in loadouts and mods. The
store migrates them token by token on load (`mp_weapon_mega10` is untouched), keeps the pre-migration file
next to the store as `.pre-rename`, and the server normalizes the old names sent by older clients before it
validates them. The enum names in `_pdef.nut` keep their numeric values, so existing binary data stays valid.

## Tests

`tests/dynamic_weapons_manifest_tests.cpp` (CI: `dynamic_weapons_manifest_tests`) covers the manifest
grammar, class name rules and the roster token. `tests/persistentdata_codec_tests.cpp` covers the rename
migration. The native registration itself needs the retail modules and is exercised by running the three
modes: listen, legacy dedicated and `-r1o_dedi`, each with a client that gets a weapon from the manifest.
