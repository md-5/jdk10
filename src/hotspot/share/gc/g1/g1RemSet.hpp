/*
 * Copyright (c) 2001, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1REMSET_HPP
#define SHARE_GC_G1_G1REMSET_HPP

#include "gc/g1/g1CardTable.hpp"
#include "gc/g1/g1OopClosures.hpp"
#include "gc/g1/g1GCPhaseTimes.hpp"
#include "gc/g1/g1RemSetSummary.hpp"
#include "gc/g1/heapRegion.hpp"
#include "memory/allocation.hpp"
#include "memory/iterator.hpp"
#include "utilities/ticks.hpp"

// A G1RemSet provides ways of iterating over pointers into a selected
// collection set.

class BitMap;
class CardTableBarrierSet;
class G1BlockOffsetTable;
class CodeBlobClosure;
class G1CollectedHeap;
class G1CMBitMap;
class G1HotCardCache;
class G1RemSetScanState;
class G1ParScanThreadState;
class G1ParScanThreadStateSet;
class G1Policy;
class G1ScanCardClosure;
class HeapRegionClaimer;

// A G1RemSet in which each heap region has a rem set that records the
// external heap references into it.  Uses a mod ref bs to track updates,
// so that they can be used to update the individual region remsets.
class G1RemSet: public CHeapObj<mtGC> {
private:
  G1RemSetScanState* _scan_state;

  G1RemSetSummary _prev_period_summary;

  G1CollectedHeap* _g1h;

  G1CardTable*           _ct;
  G1Policy*              _g1p;
  G1HotCardCache*        _hot_card_cache;

  void print_merge_heap_roots_stats();
public:

  typedef CardTable::CardValue CardValue;
  // Gives an approximation on how many threads can be expected to add records to
  // a remembered set in parallel. This can be used for sizing data structures to
  // decrease performance losses due to data structure sharing.
  // Examples for quantities that influence this value are the maximum number of
  // mutator threads, maximum number of concurrent refinement or GC threads.
  static uint num_par_rem_sets();

  // Initialize data that depends on the heap size being known.
  void initialize(size_t capacity, uint max_regions);

  G1RemSet(G1CollectedHeap* g1h,
           G1CardTable* ct,
           G1HotCardCache* hot_card_cache);
  ~G1RemSet();

  // Scan all cards in the non-collection set regions that potentially contain
  // references into the current whole collection set.
  void scan_heap_roots(G1ParScanThreadState* pss,
                       uint worker_id,
                       G1GCPhaseTimes::GCParPhases scan_phase,
                       G1GCPhaseTimes::GCParPhases objcopy_phase);

  // Merge cards from various sources (remembered sets, hot card cache, log buffers)
  // and calculate the cards that need to be scanned later (via scan_heap_roots()).
  // If initial_evacuation is set, this is called during the initial evacuation.
  void merge_heap_roots(bool initial_evacuation);

  // Prepare for and cleanup after scanning the heap roots. Must be called
  // once before and after in sequential code.
  void prepare_for_scan_heap_roots();
  // Cleans the card table from temporary duplicate detection information.
  void cleanup_after_scan_heap_roots();
  // Prepares the given region for heap root scanning.
  void prepare_for_scan_heap_roots(uint region_idx);

  // Do work for regions in the current increment of the collection set, scanning
  // non-card based (heap) roots.
  void scan_collection_set_regions(G1ParScanThreadState* pss,
                                   uint worker_id,
                                   G1GCPhaseTimes::GCParPhases scan_phase,
                                   G1GCPhaseTimes::GCParPhases coderoots_phase,
                                   G1GCPhaseTimes::GCParPhases objcopy_phase);

  // Two methods for concurrent refinement support, executed concurrently to
  // the mutator:
  // Cleans the card at "*card_ptr_addr" before refinement, returns true iff the
  // card needs later refinement. Note that "*card_ptr_addr" could be updated to
  // a different card due to use of hot card cache.
  bool clean_card_before_refine(CardValue** const card_ptr_addr);
  // Refine the region corresponding to "card_ptr". Must be called after
  // being filtered by clean_card_before_refine(), and after proper
  // fence/synchronization.
  void refine_card_concurrently(CardValue* const card_ptr,
                                const uint worker_id);

  // Print accumulated summary info from the start of the VM.
  void print_summary_info();

  // Print accumulated summary info from the last time called.
  void print_periodic_summary_info(const char* header, uint period_count);

  // Rebuilds the remembered set by scanning from bottom to TARS for all regions
  // using the given work gang.
  void rebuild_rem_set(G1ConcurrentMark* cm, WorkGang* workers, uint worker_id_offset);
};

#endif // SHARE_GC_G1_G1REMSET_HPP
