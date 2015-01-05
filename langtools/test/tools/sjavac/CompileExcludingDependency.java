/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

/*
 * @test
 * @summary Tests compiling class A that depends on class B without compiling class B
 * @bug 8054689
 * @author Fredrik O
 * @author sogoel (rewrite)
 * @library /tools/lib
 * @build Wrapper ToolBox
 * @run main Wrapper CompileExcludingDependency
 */

import java.util.*;
import java.nio.file.*;

public class CompileExcludingDependency extends SJavacTester {
    public static void main(String... args) throws Exception {
        CompileExcludingDependency ced = new CompileExcludingDependency();
        ced.test();
    }

    // Verify that excluding classes from compilation but not from linking works
    void test() throws Exception {
        Files.createDirectory(BIN);
        clean(GENSRC,BIN);
        Map<String,Long> previous_bin_state = collectState(BIN);
        ToolBox tb = new ToolBox();
        tb.writeFile(GENSRC.resolve("alfa/omega/A.java"),
                 "package alfa.omega; public class A { beta.B b; }");
        tb.writeFile(GENSRC.resolve("beta/B.java"),
                 "package beta; public class B { }");

        compile("-x", "beta", "-src", "gensrc", "-x", "alfa/omega", "-sourcepath", "gensrc",
                "-d", "bin", SERVER_ARG);

        Map<String,Long> new_bin_state = collectState(BIN);
        verifyThatFilesHaveBeenAdded(previous_bin_state, new_bin_state,
                                     "bin/alfa/omega/A.class",
                                     "bin/javac_state");
        clean(GENSRC, BIN);
    }
}
