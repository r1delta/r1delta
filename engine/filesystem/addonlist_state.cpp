#include "addonlist_state.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace AddonListState {
namespace {

constexpr size_t MaxAddonListSize = 1024 * 1024;
constexpr size_t MaxAddonCount = 4096;

enum class TokenType {
	End,
	Value,
	OpenBrace,
	CloseBrace,
	Invalid,
};

struct Token {
	TokenType type = TokenType::Invalid;
	std::string value;
};

char LowerAscii(char value)
{
	if (value >= 'A' && value <= 'Z')
		return static_cast<char>(value + ('a' - 'A'));
	return value;
}

std::string NormalizeName(std::string_view name)
{
	std::string normalized(name);
	for (char& value : normalized)
		value = LowerAscii(value);
	return normalized;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
	if (left.size() != right.size())
		return false;
	for (size_t i = 0; i < left.size(); ++i) {
		if (LowerAscii(left[i]) != LowerAscii(right[i]))
			return false;
	}
	return true;
}

class Tokenizer {
public:
	explicit Tokenizer(std::string_view contents) : contents_(contents) {}

	Token Next()
	{
		SkipWhitespaceAndComments();
		if (position_ == contents_.size())
			return { TokenType::End, {} };

		const char current = contents_[position_++];
		if (current == '{')
			return { TokenType::OpenBrace, {} };
		if (current == '}')
			return { TokenType::CloseBrace, {} };
		if (current == '"')
			return ParseQuotedValue();

		std::string value(1, current);
		while (position_ < contents_.size()) {
			const char next = contents_[position_];
			if (std::isspace(static_cast<unsigned char>(next)) || next == '{' || next == '}')
				break;
			if (next == '/' && position_ + 1 < contents_.size() && contents_[position_ + 1] == '/')
				break;
			value.push_back(next);
			++position_;
		}
		return { value.empty() ? TokenType::Invalid : TokenType::Value, std::move(value) };
	}

private:
	void SkipWhitespaceAndComments()
	{
		for (;;) {
			while (position_ < contents_.size()
				&& std::isspace(static_cast<unsigned char>(contents_[position_])))
				++position_;
			if (position_ + 1 >= contents_.size()
				|| contents_[position_] != '/' || contents_[position_ + 1] != '/')
				return;
			position_ += 2;
			while (position_ < contents_.size() && contents_[position_] != '\n')
				++position_;
		}
	}

	Token ParseQuotedValue()
	{
		std::string value;
		while (position_ < contents_.size()) {
			const char current = contents_[position_++];
			if (current == '"')
				return { TokenType::Value, std::move(value) };
			if (current == '\r' || current == '\n' || current == '\0')
				return { TokenType::Invalid, {} };
			if (current != '\\') {
				value.push_back(current);
				continue;
			}
			if (position_ == contents_.size())
				return { TokenType::Invalid, {} };
			const char escaped = contents_[position_++];
			switch (escaped) {
			case '\\': value.push_back('\\'); break;
			case '"': value.push_back('"'); break;
			case 'n': value.push_back('\n'); break;
			case 'r': value.push_back('\r'); break;
			case 't': value.push_back('\t'); break;
			default:
				value.push_back('\\');
				value.push_back(escaped);
				break;
			}
		}
		return { TokenType::Invalid, {} };
	}

	std::string_view contents_;
	size_t position_ = 0;
};

bool ParseEnabledValue(std::string_view value, bool& enabled)
{
	int numericValue = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), numericValue);
	if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
		return false;
	enabled = numericValue != 0;
	return true;
}

}

bool Parse(std::string_view contents, std::vector<Entry>& entries)
{
	if (contents.empty() || contents.size() > MaxAddonListSize)
		return false;

	Tokenizer tokenizer(contents);
	const Token root = tokenizer.Next();
	if (root.type != TokenType::Value || !EqualsIgnoreCase(root.value, "AddonList")
		|| tokenizer.Next().type != TokenType::OpenBrace)
		return false;

	std::vector<Entry> parsed;
	std::unordered_map<std::string, size_t> indices;
	for (;;) {
		Token name = tokenizer.Next();
		if (name.type == TokenType::CloseBrace)
			break;
		if (name.type != TokenType::Value || !IsSafeDirectoryName(name.value))
			return false;

		Token value = tokenizer.Next();
		bool enabled = false;
		if (value.type != TokenType::Value || !ParseEnabledValue(value.value, enabled))
			return false;

		const std::string normalized = NormalizeName(name.value);
		const auto existing = indices.find(normalized);
		if (existing != indices.end()) {
			parsed[existing->second] = { std::move(name.value), enabled };
			continue;
		}
		if (parsed.size() == MaxAddonCount)
			return false;
		indices.emplace(normalized, parsed.size());
		parsed.push_back({ std::move(name.value), enabled });
	}
	if (tokenizer.Next().type != TokenType::End)
		return false;
	entries = std::move(parsed);
	return true;
}

bool Load(const std::filesystem::path& path, std::vector<Entry>& entries)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error) || error)
		return false;
	const uintmax_t fileSize = std::filesystem::file_size(path, error);
	if (error || !fileSize || fileSize > MaxAddonListSize)
		return false;

	std::string contents(static_cast<size_t>(fileSize), '\0');
	std::ifstream file(path, std::ios::binary);
	if (!file.read(contents.data(), static_cast<std::streamsize>(contents.size())))
		return false;
	return Parse(contents, entries);
}

bool IsEnabled(const std::vector<Entry>& entries, std::string_view name)
{
	for (const Entry& entry : entries) {
		if (EqualsIgnoreCase(entry.name, name))
			return entry.enabled;
	}
	return false;
}

bool IsSafeDirectoryName(std::string_view name)
{
	if (name.empty() || name == "." || name == ".."
		|| name.back() == '.' || name.back() == ' '
		|| name.find_first_of("<>:\"/\\|?*") != std::string_view::npos)
		return false;
	for (unsigned char value : name) {
		if (value < 0x20 || value == 0x7F)
			return false;
	}

	const size_t extension = name.find('.');
	const std::string stem = NormalizeName(name.substr(0, extension));
	if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul")
		return false;
	return !((stem.size() == 4 && (stem.compare(0, 3, "com") == 0
			|| stem.compare(0, 3, "lpt") == 0))
		&& stem[3] >= '1' && stem[3] <= '9');
}

uint64_t RescanState::Generation() const
{
	return generation_;
}

bool RescanState::IsSuspended() const
{
	return updateDepth_ != 0;
}

bool RescanState::CanPublish(uint64_t generation) const
{
	return !IsSuspended() && generation == generation_;
}

void RescanState::RequestRescan()
{
	++generation_;
}

void RescanState::BeginSearchPathUpdate()
{
	++updateDepth_;
	RequestRescan();
}

SearchPathUpdateEnd RescanState::EndSearchPathUpdate()
{
	if (!updateDepth_)
		return SearchPathUpdateEnd::Unmatched;
	if (--updateDepth_)
		return SearchPathUpdateEnd::Nested;
	RequestRescan();
	return SearchPathUpdateEnd::Outermost;
}

}
