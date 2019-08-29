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

#ifndef SHARE_CLASSFILE_STRINGTABLE_HPP
#define SHARE_CLASSFILE_STRINGTABLE_HPP

#include "memory/allocation.hpp"
#include "memory/padded.hpp"
#include "oops/oop.hpp"
#include "oops/weakHandle.hpp"
#include "utilities/tableStatistics.hpp"

class CompactHashtableWriter;
class JavaThread;
class SerializeClosure;

class StringTable;
class StringTableConfig;
class StringTableCreateEntry;

class StringTable : public CHeapObj<mtSymbol>{
  friend class VMStructs;
  friend class Symbol;
  friend class StringTableConfig;
  friend class StringTableCreateEntry;

  static volatile bool _has_work;
  static volatile size_t _uncleaned_items_count;

  // Set if one bucket is out of balance due to hash algorithm deficiency
  static volatile bool _needs_rehashing;

  static void grow(JavaThread* jt);
  static void clean_dead_entries(JavaThread* jt);

  static double get_load_factor();
  static double get_dead_factor();

  static void check_concurrent_work();
  static void trigger_concurrent_work();

  static size_t item_added();
  static void item_removed();
  static size_t add_items_to_clean(size_t ndead);

  static oop intern(Handle string_or_null_h, const jchar* name, int len, TRAPS);
  static oop do_intern(Handle string_or_null, const jchar* name, int len, uintx hash, TRAPS);
  static oop do_lookup(const jchar* name, int len, uintx hash);

  static void print_table_statistics(outputStream* st, const char* table_name);

  static bool do_rehash();

 public:
  static size_t table_size();
  static TableStatistics get_table_statistics();

  static void create_table();

  static void do_concurrent_work(JavaThread* jt);
  static bool has_work() { return _has_work; }

  // GC support

  // Must be called before a parallel walk where strings might die.
  static void reset_dead_counter() { _uncleaned_items_count = 0; }

  // After the parallel walk this method must be called to trigger
  // cleaning. Note it might trigger a resize instead.
  static void finish_dead_counter() { check_concurrent_work(); }

  // If GC uses ParState directly it should add the number of cleared
  // strings to this method.
  static void inc_dead_counter(size_t ndead) { add_items_to_clean(ndead); }

  // Serially invoke "f->do_oop" on the locations of all oops in the table.
  // Used by JFR leak profiler.  TODO: it should find these oops through
  // the WeakProcessor.
  static void oops_do(OopClosure* f);

  // Probing
  static oop lookup(Symbol* symbol);
  static oop lookup(const jchar* chars, int length);

  // Interning
  static oop intern(Symbol* symbol, TRAPS);
  static oop intern(oop string, TRAPS);
  static oop intern(const char *utf8_string, TRAPS);

  // Rehash the string table if it gets out of balance
  static void rehash_table();
  static bool needs_rehashing() { return _needs_rehashing; }
  static inline void update_needs_rehash(bool rehash) {
    if (rehash) {
      _needs_rehashing = true;
    }
  }

  // Sharing
 private:
  static oop lookup_shared(const jchar* name, int len, unsigned int hash) NOT_CDS_JAVA_HEAP_RETURN_(NULL);
  static void copy_shared_string_table(CompactHashtableWriter* ch_table) NOT_CDS_JAVA_HEAP_RETURN;
 public:
  static oop create_archived_string(oop s, Thread* THREAD) NOT_CDS_JAVA_HEAP_RETURN_(NULL);
  static void shared_oops_do(OopClosure* f) NOT_CDS_JAVA_HEAP_RETURN;
  static void write_to_archive() NOT_CDS_JAVA_HEAP_RETURN;
  static void serialize_shared_table_header(SerializeClosure* soc) NOT_CDS_JAVA_HEAP_RETURN;

  // Jcmd
  static void dump(outputStream* st, bool verbose=false);
  // Debugging
  static size_t verify_and_compare_entries();
  static void verify();
};

#endif // SHARE_CLASSFILE_STRINGTABLE_HPP
