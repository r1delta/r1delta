#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

namespace r2port {

// Work queue that hands conversion work out grouped by R2 package family.
//
// r2tex converts an asset by decoding the whole RTech-compressed family that holds it (hundreds of MiB,
// 0.2 to 1 s of CPU) and keeps a small LRU of decoded families. Handing items to workers in arrival order
// makes every worker want a different family, the LRU thrashes, and a map load decodes the same family
// several times over. Here a worker CLAIMS a family and drains every queued item of it before it takes
// another one, no family is ever claimed by two workers, and at most `maxActive` families are claimed at a
// time, which also bounds the memory held by decoded families. Items that do not need a decode (family
// unknown, or already in the disk cache) carry kNoFamily and are handed out freely.
//
// Not thread safe: the owner serializes every call (the provider uses its cache mutex).
template <class Item>
class FamilyQueue {
public:
    using Family = std::uint32_t;
    static constexpr Family kNoFamily = std::numeric_limits<Family>::max();

    explicit FamilyQueue(std::size_t maxActive = 4) : m_maxActive(maxActive ? maxActive : 1) {}

    void SetMaxActiveFamilies(std::size_t maxActive) { m_maxActive = maxActive ? maxActive : 1; }
    std::size_t MaxActiveFamilies() const { return m_maxActive; }
    std::size_t Size() const { return m_size; }
    bool Empty() const { return m_size == 0; }
    std::size_t ActiveFamilies() const { return m_active.size(); }

    // `urgent` items go first and may be claimed past the active-family limit (a reader is blocked on them).
    // `material` items sort their family ahead of texture-only families: a VMT conversion queues its textures.
    void Push(Item item, Family family, bool material, bool urgent)
    {
        Entry entry{ std::move(item), material, urgent };
        if (family == kNoFamily) {
            if (urgent)
                m_free.push_front(std::move(entry));
            else
                m_free.push_back(std::move(entry));
        }
        else {
            Bucket& bucket = m_buckets[family];
            bucket.materials += material ? 1 : 0;
            bucket.urgent += urgent ? 1 : 0;
            if (urgent)
                bucket.items.push_front(std::move(entry));
            else
                bucket.items.push_back(std::move(entry));
        }
        ++m_size;
    }

    // Marks the first queued item matching `pred` urgent and moves it to the front of its list.
    template <class Pred>
    bool Promote(Pred pred)
    {
        if (PromoteIn(m_free, nullptr, pred))
            return true;
        for (auto& [family, bucket] : m_buckets) {
            if (PromoteIn(bucket.items, &bucket, pred))
                return true;
        }
        return false;
    }

    // Gives the caller its next item. `owned` is the family the caller holds (kNoFamily when none); it is
    // kept while the family has queued items and released when it runs dry. Returns false when nothing can
    // be handed out right now (empty, or every family with work is claimed or the active limit is reached).
    // `resident(family)` tells whether a family is still decoded in the cache (prefer it).
    template <class Resident>
    bool Take(Family& owned, Item& out, Resident resident)
    {
        if (owned != kNoFamily) {
            const auto found = m_buckets.find(owned);
            if (found != m_buckets.end() && !found->second.items.empty()) {
                PopFront(found->second, out);
                return true;
            }
            Release(owned);
            owned = kNoFamily;
        }
        if (!m_free.empty()) {
            out = std::move(m_free.front().item);
            m_free.pop_front();
            --m_size;
            return true;
        }

        Family best = kNoFamily;
        Score bestScore{};
        for (const auto& [family, bucket] : m_buckets) {
            if (bucket.items.empty() || m_active.count(family))
                continue;
            const Score score{ bucket.urgent > 0, bucket.materials > 0, resident(family), bucket.items.size() };
            if (best == kNoFamily || bestScore < score) {
                best = family;
                bestScore = score;
            }
        }
        if (best == kNoFamily)
            return false;
        const std::size_t limit = m_maxActive + (bestScore.urgent ? kUrgentSlack : 0);
        if (m_active.size() >= limit)
            return false;
        m_active.insert(best);
        owned = best;
        PopFront(m_buckets[best], out);
        return true;
    }

    bool Take(Family& owned, Item& out)
    {
        return Take(owned, out, [](Family) { return false; });
    }

    // For a worker that stops while it still owns a family.
    void Release(Family& owned)
    {
        if (owned == kNoFamily)
            return;
        m_active.erase(owned);
        const auto found = m_buckets.find(owned);
        if (found != m_buckets.end() && found->second.items.empty())
            m_buckets.erase(found);
        owned = kNoFamily;
    }

    // Removes and returns every queued item (shutdown).
    std::vector<Item> Drain()
    {
        std::vector<Item> items;
        items.reserve(m_size);
        for (Entry& entry : m_free)
            items.push_back(std::move(entry.item));
        for (auto& [family, bucket] : m_buckets)
            for (Entry& entry : bucket.items)
                items.push_back(std::move(entry.item));
        Clear();
        return items;
    }

    void Clear()
    {
        m_free.clear();
        m_buckets.clear();
        m_active.clear();
        m_size = 0;
    }

private:
    static constexpr std::size_t kUrgentSlack = 2;

    struct Entry {
        Item item;
        bool material{};
        bool urgent{};
    };
    struct Bucket {
        std::deque<Entry> items;
        std::size_t materials{};
        std::size_t urgent{};
    };
    struct Score {
        bool urgent{};
        bool material{};
        bool resident{};
        std::size_t count{};
        bool operator<(const Score& other) const
        {
            if (urgent != other.urgent) return !urgent;
            if (material != other.material) return !material;
            if (resident != other.resident) return !resident;
            return count < other.count;
        }
    };

    void PopFront(Bucket& bucket, Item& out)
    {
        Entry& front = bucket.items.front();
        bucket.materials -= front.material ? 1 : 0;
        bucket.urgent -= front.urgent ? 1 : 0;
        out = std::move(front.item);
        bucket.items.pop_front();
        --m_size;
    }

    template <class Pred>
    static bool PromoteIn(std::deque<Entry>& items, Bucket* bucket, Pred& pred)
    {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (!pred(it->item))
                continue;
            if (!it->urgent) {
                it->urgent = true;
                if (bucket)
                    ++bucket->urgent;
            }
            if (it != items.begin()) {
                Entry moved = std::move(*it);
                items.erase(it);
                items.push_front(std::move(moved));
            }
            return true;
        }
        return false;
    }

    std::size_t m_maxActive;
    std::size_t m_size{};
    std::deque<Entry> m_free;
    std::unordered_map<Family, Bucket> m_buckets;
    std::unordered_set<Family> m_active;
};

} // namespace r2port
