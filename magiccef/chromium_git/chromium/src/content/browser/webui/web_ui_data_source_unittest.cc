// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/webui/web_ui_data_source_impl.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/test/test_content_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {
namespace {

const int kDummyStringId = 123;
const int kDummyDefaultResourceId = 456;
const int kDummyResourceId = 789;

const char kDummyString[] = "foo";
const char kDummyDefaultResource[] = "<html>foo</html>";
const char kDummyResource[] = "<html>blah</html>";

class TestClient : public TestContentClient {
 public:
  TestClient() : is_gzipped_(false) {}
  ~TestClient() override {}

  base::string16 GetLocalizedString(int message_id) const override {
    if (message_id == kDummyStringId)
      return base::UTF8ToUTF16(kDummyString);
    return base::string16();
  }

  base::RefCountedMemory* GetDataResourceBytes(
      int resource_id) const override {
    base::RefCountedStaticMemory* bytes = nullptr;
    if (resource_id == kDummyDefaultResourceId) {
      bytes = new base::RefCountedStaticMemory(
          kDummyDefaultResource, base::size(kDummyDefaultResource));
    } else if (resource_id == kDummyResourceId) {
      bytes = new base::RefCountedStaticMemory(kDummyResource,
                                               base::size(kDummyResource));
    }
    return bytes;
  }

  bool IsDataResourceGzipped(int resource_id) const override {
    return is_gzipped_;
  }

  // Sets the response for |IsDataResourceGzipped()|.
  void SetIsDataResourceGzipped(bool is_gzipped) { is_gzipped_ = is_gzipped; }

 private:
  bool is_gzipped_;
};

}  // namespace

class WebUIDataSourceTest : public testing::Test {
 public:
  WebUIDataSourceTest() {}
  ~WebUIDataSourceTest() override {}
  WebUIDataSourceImpl* source() { return source_.get(); }

  void StartDataRequest(const std::string& path,
                        const URLDataSource::GotDataCallback& callback) {
    source_->StartDataRequest(path, ResourceRequestInfo::WebContentsGetter(),
                              callback);
  }

  std::string GetMimeType(const std::string& path) const {
    return source_->GetMimeType(path);
  }

  void HandleRequest(const std::string& path,
                     const WebUIDataSourceImpl::GotDataCallback&) {
    request_path_ = path;
  }

  void RequestFilterQueryStringCallback(
      scoped_refptr<base::RefCountedMemory> data);

 protected:
  std::string request_path_;
  TestClient client_;

 private:
  void SetUp() override {
    SetContentClient(&client_);
    WebUIDataSource* source = WebUIDataSourceImpl::Create("host");
    WebUIDataSourceImpl* source_impl = static_cast<WebUIDataSourceImpl*>(
        source);
    source_impl->disable_load_time_data_defaults_for_testing();
    source_ = base::WrapRefCounted(source_impl);
  }

  TestBrowserThreadBundle thread_bundle_;
  scoped_refptr<WebUIDataSourceImpl> source_;
};

void EmptyStringsCallback(scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find("loadTimeData.data = {"), std::string::npos);
  EXPECT_NE(result.find("};"), std::string::npos);
}

TEST_F(WebUIDataSourceTest, EmptyStrings) {
  source()->SetJsonPath("strings.js");
  StartDataRequest("strings.js", base::Bind(&EmptyStringsCallback));
}

void SomeValuesCallback(scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find("\"flag\":true"), std::string::npos);
  EXPECT_NE(result.find("\"counter\":10"), std::string::npos);
  EXPECT_NE(result.find("\"debt\":-456"), std::string::npos);
  EXPECT_NE(result.find("\"planet\":\"pluto\""), std::string::npos);
  EXPECT_NE(result.find("\"button\":\"foo\""), std::string::npos);
}

TEST_F(WebUIDataSourceTest, SomeValues) {
  source()->SetJsonPath("strings.js");
  source()->AddBoolean("flag", true);
  source()->AddInteger("counter", 10);
  source()->AddInteger("debt", -456);
  source()->AddString("planet", base::ASCIIToUTF16("pluto"));
  source()->AddLocalizedString("button", kDummyStringId);
  StartDataRequest("strings.js", base::Bind(&SomeValuesCallback));
}

void DefaultResourceFoobarCallback(scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find(kDummyDefaultResource), std::string::npos);
}

void DefaultResourceStringsCallback(
    scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find(kDummyDefaultResource), std::string::npos);
}

TEST_F(WebUIDataSourceTest, DefaultResource) {
  source()->SetDefaultResource(kDummyDefaultResourceId);
  StartDataRequest("foobar", base::Bind(&DefaultResourceFoobarCallback));
  StartDataRequest("strings.js", base::Bind(&DefaultResourceStringsCallback));
}

void NamedResourceFoobarCallback(scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find(kDummyResource), std::string::npos);
}

void NamedResourceStringsCallback(scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find(kDummyDefaultResource), std::string::npos);
}

TEST_F(WebUIDataSourceTest, NamedResource) {
  source()->SetDefaultResource(kDummyDefaultResourceId);
  source()->AddResourcePath("foobar", kDummyResourceId);
  StartDataRequest("foobar", base::Bind(&NamedResourceFoobarCallback));
  StartDataRequest("strings.js", base::Bind(&NamedResourceStringsCallback));
}

void NamedResourceWithQueryStringCallback(
    scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  EXPECT_NE(result.find(kDummyResource), std::string::npos);
}

TEST_F(WebUIDataSourceTest, NamedResourceWithQueryString) {
  source()->SetDefaultResource(kDummyDefaultResourceId);
  source()->AddResourcePath("foobar", kDummyResourceId);
  StartDataRequest("foobar?query?string",
                   base::Bind(&NamedResourceWithQueryStringCallback));
}

void WebUIDataSourceTest::RequestFilterQueryStringCallback(
    scoped_refptr<base::RefCountedMemory> data) {
  std::string result(data->front_as<char>(), data->size());
  // Check that the query string is passed to the request filter (and not
  // trimmed).
  EXPECT_EQ("foobar?query?string", request_path_);
}

TEST_F(WebUIDataSourceTest, RequestFilterQueryString) {
  request_path_ = std::string();
  source()->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&WebUIDataSourceTest::HandleRequest,
                          base::Unretained(this)));
  source()->SetDefaultResource(kDummyDefaultResourceId);
  source()->AddResourcePath("foobar", kDummyResourceId);
  StartDataRequest(
      "foobar?query?string",
      base::Bind(&WebUIDataSourceTest::RequestFilterQueryStringCallback,
                 base::Unretained(this)));
}

TEST_F(WebUIDataSourceTest, MimeType) {
  const char* css = "text/css";
  const char* html = "text/html";
  const char* js = "application/javascript";
  const char* png = "image/png";
  EXPECT_EQ(GetMimeType(std::string()), html);
  EXPECT_EQ(GetMimeType("foo"), html);
  EXPECT_EQ(GetMimeType("foo.html"), html);
  EXPECT_EQ(GetMimeType(".js"), js);
  EXPECT_EQ(GetMimeType("foo.js"), js);
  EXPECT_EQ(GetMimeType("js"), html);
  EXPECT_EQ(GetMimeType("foojs"), html);
  EXPECT_EQ(GetMimeType("foo.jsp"), html);
  EXPECT_EQ(GetMimeType("foocss"), html);
  EXPECT_EQ(GetMimeType("foo.css"), css);
  EXPECT_EQ(GetMimeType(".css.foo"), html);
  EXPECT_EQ(GetMimeType("foopng"), html);
  EXPECT_EQ(GetMimeType("foo.png"), png);
  EXPECT_EQ(GetMimeType(".png.foo"), html);

  // With query strings.
  EXPECT_EQ(GetMimeType("foo?abc?abc"), html);
  EXPECT_EQ(GetMimeType("foo.html?abc?abc"), html);
  EXPECT_EQ(GetMimeType("foo.css?abc?abc"), css);
  EXPECT_EQ(GetMimeType("foo.js?abc?abc"), js);
}

TEST_F(WebUIDataSourceTest, IsGzipped) {
  source()->SetJsonPath("strings.js");
  source()->SetDefaultResource(kDummyDefaultResourceId);

  // Test that WebUIDataSource delegates IsGzipped to the content client.
  client_.SetIsDataResourceGzipped(false);
  EXPECT_FALSE(source()->IsGzipped("foobar"));
  EXPECT_FALSE(source()->IsGzipped(""));
  // Test that |json_path_| is correctly reported as non-gzipped.
  EXPECT_FALSE(source()->IsGzipped("strings.js"));

  client_.SetIsDataResourceGzipped(true);
  EXPECT_TRUE(source()->IsGzipped("foobar"));
  EXPECT_TRUE(source()->IsGzipped(""));
  // Test that |json_path_| is correctly reported as non-gzipped.
  EXPECT_FALSE(source()->IsGzipped("strings.js"));
}

TEST_F(WebUIDataSourceTest, IsGzippedNoDefaultResource) {
  // Test that WebUIDataSource reports non existing resources as non-gzipped
  // and does not trigger any CHECKs.
  client_.SetIsDataResourceGzipped(false);
  EXPECT_FALSE(source()->IsGzipped("foobar"));

  client_.SetIsDataResourceGzipped(true);
  EXPECT_FALSE(source()->IsGzipped("foobar"));
}

TEST_F(WebUIDataSourceTest, IsGzippedWithRequestFiltering) {
  source()->SetRequestFilter(
      base::BindRepeating(
          [](const std::string& path) { return path == "json/special/path"; }),
      base::BindRepeating(&WebUIDataSourceTest::HandleRequest,
                          base::Unretained(this)));
  source()->SetDefaultResource(kDummyDefaultResourceId);

  client_.SetIsDataResourceGzipped(false);
  EXPECT_FALSE(source()->IsGzipped("json/special/path"));
  EXPECT_FALSE(source()->IsGzipped("other/path"));

  client_.SetIsDataResourceGzipped(true);
  EXPECT_FALSE(source()->IsGzipped("json/special/path"));
  EXPECT_TRUE(source()->IsGzipped("other/path"));
}

TEST_F(WebUIDataSourceTest, ShouldServeMimeTypeAsContentTypeHeader) {
  EXPECT_TRUE(source()->source()->ShouldServeMimeTypeAsContentTypeHeader());
}

}  // namespace content
