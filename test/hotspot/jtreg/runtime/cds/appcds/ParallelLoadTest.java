/*
 * Copyright (c) 2014, 2019, Oracle and/or its affiliates. All rights reserved.
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

/*
 * @test
 * @summary Load app classes from CDS archive in parallel threads
 * @library /test/lib
 * @requires vm.cds
 * @modules jdk.jartool/sun.tools.jar
 * @compile test-classes/ParallelLoad.java
 * @compile test-classes/ParallelClasses.java
 * @run driver ParallelLoadTest
 */

public class ParallelLoadTest {
    public static final int MAX_CLASSES = 40;

    /* For easy stress testing, do this:

       i=0; while jtreg -DParallelLoadTest.app.loops=100 -DParallelLoadTest.boot.loops=100 \
           ParallelLoadTest.java; do i=$(expr $i + 1); echo =====$i; done

     */

    private static final int APP_LOOPS  = Integer.parseInt(System.getProperty("ParallelLoadTest.app.loops", "1"));
    private static final int BOOT_LOOPS = Integer.parseInt(System.getProperty("ParallelLoadTest.boot.loops", "1"));

    public static void main(String[] args) throws Exception {
        JarBuilder.build("parallel_load", getClassList(true));
        String testJar = TestCommon.getTestJar("parallel_load.jar");

        // (1) Load the classes from app class loader
        TestCommon.testDump(testJar, getClassList(false));
        for (int i = 0; i < APP_LOOPS; i++) {
            TestCommon.run("-cp", testJar,  "ParallelLoad").assertNormalExit();
        }

        // (2) Load the classes from boot class loader
        String bootcp = "-Xbootclasspath/a:" + testJar;
        TestCommon.testDump(null, getClassList(false), bootcp);
        for (int i = 0; i < BOOT_LOOPS; i++) {
            TestCommon.run(bootcp,
                           // "-Xlog:class+load=debug",
                           "ParallelLoad").assertNormalExit();
        }
    }

    private static String[] getClassList(boolean includeWatchdog) {
        int extra = includeWatchdog ? 3 : 2;
        String[] classList = new String[MAX_CLASSES + extra];

        int i;
        for (i = 0; i < MAX_CLASSES; i++) {
            classList[i] = "ParallelClass" + i;
        }

        classList[i++] = "ParallelLoad";
        classList[i++] = "ParallelLoadThread";
        if (includeWatchdog)
            classList[i++] = "ParallelLoadWatchdog";

        return classList;
    }
}
