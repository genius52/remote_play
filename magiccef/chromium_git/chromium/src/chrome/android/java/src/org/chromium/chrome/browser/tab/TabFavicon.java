// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.content.res.Resources;
import android.graphics.Bitmap;

import org.chromium.base.ObserverList.RewindableIterator;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.chrome.R;
import org.chromium.content_public.browser.WebContents;

/**
 * Fetches a favicon for active WebContents in a Tab.
 */
public class TabFavicon extends TabWebContentsUserData {
    private static final Class<TabFavicon> USER_DATA_KEY = TabFavicon.class;

    private final Tab mTab;
    private final long mNativeTabFavicon;

    /**
     * The size in pixels at which favicons will be drawn. Ideally mFavicon will have this size to
     * avoid scaling artifacts.
     */
    private final int mIdealFaviconSize;

    private Bitmap mFavicon;
    private int mFaviconWidth;
    private int mFaviconHeight;
    private String mFaviconUrl;

    static TabFavicon from(Tab tab) {
        TabFavicon favicon = get(tab);
        if (favicon == null) {
            favicon = tab.getUserDataHost().setUserData(USER_DATA_KEY, new TabFavicon(tab));
        }
        return favicon;
    }

    private static TabFavicon get(Tab tab) {
        if (tab == null || !tab.isInitialized()) return null;
        return tab.getUserDataHost().getUserData(USER_DATA_KEY);
    }

    /**
     * @param tab Tab containing the web contents's favicon.
     * @return {@link Bitmap} of the favicon.
     */
    public static Bitmap getBitmap(Tab tab) {
        TabFavicon tabFavicon = get(tab);
        return tabFavicon != null ? tabFavicon.getFavicon() : null;
    }

    private TabFavicon(Tab tab) {
        super(tab);
        Resources resources = tab.getThemedApplicationContext().getResources();
        mIdealFaviconSize = resources.getDimensionPixelSize(R.dimen.default_favicon_size);
        mTab = tab;
        mNativeTabFavicon = nativeInit();
    }

    @Override
    public void initWebContents(WebContents webContents) {
        nativeSetWebContents(mNativeTabFavicon, webContents);
    }

    @Override
    public void cleanupWebContents(WebContents webContents) {
        nativeResetWebContents(mNativeTabFavicon);
    }

    @Override
    public void destroyInternal() {
        nativeOnDestroyed(mNativeTabFavicon);
    }

    /**
     * @return The bitmap of the favicon scaled to 16x16dp. null if no favicon
     *         is specified or it requires the default favicon.
     */
    private Bitmap getFavicon() {
        // If we have no content or a native page, return null.
        if (mTab.isNativePage() || mTab.getWebContents() == null) return null;

        // Use the cached favicon only if the page wasn't changed.
        if (mFavicon != null && mFaviconUrl != null && mFaviconUrl.equals(mTab.getUrl())) {
            return mFavicon;
        }

        return nativeGetFavicon(mNativeTabFavicon);
    }

    /**
     * @param width new favicon's width.
     * @param height new favicon's height.
     * @return true iff the new favicon should replace the current one.
     */
    private boolean isBetterFavicon(int width, int height) {
        if (isIdealFaviconSize(width, height)) return true;

        // Prefer square favicons over rectangular ones
        if (mFaviconWidth != mFaviconHeight && width == height) return true;
        if (mFaviconWidth == mFaviconHeight && width != height) return false;

        // Do not update favicon if it's already at least as big as the ideal size in both dimens
        if (mFaviconWidth >= mIdealFaviconSize && mFaviconHeight >= mIdealFaviconSize) return false;

        // Update favicon if the new one is larger in one dimen, but not smaller in the other
        return (width > mFaviconWidth && !(height < mFaviconHeight))
                || (!(width < mFaviconWidth) && height > mFaviconHeight);
    }

    private boolean isIdealFaviconSize(int width, int height) {
        return width == mIdealFaviconSize && height == mIdealFaviconSize;
    }

    @CalledByNative
    private void onFaviconAvailable(Bitmap icon) {
        if (icon == null) return;
        String url = mTab.getUrl();
        boolean pageUrlChanged = !url.equals(mFaviconUrl);
        // This method will be called multiple times if the page has more than one favicon.
        // We are trying to use the |mIdealFaviconSize|x|mIdealFaviconSize| DP icon here, or the
        // first one larger than that received. Bitmap.createScaledBitmap will return the original
        // bitmap if it is already |mIdealFaviconSize|x|mIdealFaviconSize| DP.
        if (pageUrlChanged || isBetterFavicon(icon.getWidth(), icon.getHeight())) {
            mFavicon = Bitmap.createScaledBitmap(icon, mIdealFaviconSize, mIdealFaviconSize, true);
            mFaviconWidth = icon.getWidth();
            mFaviconHeight = icon.getHeight();
            mFaviconUrl = url;
        }

        RewindableIterator<TabObserver> observers = mTab.getTabObservers();
        while (observers.hasNext()) observers.next().onFaviconUpdated(mTab, icon);
    }

    private native long nativeInit();
    private native void nativeOnDestroyed(long nativeTabFavicon);
    private native void nativeSetWebContents(long nativeTabFavicon, WebContents webContents);
    private native void nativeResetWebContents(long nativeTabFavicon);
    private native Bitmap nativeGetFavicon(long nativeTabFavicon);
}
