/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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
package jdk.internal.jimage.decompressor;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Objects;
import jdk.internal.jimage.decompressor.ResourceDecompressor.StringsProvider;

/**
 *
 * A resource header for compressed resource. This class is handled internally,
 * you don't have to add header to the resource, headers are added automatically
 * for compressed resources.
 */
public final class CompressedResourceHeader {

    private static final int SIZE = 21;
    public static final int MAGIC = 0xCAFEFAFA;
    private final int uncompressedSize;
    private final int compressedSize;
    private final int decompressorNameOffset;
    private final int contentOffset;
    private final boolean isTerminal;

    public CompressedResourceHeader(int compressedSize,
            int uncompressedSize, int decompressorNameOffset, int contentOffset,
            boolean isTerminal) {
        this.compressedSize = compressedSize;
        this.uncompressedSize = uncompressedSize;
        this.decompressorNameOffset = decompressorNameOffset;
        this.contentOffset = contentOffset;
        this.isTerminal = isTerminal;
    }

    public boolean isTerminal() {
        return isTerminal;
    }

    public int getDecompressorNameOffset() {
        return decompressorNameOffset;
    }

    public int getContentOffset() {
        return contentOffset;
    }

    public String getStoredContent(StringsProvider provider) {
        Objects.nonNull(provider);
        if(contentOffset == -1) {
            return null;
        }
        return provider.getString(contentOffset);
    }

    public int getUncompressedSize() {
        return uncompressedSize;
    }

    public int getResourceSize() {
        return compressedSize;
    }

    public byte[] getBytes(ByteOrder order) {
        Objects.requireNonNull(order);
        ByteBuffer buffer = ByteBuffer.allocate(SIZE);
        buffer.order(order);
        buffer.putInt(MAGIC);
        buffer.putInt(compressedSize);
        buffer.putInt(uncompressedSize);
        buffer.putInt(decompressorNameOffset);
        buffer.putInt(contentOffset);
        buffer.put(isTerminal ? (byte)1 : (byte)0);
        return buffer.array();
    }

    public static int getSize() {
        return SIZE;
    }

    public static CompressedResourceHeader readFromResource(ByteOrder order,
            byte[] resource) {
        Objects.requireNonNull(order);
        Objects.requireNonNull(resource);
        if (resource.length < getSize()) {
            return null;
        }
        ByteBuffer buffer = ByteBuffer.wrap(resource, 0, SIZE);
        buffer.order(order);
        int magic = buffer.getInt();
        if(magic != MAGIC) {
            return null;
        }
        int size = buffer.getInt();
        int uncompressedSize = buffer.getInt();
        int decompressorNameOffset = buffer.getInt();
        int contentIndex = buffer.getInt();
        byte isTerminal = buffer.get();
        return new CompressedResourceHeader(size, uncompressedSize,
                decompressorNameOffset, contentIndex, isTerminal == 1);
    }
}
