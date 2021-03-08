// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_IMAGE_ANNOTATION_ANNOTATOR_H_
#define SERVICES_IMAGE_ANNOTATION_ANNOTATOR_H_

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/data_decoder/public/mojom/json_parser.mojom.h"
#include "services/image_annotation/public/mojom/image_annotation.mojom.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace service_manager {
class Connector;
}  // namespace service_manager

namespace image_annotation {

// The annotator communicates with the external image annotation server to
// perform image labeling at the behest of clients.
//
// Clients make requests of the annotator by providing an image "source ID"
// (which is either an image URL or hash of an image data URI) and an associated
// ImageProcessor (the interface through which the annotator can obtain image
// pixels if necessary).
//
// The annotator maintains a cache of previously-computed results, and will
// compute new results either by sending image URLs (for publicly-crawled
// images) or image pixels to the external server.
class Annotator : public mojom::Annotator {
 public:
  // The HTTP request header in which the API key should be transmitted.
  static constexpr char kGoogApiKeyHeader[] = "X-Goog-Api-Key";

  // The minimum side length needed to request description annotations.
  static constexpr int32_t kDescMinDimension = 150;

  // The maximum aspect ratio permitted to request description annotations.
  static constexpr double kDescMaxAspectRatio = 2.5;

  // Constructs an annotator.
  //  |server_url|        : the URL of the server with which the annotator
  //                        communicates. The annotator gracefully handles (i.e.
  //                        returns errors when constructed with) an empty
  //                        server URL.
  //  |api_key|           : the Google API key used to authenticate
  //                        communication with the image annotation server. If
  //                        empty, no API key header will be sent.
  //  |throttle|          : the miminum amount of time to wait between sending
  //                        new HTTP requests to the image annotation server.
  //  |batch_size|        : The maximum number of image annotation requests that
  //                        should be batched into a single request to the
  //                        server.
  //  |min_ocr_confidence|: The minimum confidence value needed to return an OCR
  //                        result.
  Annotator(GURL server_url,
            std::string api_key,
            base::TimeDelta throttle,
            int batch_size,
            double min_ocr_confidence,
            scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
            service_manager::Connector* connector);
  ~Annotator() override;

  // Start providing behavior for the given Mojo request.
  void BindRequest(mojom::AnnotatorRequest request);

  // mojom::Annotator:
  void AnnotateImage(const std::string& source_id,
                     const std::string& description_language_tag,
                     mojom::ImageProcessorPtr image,
                     AnnotateImageCallback callback) override;

 private:
  // The relevant info for a request from a client feature for a single image.
  struct ClientRequestInfo {
    ClientRequestInfo(mojom::ImageProcessorPtr image_processor,
                      AnnotateImageCallback callback);
    ~ClientRequestInfo();

    mojom::ImageProcessorPtr image_processor;  // The interface to use for local
                                               // processing for this client.

    AnnotateImageCallback callback;  // The callback to execute when
                                     // processing has finished.
  };

  // The relevant info for a request to the image annotation server for a single
  // image.
  struct ServerRequestInfo {
    ServerRequestInfo(const std::string& source_id,
                      bool desc_requested,
                      const std::string& desc_lang_tag,
                      const std::vector<uint8_t>& image_bytes);
    ServerRequestInfo(const ServerRequestInfo& other) = delete;
    ~ServerRequestInfo();

    // Use in a deque requires a move-assign operator.
    ServerRequestInfo& operator=(ServerRequestInfo&& other);
    ServerRequestInfo& operator=(const ServerRequestInfo& other) = delete;

    std::string source_id;  // The URL or hashed data URI for the image.

    bool desc_requested;  // Whether or not descriptions have been requested.
    std::string desc_lang_tag;  // The language in which descriptions have been
                                // requested.

    std::vector<uint8_t> image_bytes;  // The encoded pixel data for the image.
  };

  // The pair of (source ID, desc lang) for a client request.
  //
  // Unique request keys represent unique requests to the server (i.e. two
  // client requests that induce the same request key can be served by a single
  // server request).
  using RequestKey = std::pair<std::string, std::string>;

  // List of URL loader objects.
  using UrlLoaderList = std::list<std::unique_ptr<network::SimpleURLLoader>>;

  // Returns true if the given dimensions fit the policy of the description
  // backend (i.e. the image has size / shape on which it is acceptable to run
  // the description model).
  static bool IsWithinDescPolicy(int32_t width, int32_t height);

  // Constructs and returns a JSON object containing an request for the
  // given images.
  static std::string FormatJsonRequest(
      std::deque<ServerRequestInfo>::iterator begin_it,
      std::deque<ServerRequestInfo>::iterator end_it);

  // Creates a URL loader that calls the image annotation server with an
  // annotation request for the given images.
  static std::unique_ptr<network::SimpleURLLoader> MakeRequestLoader(
      const GURL& server_url,
      const std::string& api_key,
      std::deque<ServerRequestInfo>::iterator begin_it,
      std::deque<ServerRequestInfo>::iterator end_it);

  // Create or reuse a connection to the data decoder service for safe JSON
  // parsing.
  data_decoder::mojom::JsonParser& GetJsonParser();

  // Removes the given request, reassigning local processing if its associated
  // image processor had some ongoing.
  void RemoveRequestInfo(const RequestKey& request_key,
                         std::list<ClientRequestInfo>::iterator request_info_it,
                         bool canceled);

  // Called when a local handler returns compressed image data for the given
  // request key.
  void OnJpgImageDataReceived(
      const RequestKey& request_key,
      std::list<ClientRequestInfo>::iterator request_info_it,
      const std::vector<uint8_t>& image_bytes,
      int32_t width,
      int32_t height);

  // Called periodically to send the next batch of requests to the image
  // annotation server.
  void SendRequestBatchToServer();

  // Called when the image annotation server responds with annotations for the
  // given request keys.
  void OnServerResponseReceived(const std::set<RequestKey>& request_keys,
                                UrlLoaderList::iterator server_request_it,
                                std::unique_ptr<std::string> json_response);

  // Called when the data decoder service provides parsed JSON data for a server
  // response.
  void OnResponseJsonParsed(const std::set<RequestKey>& request_keys,
                            base::Optional<base::Value> json_data,
                            const base::Optional<std::string>& error);

  // Adds the given results to the cache (if successful) and notifies clients.
  void ProcessResults(
      const std::set<RequestKey>& request_keys,
      const std::map<std::string, mojom::AnnotateImageResultPtr>& results);

  // Maps from request key to previously-obtained annotation results.
  // TODO(crbug.com/916420): periodically clear entries from this cache.
  std::map<RequestKey, mojom::AnnotateImageResultPtr> cached_results_;

  // Maps from request key to its list of request infos (i.e. info of clients
  // that have made requests with that language and source ID).
  std::map<RequestKey, std::list<ClientRequestInfo>> request_infos_;

  // Maps from request keys of images currently being locally processed to the
  // ImageProcessors responsible for their processing.
  //
  // The value is a weak pointer to an entry in the client info list for the
  // given request key.
  //
  // Note that separate local processing will be scheduled for two requests that
  // share a source ID but differ in language. This is suboptimal; in future we
  // could share local processing among all relevant requests.
  std::map<RequestKey, mojom::ImageProcessorPtr*> local_processors_;

  // A list of currently-ongoing HTTP requests to the image annotation server.
  UrlLoaderList ongoing_server_requests_;

  // A queue of requests for the image annotation server waiting to be made.
  std::deque<ServerRequestInfo> server_request_queue_;

  // The set of request keys for which a server request has been scheduled but
  // not yet returned to clients.
  //
  // This comprises request keys for which:
  //   - A server query has been queued (but not yet sent),
  //   - A server query is ongoing,
  //   - A server query has been returned and is being parsed.
  std::set<RequestKey> pending_requests_;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  service_manager::Connector* const connector_;

  mojo::BindingSet<mojom::Annotator> bindings_;

  // Should not be used directly; GetJsonParser() should be called instead.
  data_decoder::mojom::JsonParserPtr json_parser_;

  // A timer used to throttle server request frequency.
  base::RepeatingTimer server_request_timer_;

  const GURL server_url_;

  const std::string api_key_;

  const int batch_size_;

  const double min_ocr_confidence_;

  DISALLOW_COPY_AND_ASSIGN(Annotator);
};

}  // namespace image_annotation

#endif  // SERVICES_IMAGE_ANNOTATION_ANNOTATOR_H_
