//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// btree_multi_index.h
//
// Identification: src/backend/index/btree_multi_index.h
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>
#include <string>
#include <map>

#include "stx/btree_multimap.h"
#include "backend/catalog/manager.h"
#include "backend/common/types.h"
#include "backend/common/synch.h"
#include "backend/index/index.h"

namespace peloton {
namespace index {

/**
 * B+tree-based index implementation.
 *
 * @see Index
 */
template <typename KeyType, class KeyComparator, class KeyEqualityChecker>
class BtreeIndex : public Index {
  friend class IndexFactory;

  typedef ItemPointer ValueType;

  typedef stx::btree_multimap<KeyType, ValueType, KeyComparator> MapType;
  // typedef std::multimap<KeyType, ValueType, KeyComparator> MapType;

 public:
  BtreeIndex(IndexMetadata *metadata)
 : Index(metadata),
   container(KeyComparator(metadata)),
   equals(metadata),
   comparator(metadata) {}

  ~BtreeIndex() {}

  bool InsertEntry(const storage::Tuple *key, const ItemPointer location) {
    KeyType index_key;
    index_key.SetFromKey(key);

    {
      index_lock.WriteLock();

      // Insert the key, val pair
      container.insert(std::pair<KeyType, ValueType>(index_key, location));

      index_lock.Unlock();
    }

    return true;
  }

  bool DeleteEntry(const storage::Tuple *key, const ItemPointer location) {
    KeyType index_key;
    index_key.SetFromKey(key);

    {
      index_lock.WriteLock();

      // Delete the <key, location> pair
      bool stop = false;
      while (stop == false) {
        auto entries = container.equal_range(index_key);
        stop = true;

        for (auto iterator = entries.first; iterator != entries.second; iterator++) {
          ItemPointer value = iterator->second;

          // remove matching < key, location > entry
          if ((value.block == location.block) && (value.offset == location.offset)) {
            container.erase(iterator);
            stop = false;
            break;
          }

        }
      }

      index_lock.Unlock();
    }

    return true;
  }

  bool UpdateEntry(const storage::Tuple *key, const ItemPointer location) {
    KeyType index_key;
    index_key.SetFromKey(key);

    {
      index_lock.WriteLock();

      // insert the key, val pair
      container.insert(std::pair<KeyType, ValueType>(index_key, location));

      index_lock.Unlock();
    }

    return true;
  }

  std::vector<ItemPointer> Scan(const std::vector<Value> &values,
                                const std::vector<oid_t> &key_column_ids,
                                const std::vector<ExpressionType> &expr_types) {
    std::vector<ItemPointer> result;
    KeyType index_key;

    {
      index_lock.ReadLock();

      // check if we have leading column equality
      oid_t leading_column_id = 0;
      auto key_column_ids_itr = std::find(
          key_column_ids.begin(), key_column_ids.end(), leading_column_id);
      bool special_case = false;
      if (key_column_ids_itr != key_column_ids.end()) {
        auto offset = std::distance(key_column_ids.begin(), key_column_ids_itr);
        if (expr_types[offset] == EXPRESSION_TYPE_COMPARE_EQUAL) {
          special_case = true;
          LOG_INFO("Special case");
        }
      }

      auto itr_begin = container.begin();
      auto itr_end = container.end();
      storage::Tuple *start_key = nullptr;
      // start scanning from upper bound if possible
      if (special_case == true) {
        start_key = new storage::Tuple(metadata->GetKeySchema(), true);
        // set the lower bound tuple
        auto all_equal =
            SetLowerBoundTuple(start_key, values, key_column_ids, expr_types);
        index_key.SetFromKey(start_key);

        // all equal case
        if (all_equal) {
          auto ret = container.equal_range(index_key);
          itr_begin = ret.first;
          itr_end = ret.second;
          LOG_INFO("equal range return %ld", std::distance(itr_begin, itr_end));
        } else {
          itr_begin = container.upper_bound(index_key);
        }
      }

      // scan all entries comparing against arbitrary key
      for (auto itr = itr_begin; itr != itr_end; itr++) {
        auto index_key = itr->first;
        auto tuple = index_key.GetTupleForComparison(metadata->GetKeySchema());

        if (Compare(tuple, key_column_ids, expr_types, values) == true) {
          ItemPointer location = itr->second;
          result.push_back(location);
        }
      }

      delete start_key;

      index_lock.Unlock();
    }

    return result;
  }

  std::vector<ItemPointer> ScanAllKeys() {
    std::vector<ItemPointer> result;

    {
      index_lock.ReadLock();

      auto itr = container.begin();

      // scan all entries
      while (itr != container.end()) {
        ItemPointer location = itr->second;
        result.push_back(location);
        itr++;
      }

      index_lock.Unlock();
    }

    return result;
  }

  /**
   * @brief Return all locations related to this key.
   */
  std::vector<ItemPointer> ScanKey(const storage::Tuple *key) {
    std::vector<ItemPointer> retval;
    KeyType index_key;
    index_key.SetFromKey(key);

    {
      index_lock.ReadLock();

      // find the <key, location> pair
      auto entries = container.equal_range(index_key);
      for (auto entry = entries.first; entry != entries.second; ++entry) {
        retval.push_back(entry->second);
      }

      index_lock.Unlock();
    }

    return std::move(retval);
  }

  std::string GetTypeName() const { return "Btree"; }

 protected:
  MapType container;

  // equality checker and comparator
  KeyEqualityChecker equals;
  KeyComparator comparator;

  // synch helper
  RWLock index_lock;
};

}  // End index namespace
}  // End peloton namespace
