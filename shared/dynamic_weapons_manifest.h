#pragma once

#include <string>
#include <vector>

// Pure (engine independent) half of the dynamic weapon registration: the
// scripts/weapon_manifest.txt grammar, class name rules and the roster token.
// Kept separate from dynamic_weapons.cpp so it can be unit tested on its own
// (tests/dynamic_weapons_manifest_tests.cpp).
namespace r1delta::dynamic_weapons
{
// Longest native weapon class name (CClassMap char[40], terminator included).
inline constexpr std::size_t MaxClassNameLength = 39;

struct ManifestParse
{
    bool ok = false;
    std::vector<std::string> names; // sorted, unique, lower case class names
    std::string error;              // set when !ok
};

// Normalizes one manifest entry to a weapon class name: folder and extension are
// dropped ("scripts/weapons/Mp_Weapon_X.txt" -> "mp_weapon_x"), the name is lower
// cased and may only contain [a-z0-9_].
bool NormalizeClassName(const std::string& entry, std::string& name, std::string& error);

// Grammar: `rootkey { "file" "<weapon>" ... }`, bare or quoted tokens, `//` line
// comments. Anything else is an error, so a typo in a mod can never silently drop a
// weapon. An empty weapon list is an error.
ManifestParse ParseManifest(const std::string& text);

// "<count>:<fnv1a64 hex>" of the sorted names; identical on every machine that
// mounts the same manifest. Sent to the server in the delta_weapon_roster userinfo.
std::string MakeRosterToken(const std::vector<std::string>& names);
}
