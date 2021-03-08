// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.payments;

import org.chromium.base.annotations.JNINamespace;
import org.chromium.content_public.browser.WebContents;

/** SSL validity checker. */
@JNINamespace("payments")
public class SslValidityChecker {
    /**
     * Returns true for web contents with a valid SSL certificate.
     *
     * @param webContents The web contents to check.
     * @return Whether the web contents have a valid SSL certificate.
     */
    public static boolean isSslCertificateValid(WebContents webContents) {
        return nativeIsSslCertificateValid(webContents);
    }

    /**
     * Returns true for web contents that is allowed in a payment handler window.
     *
     * @param webContents The web contents to check.
     * @return Whether the web contents is a allowed in a payment handler window.
     */
    public static boolean isValidPageInPaymentHandlerWindow(WebContents webContents) {
        return nativeIsValidPageInPaymentHandlerWindow(webContents);
    }

    private SslValidityChecker() {}

    private static native boolean nativeIsSslCertificateValid(WebContents webContents);
    private static native boolean nativeIsValidPageInPaymentHandlerWindow(WebContents webContents);
}