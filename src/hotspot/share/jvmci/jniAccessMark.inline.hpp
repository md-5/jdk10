/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_JVMCI_JNIACCESSMARK_INLINE_HPP
#define SHARE_JVMCI_JNIACCESSMARK_INLINE_HPP

#include "jvmci/jvmciEnv.hpp"
#include "runtime/interfaceSupport.inline.hpp"

// Wrapper for a JNI call into the JVMCI shared library.
// This performs a ThreadToNativeFromVM transition so that the VM
// will not be blocked if the call takes a long time (e.g., due
// to a GC in the shared library).
class JNIAccessMark : public StackObj {
 private:
  ThreadToNativeFromVM ttnfv;
  HandleMark hm;
  JNIEnv* _env;
 public:
  inline JNIAccessMark(JVMCIEnv* jvmci_env) :
    ttnfv(JavaThread::current()), hm(JavaThread::current()) {
    _env = jvmci_env->_env;
  }
  JNIEnv* env() const { return _env; }
  JNIEnv* operator () () const { return _env; }
};

#endif // SHARE_JVMCI_JNIACCESSMARK_INLINE_HPP
