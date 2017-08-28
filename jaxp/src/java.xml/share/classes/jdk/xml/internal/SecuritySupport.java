/*
 * Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.
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
package jdk.xml.internal;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.security.PrivilegedActionException;
import java.security.PrivilegedExceptionAction;
import java.text.MessageFormat;
import java.util.Locale;
import java.util.MissingResourceException;
import java.util.Properties;
import java.util.ResourceBundle;

/**
 * This class contains utility methods for reading resources in the JAXP packages
 */
public class SecuritySupport {
    public final static String NEWLINE = getSystemProperty("line.separator", "\n");

    /**
     * Cache for properties in java.home/conf/jaxp.properties
     */
    static final Properties cacheProps = new Properties();

    /**
     * Flag indicating whether java.home/conf/jaxp.properties has been read
     */
    static volatile boolean firstTime = true;

    private SecuritySupport() {}

    public static String getErrorMessage(Locale locale, String bundle, String key,
            Object[] arguments) {
        ResourceBundle rb;
        if (locale != null) {
            rb = ResourceBundle.getBundle(bundle,locale);
        } else {
            rb = ResourceBundle.getBundle(bundle);
        }

        String msg = rb.getString(key);
        if (arguments != null) {
            msg = MessageFormat.format(msg, arguments);
        }
        return msg;
    }

    /**
     * Reads a system property with privilege
     *
     * @param propName the name of the property
     * @return the value of the property
     */
    public static String getSystemProperty(final String propName) {
        return
        AccessController.doPrivileged(
                (PrivilegedAction<String>) () -> (String)System.getProperty(propName));
    }

    /**
     * Reads a system property with privilege
     *
     * @param propName the name of the property
     * @return the value of the property
     */
    public static String getSystemProperty(final String propName, String defValue) {
        String value = getSystemProperty(propName);
        if (value == null) {
            return defValue;
        }
        return value;
    }

    /**
     * Reads a system property with specified type.
     *
     * @param <T> the type of the property value
     * @param type the type of the property value
     * @param propName the name of the property
     * @param defValue the default value
     * @return the value of the property, or the default value if no system
     * property is found
     */
    public static <T> T getSystemProperty(Class<T> type, String propName, String defValue) {
        String value = getSystemProperty(propName);
        if (value == null) {
            value = defValue;
        }
        if (Integer.class.isAssignableFrom(type)) {
            return type.cast(Integer.parseInt(value));
        } else if (Boolean.class.isAssignableFrom(type)) {
            return type.cast(Boolean.parseBoolean(value));
        }
        return type.cast(value);
    }

    /**
     * Reads JAXP system property in this order: system property,
     * $java.home/conf/jaxp.properties if the system property is not specified
     *
     * @param <T> the type of the property value
     * @param type the type of the property value
     * @param propName the name of the property
     * @param defValue the default value
     * @return the value of the property, or the default value if no system
     * property is found
     */
    public static <T> T getJAXPSystemProperty(Class<T> type, String propName, String defValue) {
        String value = getJAXPSystemProperty(propName);
        if (value == null) {
            value = defValue;
        }
        if (Integer.class.isAssignableFrom(type)) {
            return type.cast(Integer.parseInt(value));
        } else if (Boolean.class.isAssignableFrom(type)) {
            return type.cast(Boolean.parseBoolean(value));
        }
        return type.cast(value);
    }

    /**
     * Reads JAXP system property in this order: system property,
     * $java.home/conf/jaxp.properties if the system property is not specified
     *
     * @param propName the name of the property
     * @return the value of the property
     */
    public static String getJAXPSystemProperty(String propName) {
        String value = getSystemProperty(propName);
        if (value == null) {
            value = readJAXPProperty(propName);
        }
        return value;
    }

    /**
     * Reads the specified property from $java.home/conf/jaxp.properties
     *
     * @param propName the name of the property
     * @return the value of the property
     */
    public static String readJAXPProperty(String propName) {
        String value = null;
        InputStream is = null;
        try {
            if (firstTime) {
                synchronized (cacheProps) {
                    if (firstTime) {
                        String configFile = getSystemProperty("java.home") + File.separator
                                + "conf" + File.separator + "jaxp.properties";
                        File f = new File(configFile);
                        if (isFileExists(f)) {
                            is = getFileInputStream(f);
                            cacheProps.load(is);
                        }
                        firstTime = false;
                    }
                }
            }
            value = cacheProps.getProperty(propName);

        } catch (IOException ex) {
        } finally {
            if (is != null) {
                try {
                    is.close();
                } catch (IOException ex) {}
            }
        }

        return value;
    }

    /**
     * Tests whether the file denoted by this abstract pathname is a directory.
     * @param f the file to be tested
     * @return true if it is a directory, false otherwise
     */
    public static boolean isDirectory(final File f) {
        return (AccessController.doPrivileged((PrivilegedAction<Boolean>) ()
                -> f.isDirectory()));
    }

    /**
     * Tests whether the file exists.
     *
     * @param f the file to be tested
     * @return true if the file exists, false otherwise
     */
    public static boolean isFileExists(final File f) {
        return (AccessController.doPrivileged((PrivilegedAction<Boolean>) ()
                -> f.exists()));
    }

    public static FileInputStream getFileInputStream(final File file)
            throws FileNotFoundException {
        try {
            return AccessController.doPrivileged((PrivilegedExceptionAction<FileInputStream>) ()
                    -> new FileInputStream(file));
        } catch (PrivilegedActionException e) {
            throw (FileNotFoundException) e.getException();
        }
    }

    /**
     * Gets a resource bundle using the specified base name, the default locale, and the caller's class loader.
     * @param bundle the base name of the resource bundle, a fully qualified class name
     * @return a resource bundle for the given base name and the default locale
     */
    public static ResourceBundle getResourceBundle(String bundle) {
        return getResourceBundle(bundle, Locale.getDefault());
    }

    /**
     * Gets a resource bundle using the specified base name and locale, and the caller's class loader.
     * @param bundle the base name of the resource bundle, a fully qualified class name
     * @param locale the locale for which a resource bundle is desired
     * @return a resource bundle for the given base name and locale
     */
    public static ResourceBundle getResourceBundle(final String bundle, final Locale locale) {
        return AccessController.doPrivileged((PrivilegedAction<ResourceBundle>) () -> {
            try {
                return ResourceBundle.getBundle(bundle, locale);
            } catch (MissingResourceException e) {
                try {
                    return ResourceBundle.getBundle(bundle, new Locale("en", "US"));
                } catch (MissingResourceException e2) {
                    throw new MissingResourceException(
                            "Could not load any resource bundle by " + bundle, bundle, "");
                }
            }
        });
    }
}
