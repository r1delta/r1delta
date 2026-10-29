#include "persistentdata_store.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace PersistentDataStore {
namespace {

constexpr std::string_view kHeader =
	"// R1Delta persistent data v1. Written by the game; only edit it while the game is closed.\n";
constexpr std::string_view kTrailerPrefix = "// end entries=";
constexpr std::string_view kChecksumLabel = " fnv1a64=";
constexpr std::string_view kPersistPrefix = "__ ";

uint64_t Fnv1a64(uint64_t hash, std::string_view text)
{
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 0x100000001B3ULL;
	}
	return hash;
}

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;

bool IsLineSpace(char c)
{
	return c == ' ' || c == '\t';
}

std::string_view TrimLeft(std::string_view text)
{
	while (!text.empty() && IsLineSpace(text.front()))
		text.remove_prefix(1);
	return text;
}

std::string_view TrimRight(std::string_view text)
{
	while (!text.empty() && (IsLineSpace(text.back()) || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

bool IsPersistentLine(std::string_view line)
{
	line = TrimLeft(line);
	return line.size() > 2 && line[0] == '_' && line[1] == '_' && IsLineSpace(line[2]);
}

// Parses `__ key "value"` (or an unquoted single-token value, which older
// profile writers could produce). The line must not include its newline.
bool ParseEntryLine(std::string_view line, std::string_view& key, std::string_view& value)
{
	line = TrimRight(TrimLeft(line));
	if (!IsPersistentLine(line))
		return false;
	line = TrimLeft(line.substr(2));

	const size_t keyEnd = line.find_first_of(" \t");
	if (keyEnd == std::string_view::npos || keyEnd == 0)
		return false;
	key = line.substr(0, keyEnd);
	line = TrimLeft(line.substr(keyEnd));
	if (line.empty())
		return false;

	if (line.front() == '"') {
		if (line.size() < 2 || line.back() != '"')
			return false;
		value = line.substr(1, line.size() - 2);
		if (value.find('"') != std::string_view::npos)
			return false;
	}
	else {
		if (line.find_first_of(" \t\"") != std::string_view::npos)
			return false;
		value = line;
	}

	return IsSafeToken(key, MaxKeySize)
		&& key.find(' ') == std::string_view::npos
		&& IsSafeToken(value, MaxValueSize);
}

template <typename Fn>
void ForEachLine(std::string_view contents, Fn&& fn)
{
	size_t offset = 0;
	while (offset < contents.size()) {
		size_t end = contents.find('\n', offset);
		const bool terminated = end != std::string_view::npos;
		if (!terminated)
			end = contents.size();
		fn(contents.substr(offset, end - offset), terminated);
		offset = end + 1;
	}
}

bool ParseTrailer(std::string_view line, size_t& count, uint64_t& checksum)
{
	line = TrimRight(line);
	if (line.substr(0, kTrailerPrefix.size()) != kTrailerPrefix)
		return false;
	line.remove_prefix(kTrailerPrefix.size());

	const size_t labelPos = line.find(kChecksumLabel);
	if (labelPos == std::string_view::npos || labelPos == 0)
		return false;

	count = 0;
	for (char c : line.substr(0, labelPos)) {
		if (c < '0' || c > '9' || count > (SIZE_MAX / 10))
			return false;
		count = count * 10 + static_cast<size_t>(c - '0');
	}

	const std::string_view hex = line.substr(labelPos + kChecksumLabel.size());
	if (hex.size() != 16)
		return false;
	checksum = 0;
	for (char c : hex) {
		uint64_t nibble;
		if (c >= '0' && c <= '9')
			nibble = static_cast<uint64_t>(c - '0');
		else if (c >= 'a' && c <= 'f')
			nibble = static_cast<uint64_t>(c - 'a' + 10);
		else
			return false;
		checksum = (checksum << 4) | nibble;
	}
	return true;
}

}

bool IsSafeToken(std::string_view text, size_t maxSize)
{
	if (text.empty() || text.size() > maxSize)
		return false;
	for (char c : text) {
		if (c < 32 || c > 126)
			return false;
		switch (c) {
		case '"': case '\\': case '{': case '}': case '\'': case '`':
		case ';': case '/': case '*': case '<': case '>': case '&':
		case '|': case '$': case '!': case '?': case '+': case '%':
			return false;
		default:
			break;
		}
	}
	return true;
}

std::string FormatLine(std::string_view key, std::string_view value)
{
	std::string line;
	line.reserve(kPersistPrefix.size() + key.size() + value.size() + 4);
	line.append(kPersistPrefix);
	line.append(key);
	line.append(" \"");
	line.append(value);
	line.append("\"\n");
	return line;
}

std::string Serialize(const Entries& entries)
{
	std::string output;
	output.reserve(kHeader.size() + entries.size() * 64 + 64);
	output.append(kHeader);

	uint64_t checksum = kFnvOffset;
	for (const auto& [key, value] : entries) {
		const std::string line = FormatLine(key, value);
		checksum = Fnv1a64(checksum, line);
		output.append(line);
	}

	char trailer[96];
	snprintf(trailer, sizeof(trailer), "%.*s%zu%.*s%016llx\n",
		static_cast<int>(kTrailerPrefix.size()), kTrailerPrefix.data(),
		entries.size(),
		static_cast<int>(kChecksumLabel.size()), kChecksumLabel.data(),
		static_cast<unsigned long long>(checksum));
	output.append(trailer);
	return output;
}

ParseStatus Parse(std::string_view contents, Entries& entries)
{
	entries.clear();
	if (contents.empty() || contents.size() > MaxFileSize)
		return ParseStatus::Invalid;

	bool damaged = false;
	bool sawTrailer = false;
	bool trailerLast = false;
	size_t trailerCount = 0;
	uint64_t trailerChecksum = 0;
	size_t entryLines = 0;
	uint64_t checksum = kFnvOffset;

	ForEachLine(contents, [&](std::string_view line, bool terminated) {
		const std::string_view trimmed = TrimRight(TrimLeft(line));
		if (trimmed.empty())
			return;
		if (sawTrailer) {
			// Anything after the trailer means the file was appended to or torn.
			trailerLast = false;
			damaged = true;
		}
		if (trimmed.substr(0, 2) == "//") {
			size_t count = 0;
			uint64_t sum = 0;
			if (ParseTrailer(trimmed, count, sum)) {
				sawTrailer = true;
				trailerLast = terminated;
				trailerCount = count;
				trailerChecksum = sum;
			}
			return;
		}

		std::string_view key;
		std::string_view value;
		if (!terminated || !ParseEntryLine(line, key, value)) {
			damaged = true;
			return;
		}
		++entryLines;
		checksum = Fnv1a64(checksum, FormatLine(key, value));
		auto [it, inserted] = entries.try_emplace(std::string(key), value);
		if (!inserted) {
			it->second.assign(value);
			damaged = true;
		}
	});

	const bool intact = sawTrailer && trailerLast && !damaged
		&& trailerCount == entryLines && trailerChecksum == checksum;
	if (intact)
		return ParseStatus::Valid;
	return entries.empty() ? ParseStatus::Invalid : ParseStatus::Damaged;
}

size_t ExtractLegacyProfileEntries(std::string_view profile, Entries& entries)
{
	size_t extracted = 0;
	ForEachLine(profile, [&](std::string_view line, bool) {
		std::string_view key;
		std::string_view value;
		if (!ParseEntryLine(line, key, value))
			return;
		entries.insert_or_assign(std::string(key), std::string(value));
		++extracted;
	});
	return extracted;
}

bool ProfileHasOwnerMarker(std::string_view profile)
{
	const std::string_view name = ProfileOwnerConVar;
	bool found = false;
	ForEachLine(profile, [&](std::string_view line, bool) {
		line = TrimLeft(line);
		if (line.substr(0, name.size()) == name
			&& line.size() > name.size() && IsLineSpace(line[name.size()]))
			found = true;
	});
	return found;
}

size_t MergeLegacyEntries(Entries& store, const Entries& legacy)
{
	size_t changed = 0;
	for (const auto& [key, value] : legacy) {
		auto [it, inserted] = store.try_emplace(key, value);
		if (inserted) {
			++changed;
		}
		else if (it->second != value) {
			it->second = value;
			++changed;
		}
	}
	return changed;
}

std::string StripPersistentLines(std::string_view profile)
{
	std::string output;
	output.reserve(profile.size());
	size_t offset = 0;
	while (offset < profile.size()) {
		size_t end = profile.find('\n', offset);
		end = end == std::string_view::npos ? profile.size() : end + 1;
		const std::string_view line = profile.substr(offset, end - offset);
		if (!IsPersistentLine(line))
			output.append(line);
		offset = end;
	}
	return output;
}

namespace {

char LowerAscii(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IsAsciiAlnum(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool EqualsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (LowerAscii(a[i]) != LowerAscii(b[i]))
			return false;
	}
	return true;
}

}

bool RenameTokens(std::string_view text, std::string& renamed)
{
	renamed.clear();
	bool changed = false;
	size_t copied = 0; // text[0, copied) is already in `renamed`
	for (size_t i = 0; i < text.size();) {
		const RenamedName* hit = nullptr;
		// Every old name starts with 'm'; skip the table walk everywhere else.
		if (LowerAscii(text[i]) == 'm' && (i == 0 || !(IsAsciiAlnum(text[i - 1]) || text[i - 1] == '_'))) {
			for (const RenamedName& rename : RenamedNames) {
				if (text.size() - i < rename.from.size())
					continue;
				if (!EqualsNoCase(text.substr(i, rename.from.size()), rename.from))
					continue;
				const size_t end = i + rename.from.size();
				if (end < text.size() && IsAsciiAlnum(text[end]))
					continue;
				hit = &rename;
				break;
			}
		}
		if (!hit) {
			++i;
			continue;
		}
		if (!changed) {
			changed = true;
			renamed.reserve(text.size() + 32);
		}
		renamed.append(text.substr(copied, i - copied));
		renamed.append(hit->to);
		i += hit->from.size();
		copied = i;
	}
	if (changed)
		renamed.append(text.substr(copied));
	return changed;
}

size_t MigrateRenamedNames(Entries& entries)
{
	struct MovedEntry
	{
		std::string oldKey;
		std::string newKey;
		std::string value;
	};
	std::vector<MovedEntry> moved;
	size_t changed = 0;
	std::string renamed;

	for (auto& [key, value] : entries) {
		bool touched = false;
		if (RenameTokens(value, renamed) && renamed.size() <= MaxValueSize) {
			value = renamed;
			touched = true;
		}
		if (RenameTokens(key, renamed) && renamed.size() <= MaxKeySize) {
			moved.push_back({ key, renamed, value });
			touched = true;
		}
		if (touched)
			++changed;
	}

	// Re-key after the walk: the map must not change while it is iterated.
	// Entries that already live under the new name were never moved, so they are
	// still present here and win over the old variant.
	for (MovedEntry& entry : moved) {
		entries.erase(entry.oldKey);
		entries.try_emplace(std::move(entry.newKey), std::move(entry.value));
	}
	return changed;
}

LoadoutVerdict JudgeLoadoutValue(std::string_view key, std::string_view value)
{
	const auto contains = [&](std::string_view needle) {
		return value.find(needle) != std::string_view::npos;
	};

	if (key.find("titanLoadouts") != std::string_view::npos) {
		if (contains("mp_weapon_mega")
			|| contains("mp_titanweapon_minigun")
			|| contains("mp_titanweapon_charge_cannon"))
			return LoadoutVerdict::Accept;
		if (contains("mp_weapon"))
			return LoadoutVerdict::Reject;
	}

	if (key.find("pilotLoadouts") != std::string_view::npos) {
		if (contains("mp_titanweapon"))
			return LoadoutVerdict::Reject;
	}

	return LoadoutVerdict::Neutral;
}

}
