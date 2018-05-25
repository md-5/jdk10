/*
 * Copyright (c) 2007, 2018, Oracle and/or its affiliates. All rights reserved.
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

package nsk.jvmti.RedefineClasses;

import java.io.*;

/**
 * This test checks that a class can be redefined and<br>
 * no JVMTI events are generated by the JVMTI function
 * <code>RedefineClasses()</code>.<br>
 * The test creates an instance of tested class <code>redefclass031r</code>.
 * Then the test makes classfile redifinition of the loaded class
 * <code>redefclass031r</code>.<br>
 * Bytes defining new version of the class <code>redefclass031r</code>
 * are taken from the same classfile, i.e. actually the new version
 * is identical with the old version.
 */
public class redefclass031 {
    static final int PASSED = 0;
    static final int FAILED = 2;
    static final int JCK_STATUS_BASE = 95;

    static boolean DEBUG_MODE = false;

    private PrintStream out;

    static {
        try {
            System.loadLibrary("redefclass031");
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Could not load redefclass031 library");
            System.err.println("java.library.path:" +
                System.getProperty("java.library.path"));
            throw e;
        }
    }

    native static int makeRedefinition(int vrb, Class redefClass,
        byte[] classBytes);

    public static void main(String[] argv) {
        argv = nsk.share.jvmti.JVMTITest.commonInit(argv);

        System.exit(run(argv, System.out) + JCK_STATUS_BASE);
    }

    public static int run(String argv[], PrintStream out) {
        return new redefclass031().runIt(argv, out);
    }

    private int runIt(String argv[], PrintStream out) {
        File newRedefClassFile = null;
        byte[] redefClassBytes;

        this.out = out;
        for (int i = 0; i < argv.length; i++) {
            String token = argv[i];

            if (token.equals("-v")) // verbose mode
                DEBUG_MODE = true;
        }

        redefclass031r redefClsObj = new redefclass031r();
        if (redefClsObj.checkIt(out, DEBUG_MODE) == 19) {
            if (DEBUG_MODE)
                out.println("Successfully check the class redefclass031r");
        } else {
            out.println("TEST: failed to check the class redefclass031r");
            return FAILED;
        }

// try to redefine class redefclass031r
        String fileName =
            redefclass031r.class.getName().replace('.', File.separatorChar) +
            ".class";
        if (DEBUG_MODE)
            out.println("Trying to redefine class from the file: " + fileName);
        try {
            ClassLoader cl = redefclass031.class.getClassLoader();
            InputStream in = cl.getSystemResourceAsStream(fileName);
            if (in == null) {
                out.println("# Class file \"" + fileName + "\" not found");
                return FAILED;
            }
            redefClassBytes = new byte[in.available()];
            in.read(redefClassBytes);
            in.close();
        } catch (Exception ex) {
            out.println("# Unexpected exception while reading class file:");
            out.println("# " + ex);
            return FAILED;
        }

// make real redefinition
        if (DEBUG_MODE)
            return makeRedefinition(1, redefClsObj.getClass(),
                redefClassBytes);
        else
            return makeRedefinition(0, redefClsObj.getClass(),
                redefClassBytes);
    }
}
