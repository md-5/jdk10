/*
 * Copyright (c) 1997, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "classfile/classLoaderDataGraph.inline.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "code/icBuffer.hpp"
#include "code/nmethod.hpp"
#include "code/pcDesc.hpp"
#include "code/scopeDesc.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcLocker.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/workgroup.hpp"
#include "interpreter/interpreter.hpp"
#include "jfr/jfrEvents.hpp"
#include "logging/log.hpp"
#include "logging/logStream.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "oops/symbol.hpp"
#include "runtime/atomic.hpp"
#include "runtime/compilationPolicy.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/osThread.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/safepointMechanism.inline.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/sweeper.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/thread.inline.hpp"
#include "runtime/threadSMR.hpp"
#include "runtime/timerTrace.hpp"
#include "services/runtimeService.hpp"
#include "utilities/events.hpp"
#include "utilities/macros.hpp"
#ifdef COMPILER1
#include "c1/c1_globals.hpp"
#endif

static void post_safepoint_begin_event(EventSafepointBegin& event,
                                       uint64_t safepoint_id,
                                       int thread_count,
                                       int critical_thread_count) {
  if (event.should_commit()) {
    event.set_safepointId(safepoint_id);
    event.set_totalThreadCount(thread_count);
    event.set_jniCriticalThreadCount(critical_thread_count);
    event.commit();
  }
}

static void post_safepoint_cleanup_event(EventSafepointCleanup& event, uint64_t safepoint_id) {
  if (event.should_commit()) {
    event.set_safepointId(safepoint_id);
    event.commit();
  }
}

static void post_safepoint_synchronize_event(EventSafepointStateSynchronization& event,
                                             uint64_t safepoint_id,
                                             int initial_number_of_threads,
                                             int threads_waiting_to_block,
                                             uint64_t iterations) {
  if (event.should_commit()) {
    event.set_safepointId(safepoint_id);
    event.set_initialThreadCount(initial_number_of_threads);
    event.set_runningThreadCount(threads_waiting_to_block);
    event.set_iterations(iterations);
    event.commit();
  }
}

static void post_safepoint_cleanup_task_event(EventSafepointCleanupTask& event,
                                              uint64_t safepoint_id,
                                              const char* name) {
  if (event.should_commit()) {
    event.set_safepointId(safepoint_id);
    event.set_name(name);
    event.commit();
  }
}

static void post_safepoint_end_event(EventSafepointEnd& event, uint64_t safepoint_id) {
  if (event.should_commit()) {
    event.set_safepointId(safepoint_id);
    event.commit();
  }
}

// --------------------------------------------------------------------------------------------------
// Implementation of Safepoint begin/end

SafepointSynchronize::SynchronizeState volatile SafepointSynchronize::_state = SafepointSynchronize::_not_synchronized;
int SafepointSynchronize::_waiting_to_block = 0;
volatile uint64_t SafepointSynchronize::_safepoint_counter = 0;
const uint64_t SafepointSynchronize::InactiveSafepointCounter = 0;
int SafepointSynchronize::_current_jni_active_count = 0;

WaitBarrier* SafepointSynchronize::_wait_barrier;

static volatile bool PageArmed = false;        // safepoint polling page is RO|RW vs PROT_NONE
static bool timeout_error_printed = false;

// Statistic related
static jlong _safepoint_begin_time = 0;
static volatile int _nof_threads_hit_polling_page = 0;

void SafepointSynchronize::init(Thread* vmthread) {
  // WaitBarrier should never be destroyed since we will have
  // threads waiting on it while exiting.
  _wait_barrier = new WaitBarrier(vmthread);
  SafepointTracing::init();
}

void SafepointSynchronize::increment_jni_active_count() {
  assert(Thread::current()->is_VM_thread(), "Only VM thread may increment");
  ++_current_jni_active_count;
}

void SafepointSynchronize::decrement_waiting_to_block() {
  assert(_waiting_to_block > 0, "sanity check");
  assert(Thread::current()->is_VM_thread(), "Only VM thread may decrement");
  --_waiting_to_block;
}

static bool thread_not_running(ThreadSafepointState *cur_state) {
  if (!cur_state->is_running()) {
    return true;
  }
  cur_state->examine_state_of_thread(SafepointSynchronize::safepoint_counter());
  if (!cur_state->is_running()) {
    return true;
  }
  LogTarget(Trace, safepoint) lt;
  if (lt.is_enabled()) {
    ResourceMark rm;
    LogStream ls(lt);
    cur_state->print_on(&ls);
  }
  return false;
}

#ifdef ASSERT
static void assert_list_is_valid(const ThreadSafepointState* tss_head, int still_running) {
  int a = 0;
  const ThreadSafepointState *tmp_tss = tss_head;
  while (tmp_tss != NULL) {
    ++a;
    assert(tmp_tss->is_running(), "Illegal initial state");
    tmp_tss = tmp_tss->get_next();
  }
  assert(a == still_running, "Must be the same");
}
#endif // ASSERT

static void back_off(int64_t start_time) {
  // We start with fine-grained nanosleeping until a millisecond has
  // passed, at which point we resort to plain naked_short_sleep.
  if (os::javaTimeNanos() - start_time < NANOSECS_PER_MILLISEC) {
    os::naked_short_nanosleep(10 * (NANOUNITS / MICROUNITS));
  } else {
    os::naked_short_sleep(1);
  }
}

int SafepointSynchronize::synchronize_threads(jlong safepoint_limit_time, int nof_threads, int* initial_running)
{
  JavaThreadIteratorWithHandle jtiwh;

#ifdef ASSERT
  for (; JavaThread *cur = jtiwh.next(); ) {
    assert(cur->safepoint_state()->is_running(), "Illegal initial state");
  }
  jtiwh.rewind();
#endif // ASSERT

  // Iterate through all threads until it has been determined how to stop them all at a safepoint.
  int still_running = nof_threads;
  ThreadSafepointState *tss_head = NULL;
  ThreadSafepointState **p_prev = &tss_head;
  for (; JavaThread *cur = jtiwh.next(); ) {
    ThreadSafepointState *cur_tss = cur->safepoint_state();
    assert(cur_tss->get_next() == NULL, "Must be NULL");
    if (thread_not_running(cur_tss)) {
      --still_running;
    } else {
      *p_prev = cur_tss;
      p_prev = cur_tss->next_ptr();
    }
  }
  *p_prev = NULL;

  DEBUG_ONLY(assert_list_is_valid(tss_head, still_running);)

  *initial_running = still_running;

  // If there is no thread still running, we are already done.
  if (still_running <= 0) {
    assert(tss_head == NULL, "Must be empty");
    return 1;
  }

  int iterations = 1; // The first iteration is above.
  int64_t start_time = os::javaTimeNanos();

  do {
    // Check if this has taken too long:
    if (SafepointTimeout && safepoint_limit_time < os::javaTimeNanos()) {
      print_safepoint_timeout();
    }
    if (int(iterations) == -1) { // overflow - something is wrong.
      // We can only overflow here when we are using global
      // polling pages. We keep this guarantee in its original
      // form so that searches of the bug database for this
      // failure mode find the right bugs.
      guarantee (!PageArmed, "invariant");
    }

    p_prev = &tss_head;
    ThreadSafepointState *cur_tss = tss_head;
    while (cur_tss != NULL) {
      assert(cur_tss->is_running(), "Illegal initial state");
      if (thread_not_running(cur_tss)) {
        --still_running;
        *p_prev = NULL;
        ThreadSafepointState *tmp = cur_tss;
        cur_tss = cur_tss->get_next();
        tmp->set_next(NULL);
      } else {
        *p_prev = cur_tss;
        p_prev = cur_tss->next_ptr();
        cur_tss = cur_tss->get_next();
      }
    }

    DEBUG_ONLY(assert_list_is_valid(tss_head, still_running);)

    if (still_running > 0) {
      back_off(start_time);
    }

    iterations++;
  } while (still_running > 0);

  assert(tss_head == NULL, "Must be empty");

  return iterations;
}

void SafepointSynchronize::arm_safepoint() {
  // Begin the process of bringing the system to a safepoint.
  // Java threads can be in several different states and are
  // stopped by different mechanisms:
  //
  //  1. Running interpreted
  //     When executing branching/returning byte codes interpreter
  //     checks if the poll is armed, if so blocks in SS::block().
  //     When using global polling the interpreter dispatch table
  //     is changed to force it to check for a safepoint condition
  //     between bytecodes.
  //  2. Running in native code
  //     When returning from the native code, a Java thread must check
  //     the safepoint _state to see if we must block.  If the
  //     VM thread sees a Java thread in native, it does
  //     not wait for this thread to block.  The order of the memory
  //     writes and reads of both the safepoint state and the Java
  //     threads state is critical.  In order to guarantee that the
  //     memory writes are serialized with respect to each other,
  //     the VM thread issues a memory barrier instruction.
  //  3. Running compiled Code
  //     Compiled code reads the local polling page that
  //     is set to fault if we are trying to get to a safepoint.
  //  4. Blocked
  //     A thread which is blocked will not be allowed to return from the
  //     block condition until the safepoint operation is complete.
  //  5. In VM or Transitioning between states
  //     If a Java thread is currently running in the VM or transitioning
  //     between states, the safepointing code will poll the thread state
  //     until the thread blocks itself when it attempts transitions to a
  //     new state or locking a safepoint checked monitor.

  // We must never miss a thread with correct safepoint id, so we must make sure we arm
  // the wait barrier for the next safepoint id/counter.
  // Arming must be done after resetting _current_jni_active_count, _waiting_to_block.
  _wait_barrier->arm(static_cast<int>(_safepoint_counter + 1));

  assert((_safepoint_counter & 0x1) == 0, "must be even");
  // The store to _safepoint_counter must happen after any stores in arming.
  OrderAccess::release_store(&_safepoint_counter, _safepoint_counter + 1);

  // We are synchronizing
  OrderAccess::storestore(); // Ordered with _safepoint_counter
  _state = _synchronizing;

  if (SafepointMechanism::uses_thread_local_poll()) {
    // Arming the per thread poll while having _state != _not_synchronized means safepointing
    log_trace(safepoint)("Setting thread local yield flag for threads");
    OrderAccess::storestore(); // storestore, global state -> local state
    for (JavaThreadIteratorWithHandle jtiwh; JavaThread *cur = jtiwh.next(); ) {
      // Make sure the threads start polling, it is time to yield.
      SafepointMechanism::arm_local_poll(cur);
    }
  }
  OrderAccess::fence(); // storestore|storeload, global state -> local state

  if (SafepointMechanism::uses_global_page_poll()) {
    // Make interpreter safepoint aware
    Interpreter::notice_safepoints();

    // Make polling safepoint aware
    guarantee (!PageArmed, "invariant") ;
    PageArmed = true;
    os::make_polling_page_unreadable();
  }
}

// Roll all threads forward to a safepoint and suspend them all
void SafepointSynchronize::begin() {
  assert(Thread::current()->is_VM_thread(), "Only VM thread may execute a safepoint");

  EventSafepointBegin begin_event;
  SafepointTracing::begin(VMThread::vm_op_type());

  Universe::heap()->safepoint_synchronize_begin();

  // By getting the Threads_lock, we assure that no threads are about to start or
  // exit. It is released again in SafepointSynchronize::end().
  Threads_lock->lock();

  assert( _state == _not_synchronized, "trying to safepoint synchronize with wrong state");

  int nof_threads = Threads::number_of_threads();

  _nof_threads_hit_polling_page = 0;

  log_debug(safepoint)("Safepoint synchronization initiated using %s wait barrier. (%d threads)", _wait_barrier->description(), nof_threads);

  // Reset the count of active JNI critical threads
  _current_jni_active_count = 0;

  // Set number of threads to wait for
  _waiting_to_block = nof_threads;

  jlong safepoint_limit_time = 0;
  if (SafepointTimeout) {
    // Set the limit time, so that it can be compared to see if this has taken
    // too long to complete.
    safepoint_limit_time = SafepointTracing::start_of_safepoint() + (jlong)SafepointTimeoutDelay * (NANOUNITS / MILLIUNITS);
    timeout_error_printed = false;
  }

  EventSafepointStateSynchronization sync_event;
  int initial_running = 0;

  // Arms the safepoint, _current_jni_active_count and _waiting_to_block must be set before.
  arm_safepoint();

  // Will spin until all threads are safe.
  int iterations = synchronize_threads(safepoint_limit_time, nof_threads, &initial_running);
  assert(_waiting_to_block == 0, "No thread should be running");

#ifndef PRODUCT
  if (safepoint_limit_time != 0) {
    jlong current_time = os::javaTimeNanos();
    if (safepoint_limit_time < current_time) {
      log_warning(safepoint)("# SafepointSynchronize: Finished after "
                    INT64_FORMAT_W(6) " ms",
                    (int64_t)(current_time - SafepointTracing::start_of_safepoint()) / (NANOUNITS / MILLIUNITS));
    }
  }
#endif

  assert(Threads_lock->owned_by_self(), "must hold Threads_lock");

  // Record state
  _state = _synchronized;

  OrderAccess::fence();

#ifdef ASSERT
  // Make sure all the threads were visited.
  for (JavaThreadIteratorWithHandle jtiwh; JavaThread *cur = jtiwh.next(); ) {
    assert(cur->was_visited_for_critical_count(_safepoint_counter), "missed a thread");
  }
#endif // ASSERT

  // Update the count of active JNI critical regions
  GCLocker::set_jni_lock_count(_current_jni_active_count);

  post_safepoint_synchronize_event(sync_event,
                                   _safepoint_counter,
                                   initial_running,
                                   _waiting_to_block, iterations);

  SafepointTracing::synchronized(nof_threads, initial_running, _nof_threads_hit_polling_page);

  // We do the safepoint cleanup first since a GC related safepoint
  // needs cleanup to be completed before running the GC op.
  EventSafepointCleanup cleanup_event;
  do_cleanup_tasks();
  post_safepoint_cleanup_event(cleanup_event, _safepoint_counter);

  post_safepoint_begin_event(begin_event, _safepoint_counter, nof_threads, _current_jni_active_count);
  SafepointTracing::cleanup();
}

void SafepointSynchronize::disarm_safepoint() {
  uint64_t safepoint_id = _safepoint_counter;
  {
    JavaThreadIteratorWithHandle jtiwh;
#ifdef ASSERT
    // A pending_exception cannot be installed during a safepoint.  The threads
    // may install an async exception after they come back from a safepoint into
    // pending_exception after they unblock.  But that should happen later.
    for (; JavaThread *cur = jtiwh.next(); ) {
      assert (!(cur->has_pending_exception() &&
                cur->safepoint_state()->is_at_poll_safepoint()),
              "safepoint installed a pending exception");
    }
#endif // ASSERT

    if (SafepointMechanism::uses_global_page_poll()) {
      guarantee (PageArmed, "invariant");
      // Make polling safepoint aware
      os::make_polling_page_readable();
      PageArmed = false;
      // Remove safepoint check from interpreter
      Interpreter::ignore_safepoints();
    }

    OrderAccess::fence(); // keep read and write of _state from floating up
    assert(_state == _synchronized, "must be synchronized before ending safepoint synchronization");

    // Change state first to _not_synchronized.
    // No threads should see _synchronized when running.
    _state = _not_synchronized;

    // Set the next dormant (even) safepoint id.
    assert((_safepoint_counter & 0x1) == 1, "must be odd");
    OrderAccess::release_store(&_safepoint_counter, _safepoint_counter + 1);

    OrderAccess::fence(); // Keep the local state from floating up.

    jtiwh.rewind();
    for (; JavaThread *current = jtiwh.next(); ) {
      // Clear the visited flag to ensure that the critical counts are collected properly.
      DEBUG_ONLY(current->reset_visited_for_critical_count(safepoint_id);)
      ThreadSafepointState* cur_state = current->safepoint_state();
      assert(!cur_state->is_running(), "Thread not suspended at safepoint");
      cur_state->restart(); // TSS _running
      assert(cur_state->is_running(), "safepoint state has not been reset");

      SafepointMechanism::disarm_if_needed(current, false /* NO release */);
    }
  } // ~JavaThreadIteratorWithHandle

  // Release threads lock, so threads can be created/destroyed again.
  Threads_lock->unlock();

  // Wake threads after local state is correctly set.
  _wait_barrier->disarm();
}

// Wake up all threads, so they are ready to resume execution after the safepoint
// operation has been carried out
void SafepointSynchronize::end() {
  assert(Threads_lock->owned_by_self(), "must hold Threads_lock");
  EventSafepointEnd event;
  uint64_t safepoint_id = _safepoint_counter;
  assert(Thread::current()->is_VM_thread(), "Only VM thread can execute a safepoint");

  disarm_safepoint();

  Universe::heap()->safepoint_synchronize_end();

  SafepointTracing::end();

  post_safepoint_end_event(event, safepoint_id);
}

bool SafepointSynchronize::is_cleanup_needed() {
  // Need a safepoint if there are many monitors to deflate.
  if (ObjectSynchronizer::is_cleanup_needed()) return true;
  // Need a safepoint if some inline cache buffers is non-empty
  if (!InlineCacheBuffer::is_empty()) return true;
  return false;
}

class ParallelSPCleanupThreadClosure : public ThreadClosure {
private:
  CodeBlobClosure* _nmethod_cl;
  DeflateMonitorCounters* _counters;

public:
  ParallelSPCleanupThreadClosure(DeflateMonitorCounters* counters) :
    _nmethod_cl(UseCodeAging ? NMethodSweeper::prepare_reset_hotness_counters() : NULL),
    _counters(counters) {}

  void do_thread(Thread* thread) {
    ObjectSynchronizer::deflate_thread_local_monitors(thread, _counters);
    if (_nmethod_cl != NULL && thread->is_Java_thread() &&
        ! thread->is_Code_cache_sweeper_thread()) {
      JavaThread* jt = (JavaThread*) thread;
      jt->nmethods_do(_nmethod_cl);
    }
  }
};

class ParallelSPCleanupTask : public AbstractGangTask {
private:
  SubTasksDone _subtasks;
  ParallelSPCleanupThreadClosure _cleanup_threads_cl;
  uint _num_workers;
  DeflateMonitorCounters* _counters;
public:
  ParallelSPCleanupTask(uint num_workers, DeflateMonitorCounters* counters) :
    AbstractGangTask("Parallel Safepoint Cleanup"),
    _subtasks(SubTasksDone(SafepointSynchronize::SAFEPOINT_CLEANUP_NUM_TASKS)),
    _cleanup_threads_cl(ParallelSPCleanupThreadClosure(counters)),
    _num_workers(num_workers),
    _counters(counters) {}

  void work(uint worker_id) {
    uint64_t safepoint_id = SafepointSynchronize::safepoint_counter();
    // All threads deflate monitors and mark nmethods (if necessary).
    Threads::possibly_parallel_threads_do(true, &_cleanup_threads_cl);

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_DEFLATE_MONITORS)) {
      const char* name = "deflating global idle monitors";
      EventSafepointCleanupTask event;
      TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
      ObjectSynchronizer::deflate_idle_monitors(_counters);

      post_safepoint_cleanup_task_event(event, safepoint_id, name);
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_UPDATE_INLINE_CACHES)) {
      const char* name = "updating inline caches";
      EventSafepointCleanupTask event;
      TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
      InlineCacheBuffer::update_inline_caches();

      post_safepoint_cleanup_task_event(event, safepoint_id, name);
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_COMPILATION_POLICY)) {
      const char* name = "compilation policy safepoint handler";
      EventSafepointCleanupTask event;
      TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
      CompilationPolicy::policy()->do_safepoint_work();

      post_safepoint_cleanup_task_event(event, safepoint_id, name);
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_SYMBOL_TABLE_REHASH)) {
      if (SymbolTable::needs_rehashing()) {
        const char* name = "rehashing symbol table";
        EventSafepointCleanupTask event;
        TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
        SymbolTable::rehash_table();

        post_safepoint_cleanup_task_event(event, safepoint_id, name);
      }
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_STRING_TABLE_REHASH)) {
      if (StringTable::needs_rehashing()) {
        const char* name = "rehashing string table";
        EventSafepointCleanupTask event;
        TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
        StringTable::rehash_table();

        post_safepoint_cleanup_task_event(event, safepoint_id, name);
      }
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_CLD_PURGE)) {
      // CMS delays purging the CLDG until the beginning of the next safepoint and to
      // make sure concurrent sweep is done
      const char* name = "purging class loader data graph";
      EventSafepointCleanupTask event;
      TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
      ClassLoaderDataGraph::purge_if_needed();

      post_safepoint_cleanup_task_event(event, safepoint_id, name);
    }

    if (_subtasks.try_claim_task(SafepointSynchronize::SAFEPOINT_CLEANUP_SYSTEM_DICTIONARY_RESIZE)) {
      const char* name = "resizing system dictionaries";
      EventSafepointCleanupTask event;
      TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
      ClassLoaderDataGraph::resize_if_needed();

      post_safepoint_cleanup_task_event(event, safepoint_id, name);
    }

    _subtasks.all_tasks_completed(_num_workers);
  }
};

// Various cleaning tasks that should be done periodically at safepoints.
void SafepointSynchronize::do_cleanup_tasks() {

  TraceTime timer("safepoint cleanup tasks", TRACETIME_LOG(Info, safepoint, cleanup));

  // Prepare for monitor deflation.
  DeflateMonitorCounters deflate_counters;
  ObjectSynchronizer::prepare_deflate_idle_monitors(&deflate_counters);

  CollectedHeap* heap = Universe::heap();
  assert(heap != NULL, "heap not initialized yet?");
  WorkGang* cleanup_workers = heap->get_safepoint_workers();
  if (cleanup_workers != NULL) {
    // Parallel cleanup using GC provided thread pool.
    uint num_cleanup_workers = cleanup_workers->active_workers();
    ParallelSPCleanupTask cleanup(num_cleanup_workers, &deflate_counters);
    StrongRootsScope srs(num_cleanup_workers);
    cleanup_workers->run_task(&cleanup);
  } else {
    // Serial cleanup using VMThread.
    ParallelSPCleanupTask cleanup(1, &deflate_counters);
    StrongRootsScope srs(1);
    cleanup.work(0);
  }

  // Needs to be done single threaded by the VMThread.  This walks
  // the thread stacks looking for references to metadata before
  // deciding to remove it from the metaspaces.
  if (ClassLoaderDataGraph::should_clean_metaspaces_and_reset()) {
    const char* name = "cleanup live ClassLoaderData metaspaces";
    TraceTime timer(name, TRACETIME_LOG(Info, safepoint, cleanup));
    ClassLoaderDataGraph::walk_metadata_and_clean_metaspaces();
  }

  // Finish monitor deflation.
  ObjectSynchronizer::finish_deflate_idle_monitors(&deflate_counters);

  assert(InlineCacheBuffer::is_empty(), "should have cleaned up ICBuffer");
}

// Methods for determining if a JavaThread is safepoint safe.

// False means unsafe with undetermined state.
// True means a determined state, but it may be an unsafe state.
// If called from a non-safepoint context safepoint_count MUST be InactiveSafepointCounter.
bool SafepointSynchronize::try_stable_load_state(JavaThreadState *state, JavaThread *thread, uint64_t safepoint_count) {
  assert((safepoint_count != InactiveSafepointCounter &&
          Thread::current() == (Thread*)VMThread::vm_thread() &&
          SafepointSynchronize::_state != _not_synchronized)
         || safepoint_count == InactiveSafepointCounter, "Invalid check");

  // To handle the thread_blocked state on the backedge of the WaitBarrier from
  // previous safepoint and reading the reset value (0/InactiveSafepointCounter) we
  // re-read state after we read thread safepoint id. The JavaThread changes its
  // thread state from thread_blocked before resetting safepoint id to 0.
  // This guarantees the second read will be from an updated thread state. It can
  // either be different state making this an unsafe state or it can see blocked
  // again. When we see blocked twice with a 0 safepoint id, either:
  // - It is normally blocked, e.g. on Mutex, TBIVM.
  // - It was in SS:block(), looped around to SS:block() and is blocked on the WaitBarrier.
  // - It was in SS:block() but now on a Mutex.
  // All of these cases are safe.

  *state = thread->thread_state();
  OrderAccess::loadload();
  uint64_t sid = thread->safepoint_state()->get_safepoint_id();  // Load acquire
  if (sid != InactiveSafepointCounter && sid != safepoint_count) {
    // In an old safepoint, state not relevant.
    return false;
  }
  return *state == thread->thread_state();
}

static bool safepoint_safe_with(JavaThread *thread, JavaThreadState state) {
  switch(state) {
  case _thread_in_native:
    // native threads are safe if they have no java stack or have walkable stack
    return !thread->has_last_Java_frame() || thread->frame_anchor()->walkable();

  case _thread_blocked:
    // On wait_barrier or blocked.
    // Blocked threads should already have walkable stack.
    assert(!thread->has_last_Java_frame() || thread->frame_anchor()->walkable(), "blocked and not walkable");
    return true;

  default:
    return false;
  }
}

bool SafepointSynchronize::handshake_safe(JavaThread *thread) {
  // This function must be called with the Threads_lock held so an externally
  // suspended thread cannot be resumed thus it is safe.
  assert(Threads_lock->owned_by_self() && Thread::current()->is_VM_thread(),
         "Must hold Threads_lock and be VMThread");
  if (thread->is_ext_suspended() || thread->is_terminated()) {
    return true;
  }
  JavaThreadState stable_state;
  if (try_stable_load_state(&stable_state, thread, InactiveSafepointCounter)) {
    return safepoint_safe_with(thread, stable_state);
  }
  return false;
}

// See if the thread is running inside a lazy critical native and
// update the thread critical count if so.  Also set a suspend flag to
// cause the native wrapper to return into the JVM to do the unlock
// once the native finishes.
static void check_for_lazy_critical_native(JavaThread *thread, JavaThreadState state) {
  if (state == _thread_in_native &&
      thread->has_last_Java_frame() &&
      thread->frame_anchor()->walkable()) {
    // This thread might be in a critical native nmethod so look at
    // the top of the stack and increment the critical count if it
    // is.
    frame wrapper_frame = thread->last_frame();
    CodeBlob* stub_cb = wrapper_frame.cb();
    if (stub_cb != NULL &&
        stub_cb->is_nmethod() &&
        stub_cb->as_nmethod_or_null()->is_lazy_critical_native()) {
      // A thread could potentially be in a critical native across
      // more than one safepoint, so only update the critical state on
      // the first one.  When it returns it will perform the unlock.
      if (!thread->do_critical_native_unlock()) {
#ifdef ASSERT
        if (!thread->in_critical()) {
          GCLocker::increment_debug_jni_lock_count();
        }
#endif
        thread->enter_critical();
        // Make sure the native wrapper calls back on return to
        // perform the needed critical unlock.
        thread->set_critical_native_unlock();
      }
    }
  }
}

// -------------------------------------------------------------------------------------------------------
// Implementation of Safepoint blocking point

void SafepointSynchronize::block(JavaThread *thread) {
  assert(thread != NULL, "thread must be set");
  assert(thread->is_Java_thread(), "not a Java thread");

  // Threads shouldn't block if they are in the middle of printing, but...
  ttyLocker::break_tty_lock_for_safepoint(os::current_thread_id());

  // Only bail from the block() call if the thread is gone from the
  // thread list; starting to exit should still block.
  if (thread->is_terminated()) {
     // block current thread if we come here from native code when VM is gone
     thread->block_if_vm_exited();

     // otherwise do nothing
     return;
  }

  JavaThreadState state = thread->thread_state();
  thread->frame_anchor()->make_walkable(thread);

  uint64_t safepoint_id = SafepointSynchronize::safepoint_counter();
  // Check that we have a valid thread_state at this point
  switch(state) {
    case _thread_in_vm_trans:
    case _thread_in_Java:        // From compiled code
    case _thread_in_native_trans:
    case _thread_blocked_trans:
    case _thread_new_trans:

      // We have no idea where the VMThread is, it might even be at next safepoint.
      // So we can miss this poll, but stop at next.

      // Load dependent store, it must not pass loading of safepoint_id.
      thread->safepoint_state()->set_safepoint_id(safepoint_id); // Release store

      // This part we can skip if we notice we miss or are in a future safepoint.
      OrderAccess::storestore();
      // Load in wait barrier should not float up
      thread->set_thread_state_fence(_thread_blocked);

      _wait_barrier->wait(static_cast<int>(safepoint_id));
      assert(_state != _synchronized, "Can't be");

      // If barrier is disarmed stop store from floating above loads in barrier.
      OrderAccess::loadstore();
      thread->set_thread_state(state);

      // Then we reset the safepoint id to inactive.
      thread->safepoint_state()->reset_safepoint_id(); // Release store

      OrderAccess::fence();

      break;

    default:
     fatal("Illegal threadstate encountered: %d", state);
  }
  guarantee(thread->safepoint_state()->get_safepoint_id() == InactiveSafepointCounter,
            "The safepoint id should be set only in block path");

  // Check for pending. async. exceptions or suspends - except if the
  // thread was blocked inside the VM. has_special_runtime_exit_condition()
  // is called last since it grabs a lock and we only want to do that when
  // we must.
  //
  // Note: we never deliver an async exception at a polling point as the
  // compiler may not have an exception handler for it. The polling
  // code will notice the async and deoptimize and the exception will
  // be delivered. (Polling at a return point is ok though). Sure is
  // a lot of bother for a deprecated feature...
  //
  // We don't deliver an async exception if the thread state is
  // _thread_in_native_trans so JNI functions won't be called with
  // a surprising pending exception. If the thread state is going back to java,
  // async exception is checked in check_special_condition_for_native_trans().

  if (state != _thread_blocked_trans &&
      state != _thread_in_vm_trans &&
      thread->has_special_runtime_exit_condition()) {
    thread->handle_special_runtime_exit_condition(
      !thread->is_at_poll_safepoint() && (state != _thread_in_native_trans));
  }

  // cross_modify_fence is done by SafepointMechanism::block_if_requested_slow
  // which is the only caller here.
}

// ------------------------------------------------------------------------------------------------------
// Exception handlers


void SafepointSynchronize::handle_polling_page_exception(JavaThread *thread) {
  assert(thread->is_Java_thread(), "polling reference encountered by VM thread");
  assert(thread->thread_state() == _thread_in_Java, "should come from Java code");
  if (!ThreadLocalHandshakes) {
    assert(SafepointSynchronize::is_synchronizing(), "polling encountered outside safepoint synchronization");
  }

  if (log_is_enabled(Info, safepoint, stats)) {
    Atomic::inc(&_nof_threads_hit_polling_page);
  }

  ThreadSafepointState* state = thread->safepoint_state();

  state->handle_polling_page_exception();
}


void SafepointSynchronize::print_safepoint_timeout() {
  if (!timeout_error_printed) {
    timeout_error_printed = true;
    // Print out the thread info which didn't reach the safepoint for debugging
    // purposes (useful when there are lots of threads in the debugger).
    LogTarget(Warning, safepoint) lt;
    if (lt.is_enabled()) {
      ResourceMark rm;
      LogStream ls(lt);

      ls.cr();
      ls.print_cr("# SafepointSynchronize::begin: Timeout detected:");
      ls.print_cr("# SafepointSynchronize::begin: Timed out while spinning to reach a safepoint.");
      ls.print_cr("# SafepointSynchronize::begin: Threads which did not reach the safepoint:");
      for (JavaThreadIteratorWithHandle jtiwh; JavaThread *cur_thread = jtiwh.next(); ) {
        if (cur_thread->safepoint_state()->is_running()) {
          ls.print("# ");
          cur_thread->print_on(&ls);
          ls.cr();
        }
      }
      ls.print_cr("# SafepointSynchronize::begin: (End of list)");
    }
  }

  // To debug the long safepoint, specify both AbortVMOnSafepointTimeout &
  // ShowMessageBoxOnError.
  if (AbortVMOnSafepointTimeout) {
    // Send the blocking thread a signal to terminate and write an error file.
    for (JavaThreadIteratorWithHandle jtiwh; JavaThread *cur_thread = jtiwh.next(); ) {
      if (cur_thread->safepoint_state()->is_running()) {
        if (!os::signal_thread(cur_thread, SIGILL, "blocking a safepoint")) {
          break; // Could not send signal. Report fatal error.
        }
        // Give cur_thread a chance to report the error and terminate the VM.
        os::sleep(Thread::current(), 3000, false);
      }
    }
    fatal("Safepoint sync time longer than " INTX_FORMAT "ms detected when executing %s.",
          SafepointTimeoutDelay, VMThread::vm_operation()->name());
  }
}

// -------------------------------------------------------------------------------------------------------
// Implementation of ThreadSafepointState

ThreadSafepointState::ThreadSafepointState(JavaThread *thread)
  : _at_poll_safepoint(false), _thread(thread), _safepoint_safe(false),
    _safepoint_id(SafepointSynchronize::InactiveSafepointCounter),
    _orig_thread_state(_thread_uninitialized), _next(NULL) {
}

void ThreadSafepointState::create(JavaThread *thread) {
  ThreadSafepointState *state = new ThreadSafepointState(thread);
  thread->set_safepoint_state(state);
}

void ThreadSafepointState::destroy(JavaThread *thread) {
  if (thread->safepoint_state()) {
    delete(thread->safepoint_state());
    thread->set_safepoint_state(NULL);
  }
}

uint64_t ThreadSafepointState::get_safepoint_id() const {
  return OrderAccess::load_acquire(&_safepoint_id);
}

void ThreadSafepointState::reset_safepoint_id() {
  OrderAccess::release_store(&_safepoint_id, SafepointSynchronize::InactiveSafepointCounter);
}

void ThreadSafepointState::set_safepoint_id(uint64_t safepoint_id) {
  OrderAccess::release_store(&_safepoint_id, safepoint_id);
}

void ThreadSafepointState::examine_state_of_thread(uint64_t safepoint_count) {
  assert(is_running(), "better be running or just have hit safepoint poll");

  JavaThreadState stable_state;
  if (!SafepointSynchronize::try_stable_load_state(&stable_state, _thread, safepoint_count)) {
    // We could not get stable state of the JavaThread.
    // Consider it running and just return.
    return;
  }

  // Save the state at the start of safepoint processing.
  _orig_thread_state = stable_state;

  // Check for a thread that is suspended. Note that thread resume tries
  // to grab the Threads_lock which we own here, so a thread cannot be
  // resumed during safepoint synchronization.

  // We check to see if this thread is suspended without locking to
  // avoid deadlocking with a third thread that is waiting for this
  // thread to be suspended. The third thread can notice the safepoint
  // that we're trying to start at the beginning of its SR_lock->wait()
  // call. If that happens, then the third thread will block on the
  // safepoint while still holding the underlying SR_lock. We won't be
  // able to get the SR_lock and we'll deadlock.
  //
  // We don't need to grab the SR_lock here for two reasons:
  // 1) The suspend flags are both volatile and are set with an
  //    Atomic::cmpxchg() call so we should see the suspended
  //    state right away.
  // 2) We're being called from the safepoint polling loop; if
  //    we don't see the suspended state on this iteration, then
  //    we'll come around again.
  //
  bool is_suspended = _thread->is_ext_suspended();
  if (is_suspended) {
    account_safe_thread();
    return;
  }

  if (safepoint_safe_with(_thread, stable_state)) {
    check_for_lazy_critical_native(_thread, stable_state);
    account_safe_thread();
    return;
  }

  // All other thread states will continue to run until they
  // transition and self-block in state _blocked
  // Safepoint polling in compiled code causes the Java threads to do the same.
  // Note: new threads may require a malloc so they must be allowed to finish

  assert(is_running(), "examine_state_of_thread on non-running thread");
  return;
}

void ThreadSafepointState::account_safe_thread() {
  SafepointSynchronize::decrement_waiting_to_block();
  if (_thread->in_critical()) {
    // Notice that this thread is in a critical section
    SafepointSynchronize::increment_jni_active_count();
  }
  DEBUG_ONLY(_thread->set_visited_for_critical_count(SafepointSynchronize::safepoint_counter());)
  assert(!_safepoint_safe, "Must be unsafe before safe");
  _safepoint_safe = true;
}

void ThreadSafepointState::restart() {
  assert(_safepoint_safe, "Must be safe before unsafe");
  _safepoint_safe = false;
}

void ThreadSafepointState::print_on(outputStream *st) const {
  const char *s = _safepoint_safe ? "_at_safepoint" : "_running";

  st->print_cr("Thread: " INTPTR_FORMAT
              "  [0x%2x] State: %s _at_poll_safepoint %d",
               p2i(_thread), _thread->osthread()->thread_id(), s, _at_poll_safepoint);

  _thread->print_thread_state_on(st);
}

void ThreadSafepointState::print() const { print_on(tty); }

// ---------------------------------------------------------------------------------------------------------------------

// Block the thread at poll or poll return for safepoint/handshake.
void ThreadSafepointState::handle_polling_page_exception() {

  // If we're using a global poll, then the thread should not be
  // marked as safepoint safe yet.
  assert(!SafepointMechanism::uses_global_page_poll() || !_safepoint_safe,
         "polling page exception on thread safepoint safe");

  // Step 1: Find the nmethod from the return address
  address real_return_addr = thread()->saved_exception_pc();

  CodeBlob *cb = CodeCache::find_blob(real_return_addr);
  assert(cb != NULL && cb->is_compiled(), "return address should be in nmethod");
  CompiledMethod* nm = (CompiledMethod*)cb;

  // Find frame of caller
  frame stub_fr = thread()->last_frame();
  CodeBlob* stub_cb = stub_fr.cb();
  assert(stub_cb->is_safepoint_stub(), "must be a safepoint stub");
  RegisterMap map(thread(), true);
  frame caller_fr = stub_fr.sender(&map);

  // Should only be poll_return or poll
  assert( nm->is_at_poll_or_poll_return(real_return_addr), "should not be at call" );

  // This is a poll immediately before a return. The exception handling code
  // has already had the effect of causing the return to occur, so the execution
  // will continue immediately after the call. In addition, the oopmap at the
  // return point does not mark the return value as an oop (if it is), so
  // it needs a handle here to be updated.
  if( nm->is_at_poll_return(real_return_addr) ) {
    // See if return type is an oop.
    bool return_oop = nm->method()->is_returning_oop();
    Handle return_value;
    if (return_oop) {
      // The oop result has been saved on the stack together with all
      // the other registers. In order to preserve it over GCs we need
      // to keep it in a handle.
      oop result = caller_fr.saved_oop_result(&map);
      assert(oopDesc::is_oop_or_null(result), "must be oop");
      return_value = Handle(thread(), result);
      assert(Universe::heap()->is_in_or_null(result), "must be heap pointer");
    }

    // Block the thread
    SafepointMechanism::block_if_requested(thread());

    // restore oop result, if any
    if (return_oop) {
      caller_fr.set_saved_oop_result(&map, return_value());
    }
  }

  // This is a safepoint poll. Verify the return address and block.
  else {
    set_at_poll_safepoint(true);

    // verify the blob built the "return address" correctly
    assert(real_return_addr == caller_fr.pc(), "must match");

    // Block the thread
    SafepointMechanism::block_if_requested(thread());
    set_at_poll_safepoint(false);

    // If we have a pending async exception deoptimize the frame
    // as otherwise we may never deliver it.
    if (thread()->has_async_condition()) {
      ThreadInVMfromJavaNoAsyncException __tiv(thread());
      Deoptimization::deoptimize_frame(thread(), caller_fr.id());
    }

    // If an exception has been installed we must check for a pending deoptimization
    // Deoptimize frame if exception has been thrown.

    if (thread()->has_pending_exception() ) {
      RegisterMap map(thread(), true);
      frame caller_fr = stub_fr.sender(&map);
      if (caller_fr.is_deoptimized_frame()) {
        // The exception patch will destroy registers that are still
        // live and will be needed during deoptimization. Defer the
        // Async exception should have deferred the exception until the
        // next safepoint which will be detected when we get into
        // the interpreter so if we have an exception now things
        // are messed up.

        fatal("Exception installed and deoptimization is pending");
      }
    }
  }
}


// -------------------------------------------------------------------------------------------------------
// Implementation of SafepointTracing

jlong SafepointTracing::_last_safepoint_begin_time_ns = 0;
jlong SafepointTracing::_last_safepoint_sync_time_ns = 0;
jlong SafepointTracing::_last_safepoint_cleanup_time_ns = 0;
jlong SafepointTracing::_last_safepoint_end_time_ns = 0;
jlong SafepointTracing::_last_safepoint_end_time_epoch_ms = 0;
jlong SafepointTracing::_last_app_time_ns = 0;
int SafepointTracing::_nof_threads = 0;
int SafepointTracing::_nof_running = 0;
int SafepointTracing::_page_trap = 0;
VM_Operation::VMOp_Type SafepointTracing::_current_type;
jlong     SafepointTracing::_max_sync_time = 0;
jlong     SafepointTracing::_max_vmop_time = 0;
uint64_t  SafepointTracing::_op_count[VM_Operation::VMOp_Terminating] = {0};

void SafepointTracing::init() {
  // Application start
  _last_safepoint_end_time_ns = os::javaTimeNanos();
  // amount of time since epoch
  _last_safepoint_end_time_epoch_ms = os::javaTimeMillis();
}

// Helper method to print the header.
static void print_header(outputStream* st) {
  // The number of spaces is significant here, and should match the format
  // specifiers in print_statistics().

  st->print("VM Operation                 "
            "[ threads: total initial_running ]"
            "[ time:       sync    cleanup       vmop      total ]");

  st->print_cr(" page_trap_count");
}

// This prints a nice table.  To get the statistics to not shift due to the logging uptime
// decorator, use the option as: -Xlog:safepoint+stats:[outputfile]:none
void SafepointTracing::statistics_log() {
  LogTarget(Info, safepoint, stats) lt;
  assert (lt.is_enabled(), "should only be called when printing statistics is enabled");
  LogStream ls(lt);

  static int _cur_stat_index = 0;

  // Print header every 30 entries
  if ((_cur_stat_index % 30) == 0) {
    print_header(&ls);
    _cur_stat_index = 1;  // wrap
  } else {
    _cur_stat_index++;
  }

  ls.print("%-28s [       "
           INT32_FORMAT_W(8) "        " INT32_FORMAT_W(8) " "
           "]",
           VM_Operation::name(_current_type),
           _nof_threads,
           _nof_running);
  ls.print("[       "
           INT64_FORMAT_W(10) " " INT64_FORMAT_W(10) " "
           INT64_FORMAT_W(10) " " INT64_FORMAT_W(10) " ]",
           (int64_t)(_last_safepoint_sync_time_ns - _last_safepoint_begin_time_ns),
           (int64_t)(_last_safepoint_cleanup_time_ns - _last_safepoint_sync_time_ns),
           (int64_t)(_last_safepoint_end_time_ns - _last_safepoint_cleanup_time_ns),
           (int64_t)(_last_safepoint_end_time_ns - _last_safepoint_begin_time_ns));

  ls.print_cr(INT32_FORMAT_W(16), _page_trap);
}

// This method will be called when VM exits. This tries to summarize the sampling.
// Current thread may already be deleted, so don't use ResourceMark.
void SafepointTracing::statistics_exit_log() {
  if (!log_is_enabled(Info, safepoint, stats)) {
    return;
  }
  for (int index = 0; index < VM_Operation::VMOp_Terminating; index++) {
    if (_op_count[index] != 0) {
      log_info(safepoint, stats)("%-28s" UINT64_FORMAT_W(10), VM_Operation::name(index),
               _op_count[index]);
    }
  }

  log_info(safepoint, stats)("VM operations coalesced during safepoint " INT64_FORMAT,
                              VMThread::get_coalesced_count());
  log_info(safepoint, stats)("Maximum sync time  " INT64_FORMAT" ns",
                              (int64_t)(_max_sync_time));
  log_info(safepoint, stats)("Maximum vm operation time (except for Exit VM operation)  "
                              INT64_FORMAT " ns",
                              (int64_t)(_max_vmop_time));
}

void SafepointTracing::begin(VM_Operation::VMOp_Type type) {
  _op_count[type]++;
  _current_type = type;

  // update the time stamp to begin recording safepoint time
  _last_safepoint_begin_time_ns = os::javaTimeNanos();
  _last_safepoint_sync_time_ns = 0;
  _last_safepoint_cleanup_time_ns = 0;

  _last_app_time_ns = _last_safepoint_begin_time_ns - _last_safepoint_end_time_ns;
  _last_safepoint_end_time_ns = 0;

  RuntimeService::record_safepoint_begin(_last_app_time_ns);
}

void SafepointTracing::synchronized(int nof_threads, int nof_running, int traps) {
  _last_safepoint_sync_time_ns = os::javaTimeNanos();
  _nof_threads = nof_threads;
  _nof_running = nof_running;
  _page_trap   = traps;
  RuntimeService::record_safepoint_synchronized(_last_safepoint_sync_time_ns - _last_safepoint_begin_time_ns);
}

void SafepointTracing::cleanup() {
  _last_safepoint_cleanup_time_ns = os::javaTimeNanos();
}

void SafepointTracing::end() {
  _last_safepoint_end_time_ns = os::javaTimeNanos();
  // amount of time since epoch
  _last_safepoint_end_time_epoch_ms = os::javaTimeMillis();

  if (_max_sync_time < (_last_safepoint_sync_time_ns - _last_safepoint_begin_time_ns)) {
    _max_sync_time = _last_safepoint_sync_time_ns - _last_safepoint_begin_time_ns;
  }
  if (_max_vmop_time < (_last_safepoint_end_time_ns - _last_safepoint_sync_time_ns)) {
    _max_vmop_time = _last_safepoint_end_time_ns - _last_safepoint_sync_time_ns;
  }
  if (log_is_enabled(Info, safepoint, stats)) {
    statistics_log();
  }

  log_info(safepoint)(
     "Safepoint \"%s\", "
     "Time since last: " JLONG_FORMAT " ns, "
     "Reaching safepoint: " JLONG_FORMAT " ns, "
     "At safepoint: " JLONG_FORMAT " ns, "
     "Total: " JLONG_FORMAT " ns",
      VM_Operation::name(_current_type),
      _last_app_time_ns,
      _last_safepoint_cleanup_time_ns - _last_safepoint_begin_time_ns,
      _last_safepoint_end_time_ns     - _last_safepoint_cleanup_time_ns,
      _last_safepoint_end_time_ns     - _last_safepoint_begin_time_ns
     );

  RuntimeService::record_safepoint_end(_last_safepoint_end_time_ns - _last_safepoint_cleanup_time_ns);
}
