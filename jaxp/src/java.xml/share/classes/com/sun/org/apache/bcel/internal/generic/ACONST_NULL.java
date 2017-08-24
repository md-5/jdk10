/*
 * reserved comment block
 * DO NOT REMOVE OR ALTER!
 */
/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.sun.org.apache.bcel.internal.generic;

/**
 * ACONST_NULL - Push null reference
 * <PRE>Stack: ... -&gt; ..., null</PRE>
 *
 * @version $Id: ACONST_NULL.java 1747278 2016-06-07 17:28:43Z britter $
 */
public class ACONST_NULL extends Instruction implements PushInstruction, TypedInstruction {

    /**
     * Push null reference
     */
    public ACONST_NULL() {
        super(com.sun.org.apache.bcel.internal.Const.ACONST_NULL, (short) 1);
    }


    /** @return Type.NULL
     */
    @Override
    public Type getType( final ConstantPoolGen cp ) {
        return Type.NULL;
    }


    /**
     * Call corresponding visitor method(s). The order is:
     * Call visitor methods of implemented interfaces first, then
     * call methods according to the class hierarchy in descending order,
     * i.e., the most specific visitXXX() call comes last.
     *
     * @param v Visitor object
     */
    @Override
    public void accept( final Visitor v ) {
        v.visitStackProducer(this);
        v.visitPushInstruction(this);
        v.visitTypedInstruction(this);
        v.visitACONST_NULL(this);
    }
}
