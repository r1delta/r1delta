#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace r2port {

// Priority work queue of the R2 material provider (replaces the family-grouped queue).
//
// Items are handed out in the order R1 is expected to ask for them, not grouped by package family: a reader that is
// blocked on an item makes it urgent (rank 0, handed out first); everything else is ordered by (tier, order, seq): tier 0
// holds the VMTs (R1 reads every VMT of a map before its textures), tier 1 the textures, `order` is the position of the
// item in the BSP material list (a texture inherits its VMT's position) so a texture converted for an early material
// sorts before one for a late material no matter which worker finished its VMT first, and `seq` breaks ties in arrival
// order.
//
// Take() scans in that order and hands out the first item the caller's admission predicate accepts, so the caller can
// skip an item whose package family is being decoded by another worker, or whose family does not fit in memory yet,
// without idling the thread.
//
// Not thread safe: the owner serializes every call (the provider uses its cache mutex).
template <class Item>
class WorkQueue {
public:
    using Family = std::uint32_t;
    static constexpr Family kNoFamily = std::numeric_limits<Family>::max();

    struct Meta {
        Family family = kNoFamily;
        std::uint32_t tier = 0;
        std::uint64_t order = 0;
        bool urgent = false;
    };

    std::size_t Size() const { return m_items.size(); }
    bool Empty() const { return m_items.empty(); }

    void Push(Item item, const Meta& meta)
    {
        const Key key{ meta.urgent ? 0u : 1u, meta.tier, meta.order, m_nextSeq++ };
        m_items.emplace(key, Entry{ std::move(item), meta });
    }

    // Makes the first queued item matching `pred` urgent. Returns false when none matches.
    template <class Pred>
    bool Promote(Pred pred)
    {
        for (auto it = m_items.begin(); it != m_items.end(); ++it) {
            if (!pred(it->second.item))
                continue;
            if (std::get<0>(it->first) == 0)
                return true;
            Entry entry = std::move(it->second);
            entry.meta.urgent = true;
            m_items.erase(it);
            m_items.emplace(Key{ 0u, entry.meta.tier, entry.meta.order, m_nextSeq++ }, std::move(entry));
            return true;
        }
        return false;
    }

    // Hands out the first item `admit(meta)` accepts; false when there is none.
    template <class Admit>
    bool Take(Item& out, Meta& meta, Admit admit)
    {
        for (auto it = m_items.begin(); it != m_items.end(); ++it) {
            if (!admit(it->second.meta))
                continue;
            out = std::move(it->second.item);
            meta = it->second.meta;
            m_items.erase(it);
            return true;
        }
        return false;
    }

    // Removes and returns every queued item (shutdown).
    std::vector<std::pair<Item, Meta>> Drain()
    {
        std::vector<std::pair<Item, Meta>> items;
        items.reserve(m_items.size());
        for (auto& [key, entry] : m_items)
            items.emplace_back(std::move(entry.item), entry.meta);
        m_items.clear();
        return items;
    }

private:
    struct Entry {
        Item item;
        Meta meta;
    };
    using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t>;

    std::map<Key, Entry> m_items;
    std::uint64_t m_nextSeq = 0;
};

} // namespace r2port
