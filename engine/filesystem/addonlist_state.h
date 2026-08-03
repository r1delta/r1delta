#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace AddonListState {

struct Entry {
	std::string name;
	bool enabled = false;
};

bool Parse(std::string_view contents, std::vector<Entry>& entries);
bool Load(const std::filesystem::path& path, std::vector<Entry>& entries);
bool IsEnabled(const std::vector<Entry>& entries, std::string_view name);
bool IsSafeDirectoryName(std::string_view name);

enum class SearchPathUpdateEnd {
	Unmatched,
	Nested,
	Outermost,
};

class RescanState {
public:
	uint64_t Generation() const;
	bool IsSuspended() const;
	bool CanPublish(uint64_t generation) const;
	void RequestRescan();
	void BeginSearchPathUpdate();
	SearchPathUpdateEnd EndSearchPathUpdate();

private:
	uint64_t generation_ = 0;
	uint32_t updateDepth_ = 0;
};

}
