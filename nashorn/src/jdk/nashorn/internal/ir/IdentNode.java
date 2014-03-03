/*
 * Copyright (c) 2010, 2013, Oracle and/or its affiliates. All rights reserved.
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

package jdk.nashorn.internal.ir;

import static jdk.nashorn.internal.codegen.CompilerConstants.__DIR__;
import static jdk.nashorn.internal.codegen.CompilerConstants.__FILE__;
import static jdk.nashorn.internal.codegen.CompilerConstants.__LINE__;
import static jdk.nashorn.internal.codegen.ObjectClassGenerator.DEBUG_FIELDS;

import jdk.nashorn.internal.codegen.ObjectClassGenerator;
import jdk.nashorn.internal.codegen.types.Type;
import jdk.nashorn.internal.ir.annotations.Immutable;
import jdk.nashorn.internal.ir.visitor.NodeVisitor;

import static jdk.nashorn.internal.runtime.UnwarrantedOptimismException.INVALID_PROGRAM_POINT;

/**
 * IR representation for an identifier.
 */
@Immutable
public final class IdentNode extends Expression implements PropertyKey, FunctionCall, Optimistic {
    private static final int PROPERTY_NAME     = 1 << 0;
    private static final int INITIALIZED_HERE  = 1 << 1;
    private static final int FUNCTION          = 1 << 2;
    private static final int FUTURESTRICT_NAME = 1 << 3;
    private static final int OPTIMISTIC        = 1 << 4;

    /** Identifier. */
    private final String name;

    /** Optimistic type */
    private final Type optimisticType;

    private final int flags;

    private final int programPoint;

    /**
     * Constructor
     *
     * @param token   token
     * @param finish  finish position
     * @param name    name of identifier
     */
    public IdentNode(final long token, final int finish, final String name) {
        super(token, finish);
        this.name           = name.intern();
        this.optimisticType = null;
        this.flags          = 0;
        this.programPoint   = INVALID_PROGRAM_POINT;
    }

    private IdentNode(final IdentNode identNode, final String name, final Type callSiteType, final int flags, final int programPoint) {
        super(identNode);
        this.name           = name;
        this.optimisticType = callSiteType;
        this.flags          = flags;
        this.programPoint   = programPoint;
    }

    /**
     * Copy constructor - create a new IdentNode for the same location
     *
     * @param identNode  identNode
     */
    public IdentNode(final IdentNode identNode) {
        super(identNode);
        this.name           = identNode.getName();
        this.optimisticType = null;
        this.flags          = identNode.flags;
        this.programPoint   = INVALID_PROGRAM_POINT;
    }

    @Override
    public Type getType() {
        return optimisticType == null ? super.getType() : optimisticType;
    }

    @Override
    public boolean isAtom() {
        return true;
    }

    /**
     * Assist in IR navigation.
     *
     * @param visitor IR navigating visitor.
     */
    @Override
    public Node accept(final NodeVisitor<? extends LexicalContext> visitor) {
        if (visitor.enterIdentNode(this)) {
            return visitor.leaveIdentNode(this);
        }

        return this;
    }

    @Override
    public void toString(final StringBuilder sb) {
        Node.optimisticType(this, sb);
        sb.append(name);
    }

    /**
     * Get the name of the identifier
     * @return  IdentNode name
     */
    public String getName() {
        return name;
    }

    @Override
    public String getPropertyName() {
        return getName();
    }

    @Override
    public boolean isLocal() {
        return !getSymbol().isScope();
    }

    /**
     * Check if this IdentNode is a property name
     * @return true if this is a property name
     */
    public boolean isPropertyName() {
        return (flags & PROPERTY_NAME) == PROPERTY_NAME;
    }

    /**
     * Flag this IdentNode as a property name
     * @return a node equivalent to this one except for the requested change.
     */
    public IdentNode setIsPropertyName() {
        if (isPropertyName()) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, flags | PROPERTY_NAME, programPoint);
    }

    /**
     * Check if this IdentNode is a future strict name
     * @return true if this is a future strict name
     */
    public boolean isFutureStrictName() {
        return (flags & FUTURESTRICT_NAME) == FUTURESTRICT_NAME;
    }

    /**
     * Flag this IdentNode as a future strict name
     * @return a node equivalent to this one except for the requested change.
     */
    public IdentNode setIsFutureStrictName() {
        if (isFutureStrictName()) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, flags | FUTURESTRICT_NAME, programPoint);
    }

    /**
     * Helper function for local def analysis.
     * @return true if IdentNode is initialized on creation
     */
    public boolean isInitializedHere() {
        return (flags & INITIALIZED_HERE) == INITIALIZED_HERE;
    }

    /**
     * Flag IdentNode to be initialized on creation
     * @return a node equivalent to this one except for the requested change.
     */
    public IdentNode setIsInitializedHere() {
        if (isInitializedHere()) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, flags | INITIALIZED_HERE, programPoint);
    }

    /**
     * Check if the name of this IdentNode is same as that of a compile-time property (currently __DIR__, __FILE__, and
     * __LINE__).
     *
     * @return true if this IdentNode's name is same as that of a compile-time property
     */
    public boolean isCompileTimePropertyName() {
        return name.equals(__DIR__.symbolName()) || name.equals(__FILE__.symbolName()) || name.equals(__LINE__.symbolName());
    }

    @Override
    public boolean isFunction() {
        return (flags & FUNCTION) == FUNCTION;
    }

    @Override
    public IdentNode setType(final TemporarySymbols ts, final Type callSiteType) {
        if (this.optimisticType == callSiteType) {
            return this;
        }
        if (DEBUG_FIELDS && ObjectClassGenerator.shouldInstrument(getName()) && getSymbol() != null && !Type.areEquivalent(getSymbol().getSymbolType(), callSiteType)) {
            ObjectClassGenerator.LOG.info(getClass().getName(), " ", this, " => ", callSiteType, " instead of ", getType());
        }
        return new IdentNode(this, name, callSiteType, flags, programPoint);
    }

    /**
     * Mark this node as being the callee operand of a {@link CallNode}.
     * @return an ident node identical to this one in all aspects except with its function flag set.
     */
    public IdentNode setIsFunction() {
        if (isFunction()) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, flags | FUNCTION, programPoint);
    }

    @Override
    public int getProgramPoint() {
        return programPoint;
    }

    @Override
    public Optimistic setProgramPoint(final int programPoint) {
        if (this.programPoint == programPoint) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, flags, programPoint);
    }

    @Override
    public Type getMostOptimisticType() {
        return Type.INT;
    }

    @Override
    public Type getMostPessimisticType() {
        return Type.OBJECT;
    }

    @Override
    public boolean canBeOptimistic() {
        return true;
    }

    @Override
    public boolean isOptimistic() {
        return (flags & OPTIMISTIC) == OPTIMISTIC;
    }

    @Override
    public Optimistic setIsOptimistic(final boolean isOptimistic) {
        if (isOptimistic() == isOptimistic) {
            return this;
        }
        return new IdentNode(this, name, optimisticType, isOptimistic ? (flags | OPTIMISTIC) : (flags & ~OPTIMISTIC), programPoint);
    }
}
