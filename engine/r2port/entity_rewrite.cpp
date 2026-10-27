#include "entity_rewrite.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace r2port::entity_rewrite {
namespace {

constexpr std::array<SpawnModeRule, static_cast<std::size_t>(SpawnMode::Count)> kSpawnModeRules = {{
    {"CTF", "gamemode_ctf", false, false, false, true, true},
    {"AT", "gamemode_at", true, true, true, true, false},
    {"FFA", "gamemode_ffa", true, true, false, false, false},
    {"TDM", "gamemode_tdm", true, true, true, true, false},
    {"PH", "gamemode_tdm", true, true, false, false, false},
}};

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct ModeTag {
    bool present = false;
    std::string_view value;
};

struct EntityView {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string_view classname;
    std::string_view targetname;
    std::string_view ropeMaterial;
    std::string_view editorClass;
    std::string_view scriptName;
    std::string_view effectName;
    Span classnameSpan;
    Span targetnameSpan;
    Span ropeMaterialSpan;
    Span teamNumberAliasKeySpan;
    Span effectNameSpan;
    std::array<ModeTag, 4> modeTags{};
    std::uint64_t skyKeys = 0;
    std::uint64_t fogKeys = 0;
    bool hasClassname = false;
    bool hasTargetname = false;
    bool hasRopeMaterial = false;
    bool hasTeamNum = false;
    bool hasTeamNumberAlias = false;
    bool duplicateTeamNum = false;
    bool duplicateTeamNumberAlias = false;
    std::size_t duplicateTeamNumOffset = 0;
    std::size_t duplicateTeamNumberAliasOffset = 0;
    bool hasEditorClass = false;
    bool hasScriptName = false;
    bool hasOrigin = false;
    bool hasEffectName = false;
    std::string_view origin;
    Span originSpan;
};

enum class TokenKind {
    Quoted,
    Bare,
    Open,
    Close,
    End,
    Invalid
};

struct Token {
    TokenKind kind = TokenKind::End;
    Span whole;
    Span text;
};

struct ParseFailure {
    std::size_t offset = 0;
    const char* message = "invalid entity-lump syntax";
};

class Scanner {
public:
    explicit Scanner(std::string_view input)
        : m_input(input)
    {
    }

    bool Next(Token& token, ParseFailure& failure)
    {
        if (!SkipTrivia(failure))
            return false;
        if (m_position >= m_input.size()) {
            token = {};
            token.kind = TokenKind::End;
            token.whole = {m_position, m_position};
            token.text = token.whole;
            return true;
        }

        const std::size_t begin = m_position;
        const char ch = m_input[m_position];
        if (ch == '{' || ch == '}') {
            ++m_position;
            token.kind = ch == '{' ? TokenKind::Open : TokenKind::Close;
            token.whole = {begin, m_position};
            token.text = token.whole;
            return true;
        }

        if (ch == '"') {
            ++m_position;
            const std::size_t textBegin = m_position;
            while (m_position < m_input.size()) {
                const char current = m_input[m_position];
                if (current == '\0') {
                    failure = {m_position, "embedded null in quoted token"};
                    return false;
                }
                if (current == '"') {
                    token.kind = TokenKind::Quoted;
                    token.whole = {begin, m_position + 1};
                    token.text = {textBegin, m_position};
                    ++m_position;
                    return true;
                }
                if (current == '\\') {
                    if (m_position + 1 >= m_input.size()) {
                        failure = {begin, "unterminated escape in quoted token"};
                        return false;
                    }
                    m_position += 2;
                    continue;
                }
                ++m_position;
            }
            failure = {begin, "unterminated quoted token"};
            return false;
        }

        while (m_position < m_input.size()) {
            const char current = m_input[m_position];
            if (IsSpace(current) || current == '{' || current == '}' || StartsComment(m_position))
                break;
            ++m_position;
        }
        if (m_position == begin) {
            failure = {begin, "unexpected token"};
            return false;
        }
        token.kind = TokenKind::Bare;
        token.whole = {begin, m_position};
        token.text = token.whole;
        return true;
    }

private:
    static bool IsSpace(char ch)
    {
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    }

    bool StartsComment(std::size_t position) const
    {
        return position + 1 < m_input.size()
            && m_input[position] == '/'
            && (m_input[position + 1] == '/' || m_input[position + 1] == '*');
    }

    bool SkipTrivia(ParseFailure& failure)
    {
        for (;;) {
            while (m_position < m_input.size() && IsSpace(m_input[m_position]))
                ++m_position;

            if (m_position == 0 && m_input.size() >= 3
                && static_cast<unsigned char>(m_input[0]) == 0xEF
                && static_cast<unsigned char>(m_input[1]) == 0xBB
                && static_cast<unsigned char>(m_input[2]) == 0xBF) {
                m_position = 3;
                continue;
            }
            if (m_position < m_input.size() && m_input[m_position] == '\0') {
                for (std::size_t index = m_position + 1; index < m_input.size(); ++index) {
                    if (m_input[index] != '\0' && !IsSpace(m_input[index])) {
                        failure = {m_position, "embedded null byte"};
                        return false;
                    }
                }
                m_position = m_input.size();
                return true;
            }

            if (!StartsComment(m_position))
                return true;

            const std::size_t commentBegin = m_position;
            if (m_input[m_position + 1] == '/') {
                m_position += 2;
                while (m_position < m_input.size() && m_input[m_position] != '\n')
                    ++m_position;
                continue;
            }

            m_position += 2;
            while (m_position + 1 < m_input.size()
                && !(m_input[m_position] == '*' && m_input[m_position + 1] == '/'))
                ++m_position;
            if (m_position + 1 >= m_input.size()) {
                failure = {commentBegin, "unterminated block comment"};
                return false;
            }
            m_position += 2;
        }
    }

    std::string_view m_input;
    std::size_t m_position = 0;
};


bool StartsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::size_t ModeTagIndex(std::string_view key)
{
    if (key == "gamemode_ctf")
        return 0;
    if (key == "gamemode_at")
        return 1;
    if (key == "gamemode_ffa")
        return 2;
    if (key == "gamemode_tdm")
        return 3;
    return 4;
}

bool IsActiveForMode(const EntityView& entity, const SpawnModeRule& rule)
{
    const std::size_t tagIndex = ModeTagIndex(rule.gameModeKey);
    if (tagIndex >= entity.modeTags.size())
        return true;
    const ModeTag& tag = entity.modeTags[tagIndex];
    return !tag.present || (!tag.value.empty() && tag.value != "0");
}

bool IsSpawnClass(std::string_view classname)
{
    return classname == "info_spawnpoint_human"
        || classname == "info_spawnpoint_human_start"
        || classname == "info_spawnpoint_titan"
        || classname == "info_spawnpoint_titan_start"
        || classname == "info_spawnpoint_flag";
}

void CountSpawnMode(const EntityView& entity,
                    const SpawnModeRule& rule,
                    SpawnModeCounts& counts)
{
    if (!IsActiveForMode(entity, rule))
        return;

    const std::string_view classname = entity.classname;
    bool counted = false;
    if (rule.countHuman && classname == "info_spawnpoint_human") {
        ++counts.human;
        counted = true;
    } else if (rule.countHumanStart && classname == "info_spawnpoint_human_start") {
        ++counts.humanStart;
        counted = true;
    } else if (rule.countTitan && classname == "info_spawnpoint_titan") {
        ++counts.titan;
        counted = true;
    } else if (rule.countTitanStart && classname == "info_spawnpoint_titan_start") {
        ++counts.titanStart;
        counted = true;
    } else if (rule.countFlag && classname == "info_spawnpoint_flag") {
        ++counts.flag;
        counted = true;
    }
    if (counted)
        ++counts.activeSpawnEntities;
}

template <typename PairCallback, typename EntityCallback>
bool ScanEntityLump(std::string_view input,
                    bool strict,
                    PairCallback&& onPair,
                    EntityCallback&& onEntity,
                    ParseFailure& failure)
{
    Scanner scanner(input);
    Token token;
    if (!scanner.Next(token, failure))
        return false;

    if (token.kind == TokenKind::Bare) {
        if (input.substr(token.text.begin, token.text.end - token.text.begin) != "ENTITIES01") {
            failure = {token.whole.begin, "unknown entity-lump header"};
            return false;
        }
        if (!scanner.Next(token, failure))
            return false;
    }

    while (token.kind != TokenKind::End) {
        if (token.kind != TokenKind::Open) {
            failure = {token.whole.begin, "expected entity opening brace"};
            return false;
        }

        EntityView entity;
        entity.begin = token.whole.begin;
        bool closed = false;
        for (;;) {
            Token key;
            if (!scanner.Next(key, failure))
                return false;
            if (key.kind == TokenKind::End) {
                failure = {entity.begin, "unterminated entity block"};
                return false;
            }
            if (key.kind == TokenKind::Close) {
                entity.end = key.whole.end;
                closed = true;
                break;
            }
            if (key.kind == TokenKind::Open) {
                failure = {key.whole.begin, "nested entity block"};
                return false;
            }
            if (strict && key.kind != TokenKind::Quoted) {
                failure = {key.whole.begin, "strict mode requires quoted key tokens"};
                return false;
            }

            Token value;
            if (!scanner.Next(value, failure))
                return false;
            if (value.kind == TokenKind::End || value.kind == TokenKind::Open || value.kind == TokenKind::Close) {
                failure = {key.whole.begin, "key has no scalar value"};
                return false;
            }
            if (strict && value.kind != TokenKind::Quoted) {
                failure = {value.whole.begin, "strict mode requires quoted value tokens"};
                return false;
            }

            const std::string_view keyText = input.substr(key.text.begin, key.text.end - key.text.begin);
            const std::string_view valueText = input.substr(value.text.begin, value.text.end - value.text.begin);
            onPair(entity, keyText, valueText);

            if (keyText == "classname") {
                if (strict && entity.hasClassname) {
                    failure = {key.whole.begin, "duplicate classname key"};
                    return false;
                }
                entity.hasClassname = true;
                entity.classname = valueText;
                entity.classnameSpan = value.text;
            } else if (keyText == "targetname") {
                if (strict && entity.hasTargetname) {
                    failure = {key.whole.begin, "duplicate targetname key"};
                    return false;
                }
                entity.hasTargetname = true;
                entity.targetname = valueText;
                entity.targetnameSpan = value.text;
            } else if (keyText == "RopeMaterial") {
                if (strict && entity.hasRopeMaterial) {
                    failure = {key.whole.begin, "duplicate RopeMaterial key"};
                    return false;
                }
                entity.hasRopeMaterial = true;
                entity.ropeMaterial = valueText;
                entity.ropeMaterialSpan = value.text;
            } else if (keyText == "TeamNum") {
                if (entity.hasTeamNum && !entity.duplicateTeamNum)
                    entity.duplicateTeamNumOffset = key.whole.begin;
                entity.duplicateTeamNum = entity.duplicateTeamNum || entity.hasTeamNum;
                entity.hasTeamNum = true;
            } else if (keyText == "teamnumber") {
                if (entity.hasTeamNumberAlias && !entity.duplicateTeamNumberAlias)
                    entity.duplicateTeamNumberAliasOffset = key.whole.begin;
                entity.duplicateTeamNumberAlias = entity.duplicateTeamNumberAlias || entity.hasTeamNumberAlias;
                if (!entity.hasTeamNumberAlias)
                    entity.teamNumberAliasKeySpan = key.text;
                entity.hasTeamNumberAlias = true;
            } else if (keyText == "origin") {
                entity.hasOrigin = true;
                entity.origin = valueText;
                entity.originSpan = value.text;
            } else if (keyText == "editorclass") {
                entity.hasEditorClass = true;
                entity.editorClass = valueText;
            } else if (keyText == "script_name") {
                entity.hasScriptName = true;
                entity.scriptName = valueText;
            } else if (keyText == "effect_name") {
                entity.hasEffectName = true;
                entity.effectName = valueText;
                entity.effectNameSpan = value.text;
            } else {
                const std::size_t modeIndex = ModeTagIndex(keyText);
                if (modeIndex < entity.modeTags.size()) {
                    entity.modeTags[modeIndex].present = true;
                    entity.modeTags[modeIndex].value = valueText;
                }
            }

            if (StartsWith(keyText, "sky"))
                ++entity.skyKeys;
            if (StartsWith(keyText, "fog"))
                ++entity.fogKeys;
        }

        if (!closed)
            return false;
        if (strict && !entity.hasClassname) {
            failure = {entity.begin, "entity has no classname key"};
            return false;
        }
        if (strict && entity.classname == "info_spawnpoint_flag") {
            if (entity.duplicateTeamNum) {
                failure = {entity.duplicateTeamNumOffset, "duplicate TeamNum key"};
                return false;
            }
            if (entity.duplicateTeamNumberAlias) {
                failure = {entity.duplicateTeamNumberAliasOffset, "duplicate teamnumber key"};
                return false;
            }
        }
        onEntity(entity);

        if (!scanner.Next(token, failure))
            return false;
    }
    return true;
}

std::string ErrorMessage(std::size_t offset, const char* message)
{
    return "entity lump parse error at byte " + std::to_string(offset) + ": " + message;
}

void AppendRange(std::string& output, std::string_view input, std::size_t begin, std::size_t end)
{
    if (end > begin)
        output.append(input.data() + begin, end - begin);
}

struct ValuePatch {
    Span span;
    std::string_view replacement;
};

void SortPatches(std::array<ValuePatch, 4>& patches, std::size_t count)
{
    for (std::size_t i = 1; i < count; ++i) {
        ValuePatch value = patches[i];
        std::size_t j = i;
        while (j > 0 && patches[j - 1].span.begin > value.span.begin) {
            patches[j] = patches[j - 1];
            --j;
        }
        patches[j] = value;
    }
}

bool IsEdenSpaceNode(const EntityView& entity, std::string_view mapName)
{
    return mapName == "mp_eden"
        && entity.classname == "info_target"
        && entity.hasTargetname
        && entity.targetname == "spacenode";
}

void AnalyzeFeatures(const EntityView& entity, MapFeatures& features)
{
    const std::string_view classname = entity.classname;
    if (classname == "worldspawn") {
        ++features.worldspawn;
        features.worldspawnSkyKeys += entity.skyKeys;
        features.worldspawnFogKeys += entity.fogKeys;
    } else if (classname == "env_fog_controller") {
        ++features.fogControllers;
    } else if (classname == "light_environment") {
        ++features.lightEnvironments;
    } else if (classname == "light_environment_volume") {
        ++features.lightEnvironmentVolumes;
    } else if (classname == "sky_camera") {
        ++features.skyCameras;
    } else if (classname == "info_player_start") {
        ++features.infoPlayerStarts;
    } else if (classname == "info_hardpoint") {
        ++features.infoHardpoints;
    } else if (classname == "assault_assaultpoint") {
        ++features.assaultPoints;
    }
    if (classname == "trigger_multiple" && entity.hasEditorClass
        && entity.editorClass == "trigger_mp_spawn_zone")
        ++features.spawnZones;
    if (StartsWith(classname, "trigger_"))
        ++features.triggerEntities;

    if (entity.hasScriptName) {
        if (entity.scriptName == "menu_camera_target")
            ++features.menuCameraTargets;
        else if (entity.scriptName == "menu_scene_ref")
            ++features.menuSceneRefs;
    }
    if (!entity.hasTargetname)
        return;
    if (StartsWith(entity.targetname, "spec_cam"))
        ++features.introCameraTargets;
    if (entity.targetname == "spacenode" || entity.targetname == "intro_SpaceNode")
        ++features.spaceNodeTargets;
    if (StartsWith(entity.targetname, "escape_node"))
        ++features.evacuationNodeTargets;
    if (entity.targetname == "skybox_cam_intro")
        ++features.skyboxIntroCameras;
    else if (entity.targetname == "skybox_cam_level")
        ++features.skyboxLevelCameras;
    else if (entity.targetname == "skybox_cam_day")
        ++features.skyboxDayCameras;
}

void CountSpawnModes(const EntityView& entity,
                     std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)>& counts)
{
    if (!IsSpawnClass(entity.classname))
        return;
    for (std::size_t index = 0; index < kSpawnModeRules.size(); ++index)
        CountSpawnMode(entity, kSpawnModeRules[index], counts[index]);
}

// R2 script_ref entities carry keys such as "NPC_Titan" (editor class info_pve_guard_spawn / info_atcoop_*). The R1
// engine pools key strings case-insensitively, so the first spelling wins and every later npc_titan/npc_spectre reports
// GetClassname() == "NPC_Titan": level.SpawnActions["npc_titan"] (case-sensitive) misses, titans get no soul and no spawn
// callbacks. R1 never reads these keys, so they are lowered in place (same length) in the entity text just appended
// to `output` starting at `entityBegin`.
bool IsNpcKey(std::string_view key)
{
    if (key.size() <= 4 || key.compare(0, 4, "NPC_") != 0)
        return false;
    for (std::size_t index = 4; index < key.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(key[index]);
        if (!(std::isalnum(ch) || ch == '_'))
            return false;
    }
    return true;
}

std::uint64_t LowerNpcKeys(std::string& output, std::size_t entityBegin)
{
    const std::string_view text(output.data() + entityBegin, output.size() - entityBegin);
    if (text.find("\"NPC_") == std::string_view::npos)
        return 0;

    std::uint64_t lowered = 0;
    Scanner scanner(text);
    Token token;
    ParseFailure failure;
    if (!scanner.Next(token, failure) || token.kind != TokenKind::Open)
        return 0;
    for (;;) {
        if (!scanner.Next(token, failure) || token.kind != TokenKind::Quoted)
            return lowered;
        const Span key = token.text;
        if (!scanner.Next(token, failure) || (token.kind != TokenKind::Quoted && token.kind != TokenKind::Bare))
            return lowered;
        if (!IsNpcKey(text.substr(key.begin, key.end - key.begin)))
            continue;
        for (std::size_t index = key.begin; index < key.end; ++index)
            output[entityBegin + index] = static_cast<char>(std::tolower(static_cast<unsigned char>(output[entityBegin + index])));
        ++lowered;
    }
}

} // namespace

Error::Error(std::size_t offset, const std::string& message)
    : std::runtime_error(message)
    , m_offset(offset)
{
}

std::size_t Error::offset() const noexcept
{
    return m_offset;
}

const SpawnModeRule& GetSpawnModeRule(SpawnMode mode) noexcept
{
    const std::size_t index = static_cast<std::size_t>(mode);
    return kSpawnModeRules[index < kSpawnModeRules.size() ? index : 0];
}

std::string RewriteEntityLump(std::string_view input,
                              const RewriteOptions& options,
                              RewriteReport* report)
{
    RewriteReport localReport;
    RewriteReport& result = report ? *report : localReport;
    result = {};

    std::string output;
    output.reserve(input.size());
    std::size_t appendCursor = 0;
    ParseFailure failure;

    const bool parsed = ScanEntityLump(
        input,
        options.strict,
        [](const EntityView&, std::string_view, std::string_view) {},
        [&](const EntityView& entity) {
            ++result.entitiesIn;
            CountSpawnModes(entity, result.spawnModes);

            const bool dropScriptMover = entity.classname == "script_mover_lightweight";
            const bool dropLightVolume = entity.classname == "light_environment_volume";
            if (dropScriptMover || dropLightVolume) {
                AppendRange(output, input, appendCursor, entity.begin);
                appendCursor = entity.end;
                if (dropScriptMover)
                    ++result.droppedScriptMoverLightweight;
                else
                    ++result.droppedLightEnvironmentVolume;
                return;
            }

            std::array<ValuePatch, 4> patches{};
            std::string renamedEffect;
            std::size_t patchCount = 0;
            AppendRange(output, input, appendCursor, entity.begin);
            const std::size_t entityOutputBegin = output.size();

            if (entity.classname == "prop_dynamic_lightweight") {
                patches[patchCount++] = {entity.classnameSpan, "prop_dynamic"};
                ++result.renamedClassnames;
            }
            if (entity.classname == "move_rope"
                && entity.hasRopeMaterial
                && entity.ropeMaterial == "cable/zipline.vmt")
                ++result.preservedZiplineMaterials;
            if (IsEdenSpaceNode(entity, options.mapName)) {
                patches[patchCount++] = {entity.targetnameSpan, "intro_SpaceNode"};
                ++result.rewrittenValues;
                ++result.renamedEdenSpaceNodes;
            }
            // Stock R1 flag-spawn entities use TeamNum; preserve it when present.
            if (entity.classname == "info_spawnpoint_flag"
                && !entity.hasTeamNum
                && entity.hasTeamNumberAlias
                && !entity.duplicateTeamNumberAlias) {
                patches[patchCount++] = {entity.teamNumberAliasKeySpan, "TeamNum"};
                ++result.renamedCtfTeamKeys;
            }
            if (options.effectNameMapper
                && entity.hasEffectName
                && (entity.classname == "info_particle_system"
                    || entity.classname == "prop_dynamic"
                    || entity.classname == "prop_dynamic_lightweight")
                && options.effectNameMapper(entity.effectName, renamedEffect)) {
                patches[patchCount++] = {entity.effectNameSpan, renamedEffect};
                ++result.renamedEffectNames;
            }
            SortPatches(patches, patchCount);

            std::size_t patchCursor = entity.begin;
            for (std::size_t index = 0; index < patchCount; ++index) {
                const ValuePatch& patch = patches[index];
                AppendRange(output, input, patchCursor, patch.span.begin);
                output.append(patch.replacement.data(), patch.replacement.size());
                patchCursor = patch.span.end;
            }
            AppendRange(output, input, patchCursor, entity.end);
            appendCursor = entity.end;
            result.loweredNpcKeys += LowerNpcKeys(output, entityOutputBegin);
            ++result.entitiesOut;
        },
        failure);

    if (!parsed) {
        result = {};
        result.unknownConstructs = 1;
        if (options.strict)
            throw Error(failure.offset, ErrorMessage(failure.offset, failure.message));
        return std::string(input);
    }

    AppendRange(output, input, appendCursor, input.size());
    return output;
}

bool ValidateEntityLump(std::string_view input, bool strict, std::string* error)
{
    ParseFailure failure;
    const bool valid = ScanEntityLump(
        input,
        strict,
        [](const EntityView&, std::string_view, std::string_view) {},
        [](const EntityView&) {},
        failure);
    if (!valid && error)
        *error = ErrorMessage(failure.offset, failure.message);
    else if (valid && error)
        error->clear();
    return valid;
}

bool AnalyzeEntityLump(std::string_view input,
                       EntityLumpAnalysis& analysis,
                       bool strict,
                       std::string* error)
{
    analysis = {};
    std::map<std::string, std::uint64_t> classnames;
    std::map<std::string, std::uint64_t> keys;
    ParseFailure failure;

    const bool valid = ScanEntityLump(
        input,
        strict,
        [&](const EntityView&, std::string_view key, std::string_view) {
            ++analysis.keyValuePairs;
            ++keys[std::string(key)];
        },
        [&](const EntityView& entity) {
            ++analysis.entities;
            if (entity.hasClassname)
                ++classnames[std::string(entity.classname)];
            AnalyzeFeatures(entity, analysis.features);
            CountSpawnModes(entity, analysis.spawnModes);
        },
        failure);

    if (!valid) {
        analysis = {};
        if (error)
            *error = ErrorMessage(failure.offset, failure.message);
        return false;
    }

    analysis.classnames.reserve(classnames.size());
    for (const auto& entry : classnames)
        analysis.classnames.push_back({entry.first, entry.second});
    analysis.keys.reserve(keys.size());
    for (const auto& entry : keys)
        analysis.keys.push_back({entry.first, entry.second});
    if (error)
        error->clear();
    return true;
}

std::string RaiseFlagSpawnOrigins(std::string_view input, double units, std::uint64_t* raised)
{
    if (raised)
        *raised = 0;

    struct Replacement {
        Span span;
        std::string text;
    };
    std::vector<Replacement> replacements;
    ParseFailure failure;
    const bool parsed = ScanEntityLump(
        input,
        false,
        [](const EntityView&, std::string_view, std::string_view) {},
        [&](const EntityView& entity) {
            if (entity.classname != "info_spawnpoint_flag" || !entity.hasOrigin)
                return;
            // "x y z": keep the x and y text as written, only z changes.
            const std::string origin(entity.origin);
            std::size_t tokenBegin[3]{};
            std::size_t tokenEnd[3]{};
            std::size_t position = 0;
            for (int token = 0; token < 3; ++token) {
                while (position < origin.size() && std::isspace(static_cast<unsigned char>(origin[position])))
                    ++position;
                if (position >= origin.size())
                    return;
                tokenBegin[token] = position;
                while (position < origin.size() && !std::isspace(static_cast<unsigned char>(origin[position])))
                    ++position;
                tokenEnd[token] = position;
            }
            while (position < origin.size() && std::isspace(static_cast<unsigned char>(origin[position])))
                ++position;
            if (position != origin.size())
                return;

            const std::string z = origin.substr(tokenBegin[2], tokenEnd[2] - tokenBegin[2]);
            char* parsedEnd = nullptr;
            const double value = std::strtod(z.c_str(), &parsedEnd);
            if (parsedEnd != z.c_str() + z.size())
                return;
            char formatted[64]{};
            std::snprintf(formatted, sizeof(formatted), "%.9g", value + units);
            replacements.push_back({entity.originSpan,
                origin.substr(0, tokenBegin[2]) + formatted});
        },
        failure);
    if (!parsed || replacements.empty())
        return std::string(input);

    std::string output;
    output.reserve(input.size() + replacements.size() * 8);
    std::size_t cursor = 0;
    for (const Replacement& replacement : replacements) {
        AppendRange(output, input, cursor, replacement.span.begin);
        output.append(replacement.text);
        cursor = replacement.span.end;
    }
    AppendRange(output, input, cursor, input.size());
    if (raised)
        *raised = replacements.size();
    return output;
}

} // namespace r2port::entity_rewrite
