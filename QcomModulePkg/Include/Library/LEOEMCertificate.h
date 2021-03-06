/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __LE_OEM_CERTIFICATE_H
#define __LE_OEM_CERTIFICATE_H

/*
    Instructions to generate private.key and X509 certificate

A) Generate a sha256 self-signed X509 certificate and a 2048 bits
   RSA private key
$ openssl req -x509 -sha256 -nodes -days 365 -newkey rsa:2048 -keyout
                                                   qtipri.key -out qti.crt.pem

B) convert private key from PKCS#8 to PKCS#1 format, save it as qti.key.
$ openssl rsa -in qtipri.key -out qti.key

C) This private key "qti.key" need to be saved under folder
   "poky/recipes-kernel/linux-quic/kernel/" and
   "poky/recipes-kernel/linux-msm-4.9/kernel/", used to generate bootimage
   signature.

D) convert certificate from PEM to DER format
$ openssl x509 -outform der -in qti.crt.pem -out qti.crt.der

E) open qti.crt.der file in a HEX editor, and then put HEX numbers into array
    LeOemCertificate[] in LEOEMCertificate.h

*/

static CONST UINT8 LeOemCertificate[] = {
    0x30, 0x82, 0x03, 0x5D, 0x30, 0x82, 0x02, 0x45, 0xA0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x09, 0x00, 0xFA, 0xCD, 0x18, 0x11, 0xED, 0xD3, 0x4D, 0xC8,
    0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01,
    0x0B, 0x05, 0x00, 0x30, 0x45, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55,
    0x04, 0x06, 0x13, 0x02, 0x41, 0x55, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03,
    0x55, 0x04, 0x08, 0x0C, 0x0A, 0x53, 0x6F, 0x6D, 0x65, 0x2D, 0x53, 0x74,
    0x61, 0x74, 0x65, 0x31, 0x21, 0x30, 0x1F, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x0C, 0x18, 0x49, 0x6E, 0x74, 0x65, 0x72, 0x6E, 0x65, 0x74, 0x20, 0x57,
    0x69, 0x64, 0x67, 0x69, 0x74, 0x73, 0x20, 0x50, 0x74, 0x79, 0x20, 0x4C,
    0x74, 0x64, 0x30, 0x1E, 0x17, 0x0D, 0x31, 0x37, 0x30, 0x38, 0x30, 0x35,
    0x30, 0x36, 0x33, 0x39, 0x32, 0x32, 0x5A, 0x17, 0x0D, 0x31, 0x38, 0x30,
    0x38, 0x30, 0x35, 0x30, 0x36, 0x33, 0x39, 0x32, 0x32, 0x5A, 0x30, 0x45,
    0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x41,
    0x55, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0C, 0x0A,
    0x53, 0x6F, 0x6D, 0x65, 0x2D, 0x53, 0x74, 0x61, 0x74, 0x65, 0x31, 0x21,
    0x30, 0x1F, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x18, 0x49, 0x6E, 0x74,
    0x65, 0x72, 0x6E, 0x65, 0x74, 0x20, 0x57, 0x69, 0x64, 0x67, 0x69, 0x74,
    0x73, 0x20, 0x50, 0x74, 0x79, 0x20, 0x4C, 0x74, 0x64, 0x30, 0x82, 0x01,
    0x22, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01,
    0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0F, 0x00, 0x30, 0x82, 0x01,
    0x0A, 0x02, 0x82, 0x01, 0x01, 0x00, 0xA6, 0xC6, 0x48, 0xE4, 0xF6, 0x8B,
    0x42, 0x2A, 0xFC, 0xB0, 0x4A, 0x7A, 0x0F, 0x1A, 0x5E, 0x40, 0x78, 0x8B,
    0xBD, 0x30, 0xBD, 0xA1, 0xBF, 0xDC, 0xB4, 0x53, 0x48, 0x31, 0x42, 0x11,
    0xCD, 0x83, 0x70, 0x48, 0x75, 0x58, 0x00, 0x82, 0x50, 0x43, 0x4B, 0xA9,
    0xCF, 0xEE, 0x81, 0x8C, 0xA4, 0x57, 0xA2, 0x55, 0xB2, 0x84, 0x5C, 0xAA,
    0xC9, 0x3F, 0xBA, 0xDE, 0x4C, 0x1B, 0x54, 0xDE, 0xF0, 0x06, 0x3E, 0x3F,
    0x3B, 0x5F, 0x6D, 0x78, 0xA3, 0xEB, 0x8F, 0x6F, 0xAC, 0xFC, 0x2F, 0x1F,
    0xFC, 0x20, 0x58, 0xDB, 0x5D, 0x86, 0x04, 0xE1, 0x45, 0xF2, 0x98, 0x9A,
    0x41, 0x00, 0x46, 0x45, 0x6C, 0x05, 0x5C, 0xE7, 0x7C, 0xC0, 0x25, 0x6F,
    0x22, 0xE2, 0xDE, 0x76, 0xA1, 0xAC, 0xC8, 0x82, 0xF7, 0x15, 0x5B, 0x0C,
    0xFF, 0xC1, 0xB1, 0xA8, 0x17, 0xB4, 0xFC, 0x0B, 0x98, 0x89, 0xBA, 0x2C,
    0xFC, 0xAD, 0xDB, 0x03, 0xD8, 0x48, 0xDD, 0x38, 0x1B, 0xC6, 0x6E, 0x1A,
    0x6D, 0xD9, 0x8B, 0x19, 0x73, 0x79, 0x33, 0xFC, 0x7C, 0x5F, 0x9B, 0x9C,
    0x91, 0x4F, 0xDC, 0x65, 0x1B, 0x1C, 0xBF, 0x66, 0xF3, 0xDA, 0x79, 0x7C,
    0x7F, 0x19, 0xE9, 0xAF, 0x75, 0x34, 0xB2, 0xA0, 0x2F, 0xA7, 0xF4, 0x2B,
    0x9C, 0x4D, 0xF5, 0xC8, 0x68, 0xC5, 0xCB, 0x89, 0x6E, 0xA0, 0x72, 0x8E,
    0x14, 0x22, 0x4F, 0x1A, 0xD8, 0x32, 0x04, 0x93, 0x91, 0x54, 0xD2, 0x54,
    0xDA, 0x5E, 0x62, 0xAB, 0xB6, 0x53, 0x56, 0xAA, 0x8E, 0x17, 0x6D, 0xE7,
    0x20, 0x28, 0xBE, 0x50, 0x68, 0x04, 0x8D, 0x38, 0xF6, 0x6F, 0x3E, 0x32,
    0x6F, 0x61, 0x47, 0xBA, 0xE7, 0x5E, 0xDD, 0x33, 0x5A, 0xD9, 0x97, 0x8E,
    0xD3, 0x9D, 0x0A, 0x78, 0x4B, 0x31, 0xFE, 0x74, 0x41, 0xC8, 0x7D, 0x77,
    0x1D, 0x8B, 0x63, 0x67, 0x7C, 0x2D, 0xE1, 0xD3, 0xF1, 0x21, 0x02, 0x03,
    0x01, 0x00, 0x01, 0xA3, 0x50, 0x30, 0x4E, 0x30, 0x1D, 0x06, 0x03, 0x55,
    0x1D, 0x0E, 0x04, 0x16, 0x04, 0x14, 0x8E, 0x52, 0xC7, 0xA0, 0x78, 0xAD,
    0xCF, 0xE3, 0x9D, 0xF9, 0xD5, 0x26, 0xE0, 0xDB, 0x80, 0x1A, 0xB8, 0xE2,
    0x2D, 0xD1, 0x30, 0x1F, 0x06, 0x03, 0x55, 0x1D, 0x23, 0x04, 0x18, 0x30,
    0x16, 0x80, 0x14, 0x8E, 0x52, 0xC7, 0xA0, 0x78, 0xAD, 0xCF, 0xE3, 0x9D,
    0xF9, 0xD5, 0x26, 0xE0, 0xDB, 0x80, 0x1A, 0xB8, 0xE2, 0x2D, 0xD1, 0x30,
    0x0C, 0x06, 0x03, 0x55, 0x1D, 0x13, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01,
    0xFF, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01,
    0x01, 0x0B, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x65, 0x5A, 0x8E,
    0x9F, 0xC3, 0x52, 0xC9, 0x52, 0xCE, 0x6B, 0xF0, 0xAA, 0x8D, 0x78, 0xA0,
    0x85, 0xB3, 0x0E, 0xEB, 0x8B, 0x17, 0xC2, 0xB4, 0xFA, 0xF4, 0x20, 0x52,
    0x98, 0xCC, 0x1D, 0x45, 0x6F, 0x83, 0x9F, 0xCF, 0xA1, 0x4D, 0x91, 0xBA,
    0xE2, 0x29, 0xC2, 0xAA, 0x9A, 0x07, 0xE0, 0xC3, 0xCE, 0x39, 0xDB, 0x5F,
    0x5F, 0x96, 0xF1, 0x77, 0x8F, 0x73, 0x6D, 0xDD, 0x83, 0x0C, 0x1D, 0x8C,
    0xCC, 0xC0, 0xC9, 0x65, 0x64, 0x67, 0x8F, 0x90, 0x3D, 0x53, 0x24, 0xED,
    0x49, 0x18, 0x8A, 0x06, 0xDC, 0x96, 0xC1, 0x40, 0x9C, 0x11, 0x41, 0x70,
    0x91, 0x7D, 0x7C, 0xFD, 0x63, 0x81, 0x73, 0x9F, 0xD9, 0x50, 0xF2, 0xA5,
    0x86, 0x6A, 0x31, 0xA5, 0x43, 0x66, 0x7D, 0xB2, 0x88, 0x4C, 0x96, 0x08,
    0x8B, 0x36, 0xA8, 0x21, 0x47, 0x17, 0xD4, 0x1D, 0xDE, 0xD9, 0x58, 0x45,
    0x36, 0x1B, 0x2A, 0x82, 0x46, 0x40, 0x6F, 0x16, 0x41, 0xFB, 0xF2, 0x17,
    0x49, 0x2A, 0x13, 0x8E, 0xCC, 0xB8, 0x5B, 0xD3, 0xDA, 0xCA, 0xB3, 0x48,
    0xFC, 0xC7, 0x5C, 0x3B, 0x2B, 0xAC, 0x56, 0x41, 0x5F, 0x2B, 0x61, 0x15,
    0x08, 0x9D, 0xDA, 0x72, 0x13, 0x43, 0x51, 0x00, 0xCE, 0xA1, 0x4C, 0x98,
    0xC6, 0xC3, 0xD4, 0xEE, 0xEF, 0x51, 0xAF, 0xFC, 0x03, 0x86, 0x77, 0xB7,
    0xB9, 0x14, 0x8A, 0x79, 0xCB, 0xC8, 0xA9, 0x0D, 0x82, 0xE3, 0xED, 0x05,
    0x99, 0x10, 0xFD, 0x80, 0x0D, 0x24, 0x72, 0x1F, 0x1C, 0xB0, 0xCA, 0x64,
    0x1D, 0x10, 0x5D, 0x3E, 0x48, 0x5B, 0x54, 0x56, 0x4E, 0xEE, 0x5D, 0x02,
    0xEC, 0xEE, 0x0B, 0x2E, 0x77, 0x74, 0xB2, 0x8C, 0xD5, 0x7C, 0xF3, 0xA4,
    0x0E, 0x1A, 0x48, 0x04, 0x81, 0x32, 0xF1, 0x39, 0x9B, 0x57, 0xC9, 0xBB,
    0xFF, 0x15, 0xB6, 0xE5, 0x20, 0x0D, 0x03, 0x60, 0xBD, 0x3F, 0x29, 0x03,
    0xEF
};

#endif
