// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.browserservices;

import android.net.Uri;
import android.support.customtabs.CustomTabsService;
import android.support.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ContextUtils;
import org.chromium.base.task.PostTask;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.browserservices.OriginVerifier.OriginVerificationListener;
import org.chromium.chrome.browser.browsing_data.BrowsingDataType;
import org.chromium.chrome.browser.browsing_data.TimePeriod;
import org.chromium.chrome.browser.preferences.ChromePreferenceManager;
import org.chromium.chrome.browser.preferences.privacy.BrowsingDataBridge;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.content_public.browser.UiThreadTaskTraits;
import org.chromium.content_public.browser.test.util.TestThreadUtils;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/** Tests for OriginVerifier. */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class OriginVerifierTest {
    @Rule
    public ChromeActivityTestRule<ChromeActivity> mActivityTestRule =
            new ChromeActivityTestRule<>(ChromeActivity.class);

    private static final long TIMEOUT_MS = 1000;
    private static final byte[] BYTE_ARRAY = new byte[] {(byte) 0xaa, (byte) 0xbb, (byte) 0xcc,
            (byte) 0x10, (byte) 0x20, (byte) 0x30, (byte) 0x01, (byte) 0x02};
    private static final String STRING_ARRAY = "AA:BB:CC:10:20:30:01:02";

    private static final String PACKAGE_NAME =
            ContextUtils.getApplicationContext().getPackageName();
    private static final String SHA_256_FINGERPRINT_PUBLIC =
            "32:A2:FC:74:D7:31:10:58:59:E5:A8:5D:F1:6D:95:F1:02:D8:5B"
            + ":22:09:9B:80:64:C5:D8:91:5C:61:DA:D1:E0";
    private static final String SHA_256_FINGERPRINT_OFFICIAL =
            "19:75:B2:F1:71:77:BC:89:A5:DF:F3:1F:9E:64:A6:CA:E2:81:A5"
            + ":3D:C1:D1:D5:9B:1D:14:7F:E1:C8:2A:FA:00";
    private static final String SHA_256_FINGERPRINT = ChromeVersionInfo.isOfficialBuild()
            ? SHA_256_FINGERPRINT_OFFICIAL
            : SHA_256_FINGERPRINT_PUBLIC;

    private Origin mHttpsOrigin;
    private Origin mHttpOrigin;

    private class TestOriginVerificationListener implements OriginVerificationListener {
        @Override
        public void onOriginVerified(String packageName, Origin origin, boolean verified,
                Boolean online) {
            mLastPackageName = packageName;
            mLastOrigin = origin;
            mLastVerified = verified;
            mVerificationResultSemaphore.release();
        }
    }

    private Semaphore mVerificationResultSemaphore;
    private OriginVerifier mUseAsOriginVerifier;
    private OriginVerifier mHandleAllUrlsVerifier;
    private volatile String mLastPackageName;
    private volatile Origin mLastOrigin;
    private volatile boolean mLastVerified;

    @Before
    public void setUp() throws Exception {
        mActivityTestRule.startMainActivityOnBlankPage();

        mHttpsOrigin = new Origin("https://www.example.com");
        mHttpOrigin = new Origin("http://www.android.com");

        mHandleAllUrlsVerifier =
                new OriginVerifier(PACKAGE_NAME, CustomTabsService.RELATION_HANDLE_ALL_URLS);
        mUseAsOriginVerifier =
                new OriginVerifier(PACKAGE_NAME, CustomTabsService.RELATION_USE_AS_ORIGIN);
        mVerificationResultSemaphore = new Semaphore(0);
    }

    @Test
    @SmallTest
    public void testSHA256CertificateChecks() {
        Assert.assertEquals(STRING_ARRAY, OriginVerifier.byteArrayToHexString(BYTE_ARRAY));
        Assert.assertEquals(SHA_256_FINGERPRINT,
                OriginVerifier.getCertificateSHA256FingerprintForPackage(PACKAGE_NAME));
    }

    @Test
    @SmallTest
    public void testOnlyHttpsAllowed() throws InterruptedException {
        Origin origin = new Origin(Uri.parse("LOL"));
        PostTask.postTask(UiThreadTaskTraits.DEFAULT,
                () -> mHandleAllUrlsVerifier.start(new TestOriginVerificationListener(), origin));
        Assert.assertTrue(
                mVerificationResultSemaphore.tryAcquire(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        Assert.assertFalse(mLastVerified);
        PostTask.postTask(UiThreadTaskTraits.DEFAULT,
                () -> mHandleAllUrlsVerifier.start(
                                new TestOriginVerificationListener(), mHttpOrigin));
        Assert.assertTrue(
                mVerificationResultSemaphore.tryAcquire(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        Assert.assertFalse(mLastVerified);
    }

    @Test
    @SmallTest
    public void testMultipleRelationships() throws Exception {
        PostTask.postTask(UiThreadTaskTraits.DEFAULT,
                () -> OriginVerifier.addVerificationOverride(PACKAGE_NAME, mHttpsOrigin,
                                CustomTabsService.RELATION_USE_AS_ORIGIN));
        PostTask.postTask(UiThreadTaskTraits.DEFAULT,
                () -> mUseAsOriginVerifier.start(
                                new TestOriginVerificationListener(), mHttpsOrigin));
        Assert.assertTrue(
                mVerificationResultSemaphore.tryAcquire(TIMEOUT_MS, TimeUnit.MILLISECONDS));
        Assert.assertTrue(mLastVerified);
        Assert.assertTrue(TestThreadUtils.runOnUiThreadBlocking(
                () -> OriginVerifier.wasPreviouslyVerified(PACKAGE_NAME, mHttpsOrigin,
                                CustomTabsService.RELATION_USE_AS_ORIGIN)));
        Assert.assertFalse(TestThreadUtils.runOnUiThreadBlocking(
                () -> OriginVerifier.wasPreviouslyVerified(PACKAGE_NAME, mHttpsOrigin,
                                CustomTabsService.RELATION_HANDLE_ALL_URLS)));
        Assert.assertEquals(mLastPackageName, PACKAGE_NAME);
        Assert.assertEquals(mLastOrigin, mHttpsOrigin);
    }

    @Test
    @SmallTest
    public void testWipedWithBrowsingData() throws InterruptedException, TimeoutException {
        CallbackHelper callbackHelper = new CallbackHelper();

        String relationship = "relationship1";
        Set<String> savedLinks = new HashSet<>();
        savedLinks.add(relationship);

        ChromePreferenceManager preferences = ChromePreferenceManager.getInstance();

        preferences.setVerifiedDigitalAssetLinks(savedLinks);
        Assert.assertTrue(preferences.getVerifiedDigitalAssetLinks().contains(relationship));

        TestThreadUtils.runOnUiThreadBlocking(() -> {
            BrowsingDataBridge.getInstance().clearBrowsingData(callbackHelper::notifyCalled,
                    new int[] {BrowsingDataType.HISTORY}, TimePeriod.ALL_TIME);
        });

        callbackHelper.waitForCallback(0);
        Assert.assertTrue(preferences.getVerifiedDigitalAssetLinks().isEmpty());
    }
}
