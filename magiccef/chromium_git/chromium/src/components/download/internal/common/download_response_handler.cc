// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/download/public/common/download_response_handler.h"

#include <memory>

#include "base/metrics/histogram_functions.h"
#include "components/download/public/common/download_stats.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/download/public/common/download_utils.h"
#include "net/http/http_status_code.h"

namespace download {

namespace {

mojom::NetworkRequestStatus ConvertInterruptReasonToMojoNetworkRequestStatus(
    DownloadInterruptReason reason) {
  switch (reason) {
    case DOWNLOAD_INTERRUPT_REASON_NONE:
      return mojom::NetworkRequestStatus::OK;
    case DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT:
      return mojom::NetworkRequestStatus::NETWORK_TIMEOUT;
    case DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED:
      return mojom::NetworkRequestStatus::NETWORK_DISCONNECTED;
    case DOWNLOAD_INTERRUPT_REASON_NETWORK_SERVER_DOWN:
      return mojom::NetworkRequestStatus::NETWORK_SERVER_DOWN;
    case DOWNLOAD_INTERRUPT_REASON_SERVER_NO_RANGE:
      return mojom::NetworkRequestStatus::SERVER_NO_RANGE;
    case DOWNLOAD_INTERRUPT_REASON_SERVER_CONTENT_LENGTH_MISMATCH:
      return mojom::NetworkRequestStatus::SERVER_CONTENT_LENGTH_MISMATCH;
    case DOWNLOAD_INTERRUPT_REASON_SERVER_UNREACHABLE:
      return mojom::NetworkRequestStatus::SERVER_UNREACHABLE;
    case DOWNLOAD_INTERRUPT_REASON_SERVER_CERT_PROBLEM:
      return mojom::NetworkRequestStatus::SERVER_CERT_PROBLEM;
    case DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
      return mojom::NetworkRequestStatus::USER_CANCELED;
    case DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED:
      return mojom::NetworkRequestStatus::NETWORK_FAILED;
    default:
      NOTREACHED();
      return mojom::NetworkRequestStatus::NETWORK_FAILED;
  }
}

}  // namespace

DownloadResponseHandler::DownloadResponseHandler(
    network::ResourceRequest* resource_request,
    Delegate* delegate,
    std::unique_ptr<DownloadSaveInfo> save_info,
    bool is_parallel_request,
    bool is_transient,
    bool fetch_error_body,
    bool follow_cross_origin_redirects,
    const DownloadUrlParameters::RequestHeadersType& request_headers,
    const std::string& request_origin,
    DownloadSource download_source,
    std::vector<GURL> url_chain)
    : delegate_(delegate),
      started_(false),
      save_info_(std::move(save_info)),
      url_chain_(std::move(url_chain)),
      method_(resource_request->method),
      referrer_(resource_request->referrer),
      referrer_policy_(resource_request->referrer_policy),
      is_transient_(is_transient),
      fetch_error_body_(fetch_error_body),
      follow_cross_origin_redirects_(follow_cross_origin_redirects),
      first_origin_(url::Origin::Create(resource_request->url)),
      request_headers_(request_headers),
      request_origin_(request_origin),
      download_source_(download_source),
      has_strong_validators_(false),
      is_partial_request_(save_info_->offset > 0),
      completed_(false),
      abort_reason_(DOWNLOAD_INTERRUPT_REASON_NONE) {
  if (!is_parallel_request) {
    RecordDownloadCountWithSource(UNTHROTTLED_COUNT, download_source);
  }
  if (resource_request->request_initiator.has_value())
    request_initiator_ = resource_request->request_initiator;
}

DownloadResponseHandler::~DownloadResponseHandler() = default;

void DownloadResponseHandler::OnReceiveResponse(
    const network::ResourceResponseHead& head) {
  create_info_ = CreateDownloadCreateInfo(head);
  cert_status_ = head.cert_status;

  // TODO(xingliu): Do not use http cache.
  // Sets page transition type correctly and call
  // |RecordDownloadSourcePageTransitionType| here.
  if (head.headers) {
    has_strong_validators_ = head.headers->HasStrongValidators();
    RecordDownloadHttpResponseCode(head.headers->response_code());
    RecordDownloadContentDisposition(create_info_->content_disposition);
  }

  // Blink verifies that the requester of this download is allowed to set a
  // suggested name for the security origin of the downlaod URL. However, this
  // assumption doesn't hold if there were cross origin redirects. Therefore,
  // clear the suggested_name for such requests.
  if (request_initiator_.has_value() &&
      !create_info_->url_chain.back().SchemeIsBlob() &&
      !create_info_->url_chain.back().SchemeIs(url::kAboutScheme) &&
      !create_info_->url_chain.back().SchemeIs(url::kDataScheme) &&
      request_initiator_.value() !=
          url::Origin::Create(create_info_->url_chain.back())) {
    create_info_->save_info->suggested_name.clear();
  }

  if (create_info_->result != DOWNLOAD_INTERRUPT_REASON_NONE)
    OnResponseStarted(mojom::DownloadStreamHandlePtr());
}

std::unique_ptr<DownloadCreateInfo>
DownloadResponseHandler::CreateDownloadCreateInfo(
    const network::ResourceResponseHead& head) {
  auto create_info = std::make_unique<DownloadCreateInfo>(
      base::Time::Now(), std::move(save_info_));

  DownloadInterruptReason result =
      head.headers
          ? HandleSuccessfulServerResponse(
                *head.headers, create_info->save_info.get(), fetch_error_body_)
          : DOWNLOAD_INTERRUPT_REASON_NONE;

  create_info->total_bytes = head.content_length > 0 ? head.content_length : 0;
  create_info->result = result;
  if (result == DOWNLOAD_INTERRUPT_REASON_NONE)
    create_info->remote_address = head.remote_endpoint.ToStringWithoutPort();
  create_info->method = method_;
  create_info->connection_info = head.connection_info;
  create_info->url_chain = url_chain_;
  create_info->referrer_url = referrer_;
  create_info->referrer_policy = referrer_policy_;
  create_info->transient = is_transient_;
  create_info->response_headers = head.headers;
  create_info->offset = create_info->save_info->offset;
  create_info->mime_type = head.mime_type;
  create_info->fetch_error_body = fetch_error_body_;
  create_info->request_headers = request_headers_;
  create_info->request_origin = request_origin_;
  create_info->download_source = download_source_;
  create_info->request_initiator = request_initiator_;

  HandleResponseHeaders(head.headers.get(), create_info.get());
  return create_info;
}

void DownloadResponseHandler::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    const network::ResourceResponseHead& head) {
  if (!follow_cross_origin_redirects_ &&
      !first_origin_.IsSameOriginWith(
          url::Origin::Create(redirect_info.new_url))) {
    abort_reason_ = DOWNLOAD_INTERRUPT_REASON_SERVER_CROSS_ORIGIN_REDIRECT;
    url_chain_.push_back(redirect_info.new_url);
    method_ = redirect_info.new_method;
    referrer_ = GURL(redirect_info.new_referrer);
    referrer_policy_ = redirect_info.new_referrer_policy;
    OnComplete(network::URLLoaderCompletionStatus(net::OK));
    return;
  }
  if (is_partial_request_) {
    // A redirect while attempting a partial resumption indicates a potential
    // middle box. Trigger another interruption so that the
    // DownloadItem can retry.
    abort_reason_ = DOWNLOAD_INTERRUPT_REASON_SERVER_UNREACHABLE;
    OnComplete(network::URLLoaderCompletionStatus(net::OK));
    return;
  }

  // Check if redirect URL is web safe.
  if (delegate_ && !delegate_->CanRequestURL(redirect_info.new_url)) {
    abort_reason_ = DOWNLOAD_INTERRUPT_REASON_NETWORK_INVALID_REQUEST;
    OnComplete(network::URLLoaderCompletionStatus(net::OK));
    return;
  }

  url_chain_.push_back(redirect_info.new_url);
  method_ = redirect_info.new_method;
  referrer_ = GURL(redirect_info.new_referrer);
  referrer_policy_ = redirect_info.new_referrer_policy;
  delegate_->OnReceiveRedirect();
}

void DownloadResponseHandler::OnUploadProgress(
    int64_t current_position,
    int64_t total_size,
    OnUploadProgressCallback callback) {
  delegate_->OnUploadProgress(current_position);
  std::move(callback).Run();
}

void DownloadResponseHandler::OnReceiveCachedMetadata(
    mojo_base::BigBuffer data) {}

void DownloadResponseHandler::OnTransferSizeUpdated(
    int32_t transfer_size_diff) {}

void DownloadResponseHandler::OnStartLoadingResponseBody(
    mojo::ScopedDataPipeConsumerHandle body) {
  if (started_)
    return;

  mojom::DownloadStreamHandlePtr stream_handle =
      mojom::DownloadStreamHandle::New();
  stream_handle->stream = std::move(body);
  stream_handle->client_request = mojo::MakeRequest(&client_ptr_);
  OnResponseStarted(std::move(stream_handle));
}

void DownloadResponseHandler::OnComplete(
    const network::URLLoaderCompletionStatus& status) {
  if (completed_)
    return;

  completed_ = true;
  DownloadInterruptReason reason = HandleRequestCompletionStatus(
      static_cast<net::Error>(status.error_code), has_strong_validators_,
      cert_status_, abort_reason_);

  if (client_ptr_) {
    client_ptr_->OnStreamCompleted(
        ConvertInterruptReasonToMojoNetworkRequestStatus(reason));
  }

  if (reason == DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED) {
    base::UmaHistogramSparse("Download.MapErrorNetworkFailed.NetworkService",
                             std::abs(status.error_code));
  }

  if (started_) {
    delegate_->OnResponseCompleted();
    return;
  }

  // OnComplete() called without OnResponseStarted(). This should only
  // happen when the request was aborted.
  if (!create_info_)
    create_info_ = CreateDownloadCreateInfo(network::ResourceResponseHead());
  create_info_->result = reason == DOWNLOAD_INTERRUPT_REASON_NONE
                             ? DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED
                             : reason;

  OnResponseStarted(mojom::DownloadStreamHandlePtr());
  delegate_->OnResponseCompleted();
}

void DownloadResponseHandler::OnResponseStarted(
    mojom::DownloadStreamHandlePtr stream_handle) {
  started_ = true;
  delegate_->OnResponseStarted(std::move(create_info_),
                               std::move(stream_handle));
}

}  // namespace download
