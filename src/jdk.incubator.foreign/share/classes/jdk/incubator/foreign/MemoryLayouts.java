/*
 *  Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 *  DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 *  This code is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 only, as
 *  published by the Free Software Foundation.  Oracle designates this
 *  particular file as subject to the "Classpath" exception as provided
 *  by Oracle in the LICENSE file that accompanied this code.
 *
 *  This code is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  version 2 for more details (a copy is included in the LICENSE file that
 *  accompanied this code).
 *
 *  You should have received a copy of the GNU General Public License version
 *  2 along with this work; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *   Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 *  or visit www.oracle.com if you need additional information or have any
 *  questions.
 *
 */

package jdk.incubator.foreign;

import java.nio.ByteOrder;

/**
 * This class defines useful layout constants. Some of the constants defined in this class are explicit in both
 * size and byte order (see {@link #BITS_64_BE}), and can therefore be used to explicitly and unambiguously specify the
 * contents of a memory segment. Other constants make implicit byte order assumptions (see
 * {@link #JAVA_INT}); as such, these constants make it easy to interoperate with other serialization-centric APIs,
 * such as {@link java.nio.ByteBuffer}.
 */
public final class MemoryLayouts {

    private MemoryLayouts() {
        //just the one, please
    }

    /**
     * A value layout constant with size of one byte, and byte order set to {@link ByteOrder#LITTLE_ENDIAN}.
     */
    public static final ValueLayout BITS_8_LE = MemoryLayout.ofValueBits(8, ByteOrder.LITTLE_ENDIAN);

    /**
     * A value layout constant with size of two bytes, and byte order set to {@link ByteOrder#LITTLE_ENDIAN}.
     */
    public static final ValueLayout BITS_16_LE = MemoryLayout.ofValueBits(16, ByteOrder.LITTLE_ENDIAN);

    /**
     * A value layout constant with size of four bytes, and byte order set to {@link ByteOrder#LITTLE_ENDIAN}.
     */
    public static final ValueLayout BITS_32_LE = MemoryLayout.ofValueBits(32, ByteOrder.LITTLE_ENDIAN);

    /**
     * A value layout constant with size of eight bytes, and byte order set to {@link ByteOrder#LITTLE_ENDIAN}.
     */
    public static final ValueLayout BITS_64_LE = MemoryLayout.ofValueBits(64, ByteOrder.LITTLE_ENDIAN);

    /**
     * A value layout constant with size of one byte, and byte order set to {@link ByteOrder#BIG_ENDIAN}.
     */
    public static final ValueLayout BITS_8_BE = MemoryLayout.ofValueBits(8, ByteOrder.BIG_ENDIAN);

    /**
     * A value layout constant with size of two bytes, and byte order set to {@link ByteOrder#BIG_ENDIAN}.
     */
    public static final ValueLayout BITS_16_BE = MemoryLayout.ofValueBits(16, ByteOrder.BIG_ENDIAN);

    /**
     * A value layout constant with size of four bytes, and byte order set to {@link ByteOrder#BIG_ENDIAN}.
     */
    public static final ValueLayout BITS_32_BE = MemoryLayout.ofValueBits(32, ByteOrder.BIG_ENDIAN);

    /**
     * A value layout constant with size of eight bytes, and byte order set to {@link ByteOrder#BIG_ENDIAN}.
     */
    public static final ValueLayout BITS_64_BE = MemoryLayout.ofValueBits(64, ByteOrder.BIG_ENDIAN);

    /**
     * A padding layout constant with size of one byte.
     */
    public static final MemoryLayout PAD_8 = MemoryLayout.ofPaddingBits(8);

    /**
     * A padding layout constant with size of two bytes.
     */
    public static final MemoryLayout PAD_16 = MemoryLayout.ofPaddingBits(16);

    /**
     * A padding layout constant with size of four bytes.
     */
    public static final MemoryLayout PAD_32 = MemoryLayout.ofPaddingBits(32);

    /**
     * A padding layout constant with size of eight bytes.
     */
    public static final MemoryLayout PAD_64 = MemoryLayout.ofPaddingBits(64);

    /**
     * A value layout constant whose size is the same as that of a Java {@code byte}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_BYTE = MemoryLayout.ofValueBits(8, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code char}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_CHAR = MemoryLayout.ofValueBits(16, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code short}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_SHORT = MemoryLayout.ofValueBits(16, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code int}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_INT = MemoryLayout.ofValueBits(32, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code long}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_LONG = MemoryLayout.ofValueBits(64, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code float}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_FLOAT = MemoryLayout.ofValueBits(32, ByteOrder.nativeOrder());

    /**
     * A value layout constant whose size is the same as that of a Java {@code double}, and byte order set to {@link ByteOrder#nativeOrder()}.
     */
    public static final ValueLayout JAVA_DOUBLE = MemoryLayout.ofValueBits(64, ByteOrder.nativeOrder());
}
