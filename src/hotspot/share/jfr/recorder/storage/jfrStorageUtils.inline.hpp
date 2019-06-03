/*
 * Copyright (c) 2016, 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_JFR_RECORDER_STORAGE_JFRSTORAGEUTILS_INLINE_HPP
#define SHARE_JFR_RECORDER_STORAGE_JFRSTORAGEUTILS_INLINE_HPP

#include "jfr/recorder/storage/jfrStorageUtils.hpp"
#include "runtime/thread.inline.hpp"

template <typename T>
inline bool UnBufferedWriteToChunk<T>::write(T* t, const u1* data, size_t size) {
  _writer.write_unbuffered(data, size);
  _processed += size;
  return true;
}

template <typename T>
inline bool DefaultDiscarder<T>::discard(T* t, const u1* data, size_t size) {
  _processed += size;
  return true;
}

template <typename Operation>
inline bool ConcurrentWriteOp<Operation>::process(typename Operation::Type* t) {
  const u1* const current_top = t->concurrent_top();
  const size_t unflushed_size = t->pos() - current_top;
  if (unflushed_size == 0) {
    t->set_concurrent_top(current_top);
    return true;
  }
  const bool result = _operation.write(t, current_top, unflushed_size);
  t->set_concurrent_top(current_top + unflushed_size);
  return result;
}

template <typename Operation>
inline bool ConcurrentWriteOpExcludeRetired<Operation>::process(typename Operation::Type* t) {
  if (t->retired()) {
    assert(t->empty(), "invariant");
    return true;
  }
  return ConcurrentWriteOp<Operation>::process(t);
}

template <typename Operation>
inline bool MutexedWriteOp<Operation>::process(typename Operation::Type* t) {
  assert(t != NULL, "invariant");
  const u1* const current_top = t->top();
  const size_t unflushed_size = t->pos() - current_top;
  if (unflushed_size == 0) {
    return true;
  }
  const bool result = _operation.write(t, current_top, unflushed_size);
  t->set_top(current_top + unflushed_size);
  return result;
}

template <typename Type>
static void retired_sensitive_acquire(Type* t) {
  assert(t != NULL, "invariant");
  if (t->retired()) {
    return;
  }
  Thread* const thread = Thread::current();
  while (!t->try_acquire(thread)) {
    if (t->retired()) {
      return;
    }
  }
}

template <typename Operation>
inline bool ExclusiveOp<Operation>::process(typename Operation::Type* t) {
  retired_sensitive_acquire(t);
  assert(t->acquired_by_self() || t->retired(), "invariant");
  // User is required to ensure proper release of the acquisition
  return MutexedWriteOp<Operation>::process(t);
}

template <typename Operation>
inline bool DiscardOp<Operation>::process(typename Operation::Type* t) {
  assert(t != NULL, "invariant");
  const u1* const current_top = _mode == concurrent ? t->concurrent_top() : t->top();
  const size_t unflushed_size = t->pos() - current_top;
  if (unflushed_size == 0) {
    if (_mode == concurrent) {
      t->set_concurrent_top(current_top);
    }
    return true;
  }
  const bool result = _operation.discard(t, current_top, unflushed_size);
  if (_mode == concurrent) {
    t->set_concurrent_top(current_top + unflushed_size);
  } else {
    t->set_top(current_top + unflushed_size);
  }
  return result;
}

#endif // SHARE_JFR_RECORDER_STORAGE_JFRSTORAGEUTILS_INLINE_HPP
