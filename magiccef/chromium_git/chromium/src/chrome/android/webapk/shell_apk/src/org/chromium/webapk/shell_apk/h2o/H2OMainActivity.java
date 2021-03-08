// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.webapk.shell_apk.h2o;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;

import org.chromium.webapk.shell_apk.HostBrowserLauncher;
import org.chromium.webapk.shell_apk.HostBrowserLauncherParams;
import org.chromium.webapk.shell_apk.TransparentLauncherActivity;

/**
 * Handles android.intent.action.MAIN intents if the host browser does not support "showing a
 * transparent window in WebAPK mode till the URL has been loaded".
 */
public class H2OMainActivity extends TransparentLauncherActivity {
    /** Minimum interval between requests for the host browser to relaunch the WebAPK. */
    private static final long MINIMUM_INTERVAL_BETWEEN_RELAUNCHES_MS = 20000;

    /** Returns whether {@link H2OMainActivity} is enabled. */
    public static boolean checkComponentEnabled(Context context) {
        PackageManager pm = context.getPackageManager();
        ComponentName component = new ComponentName(context, H2OMainActivity.class);
        int enabledSetting = pm.getComponentEnabledSetting(component);
        // Component is enabled by default.
        return enabledSetting == PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                || enabledSetting == PackageManager.COMPONENT_ENABLED_STATE_DEFAULT;
    }

    @Override
    protected void onHostBrowserSelected(HostBrowserLauncherParams params) {
        if (params == null) {
            return;
        }

        Context appContext = getApplicationContext();
        if (H2OLauncher.shouldIntentLaunchSplashActivity(params)
                && !H2OLauncher.didRequestRelaunchFromHostBrowserWithinLastMs(
                        appContext, MINIMUM_INTERVAL_BETWEEN_RELAUNCHES_MS)) {
            // Request the host browser to relaunch the WebAPK. We cannot relaunch ourselves
            // because {@link H2OLauncher#changeEnabledComponentsAndKillShellApk()} kills the
            // WebAPK app. We cannot use AlarmManager or JobScheduler because their minimum
            // delay (several seconds) is too high.
            H2OLauncher.requestRelaunchFromHostBrowser(appContext, params);
            H2OLauncher.changeEnabledComponentsAndKillShellApk(appContext,
                    new ComponentName(appContext, H2OOpaqueMainActivity.class), getComponentName());
            return;
        }

        HostBrowserLauncher.launch(appContext, params);
    }
}