/*
 * Copyright (c) 2003, 2018, Oracle and/or its affiliates. All rights reserved.
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
 */

package nsk.jvmti.PopFrame;

import nsk.share.Consts;

import nsk.share.Wicket;
import java.io.PrintStream;

/**
 * This test checks that after popping a method's frame by the JVMTI
 * function <code>PopFrame()</code>:
 * <li>lock acquired by the popped frame will be released
 * <li>no JVMTI events will be generated by the function <code>PopFrame()</code>
 */
public class popframe005 {
    static final int WAIT_TIME = 2000;

    static boolean DEBUG_MODE = false;
    static volatile int testedStep = 0; /* 0- action no yet started
                                           1- a frame is to be popped
                                           2- checking monitors state
                                           3- finishing the test */
    static volatile int result = Consts.TEST_PASSED;
    private PrintStream out;
    private popFrameCls popFrameClsThr;
    private objWaiter objWaiterThr1, objWaiterThr2;
    private Object allThreadsStoppedBarrier = new Object();
    private Wicket startGuarantee;
    private Wicket finishGuarantee;
    private Wicket allThreadsStoppedGuarantee;

    static {
        try {
            System.loadLibrary("popframe005");
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Could not load popframe005 library");
            System.err.println("java.library.path:" +
                System.getProperty("java.library.path"));
            throw e;
        }
    }

    native static boolean doPopFrame(popFrameCls popFrameClsThr);

    public static void main(String[] argv) {
        argv = nsk.share.jvmti.JVMTITest.commonInit(argv);

        System.exit(run(argv, System.out) + Consts.JCK_STATUS_BASE);
    }

    public static int run(String argv[], PrintStream out) {
        return new popframe005().runIt(argv, out);
    }

    private int runIt(String argv[], PrintStream out) {
        Object lockObj[] = new Object[2];

        this.out = out;
        for (int i = 0; i < argv.length; i++) {
            if (argv[i].equals("-v")) // verbose mode
                DEBUG_MODE = true;
        }

        lockObj[0] = new Object();
        lockObj[1] = new Object();

        startGuarantee = new Wicket("startGuarantee", 1, (DEBUG_MODE ? System.out : null));
        finishGuarantee = new Wicket("finishGuarantee", 1, (DEBUG_MODE ? System.out : null));

        allThreadsStoppedGuarantee =
            new Wicket("allThreadsStoppedGuarantee", 3, (DEBUG_MODE ? System.out : null));

        // start a thread whose top frame is to be popped
        popFrameClsThr = new popFrameCls("Tested Thread", lockObj);
        popFrameClsThr.start();

        startGuarantee.waitFor();

        // start a thread waiting for locked Object lockObj[0]
        objWaiterThr1 = new objWaiter(lockObj[0], 2);
        objWaiterThr1.start();

        // start a thread waiting for locked Object lockObj[1]
        objWaiterThr2 = new objWaiter(lockObj[1], 0);
        objWaiterThr2.start();

        // pause until the first thread exit notification-block
        allThreadsStoppedGuarantee.waitFor();
        synchronized (allThreadsStoppedBarrier) {
            if (DEBUG_MODE) {
                out.println("Passed barrier in main thread");
                out.flush();
            }
        }

        /////////////////////// popping a frame ////////////////////////
        testedStep = 1;

        if (DEBUG_MODE) {
            out.println("State transition: testedStep: 0->1");
            out.println("Going to pop the frame...");
            out.flush();
        }

        boolean retValue = doPopFrame(popFrameClsThr);

        popFrameClsThr.letItGo();
        if (!retValue)
            return Consts.TEST_FAILED;

        ///////////////////// check monitors state /////////////////////
        testedStep = 2;

        if (DEBUG_MODE) {
            out.println("State transition: testedStep: 1->2");
            out.flush();
        }

        if (!popFrameClsThr.isAlive()) {
            out.println("TEST FAILURE: thread with the method's popped frame is dead");
            return Consts.TEST_FAILED;
        }

        try {
            objWaiterThr2.join(WAIT_TIME);
        } catch (InterruptedException e) {
            if (DEBUG_MODE)
                out.println("Joining the objWaiterThr2's thread: caught " + e);
        }

        if (objWaiterThr2.isAlive()) { // objWaiterThr2 should be dead
            out.println("TEST FAILED: Lock acquired by a popped frame wasn't released\n" + "\twhen the frame had been popped");
            result = Consts.TEST_FAILED;
        }

        try {
            objWaiterThr1.join(WAIT_TIME);
        } catch (InterruptedException e) {
            if (DEBUG_MODE) {
                out.println("Joining the objWaiterThr1's thread: caught " + e);
                out.flush();
            }
        }
        if (!objWaiterThr1.isAlive()) { // objWaiterThr2 should be alive
            out.println("TEST FAILED: Lock acquired by a frame, different from the popped one,\n"
                + "\twas released when the popped frame had been popped");
            result = Consts.TEST_FAILED;
        }

        /////////////////////// finish the test ///////////////////////
        testedStep = 3;

        if (DEBUG_MODE) {
            out.println("State transition: testedStep: 2->3");
            out.flush();
        }

        finishGuarantee.unlock();

        try {
            if (popFrameClsThr.isAlive())
                popFrameClsThr.join(WAIT_TIME);
        } catch (InterruptedException e) {
            out.println("TEST INCOMPLETE: caught " + e);
            return Consts.TEST_FAILED;
        }
        if (popFrameClsThr.isAlive()) {
            out.println("TEST FAILED: thread with " +
                "the popped frame's method is still alive");
            result = Consts.TEST_FAILED;
        }

        return result;
    }

    // a thread whose top frame is to be popped and which locks two Objects
    class popFrameCls extends Thread {
        private volatile boolean flag = true;
        private Object[] lockObj;

        popFrameCls(String name, Object[] obj) {
            super(name);
            lockObj = obj;
        }

        public void run() {
            synchronized(lockObj[0]) {
                activeMethod();
            }
            if (DEBUG_MODE)
                out.println("popFrameCls (" + this +
                    "): exiting..."); out.flush();
        }

        public void activeMethod() {
            boolean compl = true;

            if (popframe005.testedStep != 0) { // popping has been done
                if (DEBUG_MODE) {
                    out.println("popFrameCls (" + this +
                        "): enter activeMethod() after popping");
                    out.flush();
                }

                // wait for checking monitors state by the main thread
                finishGuarantee.waitFor();

                if (DEBUG_MODE) {
                    out.println("popFrameCls (" + this +
                        "): leaving activeMethod()");
                    out.flush();
                }

                return;
            }

            try {
                synchronized(lockObj[1]) {
                    synchronized(allThreadsStoppedBarrier) {
                        if (DEBUG_MODE) {
                            out.println("popFrameCls (" + this + "): inside activeMethod()");
                            out.flush();
                        }

                        // notify the main thread
                        startGuarantee.unlock();

                        allThreadsStoppedGuarantee.unlock();
                    }

                    // loop until the main thread pops us
                    int i = 0;
                    int n = 1000;
                    while (flag) {
                        if (n <= 0) {
                            n = 1000;
                        }
                        if (i > n) {
                            i = 0;
                            n--;
                        }
                        i++;
                    }
                }
            } catch (Exception e) {
                out.println("FAILURE: popFrameCls (" + this + "): caught " + e);
                out.flush();
                compl = false;
            } finally {
                if (compl) {
                    out.println("TEST FAILED: finally block was executed after popping");
                    out.flush();
                }

                popframe005.result = Consts.TEST_FAILED;
            }
        }

        public void letItGo() {
            flag = false;
        }
    }

// a thread which waits for a specified Object which is locked
    class objWaiter extends Thread {
        private Object lockObj;
        private String objIdent;
        private int contrVal;

        objWaiter(Object obj, int stepVal) {
            lockObj = obj;
            contrVal = stepVal;
            if (stepVal == 2)
                objIdent = "[0]";
            else
                objIdent = "[1]";
        }

        public void run() {
            // notify the main thread
            synchronized(allThreadsStoppedBarrier) {
                if (DEBUG_MODE) {
                    out.println("objWaiter(" + this +
                            "): waiting for a lockObj" + objIdent +
                            "'s monitor; testedStep=" + testedStep);
                    out.flush();
                }

                allThreadsStoppedGuarantee.unlock();
            }

            try {
                synchronized(lockObj) {
                    if (testedStep <= contrVal) {
                        out.println("TEST FAILED: the lockObj" + objIdent +
                            "'s monitor became free too early");
                        result = Consts.TEST_FAILED;
                    } else if (DEBUG_MODE) {
                        out.println("Check PASSED: objWaiter(" + this +
                            "): enter the lockObj" + objIdent + "'s monitor");
                        out.flush();
                    }
                }

                if (DEBUG_MODE) {
                    out.println("objWaiter (" + this + "): exiting...");
                    out.flush();
                }
            } catch (Exception e) {
                out.println("TEST FAILURE: objWaiter (" + this + "): caught " + e);
            }
        }
    }
}
