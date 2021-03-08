// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/sync/bubble_sync_promo_delegate.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "services/identity/public/cpp/identity_test_utils.h"

using bookmarks::BookmarkModel;

namespace {
const char kTestBookmarkURL[] = "http://www.google.com";
} // namespace

class BookmarkBubbleViewTest : public BrowserWithTestWindowTest {
 public:
  BookmarkBubbleViewTest()
      : BrowserWithTestWindowTest(
            content::TestBrowserThreadBundle::IO_MAINLOOP) {}

  // testing::Test:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();

    profile()->CreateBookmarkModel(true);
    BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForBrowserContext(profile());
    bookmarks::test::WaitForBookmarkModelToLoad(bookmark_model);

    bookmarks::AddIfNotBookmarked(
        bookmark_model, GURL(kTestBookmarkURL), base::string16());
  }

  void TearDown() override {
    // Make sure the bubble is destroyed before the profile to avoid a crash.
    bubble_.reset();

    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  // Creates a bookmark bubble view.
  void CreateBubbleView() {
    std::unique_ptr<BubbleSyncPromoDelegate> delegate;
    bubble_.reset(new BookmarkBubbleView(NULL, NULL, std::move(delegate),
                                         profile(), GURL(kTestBookmarkURL),
                                         true));
    bubble_->Init();
  }

  std::unique_ptr<views::View> CreateFootnoteView() {
    return base::WrapUnique(bubble_->CreateFootnoteView());
  }

  std::unique_ptr<BookmarkBubbleView> bubble_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BookmarkBubbleViewTest);
};

// Verifies that the sync promo is not displayed for a signed in user.
TEST_F(BookmarkBubbleViewTest, SyncPromoSignedIn) {
  identity::MakePrimaryAccountAvailable(
      IdentityManagerFactory::GetForProfile(profile()), "fake_username");
  CreateBubbleView();
  std::unique_ptr<views::View> footnote = CreateFootnoteView();
  EXPECT_FALSE(footnote);
}

// Verifies that the sync promo is displayed for a user that is not signed in.
#if defined(OS_CHROMEOS)
// TODO(https://crbug.com/966220): Consistently failing on trybot.
#define MAYBE_SyncPromoNotSignedIn DISABLED_SyncPromoNotSignedIn
#else
#define MAYBE_SyncPromoNotSignedIn SyncPromoNotSignedIn
#endif  // defined(OS_CHROMEOS)

TEST_F(BookmarkBubbleViewTest, MAYBE_SyncPromoNotSignedIn) {
  CreateBubbleView();
  std::unique_ptr<views::View> footnote = CreateFootnoteView();
#if defined(OS_CHROMEOS)
  EXPECT_FALSE(footnote);
#else  // !defined(OS_CHROMEOS)
  EXPECT_TRUE(footnote);
#endif
}
