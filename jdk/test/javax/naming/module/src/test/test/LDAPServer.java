/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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

package test;

import java.io.*;
import java.nio.file.*;
import java.math.BigInteger;
import java.net.*;
import java.util.*;
import java.util.regex.*;
import javax.xml.bind.DatatypeConverter;

/*
 * A dummy LDAP server.
 *
 * Loads a sequence of LDAP messages from a capture file into its cache.
 * It listens for LDAP requests, finds a match in its cache and sends the
 * corresponding LDAP responses.
 *
 * The capture file contains an LDAP protocol exchange in the hexadecimal
 * dump format emitted by sun.misc.HexDumpEncoder:
 *
 * xxxx: 00 11 22 33 44 55 66 77   88 99 aa bb cc dd ee ff  ................
 *
 * Typically, LDAP protocol exchange is generated by running the LDAP client
 * application program against a real LDAP server and setting the JNDI/LDAP
 * environment property: com.sun.jndi.ldap.trace.ber to activate LDAP message
 * tracing.
 */
public class LDAPServer {

    /*
     * A cache of LDAP requests and responses.
     * Messages with the same ID are stored in a list.
     * The first element in the list is the LDAP request,
     * the remaining elements are the LDAP responses.
     */
    private final Map<Integer,List<byte[]>> cache = new HashMap<>();

    public LDAPServer(ServerSocket serverSocket, String filename)
        throws Exception {

        System.out.println("LDAPServer: Loading LDAP cache from: " + filename);
        loadCaptureFile(filename);

        System.out.println("LDAPServer: listening on port " +
            serverSocket.getLocalPort());

        try (Socket clientSocket = serverSocket.accept();
            OutputStream out = clientSocket.getOutputStream();
            InputStream in = clientSocket.getInputStream();) {

            byte[] inBuffer = new byte[8192];
            int count;

            while ((count = in.read(inBuffer)) > 0) {
                byte[] request = Arrays.copyOf(inBuffer, count);
                int[] ids = getIDs(request);
                int messageID = ids[0];
                String operation = getOperation(ids[1]);
                System.out.println("\nLDAPServer: received LDAP " + operation +
                    "  [message ID " + messageID + "]");

                List<byte[]> encodings = cache.get(messageID);
                if (encodings == null ||
                    (!Arrays.equals(request, encodings.get(0)))) {
                    throw new Exception(
                        "LDAPServer: ERROR: received an LDAP " + operation +
                        " (ID=" + messageID + ") not present in cache");
                }

                for (int i = 1; i < encodings.size(); i++) {
                    // skip the request (at index 0)
                    byte[] response = encodings.get(i);
                    out.write(response, 0, response.length);
                    ids = getIDs(response);
                    System.out.println("\nLDAPServer: Sent LDAP " +
                        getOperation(ids[1]) + "  [message ID " + ids[0] + "]");
                }
            }
        } catch (IOException e) {
            System.out.println("LDAPServer: ERROR: " + e);
            throw e;
        }

        System.out.println("\n[LDAP server exited normally]");
    }

    /*
     * Load a capture file containing an LDAP protocol exchange in the
     * hexadecimal dump format emitted by sun.misc.HexDumpEncoder:
     *
     * xxxx: 00 11 22 33 44 55 66 77   88 99 aa bb cc dd ee ff  ................
     */
    private void loadCaptureFile(String filename) throws IOException {
        StringBuilder hexString = new StringBuilder();
        String pattern = "(....): (..) (..) (..) (..) (..) (..) (..) (..)   (..) (..) (..) (..) (..) (..) (..) (..).*";

        try (Scanner fileScanner =  new Scanner(Paths.get(filename))) {
            while (fileScanner.hasNextLine()){

                try (Scanner lineScanner =
                    new Scanner(fileScanner.nextLine())) {
                    if (lineScanner.findInLine(pattern) == null) {
                        continue;
                    }
                    MatchResult result = lineScanner.match();
                    for (int i = 1; i <= result.groupCount(); i++) {
                        String digits = result.group(i);
                        if (digits.length() == 4) {
                            if (digits.equals("0000")) { // start-of-message
                                if (hexString.length() > 0) {
                                    addToCache(hexString.toString());
                                    hexString = new StringBuilder();
                                }
                            }
                            continue;
                        } else if (digits.equals("  ")) { // short message
                            continue;
                        }
                        hexString.append(digits);
                    }
                }
            }
        }
        addToCache(hexString.toString());
    }

    /*
     * Add an LDAP encoding to the cache (by messageID key).
     */
    private void addToCache(String hexString) throws IOException {
        byte[] encoding = DatatypeConverter.parseHexBinary(hexString);
        int[] ids = getIDs(encoding);
        int messageID = ids[0];
        List<byte[]> encodings = cache.get(messageID);
        if (encodings == null) {
            encodings = new ArrayList<>();
        }
        System.out.println("    adding LDAP " + getOperation(ids[1]) +
            " with message ID " + messageID + " to the cache");
        encodings.add(encoding);
        cache.put(messageID, encodings);
    }

    /*
     * Extracts the message ID and operation ID from an LDAP protocol encoding
     * and returns them in a 2-element array of integers.
     */
    private static int[] getIDs(byte[] encoding) throws IOException {
        if (encoding[0] != 0x30) {
            throw new IOException("Error: bad LDAP encoding in capture file: " +
                "expected ASN.1 SEQUENCE tag (0x30), encountered " +
                encoding[0]);
        }

        int index = 2;
        if ((encoding[1] & 0x80) == 0x80) {
            index += (encoding[1] & 0x0F);
        }

        if (encoding[index] != 0x02) {
            throw new IOException("Error: bad LDAP encoding in capture file: " +
                "expected ASN.1 INTEGER tag (0x02), encountered " +
                encoding[index]);
        }
        int length = encoding[index + 1];
        index += 2;
        int messageID =
            new BigInteger(1,
                Arrays.copyOfRange(encoding, index, index + length)).intValue();
        index += length;
        int operationID = encoding[index];

        return new int[]{messageID, operationID};
    }

    /*
     * Maps an LDAP operation ID to a string description
     */
    private static String getOperation(int operationID) {
        switch (operationID) {
        case 0x60:
            return "BindRequest";       // [APPLICATION 0]
        case 0x61:
            return "BindResponse";      // [APPLICATION 1]
        case 0x42:
            return "UnbindRequest";     // [APPLICATION 2]
        case 0x63:
            return "SearchRequest";     // [APPLICATION 3]
        case 0x64:
            return "SearchResultEntry"; // [APPLICATION 4]
        case 0x65:
            return "SearchResultDone";  // [APPLICATION 5]
        case 0x66:
            return "ModifyRequest";     // [APPLICATION 6]
        case 0x67:
            return "ModifyResponse";    // [APPLICATION 7]
        case 0x68:
            return "AddRequest";        // [APPLICATION 8]
        case 0x69:
            return "AddResponse";       // [APPLICATION 9]
        case 0x4A:
            return "DeleteRequest";     // [APPLICATION 10]
        case 0x6B:
            return "DeleteResponse";    // [APPLICATION 11]
        case 0x6C:
            return "ModifyDNRequest";   // [APPLICATION 12]
        case 0x6D:
            return "ModifyDNResponse";  // [APPLICATION 13]
        case 0x6E:
            return "CompareRequest";    // [APPLICATION 14]
        case 0x6F:
            return "CompareResponse";   // [APPLICATION 15]
        case 0x50:
            return "AbandonRequest";    // [APPLICATION 16]
        case 0x73:
            return "SearchResultReference";  // [APPLICATION 19]
        case 0x77:
            return "ExtendedRequest";   // [APPLICATION 23]
        case 0x78:
            return "ExtendedResponse";  // [APPLICATION 24]
        case 0x79:
            return "IntermediateResponse";  // [APPLICATION 25]
        default:
            return "Unknown";
        }
    }
}
