/*
 * Copyright 2005-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */
package com.sun.xml.internal.ws.developer;

import com.sun.xml.internal.ws.server.DraconianValidationErrorHandler;

import javax.jws.WebService;
import javax.xml.transform.Source;
import javax.xml.validation.Schema;
import javax.xml.ws.spi.WebServiceFeatureAnnotation;
import java.lang.annotation.Documented;
import static java.lang.annotation.ElementType.TYPE;
import java.lang.annotation.Retention;
import static java.lang.annotation.RetentionPolicy.RUNTIME;
import java.lang.annotation.Target;

/**
 * Validates all request and response messages payload(SOAP:Body) for a {@link WebService}
 * against the XML schema. To use this feature, annotate the endpoint class with
 * this annotation.
 *
 * <pre>
 * for e.g.:
 *
 * &#64;WebService
 * &#64;SchemaValidation
 * public class HelloImpl {
 *   ...
 * }
 * </pre>
 *
 * At present, schema validation works for doc/lit web services only.
 *
 * @since JAX-WS 2.1.3
 * @author Jitendra Kotamraju
 * @see SchemaValidationFeature
 */
@Retention(RUNTIME)
@Target(TYPE)
@Documented
@WebServiceFeatureAnnotation(id = SchemaValidationFeature.ID, bean = SchemaValidationFeature.class)
public @interface SchemaValidation {

    /**
     * Configure the validation behaviour w.r.t error handling. The default handler
     * just rejects any invalid schema intances. If the application want to change
     * this default behaviour(say just log the errors), it can do so by providing
     * a custom implementation of {@link ValidationErrorHandler}.
     */
    Class<? extends ValidationErrorHandler> handler() default DraconianValidationErrorHandler.class;

    /**
     * Does validation for bound headers in a SOAP message.
     *
    boolean headers() default false;
     */

    /**
     * Additional schema documents that are used to create {@link Schema} object. Useful
     * when the application adds additional SOAP headers to the message. This is a list
     * of system-ids, that are used to create {@link Source} objects and used in creation
     * of {@link Schema} object
     *
     * for e.g.:
     * @SchemaValidation(schemaLocations={"http://bar.foo/b.xsd", "http://foo.bar/a.xsd"}
     *
    String[] schemaLocations() default {};
     */

}
