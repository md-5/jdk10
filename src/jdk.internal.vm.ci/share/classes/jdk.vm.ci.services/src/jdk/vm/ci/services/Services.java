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
 */
package jdk.vm.ci.services;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Formatter;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.ServiceLoader;
import java.util.Set;

import jdk.internal.misc.VM;
import jdk.internal.reflect.Reflection;

/**
 * Provides utilities needed by JVMCI clients.
 */
public final class Services {

    // This class must be compilable and executable on JDK 8 since it's used in annotation
    // processors while building JDK 9 so use of API added in JDK 9 is made via reflection.

    /**
     * Guards code that should be run when building an JVMCI shared library but should be excluded
     * from (being compiled into) the library. Such code must be directly guarded by an {@code if}
     * statement on this field - the guard cannot be behind a method call.
     */
    public static final boolean IS_BUILDING_NATIVE_IMAGE = Boolean.parseBoolean(VM.getSavedProperty("jdk.vm.ci.services.aot"));

    /**
     * Guards code that should only be run in a JVMCI shared library. Such code must be directly
     * guarded by an {@code if} statement on this field - the guard cannot be behind a method call.
     *
     * The value of this field in a JVMCI shared library runtime must be {@code true}.
     */
    public static final boolean IS_IN_NATIVE_IMAGE;
    static {
        /*
         * Prevents javac from constant folding use of this field. It is set to true by the process
         * that builds the shared library.
         */
        IS_IN_NATIVE_IMAGE = false;
    }

    private Services() {
    }

    private static volatile Map<String, String> savedProperties = VM.getSavedProperties();
    static final boolean JVMCI_ENABLED = Boolean.parseBoolean(savedProperties.get("jdk.internal.vm.ci.enabled"));

    /**
     * Checks that JVMCI is enabled in the VM and throws an error if it isn't.
     */
    static void checkJVMCIEnabled() {
        if (!JVMCI_ENABLED) {
            throw new Error("The EnableJVMCI VM option must be true (i.e., -XX:+EnableJVMCI) to use JVMCI");
        }
    }

    /**
     * Gets an unmodifiable copy of the system properties saved when {@link System} is initialized.
     */
    public static Map<String, String> getSavedProperties() {
        checkJVMCIEnabled();
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new JVMCIPermission());
        }
        return savedProperties;
    }

    /**
     * Helper method equivalent to {@link #getSavedProperties()}{@code .getOrDefault(name, def)}.
     */
    public static String getSavedProperty(String name, String def) {
        return Services.getSavedProperties().getOrDefault(name, def);
    }

    /**
     * Helper method equivalent to {@link #getSavedProperties()}{@code .get(name)}.
     */
    public static String getSavedProperty(String name) {
        return Services.getSavedProperties().get(name);
    }

    /**
     * Causes the JVMCI subsystem to be initialized if it isn't already initialized.
     */
    public static void initializeJVMCI() {
        checkJVMCIEnabled();
        try {
            Class.forName("jdk.vm.ci.runtime.JVMCI");
        } catch (ClassNotFoundException e) {
            throw new InternalError(e);
        }
    }

    private static boolean jvmciEnabled = true;

    /**
     * When {@code -XX:-UseJVMCIClassLoader} is in use, JVMCI classes are loaded via the boot class
     * loader. When {@code null} is the second argument to
     * {@link ServiceLoader#load(Class, ClassLoader)}, service lookup will use the system class
     * loader and thus find application classes which violates the API of {@link #load} and
     * {@link #loadSingle}. To avoid this, a class loader that simply delegates to the boot class
     * loader is used.
     */
    static class LazyBootClassPath {
        static final ClassLoader bootClassPath = new ClassLoader(null) {
        };
    }

    private static ClassLoader findBootClassLoaderChild(ClassLoader start) {
        ClassLoader cl = start;
        while (cl.getParent() != null) {
            cl = cl.getParent();
        }
        return cl;
    }

    private static final Map<Class<?>, List<?>> servicesCache = IS_BUILDING_NATIVE_IMAGE ? new HashMap<>() : null;

    @SuppressWarnings("unchecked")
    private static <S> Iterable<S> load0(Class<S> service) {
        if (IS_IN_NATIVE_IMAGE || IS_BUILDING_NATIVE_IMAGE) {
            List<?> list = servicesCache.get(service);
            if (list != null) {
                return (Iterable<S>) list;
            }
            if (IS_IN_NATIVE_IMAGE) {
                throw new InternalError(String.format("No %s providers found when building native image", service.getName()));
            }
        }

        Iterable<S> providers = Collections.emptyList();
        if (jvmciEnabled) {
            ClassLoader cl = null;
            try {
                cl = getJVMCIClassLoader();
                if (cl == null) {
                    cl = LazyBootClassPath.bootClassPath;
                    // JVMCI classes are loaded via the boot class loader.
                    // If we use null as the second argument to ServiceLoader.load,
                    // service loading will use the system class loader
                    // and find classes on the application class path. Since we
                    // don't want this, we use a loader that is as close to the
                    // boot class loader as possible (since it is impossible
                    // to force service loading to use only the boot class loader).
                    cl = findBootClassLoaderChild(ClassLoader.getSystemClassLoader());
                }
                providers = ServiceLoader.load(service, cl);
            } catch (UnsatisfiedLinkError e) {
                jvmciEnabled = false;
            } catch (InternalError e) {
                if (e.getMessage().equals("JVMCI is not enabled")) {
                    jvmciEnabled = false;
                } else {
                    throw e;
                }
            }
        }
        if (IS_BUILDING_NATIVE_IMAGE) {
            synchronized (servicesCache) {
                ArrayList<S> providersList = new ArrayList<>();
                for (S provider : providers) {
                    providersList.add(provider);
                }
                servicesCache.put(service, providersList);
                providers = providersList;
            }
        }
        return providers;
    }

    /**
     * Opens all JVMCI packages to {@code otherModule}.
     */
    static void openJVMCITo(Module otherModule) {
        Module jvmci = Services.class.getModule();
        if (jvmci != otherModule) {
            Set<String> packages = jvmci.getPackages();
            for (String pkg : packages) {
                boolean opened = jvmci.isOpen(pkg, otherModule);
                if (!opened) {
                    jvmci.addOpens(pkg, otherModule);
                }
            }
        }
    }

    /**
     * Gets an {@link Iterable} of the JVMCI providers available for a given service.
     *
     * @throws SecurityException if a security manager is present and it denies <tt>
     *             {@link RuntimePermission}("jvmci")</tt>
     */
    public static <S> Iterable<S> load(Class<S> service) {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new JVMCIPermission());
        }
        return load0(service);
    }

    /**
     * Gets the JVMCI provider for a given service for which at most one provider must be available.
     *
     * @param service the service whose provider is being requested
     * @param required specifies if an {@link InternalError} should be thrown if no provider of
     *            {@code service} is available
     * @throws SecurityException if a security manager is present and it denies <tt>
     *             {@link RuntimePermission}("jvmci")</tt>
     */
    public static <S> S loadSingle(Class<S> service, boolean required) {
        SecurityManager sm = System.getSecurityManager();
        if (sm != null) {
            sm.checkPermission(new JVMCIPermission());
        }
        Iterable<S> providers = load0(service);

        S singleProvider = null;
        for (S provider : providers) {
            if (singleProvider != null) {
                throw new InternalError(String.format("Multiple %s providers found: %s, %s", service.getName(), singleProvider.getClass().getName(), provider.getClass().getName()));
            }
            singleProvider = provider;
        }
        if (singleProvider == null && required) {
            String javaHome = Services.getSavedProperty("java.home");
            String vmName = Services.getSavedProperty("java.vm.name");
            Formatter errorMessage = new Formatter();
            errorMessage.format("The VM does not expose required service %s.%n", service.getName());
            errorMessage.format("Currently used Java home directory is %s.%n", javaHome);
            errorMessage.format("Currently used VM configuration is: %s", vmName);
            throw new UnsupportedOperationException(errorMessage.toString());
        }
        return singleProvider;
    }

    static {
        Reflection.registerMethodsToFilter(Services.class, Set.of("getJVMCIClassLoader"));
    }

    /**
     * Gets the JVMCI class loader.
     *
     * @throws InternalError with the {@linkplain Throwable#getMessage() message}
     *             {@code "JVMCI is not enabled"} iff JVMCI is not enabled
     */
    private static ClassLoader getJVMCIClassLoader() {
        if (IS_IN_NATIVE_IMAGE) {
            return null;
        }
        return ClassLoader.getSystemClassLoader();
    }

    /**
     * Serializes the {@linkplain #getSavedProperties() saved system properties} to a byte array for
     * the purpose of {@linkplain #initializeSavedProperties(byte[]) initializing} the initial
     * properties in the JVMCI shared library.
     */
    @VMEntryPoint
    private static byte[] serializeSavedProperties() throws IOException {
        if (IS_IN_NATIVE_IMAGE) {
            throw new InternalError("Can only serialize saved properties in HotSpot runtime");
        }
        Map<String, String> props = Services.getSavedProperties();

        // Compute size of output on the assumption that
        // all system properties have ASCII names and values
        int estimate = 4;
        for (Map.Entry<String, String> e : props.entrySet()) {
            String name = e.getKey();
            String value = e.getValue();
            estimate += (2 + (name.length())) + (2 + (value.length()));
        }

        ByteArrayOutputStream baos = new ByteArrayOutputStream(estimate);
        DataOutputStream out = new DataOutputStream(baos);
        out.writeInt(props.size());
        for (Map.Entry<String, String> e : props.entrySet()) {
            String name = e.getKey();
            String value = e.getValue();
            out.writeUTF(name);
            out.writeUTF(value);
        }
        return baos.toByteArray();
    }

    /**
     * Initialized the {@linkplain #getSavedProperties() saved system properties} in the JVMCI
     * shared library from the {@linkplain #serializeSavedProperties() serialized saved properties}
     * in the HotSpot runtime.
     */
    @VMEntryPoint
    private static void initializeSavedProperties(byte[] serializedProperties) throws IOException {
        if (!IS_IN_NATIVE_IMAGE) {
            throw new InternalError("Can only initialize saved properties in JVMCI shared library runtime");
        }
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(serializedProperties));
        Map<String, String> props = new HashMap<>(in.readInt());
        while (in.available() != 0) {
            String name = in.readUTF();
            String value = in.readUTF();
            props.put(name, value);
        }
        savedProperties = Collections.unmodifiableMap(props);
    }
}
