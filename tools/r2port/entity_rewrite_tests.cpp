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

} // namespace

int main()
{
    try {
        TestRulesAndIdempotence();
        TestSpawnModesAndAnalysis();
        TestCtfTeamKeyFallback();
        TestMapFeatures();
        TestStrictFailureAndPermissiveFallback();
    } catch (const std::exception& exception) {
        std::cerr << "entity rewrite test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "entity rewrite tests passed\n";
    return 0;
}
