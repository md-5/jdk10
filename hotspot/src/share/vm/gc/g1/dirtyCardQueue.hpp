/*
 * Copyright (c) 2001, 2015, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_G1_DIRTYCARDQUEUE_HPP
#define SHARE_VM_GC_G1_DIRTYCARDQUEUE_HPP

#include "gc/g1/ptrQueue.hpp"
#include "memory/allocation.hpp"

class FreeIdSet;
class DirtyCardQueueSet;

// A closure class for processing card table entries.  Note that we don't
// require these closure objects to be stack-allocated.
class CardTableEntryClosure: public CHeapObj<mtGC> {
public:
  // Process the card whose card table entry is "card_ptr".  If returns
  // "false", terminate the iteration early.
  virtual bool do_card_ptr(jbyte* card_ptr, uint worker_i = 0) = 0;
};

// A ptrQueue whose elements are "oops", pointers to object heads.
class DirtyCardQueue: public PtrQueue {
public:
  DirtyCardQueue(DirtyCardQueueSet* qset, bool permanent = false);

  // Flush before destroying; queue may be used to capture pending work while
  // doing something else, with auto-flush on completion.
  ~DirtyCardQueue();

  // Process queue entries and release resources.
  void flush() { flush_impl(); }

  // Apply the closure to all elements, and reset the index to make the
  // buffer empty.  If a closure application returns "false", return
  // "false" immediately, halting the iteration.  If "consume" is true,
  // deletes processed entries from logs.
  bool apply_closure(CardTableEntryClosure* cl,
                     bool consume = true,
                     uint worker_i = 0);

  // Apply the closure to all elements of "buf", down to "index"
  // (inclusive.)  If returns "false", then a closure application returned
  // "false", and we return immediately.  If "consume" is true, entries are
  // set to NULL as they are processed, so they will not be processed again
  // later.
  static bool apply_closure_to_buffer(CardTableEntryClosure* cl,
                                      void** buf, size_t index, size_t sz,
                                      bool consume = true,
                                      uint worker_i = 0);
  void **get_buf() { return _buf;}
  size_t get_index() { return _index;}
  void reinitialize() { _buf = 0; _sz = 0; _index = 0;}

  // Compiler support.
  static ByteSize byte_offset_of_index() {
    return PtrQueue::byte_offset_of_index<DirtyCardQueue>();
  }
  using PtrQueue::byte_width_of_index;

  static ByteSize byte_offset_of_buf() {
    return PtrQueue::byte_offset_of_buf<DirtyCardQueue>();
  }
  using PtrQueue::byte_width_of_buf;

};



class DirtyCardQueueSet: public PtrQueueSet {
  // The closure used in mut_process_buffer().
  CardTableEntryClosure* _mut_process_closure;

  DirtyCardQueue _shared_dirty_card_queue;

  // Override.
  bool mut_process_buffer(void** buf);

  // Protected by the _cbl_mon.
  FreeIdSet* _free_ids;

  // The number of completed buffers processed by mutator and rs thread,
  // respectively.
  jint _processed_buffers_mut;
  jint _processed_buffers_rs_thread;

  // Current buffer node used for parallel iteration.
  BufferNode* volatile _cur_par_buffer_node;
public:
  DirtyCardQueueSet(bool notify_when_complete = true);

  void initialize(CardTableEntryClosure* cl,
                  Monitor* cbl_mon,
                  Mutex* fl_lock,
                  int process_completed_threshold,
                  int max_completed_queue,
                  Mutex* lock,
                  DirtyCardQueueSet* fl_owner,
                  bool init_free_ids = false);

  // The number of parallel ids that can be claimed to allow collector or
  // mutator threads to do card-processing work.
  static uint num_par_ids();

  static void handle_zero_index_for_thread(JavaThread* t);

  // If there exists some completed buffer, pop it, then apply the
  // specified closure to all its elements, nulling out those elements
  // processed.  If all elements are processed, returns "true".  If no
  // completed buffers exist, returns false.  If a completed buffer exists,
  // but is only partially completed before a "yield" happens, the
  // partially completed buffer (with its processed elements set to NULL)
  // is returned to the completed buffer set, and this call returns false.
  bool apply_closure_to_completed_buffer(CardTableEntryClosure* cl,
                                         uint worker_i = 0,
                                         int stop_at = 0,
                                         bool during_pause = false);

  // Helper routine for the above.
  bool apply_closure_to_completed_buffer_helper(CardTableEntryClosure* cl,
                                                uint worker_i,
                                                BufferNode* nd);

  BufferNode* get_completed_buffer(int stop_at);

  // Applies the current closure to all completed buffers,
  // non-consumptively.
  void apply_closure_to_all_completed_buffers(CardTableEntryClosure* cl);

  void reset_for_par_iteration() { _cur_par_buffer_node = _completed_buffers_head; }
  // Applies the current closure to all completed buffers, non-consumptively.
  // Parallel version.
  void par_apply_closure_to_all_completed_buffers(CardTableEntryClosure* cl);

  DirtyCardQueue* shared_dirty_card_queue() {
    return &_shared_dirty_card_queue;
  }

  // Deallocate any completed log buffers
  void clear();

  // If a full collection is happening, reset partial logs, and ignore
  // completed ones: the full collection will make them all irrelevant.
  void abandon_logs();

  // If any threads have partial logs, add them to the global list of logs.
  void concatenate_logs();
  void clear_n_completed_buffers() { _n_completed_buffers = 0;}

  jint processed_buffers_mut() {
    return _processed_buffers_mut;
  }
  jint processed_buffers_rs_thread() {
    return _processed_buffers_rs_thread;
  }

};

#endif // SHARE_VM_GC_G1_DIRTYCARDQUEUE_HPP
