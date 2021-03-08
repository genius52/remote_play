// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/fileicon_source.h"

#include "base/memory/ref_counted_memory.h"
#include "base/stl_util.h"
#include "build/build_config.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestFileIconSource : public FileIconSource {
 public:
  TestFileIconSource() {}

  MOCK_METHOD4(FetchFileIcon,
               void(const base::FilePath& path,
                    float scale_factor,
                    IconLoader::IconSize icon_size,
                    const content::URLDataSource::GotDataCallback& callback));

  ~TestFileIconSource() override {}
};

class FileIconSourceTest : public testing::Test {
 public:
  FileIconSourceTest() = default;

 private:
  content::TestBrowserThreadBundle test_browser_thread_bundle_;
};

const struct FetchFileIconExpectation {
  const char* request_path;
  const base::FilePath::CharType* unescaped_path;
  float scale_factor;
  IconLoader::IconSize size;
} kBasicExpectations[] = {
    {"?path=foo&bar", FILE_PATH_LITERAL("foo"), 1.0f, IconLoader::NORMAL},
    {"?path=foo&bar&scale=2x", FILE_PATH_LITERAL("foo"), 2.0f,
     IconLoader::NORMAL},
    {"?path=foo&iconsize=small", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::SMALL},
    {"?path=foo&iconsize=normal", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::NORMAL},
    {"?path=foo&iconsize=large", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::LARGE},
    {"?path=foo&iconsize=asdf", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::NORMAL},
    {"?path=foo&blah=b&iconsize=small", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::SMALL},
    {"?path=foo&blah&iconsize=small", FILE_PATH_LITERAL("foo"), 1.0f,
     IconLoader::SMALL},
    {"?path=a%3Fb%26c%3Dd.txt&iconsize=small", FILE_PATH_LITERAL("a?b&c=d.txt"),
     1.0f, IconLoader::SMALL},
    {"?path=a%3Ficonsize%3Dsmall&iconsize=large",
     FILE_PATH_LITERAL("a?iconsize=small"), 1.0f, IconLoader::LARGE},
    {"?path=o%40%23%24%25%26*()%20%2B%3D%3F%2C%3A%3B%22.jpg",
     FILE_PATH_LITERAL("o@#$%&*() +=?,:;\".jpg"), 1.0f, IconLoader::NORMAL},
#if defined(OS_WIN)
    {"?path=c%3A%2Ffoo%2Fbar%2Fbaz", FILE_PATH_LITERAL("c:\\foo\\bar\\baz"),
     1.0f, IconLoader::NORMAL},
    {"?path=%2Ffoo&bar=asdf&asdf", FILE_PATH_LITERAL("\\foo"), 1.0f,
     IconLoader::NORMAL},
    {"?path=c%3A%2Fusers%2Ffoo%20user%2Fbar.txt",
     FILE_PATH_LITERAL("c:\\users\\foo user\\bar.txt"), 1.0f,
     IconLoader::NORMAL},
    {"?path=c%3A%2Fusers%2F%C2%A9%202000.pdf",
     FILE_PATH_LITERAL("c:\\users\\\xa9 2000.pdf"), 1.0f, IconLoader::NORMAL},
    {"?path=%E0%B6%9A%E0%B6%BB%E0%B7%9D%E0%B6%B8%E0%B7%8A",
     FILE_PATH_LITERAL("\x0d9a\x0dbb\x0ddd\x0db8\x0dca"), 1.0f,
     IconLoader::NORMAL},
    {"?path=%2Ffoo%2Fbar", FILE_PATH_LITERAL("\\foo\\bar"), 1.0f,
     IconLoader::NORMAL},
    {"?path=%2Fbaz%20(1).txt&iconsize=small",
     FILE_PATH_LITERAL("\\baz (1).txt"), 1.0f, IconLoader::SMALL},
#else
    {"?path=%2Ffoo%2Fbar%2Fbaz", FILE_PATH_LITERAL("/foo/bar/baz"), 1.0f,
     IconLoader::NORMAL},
    {"?path=%2Ffoo&bar", FILE_PATH_LITERAL("/foo"), 1.0f, IconLoader::NORMAL},
    {"?path=%2Ffoo%2f%E0%B6%9A%E0%B6%BB%E0%B7%9D%E0%B6%B8%E0%B7%8A",
     FILE_PATH_LITERAL("/foo/\xe0\xb6\x9a\xe0\xb6\xbb\xe0\xb7\x9d")
         FILE_PATH_LITERAL("\xe0\xb6\xb8\xe0\xb7\x8a"),
     1.0f, IconLoader::NORMAL},
    {"?path=%2Ffoo%2Fbar", FILE_PATH_LITERAL("/foo/bar"), 1.0f,
     IconLoader::NORMAL},
    {"?path=%2Fbaz%20(1).txt&iconsize=small", FILE_PATH_LITERAL("/baz (1).txt"),
     1.0f, IconLoader::SMALL},
#endif
};

// Test that the callback is NULL.
MATCHER(CallbackIsNull, "") {
  return arg.is_null();
}

}  // namespace

TEST_F(FileIconSourceTest, FileIconSource_Parse) {
  std::vector<ui::ScaleFactor> supported_scale_factors;
  supported_scale_factors.push_back(ui::SCALE_FACTOR_100P);
  supported_scale_factors.push_back(ui::SCALE_FACTOR_200P);
  ui::test::ScopedSetSupportedScaleFactors scoped_supported(
      supported_scale_factors);

  for (unsigned i = 0; i < base::size(kBasicExpectations); i++) {
    auto source = std::make_unique<TestFileIconSource>();
    content::URLDataSource::GotDataCallback callback;
    EXPECT_CALL(*source.get(),
                FetchFileIcon(
                    base::FilePath(kBasicExpectations[i].unescaped_path),
                    kBasicExpectations[i].scale_factor,
                    kBasicExpectations[i].size, CallbackIsNull()));
    source->StartDataRequest(
        kBasicExpectations[i].request_path,
        content::ResourceRequestInfo::WebContentsGetter(),
        callback);
  }
}