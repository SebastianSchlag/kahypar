/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2014-2016 Sebastian Schlag <sebastian.schlag@kit.edu>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#pragma once

#include <algorithm>
#include <limits>
#include <vector>

#include "kahypar/datastructure/binary_heap.h"
#include "kahypar/datastructure/bucket_queue.h"
#include "kahypar/definitions.h"
#include "kahypar/macros.h"
#include "kahypar/meta/mandatory.h"
#include "kahypar/utils/randomize.h"

namespace kahypar {
namespace ds {
template <typename IDType = Mandatory,
          typename KeyType = Mandatory,
          typename MetaKey = Mandatory,
          bool UseRandomTieBreaking = false>
class KWayPriorityQueue {
#ifdef USE_BUCKET_PQ
  using Queue = EnhancedBucketQueue<IDType, KeyType, MetaKey>;
#else
  using Queue = BinaryMaxHeap<IDType, KeyType>;
#endif

  static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
  static constexpr PartitionID kInvalidPart = std::numeric_limits<PartitionID>::max();

  struct IndexPartMapping {
    PartitionID part;
    size_t index;
    IndexPartMapping(const PartitionID p, const size_t i) :
      part(p),
      index(i) { }
  };

 public:
  explicit KWayPriorityQueue(const PartitionID k) :
    _queues(),
    _mapping(k +  /* sentinel */ 1, { kInvalidPart, kInvalidIndex }),
    _ties(k),
    _num_entries(0),
    _num_nonempty_pqs(0),
    _num_enabled_pqs(0) { }

  KWayPriorityQueue(const KWayPriorityQueue&) = delete;
  KWayPriorityQueue& operator= (const KWayPriorityQueue&) = delete;

  KWayPriorityQueue(KWayPriorityQueue&&) = default;
  KWayPriorityQueue& operator= (KWayPriorityQueue&&) = delete;

  // PQ implementation might need different parameters for construction
  template <typename ... PQParameters>
  void initialize(PQParameters&& ... parameters) {
    // k = mapping.size() - 1. Last element in mapping is used as a sentinel.
    for (size_t i = 0; i < _mapping.size() - 1; ++i) {
      _queues.emplace_back(std::forward<PQParameters>(parameters) ...);
    }
  }

  __attribute__ ((always_inline)) size_t size(const PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    return (_mapping[part].index < _num_nonempty_pqs ? _queues[_mapping[part].index].size() : 0);
  }

  // Counts all elements in all non-empty heaps and therefore includes
  // the elements in disabled heaps as well
  __attribute__ ((always_inline)) size_t size() const {
    return _num_entries;
  }

  __attribute__ ((always_inline)) bool empty(const PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    return isUnused(part);
  }

  __attribute__ ((always_inline)) bool empty() const {
    return _num_enabled_pqs == 0 || _num_entries == 0;
  }

  __attribute__ ((always_inline)) PartitionID numEnabledParts() const {
    return _num_enabled_pqs;
  }

  __attribute__ ((always_inline)) PartitionID numNonEmptyParts() const {
    return _num_nonempty_pqs;
  }

  __attribute__ ((always_inline)) bool isEnabled(const PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    return _mapping[part].index < _num_enabled_pqs;
  }

  __attribute__ ((always_inline)) void enablePart(const PartitionID part) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    if (!isUnused(part) && !isEnabled(part)) {
      swap(_mapping[part].index, _num_enabled_pqs);
      ++_num_enabled_pqs;
      ASSERT(_num_enabled_pqs <= _num_nonempty_pqs, V(_num_enabled_pqs));
    }
  }

  __attribute__ ((always_inline)) void disablePart(const PartitionID part) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    if (isEnabled(part)) {
      --_num_enabled_pqs;
      swap(_mapping[part].index, _num_enabled_pqs);
    }
  }

  __attribute__ ((always_inline)) void insert(const IDType id, const PartitionID part,
                                              const KeyType key) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    DBG(false, "Insert: (" << id << "," << part << "," << key << ")");
    ASSERT((_mapping[part].index != kInvalidIndex) ||
           (_mapping[_num_nonempty_pqs].part == kInvalidPart),
           V(_mapping[_num_nonempty_pqs].part));
    // In order to get rid of the explicit branch, we have to use a sentinel
    // element at _mapping[k].
    _mapping[_num_nonempty_pqs].part = (_mapping[part].index == kInvalidIndex) ? part :
                                       _mapping[_num_nonempty_pqs].part;
    _mapping[part].index = (_mapping[part].index == kInvalidIndex) ? _num_nonempty_pqs++ :
                           _mapping[part].index;
    _queues[_mapping[part].index].push(id, key);
    ++_num_entries;
  }

  __attribute__ ((always_inline)) void deleteMax(IDType& max_id, KeyType& max_key,
                                                 PartitionID& max_part) {
    size_t max_index = UseRandomTieBreaking ? maxIndexRandomTieBreaking() : maxIndex();
    ASSERT(max_index < _num_enabled_pqs, V(max_index));

    max_part = _mapping[max_index].part;
    max_id = _queues[max_index].top();
    max_key = _queues[max_index].topKey();

    ASSERT(_mapping[_mapping[max_part].index].part == max_part, V(max_part));
    ASSERT(max_index != kInvalidIndex, V(max_index));
    ASSERT(max_key != MetaKey::max(), V(max_key));
    ASSERT(max_part != kInvalidPart, V(max_part) << V(max_id));
    ASSERT(static_cast<unsigned int>(max_part) < _queues.size(), "Invalid " << V(max_part));

    _queues[max_index].pop();
    if (_queues[max_index].empty()) {
      ASSERT(isEnabled(max_part), V(max_part));
      --_num_enabled_pqs;  // now points to the last enabled pq
      --_num_nonempty_pqs;  // now points to the last non-empty disbabled pq
      swap(_mapping[max_part].index, _num_enabled_pqs);
      swap(_mapping[max_part].index, _num_nonempty_pqs);
      markUnused(max_part);
    }
    --_num_entries;
  }

  __attribute__ ((always_inline)) void deleteMaxFromPartition(IDType& max_id, KeyType& max_key,
                                                              PartitionID part) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    size_t part_index = _mapping[part].index;
    ASSERT(part_index < _num_enabled_pqs, V(part_index));

    max_id = _queues[part_index].top();
    max_key = _queues[part_index].topKey();

    ASSERT(_mapping[_mapping[part].index].part == part, V(part));
    ASSERT(part_index != kInvalidIndex, V(part_index));
    ASSERT(max_key != MetaKey::max(), V(max_key));
    ASSERT(part != kInvalidPart, V(part) << V(max_id));

    _queues[part_index].pop();
    if (_queues[part_index].empty()) {
      ASSERT(isEnabled(part), V(part));
      --_num_enabled_pqs;  // now points to the last enabled pq
      --_num_nonempty_pqs;  // now points to the last non-empty disbabled pq
      swap(_mapping[part].index, _num_enabled_pqs);
      swap(_mapping[part].index, _num_nonempty_pqs);
      markUnused(part);
    }
    --_num_entries;
  }

  __attribute__ ((always_inline)) KeyType key(const IDType id,
                                              const PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    ASSERT(_mapping[part].index < _num_nonempty_pqs, V(part));
    ASSERT(_queues[_mapping[part].index].contains(id), V(id));
    return _queues[_mapping[part].index].getKey(id);
  }

  __attribute__ ((always_inline)) bool contains(const IDType id,
                                                const PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    return _mapping[part].index < _num_nonempty_pqs && _queues[_mapping[part].index].contains(id);
  }

  // Should be used only for assertions
  bool contains(const IDType id) const {
    for (size_t i = 0; i < _num_nonempty_pqs; ++i) {
      if (_queues[i].contains(id)) {
        return true;
      }
    }
    return false;
  }

  __attribute__ ((always_inline)) void updateKey(const IDType id, const PartitionID part,
                                                 const KeyType key) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    ASSERT(_mapping[part].index < _num_nonempty_pqs, V(part));
    _queues[_mapping[part].index].updateKey(id, key);
  }

  __attribute__ ((always_inline)) void updateKeyBy(const IDType id, const PartitionID part,
                                                   const KeyType key_delta) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    ASSERT(_mapping[part].index < _num_nonempty_pqs, V(part));
    _queues[_mapping[part].index].updateKeyBy(id, key_delta);
  }

  __attribute__ ((always_inline)) void remove(const IDType id,
                                              const PartitionID part) {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    ASSERT(_mapping[part].index < _num_nonempty_pqs, V(part));
    ASSERT(_queues[_mapping[part].index].contains(id), V(id) << V(part));
    _queues[_mapping[part].index].remove(id);
    if (_queues[_mapping[part].index].empty()) {
      if (isEnabled(part)) {
        --_num_enabled_pqs;  // now points to the last enabled pq
        swap(_mapping[part].index, _num_enabled_pqs);
      }
      _queues[_mapping[part].index].clear();  // eager clear, NOOP in case of ArrayStorage
      --_num_nonempty_pqs;  // now points to the last non-empty disbabled pq
      swap(_mapping[part].index, _num_nonempty_pqs);
      markUnused(part);
    }
    --_num_entries;
  }

  __attribute__ ((always_inline)) void clear() {
    for (size_t i = 0; i < _queues.size(); ++i) {
      _mapping[i] = { kInvalidPart, kInvalidIndex };
      _queues[i].clear();  // eager clear, NOOP in case of ArrayStorage
    }
    _num_entries = 0;
    _num_nonempty_pqs = 0;
    _num_enabled_pqs = 0;
  }

  IDType max() const {
    // Should only be used for testing
    return _queues[maxIndex()].top();
  }

  IDType max(PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    // Should only be used for testing
    return _queues[_mapping[part].index].top();
  }

  KeyType maxKey() const {
    // Should only be used for testing
    return _queues[maxIndex()].topKey();
  }

  KeyType maxKey(PartitionID part) const {
    ASSERT(static_cast<unsigned int>(part) < _queues.size(), "Invalid " << V(part));
    // Should only be used for testing
    return _queues[_mapping[part].index].topKey();
  }

 private:
  FRIEND_TEST(AKWayPriorityQueue, DisablesInternalHeapIfItBecomesEmptyDueToRemoval);
  FRIEND_TEST(AKWayPriorityQueue, ChoosesMaxKeyAmongAllEnabledInternalHeaps);
  FRIEND_TEST(AKWayPriorityQueue, DoesNotConsiderDisabledHeapForChoosingMax);
  FRIEND_TEST(AKWayPriorityQueue, ReconsidersDisabledHeapAgainAfterEnabling);
  FRIEND_TEST(AKWayPriorityQueue, PQIsUnusedAndDisableIfItBecomesEmptyAfterDeleteMaxFromPartition);

  __attribute__ ((always_inline)) void swap(const size_t index_a, const size_t index_b) {
    using std::swap;
    swap(_queues[index_a], _queues[index_b]);
    swap(_mapping[index_a].part, _mapping[index_b].part);
    swap(_mapping[_mapping[index_a].part].index, _mapping[_mapping[index_b].part].index);
    ASSERT(_mapping[_mapping[index_a].part].index == index_a &&
           _mapping[_mapping[index_b].part].index == index_b, "Swap failed");
  }

  __attribute__ ((always_inline)) size_t maxIndex() const {
    size_t max_index = kInvalidIndex;
    KeyType max_key = MetaKey::min();
    for (size_t index = 0; index < _num_enabled_pqs; ++index) {
      ASSERT(!_queues[index].empty(), V(index));
      const KeyType key = _queues[index].topKey();
      if (key > max_key) {
        max_key = key;
        max_index = index;
      }
    }
    ASSERT(max_index != kInvalidIndex, V(max_index));
    return max_index;
  }

  __attribute__ ((always_inline)) size_t maxIndexRandomTieBreaking() {
    KeyType max_key = MetaKey::min();
    for (size_t index = 0; index < _num_enabled_pqs; ++index) {
      ASSERT(!_queues[index].empty(), V(index));
      const KeyType key = _queues[index].topKey();
      if (key > max_key) {
        max_key = key;
        _ties.clear();
        _ties.push_back(index);
      } else if (key == max_key) {
        _ties.push_back(index);
      }
    }
    return _ties[Randomize::instance().getRandomInt(0, _ties.size() - 1)];
  }

  __attribute__ ((always_inline)) bool isUnused(const PartitionID part) const {
    ASSERT((_mapping[part].index != kInvalidIndex ? _mapping[_mapping[part].index].part != kInvalidPart : true), V(part));
    return _mapping[part].index == kInvalidIndex;
  }

  __attribute__ ((always_inline)) void markUnused(const PartitionID part) {
    _mapping[_mapping[part].index].part = kInvalidPart;
    _mapping[part].index = kInvalidIndex;
  }

  std::vector<Queue> _queues;
  std::vector<IndexPartMapping> _mapping;  // index to part mapping & part to index mapping
  std::vector<size_t> _ties;  // for random tie breaking
  size_t _num_entries;
  size_t _num_nonempty_pqs;
  size_t _num_enabled_pqs;
};
}  // namespace ds
}  // namespace kahypar
