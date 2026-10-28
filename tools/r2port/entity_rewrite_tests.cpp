#include "engine/r2port/entity_rewrite.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using r2port::entity_rewrite::AnalyzeEntityLump;
using r2port::entity_rewrite::EntityLumpAnalysis;
using r2port::entity_rewrite::Error;
using r2port::entity_rewrite::RewriteEntityLump;
using r2port::entity_rewrite::RewriteOptions;
using r2port::entity_rewrite::RewriteReport;
using r2port::entity_rewrite::SpawnMode;
using r2port::entity_rewrite::ValidateEntityLump;

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Has(std::string_view text, std::string_view fragment)
{
    return text.find(fragment) != std::string_view::npos;
}

std::uint64_t CountClass(const EntityLumpAnalysis& analysis, std::string_view name)
{
    for (const auto& entry : analysis.classnames) {
        if (entry.name == name)
            return entry.occurrences;
    }
    return 0;
}

void TestRulesAndIdempotence()
{
    const std::string input =
        "ENTITIES01\n"
        "// original order and whitespace are significant\n"
        "{\n"
        "\"classname\" \"move_rope\"\n"
        "\"RopeMaterial\" \"cable/zipline.vmt\"\n"
        "\"Zipline\" \"1\"\n"
        "\"ZiplineSagHeight\" \"50\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_target\"\n"
        "\"targetname\" \"spacenode\"\n"
        "\"origin\" \"1 2 3\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"prop_dynamic_lightweight\"\n"
        "\"editorclass\" \"unchanged_metadata\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"script_mover_lightweight\"\n"
        "\"targetname\" \"decorative_rotator\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"light_environment_volume\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_spawnpoint_human\"\n"
        "\"gamemode_tdm\" \"1\"\n"
        "\"gamemode_ffa\" \"1\"\n"
        "\"gamemode_at\" \"1\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_spawnpoint_titan_start\"\n"
        "\"gamemode_ctf\" \"1\"\n"
        "}\n";

    RewriteReport report;
    const std::string output = RewriteEntityLump(input, {"mp_eden", true}, &report);
    std::string error;
    Check(ValidateEntityLump(output, true, &error), error.c_str());
    Check(report.entitiesIn == 7 && report.entitiesOut == 5, "entity drop counts are wrong");
    Check(report.renamedClassnames == 1, "lightweight dynamic rename count is wrong");
    Check(report.rewrittenValues == 1 && report.renamedEdenSpaceNodes == 1, "Eden target rewrite count is wrong");
    Check(report.preservedZiplineMaterials == 1, "zipline material preservation count is wrong");
    Check(report.droppedScriptMoverLightweight == 1, "lightweight script mover drop count is wrong");
    Check(report.droppedLightEnvironmentVolume == 1, "light environment volume drop count is wrong");
    Check(report.unknownConstructs == 0, "valid entity input was marked unknown");

    Check(Has(output, "\"RopeMaterial\" \"cable/zipline.vmt\""), "zipline material was incorrectly rewritten");
    Check(Has(output, "\"ZiplineSagHeight\" \"50\""), "zipline keys were changed");
    Check(Has(output, "\"targetname\" \"intro_SpaceNode\""), "Eden space node was not renamed");
    Check(Has(output, "\"classname\" \"prop_dynamic\""), "lightweight dynamic was not renamed");
    Check(Has(output, "\"editorclass\" \"unchanged_metadata\""), "opaque metadata was not preserved");
    Check(!Has(output, "script_mover_lightweight"), "unsupported mover was not dropped");
    Check(!Has(output, "light_environment_volume"), "unsupported light volume was not dropped");
    Check(output.find("move_rope") < output.find("intro_SpaceNode"), "entity order changed");

    const std::string second = RewriteEntityLump(output, {"mp_eden", true});
    Check(second == output, "rewrite is not idempotent");

    const std::string otherMap = RewriteEntityLump(input, {"mp_glitch", true});
    Check(Has(otherMap, "\"targetname\" \"spacenode\""), "Eden-only target rewrite escaped its map");
}

void TestSpawnModesAndAnalysis()
{
    const std::string input =
        "ENTITIES01\n"
        "{\"classname\" \"info_spawnpoint_human\" \"gamemode_tdm\" \"1\" \"gamemode_ffa\" \"1\" \"gamemode_at\" \"1\"}\n"
        "{\"classname\" \"info_spawnpoint_human\" \"gamemode_tdm\" \"0\" \"gamemode_ffa\" \"0\" \"gamemode_at\" \"0\"}\n"
        "{\"classname\" \"info_spawnpoint_titan\" \"gamemode_at\" \"1\"}\n"
        "{\"classname\" \"info_spawnpoint_titan_start\" \"gamemode_ctf\" \"1\"}\n"
        "{\"classname\" \"info_spawnpoint_flag\" \"gamemode_ctf\" \"\"}\n";

    EntityLumpAnalysis analysis;
    std::string error;
    Check(AnalyzeEntityLump(input, analysis, true, &error), error.c_str());
    Check(analysis.entities == 5, "analysis entity total is wrong");
    Check(CountClass(analysis, "info_spawnpoint_human") == 2, "classname histogram is wrong");
    Check(analysis.keys.size() == 5, "key histogram distinct count is wrong");

    const auto& ctf = analysis.spawnModes[static_cast<std::size_t>(SpawnMode::CaptureTheFlag)];
    const auto& at = analysis.spawnModes[static_cast<std::size_t>(SpawnMode::AttritionTitanMode)];
    const auto& ffa = analysis.spawnModes[static_cast<std::size_t>(SpawnMode::FreeForAll)];
    const auto& tdm = analysis.spawnModes[static_cast<std::size_t>(SpawnMode::TeamDeathmatch)];
    const auto& ph = analysis.spawnModes[static_cast<std::size_t>(SpawnMode::PilotHunter)];
    Check(ctf.titanStart == 1 && ctf.flag == 0, "CTF fallback mapping is wrong");
    Check(at.human == 1 && at.titan == 1, "AT spawn mapping is wrong");
    Check(ffa.human == 1, "FFA spawn mapping is wrong");
    Check(tdm.human == 1, "TDM key exclusion is wrong");
    Check(ph.human == 1, "Pilot Hunter spawn mapping is wrong");
    Check(r2port::entity_rewrite::GetSpawnModeRule(SpawnMode::PilotHunter).gameModeKey == "gamemode_tdm",
          "Pilot Hunter must use the shared TDM spawn tag");
}

void TestCtfTeamKeyFallback()
{
    const std::string input =
        "ENTITIES01\n"
        "{\n"
        "\"classname\" \"info_spawnpoint_flag\"\n"
        "\"targetname\" \"flag_militia\"\n"
        "\"teamnumber\" \"3\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_spawnpoint_flag\"\n"
        "\"TeamNum\" \"2\"\n"
        "\"teamnumber\" \"3\"\n"
        "}\n"
        "{\n"
        "\"classname\" \"info_spawnpoint_human\"\n"
        "\"teamnumber\" \"2\"\n"
        "}\n";

    RewriteReport report;
    const std::string output = RewriteEntityLump(input, {"mp_glitch", true}, &report);
    std::string error;
    Check(ValidateEntityLump(output, true, &error), error.c_str());
    Check(Has(output, "\"TeamNum\" \"3\""), "CTF teamnumber fallback was not renamed");
    Check(Has(output, "\"teamnumber\" \"3\""), "existing duplicate metadata key was not preserved");
    Check(Has(output, "\"teamnumber\" \"2\""), "non-flag teamnumber metadata was changed");
    Check(report.renamedCtfTeamKeys == 1, "CTF team-key rename count is wrong");
    Check(RewriteEntityLump(output, {"mp_glitch", true}) == output,
          "CTF team-key rewrite is not idempotent");

    const std::string duplicate =
        "ENTITIES01\n"
        "{\"classname\" \"info_spawnpoint_flag\" \"teamnumber\" \"2\" \"teamnumber\" \"3\"}\n";
    Check(!ValidateEntityLump(duplicate, true, &error),
          "strict mode accepted a duplicate CTF teamnumber key");
    Check(Has(error, "duplicate teamnumber key"),
          "duplicate CTF teamnumber error did not identify the key");
}

void TestMapFeatures()
{
    const std::string input =
        "ENTITIES01\n"
        "{\"classname\" \"trigger_multiple\" \"editorclass\" \"trigger_mp_spawn_zone\"}\n"
        "{\"classname\" \"info_target_clientside\" \"script_name\" \"menu_camera_target\"}\n"
        "{\"classname\" \"info_target_clientside\" \"script_name\" \"menu_scene_ref\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"spec_cam1\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"escape_node1\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"spacenode\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"skybox_cam_intro\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"skybox_cam_level\"}\n"
        "{\"classname\" \"info_target\" \"targetname\" \"skybox_cam_day\"}\n";

    EntityLumpAnalysis analysis;
    std::string error;
    Check(AnalyzeEntityLump(input, analysis, true, &error), error.c_str());
    Check(analysis.features.spawnZones == 1, "spawn-zone editorclass was not counted");
    Check(analysis.features.menuCameraTargets == 1 && analysis.features.menuSceneRefs == 1,
          "menu camera script names were not counted");
    Check(analysis.features.introCameraTargets == 1 && analysis.features.evacuationNodeTargets == 1,
          "intro camera or evacuation targetnames were not counted");
    Check(analysis.features.spaceNodeTargets == 1, "space node targetname was not counted");
    Check(analysis.features.skyboxIntroCameras == 1
              && analysis.features.skyboxLevelCameras == 1
              && analysis.features.skyboxDayCameras == 1,
          "skybox camera targetnames were not counted");
}


void TestStrictFailureAndPermissiveFallback()
{
    const std::string malformed = "ENTITIES01\n{\n\"classname\" \"info_target\"\n";
    std::string error;
    Check(!ValidateEntityLump(malformed, true, &error), "strict validator accepted an unterminated block");
    Check(Has(error, "unterminated entity block"), "strict parser error did not identify the malformed construct");

    bool threw = false;
    try {
        (void)RewriteEntityLump(malformed, {"mp_glitch", true});
    } catch (const Error&) {
        threw = true;
    }
    Check(threw, "strict rewrite did not fail on malformed structure");

    RewriteReport report;
    const std::string unknownHeader = "OTHER_HEADER\n{\"classname\" \"info_target\"}\n";
    const std::string unchanged = RewriteEntityLump(unknownHeader, {"mp_glitch", false}, &report);
    Check(unchanged == unknownHeader, "permissive fallback modified unknown syntax");
    Check(report.unknownConstructs == 1, "permissive fallback did not report unknown syntax");
}

void TestRaiseFlagSpawnOrigins()
{
    const std::string input =
        "{\n\"TeamNum\" \"2\"\n\"origin\" \"308.41 -1782.69 -0.000796557\"\n\"classname\" \"info_spawnpoint_flag\"\n}\n"
        "{\n\"origin\" \"269.31 2139.71 0.000612214\"\n\"TeamNum\" \"3\"\n\"classname\" \"info_spawnpoint_flag\"\n}\n"
        "{\n\"origin\" \"1 2 3\"\n\"classname\" \"info_spawnpoint_human\"\n}\n"
        "{\n\"classname\" \"info_spawnpoint_flag\"\n\"origin\" \"bad\"\n}\n";
    std::uint64_t raised = 0;
    const std::string output = r2port::entity_rewrite::RaiseFlagSpawnOrigins(input, 8.0, &raised);
    Check(raised == 2, "only the two well-formed flag origins are raised");
    Check(Has(output, "\"origin\" \"308.41 -1782.69 7.99920344\""), "first flag z is not raised by 8");
    Check(Has(output, "\"origin\" \"269.31 2139.71 8.00061221\""), "second flag z is not raised by 8");
    Check(Has(output, "\"origin\" \"1 2 3\""), "non-flag origin must not change");
    Check(Has(output, "\"origin\" \"bad\""), "malformed origin must not change");
    Check(r2port::entity_rewrite::RaiseFlagSpawnOrigins("{ \"classname\" ", 8.0, &raised) == "{ \"classname\" " && raised == 0,
          "unparseable text is returned unchanged");
}

// Entity files must come out byte-exact apart from the rewritten spans: CRLF text stays CRLF, and the
// real light_environment block is never dropped, reordered or edited (only light_environment_volume goes; a later
// light_environment's cloudoffset, pitch and angles can change, see TestSunStateOwner).
void TestByteExactOutputAndLightEnvironment()
{
    const std::string head = "ENTITIES01\r\n";
    const std::string lightEnvironment =
        "{\r\n\"classname\" \"light_environment\"\r\n\"_light\" \"255 240 200 600\"\r\n\"angles\" \"-50 30 0\"\r\n}";
    const std::string volume = "{\r\n\"classname\" \"light_environment_volume\"\r\n\"origin\" \"0 0 0\"\r\n}";
    const std::string tail =
        "{\r\n\"classname\" \"env_fog_controller\"\r\n\"fogcolor\" \"1 2 3\"\r\n}\r\n"
        "{\r\n\"classname\" \"info_spawnpoint_flag\"\r\n\"teamnumber\" \"2\"\r\n\"origin\" \"1 2 0\"\r\n}\r\n";
    const std::string input = head + lightEnvironment + "\r\n" + volume + "\r\n" + tail;

    const std::string output = RewriteEntityLump(input, {"mp_glitch", false});
    std::string expected = input;
    expected.erase(expected.find(volume), volume.size());
    const std::size_t teamKey = expected.find("\"teamnumber\"");
    Check(teamKey != std::string::npos, "test input is missing the teamnumber key");
    expected.replace(teamKey + 1, 10, "TeamNum");
    Check(output == expected, "rewrite output is not byte-exact outside the rewritten spans");

    for (std::size_t i = 0; i < output.size(); ++i) {
        if (output[i] == '\n')
            Check(i > 0 && output[i - 1] == '\r', "a bare LF appeared in CRLF text");
    }
    const std::size_t first = output.find(lightEnvironment);
    Check(first != std::string::npos && first < output.find("env_fog_controller"),
          "the light_environment block moved or changed");
    Check(output.find("light_environment_volume") == std::string::npos, "the light_environment_volume was kept");

    // Raising the flag origin keeps everything else byte-exact too.
    std::uint64_t raised = 0;
    const std::string lifted = r2port::entity_rewrite::RaiseFlagSpawnOrigins(output, 8.0, &raised);
    std::string expectedLifted = output;
    const std::size_t origin = expectedLifted.find("\"1 2 0\"");
    expectedLifted.replace(origin, 7, "\"1 2 8\"");
    Check(raised == 1 && lifted == expectedLifted, "flag origin raise is not byte-exact");
}

bool TestMapper(std::string_view system, std::string& target)
{
    if (system != "P_converted")
        return false;
    target = "r2p_P_converted";
    return true;
}

void TestEffectNameRename()
{
    const std::string input =
        "{\n\"classname\" \"info_particle_system\"\n\"effect_name\" \"P_converted\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"info_particle_system\"\n\"effect_name\" \"P_r1_owned\"\n}\n"
        "{\n\"classname\" \"prop_dynamic_lightweight\"\n\"effect_name\" \"P_converted\"\n}\n"
        "{\n\"classname\" \"info_target\"\n\"effect_name\" \"P_converted\"\n}\n";
    RewriteReport report;
    RewriteOptions options{ "mp_test", false, &TestMapper };
    const std::string output = RewriteEntityLump(input, options, &report);
    Check(report.renamedEffectNames == 2, "effect_name rename count is wrong");
    Check(Has(output, "\"effect_name\" \"r2p_P_converted\"\n\"origin\""), "info_particle_system effect_name was not renamed");
    Check(Has(output, "\"P_r1_owned\""), "an unmapped effect_name changed");
    Check(Has(output, "\"classname\" \"info_target\"\n\"effect_name\" \"P_converted\""), "a non-particle class was renamed");
    RewriteReport plain;
    const std::string untouched = RewriteEntityLump(input, RewriteOptions{ "mp_test", false, nullptr }, &plain);
    Check(plain.renamedEffectNames == 0 && !Has(untouched, "r2p_"), "no mapper must leave effect_name untouched");
}

void TestNpcKeyLowering()
{
    const std::string input =
        "{\n\"classname\" \"script_ref\"\n\"NPC_Titan\" \"1\"\n\"NPC_SuperSpectre\" \"2\"\n\"origin\" \"0 0 0\"\n\"NPC_Grunt\" \"NPC_Titan\"\n}\n"
        "{\n\"classname\" \"info_target\"\n\"targetname\" \"NPC_Titan\"\n\"npc_stalker\" \"1\"\n\"NPC_x y\" \"1\"\n}\n";
    RewriteReport report;
    const std::string output = RewriteEntityLump(input, RewriteOptions{ "mp_test", false, nullptr }, &report);
    Check(report.loweredNpcKeys == 3, "NPC key count is wrong");
    Check(Has(output, "\"npc_titan\" \"1\"") && Has(output, "\"npc_superspectre\" \"2\"") && Has(output, "\"npc_grunt\" \"NPC_Titan\""),
          "NPC keys were not lowered (values must stay unchanged)");
    Check(Has(output, "\"targetname\" \"NPC_Titan\"") && Has(output, "\"NPC_x y\""), "a value or a non-identifier key changed");
    Check(output.size() == input.size(), "lowering must not change the length");
    RewriteReport again;
    Check(RewriteEntityLump(output, RewriteOptions{ "mp_test", false, nullptr }, &again) == output && again.loweredNpcKeys == 0,
          "NPC key lowering is not idempotent");
}

void TestKeyInsertion()
{
    const std::string ring =
        "{\n\"script_name\" \"rings_pristine\"\n\"model\" \"models/props/timeshift_rings_animated.mdl\"\n\"classname\" \"prop_dynamic\"\n}\n";
    RewriteReport report;
    const std::string output = RewriteEntityLump(ring, RewriteOptions{ "mp_complex3", false, nullptr }, &report);
    Check(report.insertedKeys == 1 && Has(output, "\"classname\" \"prop_dynamic\"\n\"DefaultAnim\" \"animated_slow\"\n}\n"),
          "rings_pristine did not get DefaultAnim");
    RewriteReport again;
    Check(RewriteEntityLump(output, RewriteOptions{ "mp_complex3", false, nullptr }, &again) == output && again.insertedKeys == 0,
          "key insertion is not idempotent");
    const std::string crlf =
        "{\r\n\"script_name\" \"rings_pristine\"\r\n\"classname\" \"prop_dynamic_lightweight\"\r\n}\r\n";
    const std::string crlfOut = RewriteEntityLump(crlf, RewriteOptions{ "mp_complex3", false, nullptr }, nullptr);
    Check(Has(crlfOut, "\"classname\" \"prop_dynamic\"\r\n\"DefaultAnim\" \"animated_slow\"\r\n}\r\n"),
          "CRLF line ending was not copied or the lightweight class was not renamed");
    RewriteReport other;
    Check(RewriteEntityLump(ring, RewriteOptions{ "mp_glitch", false, nullptr }, &other) == ring && other.insertedKeys == 0,
          "a rule fired on the wrong map");
    const std::string own =
        "{\n\"script_name\" \"rings_pristine\"\n\"DefaultAnim\" \"idle\"\n\"classname\" \"prop_dynamic\"\n}\n";
    Check(RewriteEntityLump(own, RewriteOptions{ "mp_complex3", false, nullptr }, nullptr) == own,
          "an existing DefaultAnim was overridden");
}

void TestSpinSequences()
{
    // mp_homestead water wheel: mover yaw 56.16 spinning 35 deg/s, props linked by guid, mover listed first and last.
    const std::string text =
        "{\n\"rotate_forever_speed\" \"35\"\n\"angles\" \"0 56.1606 0\"\n\"origin\" \"3252.75 -1465.29 70\"\n"
        "\"link_to_guid_1\" \"10939b77ef0\"\n\"link_to_guid_0\" \"10937890897\"\n\"link_guid\" \"1092a9197b1\"\n"
        "\"classname\" \"script_mover_lightweight\"\n}\n"
        "{\n\"angles\" \"-9.34793e-016 56.1606 -164.547\"\n\"model\" \"models/levels_terrain/mp_homestead/water_wheel_exterior.mdl\"\n"
        "\"link_guid\" \"10937890897\"\n\"classname\" \"prop_dynamic\"\n}\n"
        "{\n\"angles\" \"3.50835e-015 146.161 90\"\n\"link_guid\" \"10939b77ef0\"\n\"classname\" \"prop_dynamic_lightweight\"\n}\n"
        "{\n\"angles\" \"0 0 0\"\n\"link_guid\" \"deadbeef\"\n\"classname\" \"prop_dynamic\"\n}\n";
    RewriteReport report;
    const std::string output = RewriteEntityLump(text, RewriteOptions{ "mp_homestead", false, nullptr }, &report);
    Check(report.droppedScriptMoverLightweight == 1 && report.insertedKeys == 2, "rotator links did not produce two DefaultAnim keys");
    Check(Has(output, "\"classname\" \"prop_dynamic\"\n\"DefaultAnim\" \"r2spin_px_35\"\n}\n"), "wheel exterior got no r2spin_px_35");
    Check(Has(output, "\"classname\" \"prop_dynamic\"\n\"DefaultAnim\" \"r2spin_pz_35\"\n}\n"), "wheel side got no r2spin_pz_35");
    Check(!Has(output, "deadbeef\"\n\"classname\" \"prop_dynamic\"\n\"DefaultAnim\""), "an unlinked prop got DefaultAnim");
    RewriteReport again;
    Check(RewriteEntityLump(output, RewriteOptions{ "mp_homestead", false, nullptr }, &again) == output && again.insertedKeys == 0,
          "spin insertion is not idempotent");
}

// R1 keeps the last cloudmask, cloudoffset, pitch and angles it reads over all light_environment blocks. mp_black_water_canal
// lists the real block (cloudmask, cloudoffset, sun pitch/angles) first and the skybox dummy (cloudoffset "0 0 0 0", other
// sun angles) last, which left the real mask with offset 0 / scale 0 and the dummy sun direction.
void TestSunStateOwner()
{
    const std::string real =
        "{\n\"classname\" \"light_environment\"\n\"_light\" \"255 239 232 1785\"\n\"pitch\" \"-46.1746\"\n"
        "\"angles\" \"46.1746 99.1259 -62.5667\"\n\"cloudoffset\" \"-4723 -4406 18285 15055\"\n"
        "\"cloudmask\" \"world/cloud_masks/mp_blackwater_01_col\"\n}\n";
    const std::string dummy =
        "{\n\"classname\" \"light_environment\"\n\"_light\" \"255 255 255 765\"\n\"cloudoffset\" \"0 0 0 0\"\n"
        "\"pitch\" \"-51.4284\"\n\"angles\" \"51.4284 122.548 1.30448\"\n}\n";
    const std::string fixedDummy =
        "{\n\"classname\" \"light_environment\"\n\"_light\" \"255 255 255 765\"\n\"cloudoffset\" \"-4723 -4406 18285 15055\"\n"
        "\"pitch\" \"-46.1746\"\n\"angles\" \"46.1746 99.1259 -62.5667\"\n}\n";
    const std::string other = "{\n\"classname\" \"info_target\"\n\"origin\" \"1 2 3\"\n\"angles\" \"1 2 3\"\n}\n";
    const RewriteOptions options{"mp_black_water_canal", false, nullptr};

    // Dummy after the real block: cloudoffset, pitch and angles become the real ones, nothing else moves (not even _light).
    RewriteReport report;
    const std::string bwc = real + other + dummy;
    const std::string output = RewriteEntityLump(bwc, options, &report);
    Check(output == real + other + fixedDummy, "the dummy light_environment did not get the real sun state");
    Check(report.rewrittenValues == 3, "sun state rewrite was not counted");
    RewriteReport again;
    Check(RewriteEntityLump(output, options, &again) == output && again.rewrittenValues == 0,
          "sun state rewrite is not idempotent");

    // Every later block gets it; a key the later block lacks is not added (R1 keeps the real one anyway).
    const std::string partial = "{\n\"classname\" \"light_environment\"\n\"pitch\" \"-10\"\n}\n";
    const std::string fixedPartial = "{\n\"classname\" \"light_environment\"\n\"pitch\" \"-46.1746\"\n}\n";
    Check(RewriteEntityLump(real + dummy + partial + dummy, options) == real + fixedDummy + fixedPartial + fixedDummy,
          "later light_environment blocks were not all fixed");

    // A real block without angles or pitch copies only what it has.
    const std::string realCloudOnly =
        "{\n\"classname\" \"light_environment\"\n\"cloudoffset\" \"1 2 3 4\"\n\"cloudmask\" \"world/cloud_masks/x\"\n}\n";
    Check(RewriteEntityLump(realCloudOnly + dummy, options)
              == realCloudOnly
                  + "{\n\"classname\" \"light_environment\"\n\"_light\" \"255 255 255 765\"\n\"cloudoffset\" \"1 2 3 4\"\n"
                    "\"pitch\" \"-51.4284\"\n\"angles\" \"51.4284 122.548 1.30448\"\n}\n",
          "an owner without pitch/angles was copied from");

    // Real block last (the other 16 maps): byte-exact.
    Check(RewriteEntityLump(dummy + other + real, options) == dummy + other + real, "real-last order was touched");

    // No cloudmask anywhere (mp_coliseum, mp_lf_stacks): byte-exact.
    const std::string noMask =
        "{\n\"classname\" \"light_environment\"\n\"cloudoffset\" \"5467 12006 26013 27498\"\n\"pitch\" \"-22.4887\"\n"
        "\"angles\" \"22.4887 -143.639 -125.567\"\n}\n";
    Check(RewriteEntityLump(noMask + dummy, options) == noMask + dummy, "a map without cloudmask was touched");

    // Mask owner without any of the keys: nothing to copy.
    const std::string maskOnly = "{\n\"classname\" \"light_environment\"\n\"cloudmask\" \"world/cloud_masks/x\"\n}\n";
    Check(RewriteEntityLump(maskOnly + dummy, options) == maskOnly + dummy, "an owner without sun keys was copied from");

    // light_environment_volume never counts, CRLF stays CRLF.
    std::string crlfIn = real + dummy;
    std::string crlfExpected = real + fixedDummy;
    for (std::string* text : {&crlfIn, &crlfExpected}) {
        for (std::size_t i = text->find('\n'); i != std::string::npos; i = text->find('\n', i + 2))
            text->insert(i, "\r");
    }
    Check(RewriteEntityLump(crlfIn, options) == crlfExpected, "CRLF sun state rewrite is not byte-exact");
    const std::string volume =
        "{\n\"classname\" \"light_environment_volume\"\n\"cloudoffset\" \"1 2 3 4\"\n\"cloudmask\" \"y\"\n}\n";
    Check(RewriteEntityLump(real + dummy + volume, options) == real + fixedDummy + "\n",
          "light_environment_volume changed the cloud owner");
}

} // namespace

int main()
{
    try {
        TestRulesAndIdempotence();
        TestSpawnModesAndAnalysis();
        TestRaiseFlagSpawnOrigins();
        TestCtfTeamKeyFallback();
        TestMapFeatures();
        TestStrictFailureAndPermissiveFallback();
        TestByteExactOutputAndLightEnvironment();
        TestEffectNameRename();
        TestNpcKeyLowering();
        TestKeyInsertion();
        TestSpinSequences();
        TestSunStateOwner();
    } catch (const std::exception& exception) {
        std::cerr << "entity rewrite test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "entity rewrite tests passed\n";
    return 0;
}
