// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_new_script_loader.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include "base/bind_helpers.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_disk_cache.h"
#include "content/browser/service_worker/service_worker_test_utils.h"
#include "content/browser/url_loader_factory_getter.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/data_pipe_utils.h"
#include "net/base/load_flags.h"
#include "net/base/test_completion_callback.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/test/test_url_loader_client.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration.mojom.h"

namespace content {
namespace service_worker_new_script_loader_unittest {

const char kNormalScriptURL[] = "https://example.com/normal.js";
const char kNormalImportedScriptURL[] =
    "https://my-awesome-cdn.com/import_script.js";
const char kHistogramWriteResponseResult[] =
    "ServiceWorker.DiskCache.WriteResponseResult";

// MockHTTPServer is a utility to provide mocked responses for
// ServiceWorkerNewScriptLoader.
class MockHTTPServer {
 public:
  struct Response {
    Response(const std::string& headers, const std::string& body)
        : headers(headers), body(body) {}

    const std::string headers;
    const std::string body;
    bool has_certificate_error = false;
  };

  void Set(const GURL& url, const Response& response) {
    responses_.erase(url);
    responses_.emplace(url, response);
  }

  const Response& Get(const GURL& url) {
    auto found = responses_.find(url);
    EXPECT_TRUE(found != responses_.end());
    return found->second;
  }

 private:
  std::map<GURL, Response> responses_;
};

// A URLLoaderFactory that returns a mocked response provided by MockHTTPServer.
class MockNetworkURLLoaderFactory final
    : public network::mojom::URLLoaderFactory {
 public:
  explicit MockNetworkURLLoaderFactory(MockHTTPServer* mock_server)
      : mock_server_(mock_server) {}

  // network::mojom::URLLoaderFactory implementation.
  void CreateLoaderAndStart(network::mojom::URLLoaderRequest request,
                            int32_t routing_id,
                            int32_t request_id,
                            uint32_t options,
                            const network::ResourceRequest& url_request,
                            network::mojom::URLLoaderClientPtr client,
                            const net::MutableNetworkTrafficAnnotationTag&
                                traffic_annotation) override {
    last_request_ = url_request;
    const MockHTTPServer::Response& response =
        mock_server_->Get(url_request.url);

    // Pass the response header to the client.
    net::HttpResponseInfo info;
    info.headers = base::MakeRefCounted<net::HttpResponseHeaders>(
        net::HttpUtil::AssembleRawHeaders(response.headers));
    network::ResourceResponseHead response_head;
    response_head.headers = info.headers;
    response_head.headers->GetMimeType(&response_head.mime_type);
    response_head.network_accessed = access_network_;
    if (response.has_certificate_error) {
      response_head.cert_status = net::CERT_STATUS_DATE_INVALID;
    }

    if (response_head.headers->response_code() == 307) {
      client->OnReceiveRedirect(net::RedirectInfo(), response_head);
      return;
    }
    client->OnReceiveResponse(response_head);

    // Pass the response body to the client.
    if (!response.body.empty()) {
      uint32_t bytes_written = response.body.size();
      mojo::DataPipe data_pipe;
      MojoResult result = data_pipe.producer_handle->WriteData(
          response.body.data(), &bytes_written,
          MOJO_WRITE_DATA_FLAG_ALL_OR_NONE);
      ASSERT_EQ(MOJO_RESULT_OK, result);
      client->OnStartLoadingResponseBody(std::move(data_pipe.consumer_handle));
    }
    network::URLLoaderCompletionStatus status;
    status.error_code = net::OK;
    client->OnComplete(status);
  }

  void set_to_access_network(bool access_network) {
    access_network_ = access_network;
  }

  network::ResourceRequest last_request() const { return last_request_; }

  void Clone(network::mojom::URLLoaderFactoryRequest factory) override {
    NOTREACHED();
  }

 private:
  // |mock_server_| is owned by ServiceWorkerNewScriptLoaderTest.
  MockHTTPServer* mock_server_;

  // The most recent request received by this factory.
  network::ResourceRequest last_request_;

  // Controls whether a load simulates accessing network or cache.
  bool access_network_ = false;

  DISALLOW_COPY_AND_ASSIGN(MockNetworkURLLoaderFactory);
};

// ServiceWorkerNewScriptLoaderTest is for testing the handling of requests for
// installing service worker scripts via ServiceWorkerNewScriptLoader.
class ServiceWorkerNewScriptLoaderTest : public testing::Test {
 public:
  ServiceWorkerNewScriptLoaderTest()
      : thread_bundle_(TestBrowserThreadBundle::IO_MAINLOOP),
        mock_server_(std::make_unique<MockHTTPServer>()) {}
  ~ServiceWorkerNewScriptLoaderTest() override = default;

  ServiceWorkerContextCore* context() { return helper_->context(); }

  void SetUp() override {
    helper_ = std::make_unique<EmbeddedWorkerTestHelper>(base::FilePath());

    InitializeStorage();

    mock_server_->Set(GURL(kNormalScriptURL),
                      MockHTTPServer::Response(
                          std::string("HTTP/1.1 200 OK\n"
                                      "Content-Type: text/javascript\n\n"),
                          std::string("this body came from the network")));
    mock_server_->Set(
        GURL(kNormalImportedScriptURL),
        MockHTTPServer::Response(
            std::string("HTTP/1.1 200 OK\n"
                        "Content-Type: text/javascript\n\n"),
            std::string(
                "this is an import script response body from the network")));

    // Initialize URLLoaderFactory.
    mock_url_loader_factory_ =
        std::make_unique<MockNetworkURLLoaderFactory>(mock_server_.get());
    helper_->SetNetworkFactory(mock_url_loader_factory_.get());
  }

  void InitializeStorage() {
    base::RunLoop run_loop;
    context()->storage()->LazyInitializeForTest(run_loop.QuitClosure());
    run_loop.Run();
  }

  // Sets up ServiceWorkerRegistration and ServiceWorkerVersion. This should be
  // called before DoRequest().
  void SetUpRegistration(const GURL& script_url) {
    blink::mojom::ServiceWorkerRegistrationOptions options;
    options.scope = script_url.GetWithoutFilename();
    SetUpRegistrationWithOptions(script_url, options);
  }
  void SetUpRegistrationWithOptions(
      const GURL& script_url,
      blink::mojom::ServiceWorkerRegistrationOptions options) {
    registration_ = base::MakeRefCounted<ServiceWorkerRegistration>(
        options, context()->storage()->NewRegistrationId(),
        context()->AsWeakPtr());
    SetUpVersion(script_url);
  }

  // Promotes |version_| to |registration_|'s active version, and then resets
  // |version_| to null (as subsequent DoRequest() calls should not attempt to
  // install or update |version_|).
  void ActivateVersion() {
    version_->set_fetch_handler_existence(
        ServiceWorkerVersion::FetchHandlerExistence::DOES_NOT_EXIST);
    version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
    registration_->SetActiveVersion(version_);
    version_ = nullptr;
  }

  // After this is called, |version_| will be a new, uninstalled version. The
  // next time DoRequest() is called, |version_| will attempt to install,
  // possibly updating if registration has an installed worker.
  void SetUpVersion(const GURL& script_url) {
    version_ = base::MakeRefCounted<ServiceWorkerVersion>(
        registration_.get(), script_url, blink::mojom::ScriptType::kClassic,
        context()->storage()->NewVersionId(), context()->AsWeakPtr());
    version_->SetStatus(ServiceWorkerVersion::NEW);

    if (registration_->waiting_version() || registration_->active_version())
      version_->SetToPauseAfterDownload(base::DoNothing());
  }

  void DoRequest(const GURL& url,
                 std::unique_ptr<network::TestURLLoaderClient>* out_client,
                 std::unique_ptr<ServiceWorkerNewScriptLoader>* out_loader,
                 ServiceWorkerNewScriptLoader::Type type =
                     ServiceWorkerNewScriptLoader::Type::kNetworkOnly) {
    DCHECK(registration_);
    DCHECK(version_);

    // Dummy values.
    int routing_id = 0;
    int request_id = 10;
    uint32_t options = 0;

    network::ResourceRequest request;
    request.url = url;
    request.method = "GET";
    request.resource_type = static_cast<int>((url == version_->script_url())
                                                 ? ResourceType::kServiceWorker
                                                 : ResourceType::kScript);

    *out_client = std::make_unique<network::TestURLLoaderClient>();
    if (type == ServiceWorkerNewScriptLoader::Type::kResume) {
      *out_loader = ServiceWorkerNewScriptLoader::CreateForResume(
          options, request, (*out_client)->CreateInterfacePtr(), version_);
      return;
    }

    *out_loader = ServiceWorkerNewScriptLoader::CreateForNetworkOnly(
        routing_id, request_id, options, request,
        (*out_client)->CreateInterfacePtr(), version_,
        helper_->url_loader_factory_getter()->GetNetworkFactory(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  }

  // Returns false if the entry for |url| doesn't exist in the storage.
  bool VerifyStoredResponse(const GURL& url) {
    return ServiceWorkerUpdateCheckTestUtils::VerifyStoredResponse(
        LookupResourceId(url), context()->storage(),
        mock_server_->Get(url).body);
  }

  int64_t LookupResourceId(const GURL& url) {
    return version_->script_cache_map()->LookupResourceId(url);
  }

 protected:
  TestBrowserThreadBundle thread_bundle_;
  std::unique_ptr<MockNetworkURLLoaderFactory> mock_url_loader_factory_;
  std::unique_ptr<EmbeddedWorkerTestHelper> helper_;

  scoped_refptr<ServiceWorkerRegistration> registration_;
  scoped_refptr<ServiceWorkerVersion> version_;
  std::unique_ptr<MockHTTPServer> mock_server_;
};

TEST_F(ServiceWorkerNewScriptLoaderTest, Success) {
  base::HistogramTester histogram_tester;

  const GURL kScriptURL(kNormalScriptURL);
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);

  // The client should have received the response.
  EXPECT_TRUE(client->has_received_response());
  EXPECT_TRUE(client->response_body().is_valid());
  std::string response;
  EXPECT_TRUE(
      mojo::BlockingCopyToString(client->response_body_release(), &response));
  EXPECT_EQ(mock_server_->Get(kScriptURL).body, response);

  // The response should also be stored in the storage.
  EXPECT_TRUE(VerifyStoredResponse(kScriptURL));
  histogram_tester.ExpectUniqueSample(kHistogramWriteResponseResult,
                                      ServiceWorkerMetrics::WRITE_OK, 1);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Success_EmptyBody) {
  base::HistogramTester histogram_tester;

  const GURL kScriptURL("https://example.com/empty.js");
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;
  mock_server_->Set(
      kScriptURL,
      MockHTTPServer::Response(std::string("HTTP/1.1 200 OK\n"
                                           "Content-Type: text/javascript\n\n"),
                               std::string()));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);

  // The client should have received the response.
  EXPECT_TRUE(client->has_received_response());
  EXPECT_FALSE(client->response_body().is_valid());

  // The response should also be stored in the storage.
  EXPECT_TRUE(VerifyStoredResponse(kScriptURL));
  // We don't record write response result if body is empty.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Success_LargeBody) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Create a response that has a larger body than the script loader's buffer
  // to test chunked data write. We chose this multiplier to avoid hitting the
  // limit of mojo's data pipe buffer (it's about kReadBufferSize * 2 as of
  // now).
  const uint32_t kBodySize =
      ServiceWorkerNewScriptLoader::kReadBufferSize * 1.6;
  const GURL kScriptURL("https://example.com/large-body.js");
  mock_server_->Set(
      kScriptURL,
      MockHTTPServer::Response(std::string("HTTP/1.1 200 OK\n"
                                           "Content-Type: text/javascript\n\n"),
                               std::string(kBodySize, 'a')));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);

  // The client should have received the response.
  EXPECT_TRUE(client->has_received_response());
  EXPECT_TRUE(client->response_body().is_valid());
  std::string response;
  EXPECT_TRUE(
      mojo::BlockingCopyToString(client->response_body_release(), &response));
  EXPECT_EQ(mock_server_->Get(kScriptURL).body, response);

  // The response should also be stored in the storage.
  EXPECT_TRUE(VerifyStoredResponse(kScriptURL));
  // WRITE_OK should be recorded twice as we record every single write success.
  histogram_tester.ExpectUniqueSample(kHistogramWriteResponseResult,
                                      ServiceWorkerMetrics::WRITE_OK, 2);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_404) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  const GURL kScriptURL("https://example.com/nonexistent.js");
  mock_server_->Set(kScriptURL, MockHTTPServer::Response(
                                    std::string("HTTP/1.1 404 Not Found\n\n"),
                                    std::string()));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because of the 404 response.
  EXPECT_EQ(net::ERR_INVALID_RESPONSE, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_Redirect) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  const GURL kScriptURL("https://example.com/redirect.js");
  mock_server_->Set(
      kScriptURL,
      MockHTTPServer::Response(
          std::string("HTTP/1.1 307 Temporary Redirect\n\n"), std::string()));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because of the redirected response.
  EXPECT_EQ(net::ERR_UNSAFE_REDIRECT, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_CertificateError) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Serve a response with a certificate error.
  const GURL kScriptURL("https://example.com/certificate-error.js");
  MockHTTPServer::Response response(std::string("HTTP/1.1 200 OK\n\n"),
                                    std::string("body"));
  response.has_certificate_error = true;
  mock_server_->Set(kScriptURL, response);
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because of the response with the certificate
  // error.
  EXPECT_EQ(net::ERR_CERT_DATE_INVALID, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_NoMimeType) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  const GURL kScriptURL("https://example.com/no-mime-type.js");
  mock_server_->Set(kScriptURL, MockHTTPServer::Response(
                                    std::string("HTTP/1.1 200 OK\n\n"),
                                    std::string("body with no MIME type")));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because of the response with no MIME type.
  EXPECT_EQ(net::ERR_INSECURE_RESPONSE, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_BadMimeType) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  const GURL kScriptURL("https://example.com/bad-mime-type.js");
  mock_server_->Set(kScriptURL, MockHTTPServer::Response(
                                    std::string("HTTP/1.1 200 OK\n"
                                                "Content-Type: text/css\n\n"),
                                    std::string("body with bad MIME type")));
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because of the response with the bad MIME
  // type.
  EXPECT_EQ(net::ERR_INSECURE_RESPONSE, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Success_PathRestriction) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // |kScope| is not under the default scope ("/out-of-scope/"), but the
  // Service-Worker-Allowed header allows it.
  const GURL kScriptURL("https://example.com/out-of-scope/normal.js");
  const GURL kScope("https://example.com/in-scope/");
  mock_server_->Set(kScriptURL,
                    MockHTTPServer::Response(
                        std::string("HTTP/1.1 200 OK\n"
                                    "Content-Type: text/javascript\n"
                                    "Service-Worker-Allowed: /in-scope/\n\n"),
                        std::string("٩( ’ω’ )و I'm body!")));
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScope;
  SetUpRegistrationWithOptions(kScriptURL, options);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);

  // The client should have received the response.
  EXPECT_TRUE(client->has_received_response());
  EXPECT_TRUE(client->response_body().is_valid());
  std::string response;
  EXPECT_TRUE(
      mojo::BlockingCopyToString(client->response_body_release(), &response));
  EXPECT_EQ(mock_server_->Get(kScriptURL).body, response);

  // The response should also be stored in the storage.
  EXPECT_TRUE(VerifyStoredResponse(kScriptURL));
  histogram_tester.ExpectUniqueSample(kHistogramWriteResponseResult,
                                      ServiceWorkerMetrics::WRITE_OK, 1);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_PathRestriction) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // |kScope| is not under the default scope ("/out-of-scope/") and the
  // Service-Worker-Allowed header is not specified.
  const GURL kScriptURL("https://example.com/out-of-scope/normal.js");
  const GURL kScope("https://example.com/in-scope/");
  mock_server_->Set(
      kScriptURL,
      MockHTTPServer::Response(std::string("HTTP/1.1 200 OK\n"
                                           "Content-Type: text/javascript\n\n"),
                               std::string()));
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScope;
  SetUpRegistrationWithOptions(kScriptURL, options);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();

  // The request should be failed because the scope is not allowed.
  EXPECT_EQ(net::ERR_INSECURE_RESPONSE, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Error_RedundantWorker) {
  base::HistogramTester histogram_tester;

  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  const GURL kScriptURL(kNormalScriptURL);
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);

  // Make the service worker redundant.
  version_->Doom();
  ASSERT_TRUE(version_->is_redundant());

  client->RunUntilComplete();

  // The request should be aborted.
  EXPECT_EQ(net::ERR_FAILED, client->completion_status().error_code);
  EXPECT_FALSE(client->has_received_response());

  // The response shouldn't be stored in the storage.
  EXPECT_FALSE(VerifyStoredResponse(kScriptURL));
  // No sample should be recorded since a write didn't occur.
  histogram_tester.ExpectTotalCount(kHistogramWriteResponseResult, 0);
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Update) {
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Set up a registration with an incumbent.
  const GURL kScriptURL(kNormalScriptURL);
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  ActivateVersion();

  // Change the script on the server.
  mock_server_->Set(
      kScriptURL,
      MockHTTPServer::Response(std::string("HTTP/1.1 200 OK\n"
                                           "Content-Type: text/javascript\n\n"),
                               std::string("this is the updated body")));

  // Attempt to update.
  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  // |version_| should have installed.
  EXPECT_EQ(1UL, version_->script_cache_map()->size());
}

TEST_F(ServiceWorkerNewScriptLoaderTest, Update_IdenticalScript) {
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Set up a registration with an incumbent.
  const GURL kScriptURL(kNormalScriptURL);
  SetUpRegistration(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  ActivateVersion();

  // Attempt to update.
  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  // The byte-to-byte check should detect the identical script, so the
  // |version_| should not have installed.
  EXPECT_EQ(0UL, version_->script_cache_map()->size());
}

// Tests cache validation behavior when updateViaCache is 'all'.
TEST_F(ServiceWorkerNewScriptLoaderTest, UpdateViaCache_All) {
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;
  const GURL kScriptURL(kNormalScriptURL);
  const GURL kImportedScriptURL(kNormalImportedScriptURL);

  // Set up a registration.
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScriptURL.GetWithoutFilename();
  options.update_via_cache = blink::mojom::ServiceWorkerUpdateViaCache::kAll;
  SetUpRegistrationWithOptions(kScriptURL, options);

  // Install the main script and imported script. The cache should validate
  // since last update time is null.
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  network::ResourceRequest request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  // Promote to active and prepare to update.
  ActivateVersion();
  registration_->set_last_update_check(base::Time::Now());

  // Attempt to update. The requests should not validate the cache since the
  // last update was recent.
  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_FALSE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_FALSE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  // Set update check to far in the past and repeat. The requests should
  // validate the cache.
  registration_->set_last_update_check(base::Time::Now() -
                                       base::TimeDelta::FromHours(24));

  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);
}

// Tests cache validation behavior when updateViaCache is 'imports'.
TEST_F(ServiceWorkerNewScriptLoaderTest, UpdateViaCache_Imports) {
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;
  const GURL kScriptURL(kNormalScriptURL);
  const GURL kImportedScriptURL(kNormalImportedScriptURL);

  // Set up a registration.
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScriptURL.GetWithoutFilename();
  options.update_via_cache =
      blink::mojom::ServiceWorkerUpdateViaCache::kImports;
  SetUpRegistrationWithOptions(kScriptURL, options);

  // Install the main script and imported script. The cache should be validated
  // since last update time is null.
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  network::ResourceRequest request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  // Promote to active and prepare to update.
  ActivateVersion();
  registration_->set_last_update_check(base::Time::Now());

  // Attempt to update. Only the imported script should validate the cache
  // because kImports.
  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_FALSE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  // Set the time to far in the past and repeat. The requests should validate
  // cache.
  registration_->set_last_update_check(base::Time::Now() -
                                       base::TimeDelta::FromHours(24));

  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);
}

// Tests cache validation behavior when updateViaCache is 'none'.
TEST_F(ServiceWorkerNewScriptLoaderTest, UpdateViaCache_None) {
  const GURL kScriptURL(kNormalScriptURL);
  const GURL kImportedScriptURL(kNormalImportedScriptURL);
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Set up a registration.
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScriptURL.GetWithoutFilename();
  options.update_via_cache = blink::mojom::ServiceWorkerUpdateViaCache::kNone;
  SetUpRegistrationWithOptions(kScriptURL, options);

  // Install the main script and imported script. The cache should be validated
  // since kNone (and the last update time is null anyway).
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  network::ResourceRequest request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  // Promote to active and prepare to update.
  ActivateVersion();
  registration_->set_last_update_check(base::Time::Now());

  // Attempt to update. The requests should validate the cache because KNone.
  SetUpVersion(kScriptURL);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);
}

// Tests respecting ServiceWorkerVersion's |force_bypass_cache_for_scripts|
// flag.
TEST_F(ServiceWorkerNewScriptLoaderTest, ForceBypassCache) {
  const GURL kScriptURL(kNormalScriptURL);
  const GURL kImportedScriptURL(kNormalImportedScriptURL);
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  // Set up a registration.
  blink::mojom::ServiceWorkerRegistrationOptions options;
  options.scope = kScriptURL.GetWithoutFilename();
  // Use kAll to contradict |force_bypass_cache_for_scripts|. The force flag
  // should win.
  options.update_via_cache = blink::mojom::ServiceWorkerUpdateViaCache::kAll;
  SetUpRegistrationWithOptions(kScriptURL, options);
  // Also set last_update_time to a recent time, so the 24 hour validation
  // doesn't kick in.
  registration_->set_last_update_check(base::Time::Now());

  version_->set_force_bypass_cache_for_scripts(true);

  // Install the main script and imported script. The cache should be validated.
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  network::ResourceRequest request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);

  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  request = mock_url_loader_factory_->last_request();
  EXPECT_TRUE(request.load_flags & net::LOAD_VALIDATE_CACHE);
}

// Tests that EmbeddedWorkerInstance's |network_accessed_for_script_| flag is
// set when the script loader accesses network. This flag is used to enforce the
// 24 hour cache validation.
TEST_F(ServiceWorkerNewScriptLoaderTest, AccessedNetwork) {
  const GURL kScriptURL(kNormalScriptURL);
  const GURL kImportedScriptURL(kNormalImportedScriptURL);
  std::unique_ptr<network::TestURLLoaderClient> client;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader;

  SetUpRegistration(kScriptURL);

  // Install the main script. The network accessed flag should be flipped on.
  version_->embedded_worker()->network_accessed_for_script_ = false;
  mock_url_loader_factory_->set_to_access_network(true);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  EXPECT_TRUE(version_->embedded_worker()->network_accessed_for_script());

  // Install the imported script. The network accessed flag should be unchanged,
  // as it's only meant for main scripts.
  version_->embedded_worker()->network_accessed_for_script_ = false;
  mock_url_loader_factory_->set_to_access_network(true);
  DoRequest(kImportedScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  EXPECT_FALSE(version_->embedded_worker()->network_accessed_for_script());

  // Install a new main script, this time simulating coming from cache. The
  // network accessed flag should be off.
  SetUpRegistration(kScriptURL);
  version_->embedded_worker()->network_accessed_for_script_ = false;
  mock_url_loader_factory_->set_to_access_network(false);
  DoRequest(kScriptURL, &client, &loader);
  client->RunUntilComplete();
  EXPECT_EQ(net::OK, client->completion_status().error_code);
  EXPECT_FALSE(version_->embedded_worker()->network_accessed_for_script());
}

// ServiceWorkerNewScriptLoaderResumeTest is for testing operations for
// resuming paused download of scripts when ImportedScriptUpdateCheck
// is enabled.
class ServiceWorkerNewScriptLoaderResumeTest
    : public ServiceWorkerNewScriptLoaderTest {
 public:
  ServiceWorkerNewScriptLoaderResumeTest() : kScriptURL(kNormalScriptURL) {
    feature_list_.InitAndEnableFeature(
        blink::features::kServiceWorkerImportedScriptUpdateCheck);
  }
  ~ServiceWorkerNewScriptLoaderResumeTest() override = default;

  void SetUp() override {
    ServiceWorkerNewScriptLoaderTest::SetUp();
    SetUpRegistration(kScriptURL);
    // Create the old script resource in storage.
    WriteToDiskCacheSync(context()->storage(), kScriptURL, kOldResourceId,
                         kOldHeaders, kOldData, std::string());
  }

  void SetUpComparedScriptInfo(
      size_t bytes_compared,
      const std::string& new_headers,
      const std::string& diff_data_block,
      ServiceWorkerNewScriptLoader::NetworkLoaderState network_loader_state,
      ServiceWorkerNewScriptLoader::WriterState body_writer_state) {
    // Create a data pipe which has the new block sent from the network.
    ASSERT_EQ(MOJO_RESULT_OK, mojo::CreateDataPipe(nullptr, &network_producer_,
                                                   &network_consumer_));
    ServiceWorkerUpdateCheckTestUtils::CreateAndSetComparedScriptInfoForVersion(
        kScriptURL, bytes_compared, new_headers, diff_data_block,
        kOldResourceId, kNewResourceId, helper_.get(), network_loader_state,
        body_writer_state, std::move(network_consumer_),
        ServiceWorkerSingleScriptUpdateChecker::Result::kDifferent,
        version_.get());
  }

  void NotifyLoaderCompletion(net::Error error) {
    network::URLLoaderCompletionStatus status;
    status.error_code = error;
    loader_->OnComplete(status);
  }

  // Verify the received response.
  void CheckReceivedResponse(const std::string& expected_body) {
    EXPECT_TRUE(client_->has_received_response());
    EXPECT_TRUE(client_->response_body().is_valid());

    // The response should also be stored in the storage.
    EXPECT_TRUE(ServiceWorkerUpdateCheckTestUtils::VerifyStoredResponse(
        LookupResourceId(kScriptURL), context()->storage(), expected_body));

    std::string response;
    EXPECT_TRUE(mojo::BlockingCopyToString(client_->response_body_release(),
                                           &response));
    EXPECT_EQ(expected_body, response);
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  const GURL kScriptURL;
  std::unique_ptr<network::TestURLLoaderClient> client_;
  std::unique_ptr<ServiceWorkerNewScriptLoader> loader_;
  const std::vector<std::pair<std::string, std::string>> kOldHeaders = {
      {"Content-Type", "text/javascript"},
      {"Content-Length", "14"}};
  const std::string kOldData = "old-block-data";
  const int64_t kOldResourceId = 1;
  const int64_t kNewResourceId = 2;
  mojo::ScopedDataPipeProducerHandle network_producer_;
  mojo::ScopedDataPipeConsumerHandle network_consumer_;
};

// Tests resume type loader when the first script data block is different.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, FirstBlockDifferent) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 24\0\0";
  const std::string kDiffBlock = "diff-block-";
  const std::string kNetworkBlock = "network-block";
  const std::string kNewData = kDiffBlock + kNetworkBlock;

  SetUpComparedScriptInfo(
      0, kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kLoadingBody,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);

  // Send network data.
  ASSERT_TRUE(mojo::BlockingCopyFromString(kNetworkBlock, network_producer_));
  network_producer_.reset();

  // Notify the completion of network loader.
  NotifyLoaderCompletion(net::OK);
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  // The client should have received the response.
  CheckReceivedResponse(kNewData);
}

// Tests resume type loader when the script data block in the middle is
// different.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, MiddleBlockDifferent) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 34\0\0";
  const std::string kSameBlock = "old-block";
  const std::string kDiffBlock = "|diff-block|";
  const std::string kNetworkBlock = "network-block";
  const std::string kNewData = kSameBlock + kDiffBlock + kNetworkBlock;

  SetUpComparedScriptInfo(
      kSameBlock.length(), kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kLoadingBody,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);

  // Send network data.
  ASSERT_TRUE(mojo::BlockingCopyFromString(kNetworkBlock, network_producer_));
  network_producer_.reset();

  // Notify the completion of network loader.
  NotifyLoaderCompletion(net::OK);
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  // The client should have received the response.
  CheckReceivedResponse(kNewData);
}

// Tests resume type loader when the last script data block is different.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, LastBlockDifferent) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 21\0\0";
  const std::string kSameBlock = "old-block";
  const std::string kDiffBlock = "|diff-block|";
  const std::string kNewData = kSameBlock + kDiffBlock;

  SetUpComparedScriptInfo(
      kSameBlock.length(), kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kLoadingBody,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);
  network_producer_.reset();

  // Notify the completion of network loader.
  NotifyLoaderCompletion(net::OK);
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  // The client should have received the response.
  CheckReceivedResponse(kNewData);
}

// Tests resume type loader when the last script data block is different and
// OnCompleted() has been called during update check.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, LastBlockDifferentCompleted) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 21\0\0";
  const std::string kSameBlock = "old-block";
  const std::string kDiffBlock = "|diff-block|";
  const std::string kNewData = kSameBlock + kDiffBlock;

  SetUpComparedScriptInfo(
      kSameBlock.length(), kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kCompleted,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);
  network_producer_.reset();
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  // The client should have received the response.
  CheckReceivedResponse(kNewData);
}

// Tests resume type loader when the new script has more data appended.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, NewScriptLargerThanOld) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 39\0\0";
  const std::string kSameBlock = kOldData;
  const std::string kDiffBlock = "|diff-block|";
  const std::string kNetworkBlock = "network-block";
  const std::string kNewData = kSameBlock + kDiffBlock + kNetworkBlock;

  SetUpComparedScriptInfo(
      kSameBlock.length(), kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kLoadingBody,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);

  // Send network data.
  ASSERT_TRUE(mojo::BlockingCopyFromString(kNetworkBlock, network_producer_));
  network_producer_.reset();

  // Notify the completion of network loader.
  NotifyLoaderCompletion(net::OK);
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  // The client should have received the response.
  CheckReceivedResponse(kNewData);
}

// Tests resume type loader when the script changed to have no body.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, NewScriptEmptyBody) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 0\0\0";
  const std::string kNewData = "";

  SetUpComparedScriptInfo(
      0, kNewHeaders, kNewData,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kCompleted,
      ServiceWorkerNewScriptLoader::WriterState::kCompleted);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);

  network_producer_.reset();
  client_->RunUntilComplete();

  EXPECT_EQ(net::OK, client_->completion_status().error_code);

  CheckReceivedResponse(kNewData);
}

// Tests resume type loader could report error when the resumed network
// download completed with error.
TEST_F(ServiceWorkerNewScriptLoaderResumeTest, CompleteFailed) {
  const std::string kNewHeaders =
      "HTTP/1.0 200 OK\0Content-Type: text/javascript\0Content-Length: 34\0\0";
  const std::string kSameBlock = "old-block";
  const std::string kDiffBlock = "|diff-block|";
  const std::string kNetworkBlock = "network-block";
  const std::string kNewData = kSameBlock + kDiffBlock + kNetworkBlock;

  SetUpComparedScriptInfo(
      kSameBlock.length(), kNewHeaders, kDiffBlock,
      ServiceWorkerNewScriptLoader::NetworkLoaderState::kLoadingBody,
      ServiceWorkerNewScriptLoader::WriterState::kWriting);

  DoRequest(kScriptURL, &client_, &loader_,
            ServiceWorkerNewScriptLoader::Type::kResume);
  network_producer_.reset();

  // Notify the failed completion of network loader.
  NotifyLoaderCompletion(net::ERR_FAILED);
  client_->RunUntilComplete();

  EXPECT_EQ(net::ERR_FAILED, client_->completion_status().error_code);
  EXPECT_EQ(kInvalidServiceWorkerResourceId, LookupResourceId(kScriptURL));
}

}  // namespace service_worker_new_script_loader_unittest
}  // namespace content
