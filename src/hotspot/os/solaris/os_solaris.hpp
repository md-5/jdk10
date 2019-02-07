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

#ifndef OS_SOLARIS_OS_SOLARIS_HPP
#define OS_SOLARIS_OS_SOLARIS_HPP

// Solaris_OS defines the interface to Solaris operating systems

// see thr_setprio(3T) for the basis of these numbers
#define MinimumPriority 0
#define NormalPriority  64
#define MaximumPriority 127

// FX/60 is critical thread class/priority on T4
#define FXCriticalPriority 60

// Information about the protection of the page at address '0' on this os.
static bool zero_page_read_protected() { return true; }

class Solaris {
  friend class os;

 private:

  // Support for "new" libthread APIs for getting & setting thread context (2.8)
#define TRS_VALID       0
#define TRS_NONVOLATILE 1
#define TRS_LWPID       2
#define TRS_INVALID     3

  // initialized to libthread or lwp synchronization primitives depending on UseLWPSychronization
  static int_fnP_mutex_tP _mutex_lock;
  static int_fnP_mutex_tP _mutex_trylock;
  static int_fnP_mutex_tP _mutex_unlock;
  static int_fnP_mutex_tP_i_vP _mutex_init;
  static int_fnP_mutex_tP _mutex_destroy;
  static int _mutex_scope;

  static int_fnP_cond_tP_mutex_tP_timestruc_tP _cond_timedwait;
  static int_fnP_cond_tP_mutex_tP _cond_wait;
  static int_fnP_cond_tP _cond_signal;
  static int_fnP_cond_tP _cond_broadcast;
  static int_fnP_cond_tP_i_vP _cond_init;
  static int_fnP_cond_tP _cond_destroy;
  static int _cond_scope;

  static bool _synchronization_initialized;

  typedef uintptr_t       lgrp_cookie_t;
  typedef id_t            lgrp_id_t;
  typedef int             lgrp_rsrc_t;
  typedef enum lgrp_view {
    LGRP_VIEW_CALLER,       // what's available to the caller
    LGRP_VIEW_OS            // what's available to operating system
  } lgrp_view_t;

  typedef lgrp_id_t (*lgrp_home_func_t)(idtype_t idtype, id_t id);
  typedef lgrp_cookie_t (*lgrp_init_func_t)(lgrp_view_t view);
  typedef int (*lgrp_fini_func_t)(lgrp_cookie_t cookie);
  typedef lgrp_id_t (*lgrp_root_func_t)(lgrp_cookie_t cookie);
  typedef int (*lgrp_children_func_t)(lgrp_cookie_t  cookie,  lgrp_id_t  parent,
                                      lgrp_id_t *lgrp_array, uint_t lgrp_array_size);
  typedef int (*lgrp_resources_func_t)(lgrp_cookie_t  cookie,  lgrp_id_t  lgrp,
                                       lgrp_id_t *lgrp_array, uint_t lgrp_array_size,
                                       lgrp_rsrc_t type);
  typedef int (*lgrp_nlgrps_func_t)(lgrp_cookie_t cookie);
  typedef int (*lgrp_cookie_stale_func_t)(lgrp_cookie_t cookie);

  static lgrp_home_func_t _lgrp_home;
  static lgrp_init_func_t _lgrp_init;
  static lgrp_fini_func_t _lgrp_fini;
  static lgrp_root_func_t _lgrp_root;
  static lgrp_children_func_t _lgrp_children;
  static lgrp_resources_func_t _lgrp_resources;
  static lgrp_nlgrps_func_t _lgrp_nlgrps;
  static lgrp_cookie_stale_func_t _lgrp_cookie_stale;
  static lgrp_cookie_t _lgrp_cookie;

  // Large Page Support
  static bool is_valid_page_size(size_t bytes);
  static size_t page_size_for_alignment(size_t alignment);
  static bool setup_large_pages(caddr_t start, size_t bytes, size_t align);

  static void try_enable_extended_io();

  static struct sigaction *(*get_signal_action)(int);
  static struct sigaction *get_preinstalled_handler(int);
  static int (*get_libjsig_version)();
  static void save_preinstalled_handler(int, struct sigaction&);
  static void check_signal_handler(int sig);

  typedef int (*pthread_setname_np_func_t)(pthread_t, const char*);
  static pthread_setname_np_func_t _pthread_setname_np;

 public:
  // Large Page Support--ISM.
  static bool largepage_range(char* addr, size_t size);

  static address handler_start, handler_end; // start and end pc of thr_sighndlrinfo

  static bool valid_stack_address(Thread* thread, address sp);
  static bool valid_ucontext(Thread* thread, const ucontext_t* valid, const ucontext_t* suspect);
  static const ucontext_t* get_valid_uc_in_signal_handler(Thread* thread,
                                                    const ucontext_t* uc);

  static ExtendedPC  ucontext_get_ExtendedPC(const ucontext_t* uc);
  static intptr_t*   ucontext_get_sp(const ucontext_t* uc);
  // ucontext_get_fp() is only used by Solaris X86 (see note below)
  static intptr_t*   ucontext_get_fp(const ucontext_t* uc);
  static address    ucontext_get_pc(const ucontext_t* uc);
  static void ucontext_set_pc(ucontext_t* uc, address pc);

  // For Analyzer Forte AsyncGetCallTrace profiling support:
  // Parameter ret_fp is only used by Solaris X86.
  //
  // We should have different declarations of this interface in
  // os_solaris_i486.hpp and os_solaris_sparc.hpp, but that file
  // provides extensions to the os class and not the Solaris class.
  static ExtendedPC fetch_frame_from_ucontext(Thread* thread, const ucontext_t* uc,
                                              intptr_t** ret_sp, intptr_t** ret_fp);

  static bool get_frame_at_stack_banging_point(JavaThread* thread, ucontext_t* uc, frame* fr);

  static void hotspot_sigmask(Thread* thread);

  // SR_handler
  static void SR_handler(Thread* thread, ucontext_t* uc);

  static void init_thread_fpu_state(void);

 protected:
  // Solaris-specific interface goes here
  static julong available_memory();
  static julong physical_memory() { return _physical_memory; }
  static julong _physical_memory;
  static void initialize_system_info();
  static int _dev_zero_fd;
  static int get_dev_zero_fd() { return _dev_zero_fd; }
  static void set_dev_zero_fd(int fd) { _dev_zero_fd = fd; }
  static int commit_memory_impl(char* addr, size_t bytes, bool exec);
  static int commit_memory_impl(char* addr, size_t bytes,
                                size_t alignment_hint, bool exec);
  static char* mmap_chunk(char *addr, size_t size, int flags, int prot);
  static char* anon_mmap(char* requested_addr, size_t bytes, size_t alignment_hint, bool fixed);
  static bool mpss_sanity_check(bool warn, size_t * page_size);

  // Workaround for 4352906. thr_stksegment sometimes returns
  // a bad value for the primordial thread's stack base when
  // it is called more than one time.
  // Workaround is to cache the initial value to avoid further
  // calls to thr_stksegment.
  // It appears that someone (Hotspot?) is trashing the user's
  // proc_t structure (note that this is a system struct).
  static address _main_stack_base;

  static void print_distro_info(outputStream* st);
  static void print_libversion_info(outputStream* st);

 public:
  static void libthread_init();
  static void synchronization_init();
  static bool liblgrp_init();
  // This boolean allows users to forward their own non-matching signals
  // to JVM_handle_solaris_signal, harmlessly.
  static bool signal_handlers_are_installed;

  static void signal_sets_init();
  static void install_signal_handlers();
  static void set_signal_handler(int sig, bool set_installed, bool oktochain);
  static void init_signal_mem();
  static void set_our_sigflags(int, int);
  static int get_our_sigflags(int);

  // For signal-chaining
  static bool libjsig_is_loaded; // libjsig that interposes sigaction(),
                                 // signal(), sigset() is loaded
  static struct sigaction *get_chained_signal_action(int sig);
  static bool chained_handler(int sig, siginfo_t *siginfo, void *context);

  // Allows us to switch between lwp and thread -based synchronization
  static int mutex_lock(mutex_t *mx)    { return _mutex_lock(mx); }
  static int mutex_trylock(mutex_t *mx) { return _mutex_trylock(mx); }
  static int mutex_unlock(mutex_t *mx)  { return _mutex_unlock(mx); }
  static int mutex_init(mutex_t *mx)    { return _mutex_init(mx, os::Solaris::mutex_scope(), NULL); }
  static int mutex_destroy(mutex_t *mx) { return _mutex_destroy(mx); }
  static int mutex_scope()              { return _mutex_scope; }

  static void set_mutex_lock(int_fnP_mutex_tP func)      { _mutex_lock = func; }
  static void set_mutex_trylock(int_fnP_mutex_tP func)   { _mutex_trylock = func; }
  static void set_mutex_unlock(int_fnP_mutex_tP func)    { _mutex_unlock = func; }
  static void set_mutex_init(int_fnP_mutex_tP_i_vP func) { _mutex_init = func; }
  static void set_mutex_destroy(int_fnP_mutex_tP func)   { _mutex_destroy = func; }
  static void set_mutex_scope(int scope)                 { _mutex_scope = scope; }

  static int cond_timedwait(cond_t *cv, mutex_t *mx, timestruc_t *abst) { return _cond_timedwait(cv, mx, abst); }
  static int cond_wait(cond_t *cv, mutex_t *mx) { return _cond_wait(cv, mx); }
  static int cond_signal(cond_t *cv)            { return _cond_signal(cv); }
  static int cond_broadcast(cond_t *cv)         { return _cond_broadcast(cv); }
  static int cond_init(cond_t *cv)              { return _cond_init(cv, os::Solaris::cond_scope(), NULL); }
  static int cond_destroy(cond_t *cv)           { return _cond_destroy(cv); }
  static int cond_scope()                       { return _cond_scope; }

  static void set_cond_timedwait(int_fnP_cond_tP_mutex_tP_timestruc_tP func) { _cond_timedwait = func; }
  static void set_cond_wait(int_fnP_cond_tP_mutex_tP func) { _cond_wait = func; }
  static void set_cond_signal(int_fnP_cond_tP func)        { _cond_signal = func; }
  static void set_cond_broadcast(int_fnP_cond_tP func)     { _cond_broadcast = func; }
  static void set_cond_init(int_fnP_cond_tP_i_vP func)     { _cond_init = func; }
  static void set_cond_destroy(int_fnP_cond_tP func)       { _cond_destroy = func; }
  static void set_cond_scope(int scope)                    { _cond_scope = scope; }

  static bool synchronization_initialized()                { return _synchronization_initialized; }

  static void set_lgrp_home(lgrp_home_func_t func) { _lgrp_home = func; }
  static void set_lgrp_init(lgrp_init_func_t func) { _lgrp_init = func; }
  static void set_lgrp_fini(lgrp_fini_func_t func) { _lgrp_fini = func; }
  static void set_lgrp_root(lgrp_root_func_t func) { _lgrp_root = func; }
  static void set_lgrp_children(lgrp_children_func_t func)   { _lgrp_children = func; }
  static void set_lgrp_resources(lgrp_resources_func_t func) { _lgrp_resources = func; }
  static void set_lgrp_nlgrps(lgrp_nlgrps_func_t func)       { _lgrp_nlgrps = func; }
  static void set_lgrp_cookie_stale(lgrp_cookie_stale_func_t func) { _lgrp_cookie_stale = func; }
  static void set_lgrp_cookie(lgrp_cookie_t cookie)  { _lgrp_cookie = cookie; }

  static id_t lgrp_home(idtype_t type, id_t id)      { return _lgrp_home != NULL ? _lgrp_home(type, id) : -1; }
  static lgrp_cookie_t lgrp_init(lgrp_view_t view)   { return _lgrp_init != NULL ? _lgrp_init(view) : 0; }
  static int lgrp_fini(lgrp_cookie_t cookie)         { return _lgrp_fini != NULL ? _lgrp_fini(cookie) : -1; }
  static lgrp_id_t lgrp_root(lgrp_cookie_t cookie)   { return _lgrp_root != NULL ? _lgrp_root(cookie) : -1; }
  static int lgrp_children(lgrp_cookie_t  cookie,  lgrp_id_t  parent,
                           lgrp_id_t *lgrp_array, uint_t lgrp_array_size) {
    return _lgrp_children != NULL ? _lgrp_children(cookie, parent, lgrp_array, lgrp_array_size) : -1;
  }
  static int lgrp_resources(lgrp_cookie_t  cookie,  lgrp_id_t  lgrp,
                            lgrp_id_t *lgrp_array, uint_t lgrp_array_size,
                            lgrp_rsrc_t type) {
    return _lgrp_resources != NULL ? _lgrp_resources(cookie, lgrp, lgrp_array, lgrp_array_size, type) : -1;
  }

  static int lgrp_nlgrps(lgrp_cookie_t cookie)       { return _lgrp_nlgrps != NULL ? _lgrp_nlgrps(cookie) : -1; }
  static int lgrp_cookie_stale(lgrp_cookie_t cookie) {
    return _lgrp_cookie_stale != NULL ? _lgrp_cookie_stale(cookie) : -1;
  }
  static lgrp_cookie_t lgrp_cookie()                 { return _lgrp_cookie; }

  static sigset_t* unblocked_signals();
  static sigset_t* vm_signals();

  // %%% Following should be promoted to os.hpp:
  // Trace number of created threads
  static          jint  _os_thread_limit;
  static volatile jint  _os_thread_count;

  static void correct_stack_boundaries_for_primordial_thread(Thread* thr);

  // Stack overflow handling

  static int max_register_window_saves_before_flushing();

  // Stack repair handling

  // none present

};

class PlatformEvent : public CHeapObj<mtInternal> {
 private:
  double CachePad[4];   // increase odds that _mutex is sole occupant of cache line
  volatile int _Event;
  int _nParked;
  int _pipev[2];
  mutex_t _mutex[1];
  cond_t  _cond[1];
  double PostPad[2];

 protected:
  // Defining a protected ctor effectively gives us an abstract base class.
  // That is, a PlatformEvent can never be instantiated "naked" but only
  // as a part of a ParkEvent (recall that ParkEvent extends PlatformEvent).
  // TODO-FIXME: make dtor private
  ~PlatformEvent() { guarantee(0, "invariant"); }
  PlatformEvent() {
    int status;
    status = os::Solaris::cond_init(_cond);
    assert_status(status == 0, status, "cond_init");
    status = os::Solaris::mutex_init(_mutex);
    assert_status(status == 0, status, "mutex_init");
    _Event   = 0;
    _nParked = 0;
    _pipev[0] = _pipev[1] = -1;
  }

 public:
  // Exercise caution using reset() and fired() -- they may require MEMBARs
  void reset() { _Event = 0; }
  int  fired() { return _Event; }
  void park();
  int  park(jlong millis);
  void unpark();
};

class PlatformParker : public CHeapObj<mtInternal> {
 protected:
  mutex_t _mutex[1];
  cond_t  _cond[1];

 public:       // TODO-FIXME: make dtor private
  ~PlatformParker() { guarantee(0, "invariant"); }

 public:
  PlatformParker() {
    int status;
    status = os::Solaris::cond_init(_cond);
    assert_status(status == 0, status, "cond_init");
    status = os::Solaris::mutex_init(_mutex);
    assert_status(status == 0, status, "mutex_init");
  }
};

// Platform specific implementation that underpins VM Monitor/Mutex class
class PlatformMonitor : public CHeapObj<mtInternal> {
 private:
  mutex_t _mutex; // Native mutex for locking
  cond_t  _cond;  // Native condition variable for blocking

 public:
  PlatformMonitor();
  ~PlatformMonitor();
  void lock();
  void unlock();
  bool try_lock();
  int wait(jlong millis);
  void notify();
  void notify_all();
};

#endif // OS_SOLARIS_OS_SOLARIS_HPP
