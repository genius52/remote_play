// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.chrome.browser.toolbar;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.omnibox.LocationBarLayout;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.security_state.ConnectionSecurityLevel;

/**
 * Unit tests for {@link LocationBarLayout} class.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public final class ToolbarSecurityIconTest {
    private static final boolean IS_SMALL_DEVICE = true;
    private static final boolean IS_OFFLINE_PAGE = true;
    private static final boolean IS_PREVIEW = true;
    private static final int[] SECURITY_LEVELS = new int[] {ConnectionSecurityLevel.NONE,
            ConnectionSecurityLevel.HTTP_SHOW_WARNING, ConnectionSecurityLevel.DANGEROUS,
            ConnectionSecurityLevel.SECURE, ConnectionSecurityLevel.EV_SECURE};

    @Mock
    private Tab mTab;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
    }

    @Test
    public void testGetSecurityLevel() {
        assertEquals(ConnectionSecurityLevel.NONE,
                LocationBarModel.getSecurityLevel(null, !IS_OFFLINE_PAGE, null));
        assertEquals(ConnectionSecurityLevel.NONE,
                LocationBarModel.getSecurityLevel(null, IS_OFFLINE_PAGE, null));
        assertEquals(ConnectionSecurityLevel.NONE,
                LocationBarModel.getSecurityLevel(mTab, IS_OFFLINE_PAGE, null));

        for (int securityLevel : SECURITY_LEVELS) {
            when(mTab.getSecurityLevel()).thenReturn(securityLevel);
            assertEquals("Wrong security level returned for " + securityLevel, securityLevel,
                    LocationBarModel.getSecurityLevel(mTab, !IS_OFFLINE_PAGE, null));
        }

        when(mTab.getSecurityLevel()).thenReturn(ConnectionSecurityLevel.SECURE);
        assertEquals("Wrong security level returned for HTTPS publisher URL",
                ConnectionSecurityLevel.SECURE,
                LocationBarModel.getSecurityLevel(mTab, !IS_OFFLINE_PAGE, "https://example.com"));
        assertEquals("Wrong security level returned for HTTP publisher URL",
                ConnectionSecurityLevel.HTTP_SHOW_WARNING,
                LocationBarModel.getSecurityLevel(mTab, !IS_OFFLINE_PAGE, "http://example.com"));

        when(mTab.getSecurityLevel()).thenReturn(ConnectionSecurityLevel.DANGEROUS);
        assertEquals("Wrong security level returned for publisher URL on insecure page",
                ConnectionSecurityLevel.DANGEROUS,
                LocationBarModel.getSecurityLevel(mTab, !IS_OFFLINE_PAGE, null));
    }

    @Test
    public void testGetSecurityIconResource() {
        for (int securityLevel : SECURITY_LEVELS) {
            assertEquals("Wrong phone resource for security level " + securityLevel,
                    R.drawable.ic_offline_pin_24dp,
                    LocationBarModel.getSecurityIconResource(
                            securityLevel, IS_SMALL_DEVICE, IS_OFFLINE_PAGE, !IS_PREVIEW));
            assertEquals("Wrong tablet resource for security level " + securityLevel,
                    R.drawable.ic_offline_pin_24dp,
                    LocationBarModel.getSecurityIconResource(
                            securityLevel, !IS_SMALL_DEVICE, IS_OFFLINE_PAGE, !IS_PREVIEW));
            assertEquals("Wrong phone resource for security level " + securityLevel,
                    R.drawable.preview_pin_round,
                    LocationBarModel.getSecurityIconResource(
                            securityLevel, IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, IS_PREVIEW));
            assertEquals("Wrong tablet resource for security level " + securityLevel,
                    R.drawable.preview_pin_round,
                    LocationBarModel.getSecurityIconResource(
                            securityLevel, !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, IS_PREVIEW));
        }

        assertEquals(0,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.NONE,
                        IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_info,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.NONE,
                        !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));

        assertEquals(R.drawable.omnibox_info,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.HTTP_SHOW_WARNING,
                        IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_info,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.HTTP_SHOW_WARNING,
                        !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));

        assertEquals(R.drawable.omnibox_https_invalid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.DANGEROUS,
                        IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_https_invalid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.DANGEROUS,
                        !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));

        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(
                        ConnectionSecurityLevel.SECURE_WITH_POLICY_INSTALLED_CERT, IS_SMALL_DEVICE,
                        !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(
                        ConnectionSecurityLevel.SECURE_WITH_POLICY_INSTALLED_CERT, !IS_SMALL_DEVICE,
                        !IS_OFFLINE_PAGE, !IS_PREVIEW));

        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.SECURE,
                        IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.SECURE,
                        !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));

        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.EV_SECURE,
                        IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
        assertEquals(R.drawable.omnibox_https_valid,
                LocationBarModel.getSecurityIconResource(ConnectionSecurityLevel.EV_SECURE,
                        !IS_SMALL_DEVICE, !IS_OFFLINE_PAGE, !IS_PREVIEW));
    }
}
