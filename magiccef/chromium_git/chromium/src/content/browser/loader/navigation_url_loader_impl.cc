// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/navigation_url_loader_impl.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/debug/dump_without_crashing.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "base/optional.h"
#include "base/stl_util.h"
#include "base/task/post_task.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/download/public/common/download_stats.h"
#include "content/browser/about_url_loader_factory.h"
#include "content/browser/appcache/appcache_navigation_handle.h"
#include "content/browser/appcache/appcache_navigation_handle_core.h"
#include "content/browser/appcache/appcache_request_handler.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/data_url_loader_factory.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/file_url_loader_factory.h"
#include "content/browser/fileapi/file_system_url_loader_factory.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/navigation_request_info.h"
#include "content/browser/loader/navigation_loader_interceptor.h"
#include "content/browser/loader/navigation_url_loader_delegate.h"
#include "content/browser/loader/prefetch_url_loader_service.h"
#include "content/browser/loader/prefetched_signed_exchange_cache.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/loader/resource_request_info_impl.h"
#include "content/browser/navigation_subresource_loader_params.h"
#include "content/browser/resource_context_impl.h"
#include "content/browser/service_worker/service_worker_navigation_handle.h"
#include "content/browser/service_worker/service_worker_navigation_handle_core.h"
#include "content/browser/service_worker/service_worker_provider_host.h"
#include "content/browser/service_worker/service_worker_request_handler.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/url_loader_factory_getter.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/web_package/signed_exchange_consts.h"
#include "content/browser/web_package/signed_exchange_request_handler.h"
#include "content/browser/web_package/signed_exchange_url_loader_factory_for_non_network_service.h"
#include "content/browser/web_package/signed_exchange_utils.h"
#include "content/browser/webui/url_data_manager_backend.h"
#include "content/browser/webui/web_ui_url_loader_factory_internal.h"
#include "content/common/mime_sniffing_throttle.h"
#include "content/common/net/record_load_histograms.h"
#include "content/common/throttling_url_loader.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/download_utils.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_data.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "content/public/browser/shared_cors_origin_access_list.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/url_loader_request_interceptor.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/referrer.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "content/public/common/webplugininfo.h"
#include "net/base/load_flags.h"
#include "net/cert/sct_status_flags.h"
#include "net/cert/signed_certificate_timestamp_and_status.h"
#include "net/http/http_content_disposition.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_status_code.h"
#include "net/ssl/ssl_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_util.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "ppapi/buildflags/buildflags.h"
#include "services/network/loader_util.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"

#if defined(OS_ANDROID)
#include "content/browser/android/content_url_loader_factory.h"
#endif

namespace content {

namespace {

class NavigationLoaderInterceptorBrowserContainer
    : public NavigationLoaderInterceptor {
 public:
  explicit NavigationLoaderInterceptorBrowserContainer(
      std::unique_ptr<URLLoaderRequestInterceptor> browser_interceptor)
      : browser_interceptor_(std::move(browser_interceptor)) {}

  ~NavigationLoaderInterceptorBrowserContainer() override = default;

  void MaybeCreateLoader(
      const network::ResourceRequest& tentative_resource_request,
      ResourceContext* resource_context,
      LoaderCallback callback,
      FallbackCallback fallback_callback) override {
    browser_interceptor_->MaybeCreateLoader(
        tentative_resource_request, resource_context, std::move(callback));
  }

 private:
  std::unique_ptr<URLLoaderRequestInterceptor> browser_interceptor_;
};

// Only used on the IO thread.
base::LazyInstance<NavigationURLLoaderImpl::BeginNavigationInterceptor>::Leaky
    g_interceptor = LAZY_INSTANCE_INITIALIZER;

// Only used on the IO thread.
base::LazyInstance<NavigationURLLoaderImpl::URLLoaderFactoryInterceptor>::Leaky
    g_loader_factory_interceptor = LAZY_INSTANCE_INITIALIZER;

size_t GetCertificateChainsSizeInKB(const net::SSLInfo& ssl_info) {
  base::Pickle cert_pickle;
  ssl_info.cert->Persist(&cert_pickle);
  base::Pickle unverified_cert_pickle;
  ssl_info.unverified_cert->Persist(&unverified_cert_pickle);
  return (cert_pickle.size() + unverified_cert_pickle.size()) / 1000;
}

const net::NetworkTrafficAnnotationTag kNavigationUrlLoaderTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("navigation_url_loader", R"(
      semantics {
        sender: "Navigation URL Loader"
        description:
          "This request is issued by a main frame navigation to fetch the "
          "content of the page that is being navigated to."
        trigger:
          "Navigating Chrome (by clicking on a link, bookmark, history item, "
          "using session restore, etc)."
        data:
          "Arbitrary site-controlled data can be included in the URL, HTTP "
          "headers, and request body. Requests may include cookies and "
          "site-specific credentials."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: YES
        cookies_store: "user"
        setting: "This feature cannot be disabled."
        chrome_policy {
          URLBlacklist {
            URLBlacklist: { entries: '*' }
          }
        }
        chrome_policy {
          URLWhitelist {
            URLWhitelist { }
          }
        }
      }
      comments:
        "Chrome would be unable to navigate to websites without this type of "
        "request. Using either URLBlacklist or URLWhitelist policies (or a "
        "combination of both) limits the scope of these requests."
      )");

std::unique_ptr<network::ResourceRequest> CreateResourceRequest(
    NavigationRequestInfo* request_info,
    int frame_tree_node_id,
    bool allow_download) {
  // TODO(scottmg): Port over stuff from RDHI::BeginNavigationRequest() here.
  auto new_request = std::make_unique<network::ResourceRequest>();

  new_request->method = request_info->common_params.method;
  new_request->url = request_info->common_params.url;
  new_request->site_for_cookies = request_info->site_for_cookies;
  new_request->top_frame_origin = request_info->top_frame_origin;

  net::RequestPriority net_priority = net::HIGHEST;
  if (!request_info->is_main_frame &&
      base::FeatureList::IsEnabled(features::kLowPriorityIframes)) {
    net_priority = net::LOWEST;
  }
  new_request->priority = net_priority;

  new_request->render_frame_id = frame_tree_node_id;

  // The code below to set fields like request_initiator, referrer, etc has
  // been copied from ResourceDispatcherHostImpl. We did not refactor the
  // common code into a function, because RDHI uses accessor functions on the
  // URLRequest class to set these fields. whereas we use ResourceRequest here.
  new_request->request_initiator = request_info->common_params.initiator_origin;
  new_request->referrer = request_info->common_params.referrer.url;
  new_request->referrer_policy = Referrer::ReferrerPolicyForUrlRequest(
      request_info->common_params.referrer.policy);
  new_request->headers.AddHeadersFromString(
      request_info->begin_params->headers);

  new_request->resource_type =
      static_cast<int>(request_info->is_main_frame ? ResourceType::kMainFrame
                                                   : ResourceType::kSubFrame);
  if (request_info->is_main_frame)
    new_request->update_first_party_url_on_redirect = true;

  int load_flags = request_info->begin_params->load_flags;
  if (request_info->is_main_frame)
    load_flags |= net::LOAD_MAIN_FRAME_DEPRECATED;

  // Sync loads should have maximum priority and should be the only
  // requests that have the ignore limits flag set.
  DCHECK(!(load_flags & net::LOAD_IGNORE_LIMITS));

  new_request->load_flags = load_flags;

  new_request->request_body = request_info->common_params.post_data.get();
  new_request->report_raw_headers = request_info->report_raw_headers;
  new_request->allow_download = allow_download;
  new_request->has_user_gesture = request_info->common_params.has_user_gesture;
  new_request->enable_load_timing = true;

  new_request->fetch_request_mode = network::mojom::FetchRequestMode::kNavigate;
  new_request->fetch_credentials_mode =
      network::mojom::FetchCredentialsMode::kInclude;
  new_request->fetch_redirect_mode = network::mojom::FetchRedirectMode::kManual;
  new_request->fetch_request_context_type =
      static_cast<int>(request_info->begin_params->request_context_type);
  new_request->upgrade_if_insecure = request_info->upgrade_if_insecure;
  new_request->throttling_profile_id = request_info->devtools_frame_token;
  new_request->transition_type = request_info->common_params.transition;
  new_request->previews_state = request_info->common_params.previews_state;
  new_request->devtools_request_id =
      request_info->devtools_navigation_token.ToString();
  return new_request;
}

// Used only when NetworkService is disabled.
std::unique_ptr<NavigationRequestInfo> CreateNavigationRequestInfoForRedirect(
    const NavigationRequestInfo& previous_request_info,
    const network::ResourceRequest& updated_resource_request) {
  DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));

  CommonNavigationParams new_common_params =
      previous_request_info.common_params;
  new_common_params.url = updated_resource_request.url;
  new_common_params.referrer =
      Referrer(updated_resource_request.referrer,
               Referrer::NetReferrerPolicyToBlinkReferrerPolicy(
                   updated_resource_request.referrer_policy));
  new_common_params.method = updated_resource_request.method;
  new_common_params.post_data = updated_resource_request.request_body;

  mojom::BeginNavigationParamsPtr new_begin_params =
      previous_request_info.begin_params.Clone();
  new_begin_params->headers = updated_resource_request.headers.ToString();

  return std::make_unique<NavigationRequestInfo>(
      std::move(new_common_params), std::move(new_begin_params),
      updated_resource_request.site_for_cookies,
      updated_resource_request.top_frame_origin,
      previous_request_info.is_main_frame,
      previous_request_info.parent_is_main_frame,
      previous_request_info.are_ancestors_secure,
      previous_request_info.frame_tree_node_id,
      previous_request_info.is_for_guests_only,
      previous_request_info.report_raw_headers,
      previous_request_info.is_prerendering,
      previous_request_info.upgrade_if_insecure,
      nullptr /* blob_url_loader_factory */,
      previous_request_info.devtools_navigation_token,
      previous_request_info.devtools_frame_token);
}

// Called for requests that we don't have a URLLoaderFactory for.
void UnknownSchemeCallback(
    bool handled_externally,
    const network::ResourceRequest& /* resource_request */,
    network::mojom::URLLoaderRequest request,
    network::mojom::URLLoaderClientPtr client) {
  client->OnComplete(network::URLLoaderCompletionStatus(
      handled_externally ? net::ERR_ABORTED : net::ERR_UNKNOWN_URL_SCHEME));
}

// Determines whether it is safe to redirect from |from_url| to |to_url|.
bool IsRedirectSafe(const GURL& from_url,
                    const GURL& to_url,
                    ResourceContext* resource_context) {
  return IsSafeRedirectTarget(from_url, to_url) &&
         GetContentClient()->browser()->IsSafeRedirectTarget(to_url,
                                                             resource_context);
}

}  // namespace

// Kept around during the lifetime of the navigation request, and is
// responsible for dispatching a ResourceRequest to the appropriate
// URLLoader.  In order to get the right URLLoader it builds a vector
// of NavigationLoaderInterceptors and successively calls MaybeCreateLoader
// on each until the request is successfully handled. The same sequence
// may be performed multiple times when redirects happen.
// TODO(michaeln): Expose this class and add more unittests.
class NavigationURLLoaderImpl::URLLoaderRequestController
    : public network::mojom::URLLoaderClient {
 public:
  URLLoaderRequestController(
      std::vector<std::unique_ptr<NavigationLoaderInterceptor>>
          initial_interceptors,
      std::unique_ptr<network::ResourceRequest> resource_request,
      ResourceContext* resource_context,
      const GURL& url,
      bool is_main_frame,
      network::mojom::URLLoaderFactoryRequest proxied_factory_request,
      network::mojom::URLLoaderFactoryPtrInfo proxied_factory_info,
      std::set<std::string> known_schemes,
      bool bypass_redirect_checks,
      const base::WeakPtr<NavigationURLLoaderImpl>& owner)
      : interceptors_(std::move(initial_interceptors)),
        resource_request_(std::move(resource_request)),
        resource_context_(resource_context),
        url_(url),
        owner_(owner),
        response_loader_binding_(this),
        proxied_factory_request_(std::move(proxied_factory_request)),
        proxied_factory_info_(std::move(proxied_factory_info)),
        known_schemes_(std::move(known_schemes)),
        bypass_redirect_checks_(bypass_redirect_checks),
        weak_factory_(this) {}

  ~URLLoaderRequestController() override {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);

    // If neither OnCompleted nor OnReceivedResponse has been invoked, the
    // request was canceled before receiving a response, so log a cancellation.
    // Results after receiving a non-error response are logged in the renderer,
    // if the request is passed to one. If it's a download, or not passed to a
    // renderer for some other reason, results will not be logged for the
    // request. The net::OK check may not be necessary - the case where OK is
    // received without receiving any headers looks broken, anyways.
    if (!received_response_ && (!status_ || status_->error_code != net::OK)) {
      RecordLoadHistograms(
          url_, static_cast<ResourceType>(resource_request_->resource_type),
          status_ ? status_->error_code : net::ERR_ABORTED);
    }
  }

  static uint32_t GetURLLoaderOptions(bool is_main_frame) {
    uint32_t options = network::mojom::kURLLoadOptionNone;

    // Ensure that Mime sniffing works.
    options |= network::mojom::kURLLoadOptionSniffMimeType;

    if (is_main_frame) {
      // SSLInfo is not needed on subframe responses because users can inspect
      // only the certificate for the main frame when using the info bubble.
      options |= network::mojom::kURLLoadOptionSendSSLInfoWithResponse;
      options |= network::mojom::kURLLoadOptionSendSSLInfoForCertificateError;
    }

    if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      // TODO(arthursonzogni): This is a temporary option. Remove this as soon
      // as the InterceptingResourceHandler is removed.
      // See https://crbug.com/791049.
      options |= network::mojom::kURLLoadOptionPauseOnResponseStarted;
    }

    return options;
  }

  SingleRequestURLLoaderFactory::RequestHandler
  CreateDefaultRequestHandlerForNonNetworkService(
      net::URLRequestContextGetter* url_request_context_getter,
      storage::FileSystemContext* upload_file_system_context,
      AppCacheNavigationHandleCore* appcache_handle_core,
      bool was_request_intercepted) const {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));
    DCHECK(started_);

    return base::BindOnce(
        &URLLoaderRequestController::CreateNonNetworkServiceURLLoader,
        weak_factory_.GetWeakPtr(),
        base::Unretained(url_request_context_getter),
        base::Unretained(upload_file_system_context),
        // If the request has already been intercepted, the request should not
        // be intercepted again.
        base::Unretained(was_request_intercepted ? nullptr
                                                 : appcache_handle_core));
  }

  void CreateNonNetworkServiceURLLoader(
      net::URLRequestContextGetter* url_request_context_getter,
      storage::FileSystemContext* upload_file_system_context,
      AppCacheNavigationHandleCore* appcache_handle_core,
      const network::ResourceRequest& /* resource_request */,
      network::mojom::URLLoaderRequest url_loader,
      network::mojom::URLLoaderClientPtr url_loader_client) {
    // |resource_request| is unused here. We don't propagate the fields to
    // |request_info_| here because the request will usually go to
    // ResourceDispatcherHost which does its own request modifications.
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));
    DCHECK(started_);

    default_loader_used_ = true;
    uint32_t options = GetURLLoaderOptions(request_info_->is_main_frame);

    // A URLLoaderThrottle may have changed the headers.
    request_info_->begin_params->headers =
        resource_request_->headers.ToString();
    request_info_->begin_params->load_flags = resource_request_->load_flags;

    bool intercepted = false;
    if (g_interceptor.Get()) {
      // Recreate the ResourceRequest for the interceptor, in case a
      // URLLoaderThrottle had changed request_info_.
      auto latest_resource_request =
          CreateResourceRequest(request_info_.get(), frame_tree_node_id_,
                                resource_request_->allow_download);
      latest_resource_request->headers.AddHeadersFromString(
          request_info_->begin_params->headers);
      intercepted = g_interceptor.Get().Run(
          &url_loader, frame_tree_node_id_, 0 /* request_id */, options,
          *latest_resource_request, &url_loader_client,
          net::MutableNetworkTrafficAnnotationTag(
              kNavigationUrlLoaderTrafficAnnotation));
    }

    // The ResourceDispatcherHostImpl can be null in unit tests.
    ResourceDispatcherHostImpl* rdh = ResourceDispatcherHostImpl::Get();
    if (!intercepted && rdh) {
      rdh->BeginNavigationRequest(
          resource_context_, url_request_context_getter->GetURLRequestContext(),
          upload_file_system_context, *request_info_,
          std::move(navigation_ui_data_), std::move(url_loader_client),
          std::move(url_loader), appcache_handle_core, options,
          resource_request_->priority, global_request_id_);
    }

    // TODO(arthursonzogni): Detect when the ResourceDispatcherHost didn't
    // create a URLLoader. When it doesn't, do not send OnRequestStarted().
    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(&NavigationURLLoaderImpl::OnRequestStarted, owner_,
                       base::TimeTicks::Now()));
  }

  void StartWithoutNetworkService(
      net::URLRequestContextGetter* url_request_context_getter,
      storage::FileSystemContext* upload_file_system_context,
      ServiceWorkerNavigationHandleCore* service_worker_navigation_handle_core,
      AppCacheNavigationHandleCore* appcache_handle_core,
      scoped_refptr<PrefetchedSignedExchangeCache>
          prefetched_signed_exchange_cache,
      scoped_refptr<SignedExchangePrefetchMetricRecorder>
          signed_exchange_prefetch_metric_recorder,
      std::unique_ptr<NavigationRequestInfo> request_info,
      std::unique_ptr<NavigationUIData> navigation_ui_data,
      std::string accept_langs) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));
    DCHECK(!started_);
    started_ = true;
    request_info_ = std::move(request_info);
    frame_tree_node_id_ = request_info_->frame_tree_node_id;
    web_contents_getter_ = base::BindRepeating(
        &WebContents::FromFrameTreeNodeId, frame_tree_node_id_);
    navigation_ui_data_ = std::move(navigation_ui_data);
    // The ResourceDispatcherHostImpl can be null in unit tests.
    ResourceDispatcherHostImpl* rph = ResourceDispatcherHostImpl::Get();
    if (rph)
      global_request_id_ = rph->MakeGlobalRequestID();

    default_request_handler_factory_ = base::BindRepeating(
        &URLLoaderRequestController::
            CreateDefaultRequestHandlerForNonNetworkService,
        // base::Unretained(this) is safe since
        // |default_request_handler_factory_| could be called only from |this|.
        base::Unretained(this), base::Unretained(url_request_context_getter),
        base::Unretained(upload_file_system_context),
        base::Unretained(appcache_handle_core));

    StartInternal(request_info_.get(), service_worker_navigation_handle_core,
                  nullptr /* appcache_handle_core */,
                  std::move(prefetched_signed_exchange_cache),
                  std::move(signed_exchange_prefetch_metric_recorder),
                  {} /* factory_for_webui */, url_request_context_getter,
                  std::move(accept_langs));
  }

  void Start(
      std::unique_ptr<network::SharedURLLoaderFactoryInfo>
          network_loader_factory_info,
      ServiceWorkerNavigationHandleCore* service_worker_navigation_handle_core,
      AppCacheNavigationHandleCore* appcache_handle_core,
      scoped_refptr<PrefetchedSignedExchangeCache>
          prefetched_signed_exchange_cache,
      scoped_refptr<SignedExchangePrefetchMetricRecorder>
          signed_exchange_prefetch_metric_recorder,
      std::unique_ptr<NavigationRequestInfo> request_info,
      std::unique_ptr<NavigationUIData> navigation_ui_data,
      network::mojom::URLLoaderFactoryPtrInfo factory_for_webui,
      int frame_tree_node_id,
      bool needs_loader_factory_interceptor,
      base::Time ui_post_time,
      std::string accept_langs) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(base::FeatureList::IsEnabled(network::features::kNetworkService));
    DCHECK(!started_);
    ui_to_io_time_ += (base::Time::Now() - ui_post_time);
    global_request_id_ = MakeGlobalRequestID();
    frame_tree_node_id_ = frame_tree_node_id;
    started_ = true;
    web_contents_getter_ = base::BindRepeating(
        &WebContents::FromFrameTreeNodeId, frame_tree_node_id);
    navigation_ui_data_ = std::move(navigation_ui_data);

    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(&NavigationURLLoaderImpl::OnRequestStarted, owner_,
                       base::TimeTicks::Now()));

    DCHECK(network_loader_factory_info);
    network_loader_factory_ = network::SharedURLLoaderFactory::Create(
        std::move(network_loader_factory_info));
    if (needs_loader_factory_interceptor &&
        g_loader_factory_interceptor.Get()) {
      network::mojom::URLLoaderFactoryPtr factory;
      auto request = mojo::MakeRequest(&factory);
      g_loader_factory_interceptor.Get().Run(&request);
      network_loader_factory_->Clone(std::move(request));
      network_loader_factory_ =
          base::MakeRefCounted<network::WrapperSharedURLLoaderFactory>(
              std::move(factory));
    }

    if (resource_request_->request_body) {
      GetBodyBlobDataHandles(resource_request_->request_body.get(),
                             resource_context_, &blob_handles_);
    }

    StartInternal(
        request_info.get(), service_worker_navigation_handle_core,
        appcache_handle_core, std::move(prefetched_signed_exchange_cache),
        std::move(signed_exchange_prefetch_metric_recorder),
        std::move(factory_for_webui), nullptr /* url_request_context_getter */,
        std::move(accept_langs));
  }

  // Common setup routines, called by both StartWithoutNetworkService() and
  // Start(). Most parameters (except for |request_info| and
  // |url_request_context_getter|) are for setting up feature-specific
  // loaders and interceptors, and they can be null depending on the flags.
  // |url_request_context_getter| is non-null only for non-NetworkService
  // code paths.
  // TODO(kinuko): Merge this back to Start() once NetworkService is fully
  // shipped.
  void StartInternal(
      NavigationRequestInfo* request_info,
      ServiceWorkerNavigationHandleCore* service_worker_navigation_handle_core,
      AppCacheNavigationHandleCore* appcache_handle_core,
      scoped_refptr<PrefetchedSignedExchangeCache>
          prefetched_signed_exchange_cache,
      scoped_refptr<SignedExchangePrefetchMetricRecorder>
          signed_exchange_prefetch_metric_recorder,
      network::mojom::URLLoaderFactoryPtrInfo factory_for_webui,
      net::URLRequestContextGetter* url_request_context_getter,
      std::string accept_langs) {
    std::string accept_value = network::kFrameAcceptHeader;
    if (signed_exchange_utils::IsSignedExchangeHandlingEnabled(
            resource_context_)) {
      accept_value.append(kAcceptHeaderSignedExchangeSuffix);
    }
    resource_request_->headers.SetHeader(network::kAcceptHeader, accept_value);

    // NetworkService cases only.
    // Requests to WebUI scheme won't get redirected to/from other schemes
    // or be intercepted, so we just let it go here.
    if (factory_for_webui.is_valid()) {
      DCHECK(base::FeatureList::IsEnabled(network::features::kNetworkService));
      url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
          base::MakeRefCounted<network::WrapperSharedURLLoaderFactory>(
              std::move(factory_for_webui)),
          CreateURLLoaderThrottles(), 0 /* routing_id */,
          global_request_id_.request_id, network::mojom::kURLLoadOptionNone,
          resource_request_.get(), this, kNavigationUrlLoaderTrafficAnnotation,
          base::ThreadTaskRunnerHandle::Get());
      return;
    }

    // Requests to Blob scheme won't get redirected to/from other schemes
    // or be intercepted, so we just let it go here.
    if (request_info->common_params.url.SchemeIsBlob() &&
        request_info->blob_url_loader_factory) {
      url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
          network::SharedURLLoaderFactory::Create(
              std::move(request_info->blob_url_loader_factory)),
          CreateURLLoaderThrottles(), 0 /* routing_id */,
          global_request_id_.request_id, network::mojom::kURLLoadOptionNone,
          resource_request_.get(), this, kNavigationUrlLoaderTrafficAnnotation,
          base::ThreadTaskRunnerHandle::Get());
      return;
    }

    if (prefetched_signed_exchange_cache) {
      DCHECK(base::FeatureList::IsEnabled(
          features::kSignedExchangeSubresourcePrefetch));
      std::unique_ptr<NavigationLoaderInterceptor>
          prefetched_signed_exchange_interceptor =
              prefetched_signed_exchange_cache->MaybeCreateInterceptor(
                  request_info->common_params.url);
      if (prefetched_signed_exchange_interceptor) {
        interceptors_.push_back(
            std::move(prefetched_signed_exchange_interceptor));
      }
    }

    // Set-up an interceptor for service workers.
    if (service_worker_navigation_handle_core) {
      std::unique_ptr<NavigationLoaderInterceptor> service_worker_interceptor =
          ServiceWorkerRequestHandler::CreateForNavigation(
              resource_request_->url, resource_context_,
              service_worker_navigation_handle_core, *request_info,
              &service_worker_provider_host_);
      // The interceptor for service worker may not be created for some reasons
      // (e.g. the origin is not secure).
      if (service_worker_interceptor)
        interceptors_.push_back(std::move(service_worker_interceptor));
    }

    // NetworkService cases only.
    // Set-up an interceptor for AppCache if non-null |appcache_handle_core|
    // is given.
    if (appcache_handle_core) {
      DCHECK(base::FeatureList::IsEnabled(network::features::kNetworkService));
      std::unique_ptr<NavigationLoaderInterceptor> appcache_interceptor =
          AppCacheRequestHandler::InitializeForMainResourceNetworkService(
              *resource_request_, appcache_handle_core->host()->GetWeakPtr());
      if (appcache_interceptor)
        interceptors_.push_back(std::move(appcache_interceptor));
    }

    // Set-up an interceptor for SignedExchange handling if it is enabled.
    if (signed_exchange_utils::IsSignedExchangeHandlingEnabled(
            resource_context_)) {
      auto network_loader_factory = network_loader_factory_;
      if (!network_loader_factory) {
        DCHECK(
            !base::FeatureList::IsEnabled(network::features::kNetworkService));
        DCHECK(url_request_context_getter);
        network_loader_factory = base::MakeRefCounted<
            SignedExchangeURLLoaderFactoryForNonNetworkService>(
            resource_context_, url_request_context_getter);
      }
      interceptors_.push_back(CreateSignedExchangeRequestHandler(
          *request_info, std::move(network_loader_factory),
          std::move(signed_exchange_prefetch_metric_recorder),
          std::move(accept_langs)));
    }

    // See if embedders want to add interceptors.
    std::vector<std::unique_ptr<URLLoaderRequestInterceptor>>
        browser_interceptors =
            GetContentClient()
                ->browser()
                ->WillCreateURLLoaderRequestInterceptors(
                    navigation_ui_data_.get(), request_info->frame_tree_node_id,
                    network_loader_factory_);
    if (!browser_interceptors.empty()) {
      for (auto& browser_interceptor : browser_interceptors) {
        interceptors_.push_back(
            std::make_unique<NavigationLoaderInterceptorBrowserContainer>(
                std::move(browser_interceptor)));
      }
    }

    // Non-NetworkService cases only.
    // If an interceptor is not created, we no longer have to go through the
    // rest of the network service code.
    if (!base::FeatureList::IsEnabled(network::features::kNetworkService) &&
        interceptors_.empty()) {
      DCHECK(default_request_handler_factory_);
      url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
          base::MakeRefCounted<SingleRequestURLLoaderFactory>(
              default_request_handler_factory_.Run(
                  false /* was_request_intercepted */)),
          CreateURLLoaderThrottles(), -1 /* routing_id */, 0 /* request_id */,
          network::mojom::kURLLoadOptionNone, resource_request_.get(),
          this /* client */, kNavigationUrlLoaderTrafficAnnotation,
          base::ThreadTaskRunnerHandle::Get());
      return;
    }

    Restart();
  }

  // This could be called multiple times to follow a chain of redirects.
  void Restart() {
    // Clear |url_loader_| if it's not the default one (network). This allows
    // the restarted request to use a new loader, instead of, e.g., reusing the
    // AppCache or service worker loader. For an optimization, we keep and reuse
    // the default url loader if the all |interceptors_| doesn't handle the
    // redirected request. If the network service is enabled, reset the loader
    // if the redirected URL's scheme and the previous URL scheme don't match in
    // their use or disuse of the network service loader.
    if (!default_loader_used_ ||
        (base::FeatureList::IsEnabled(network::features::kNetworkService) &&
         url_chain_.size() > 1 &&
         IsURLHandledByNetworkService(url_chain_[url_chain_.size() - 1]) !=
             IsURLHandledByNetworkService(url_chain_[url_chain_.size() - 2]))) {
      url_loader_.reset();
    }
    interceptor_index_ = 0;
    received_response_ = false;
    MaybeStartLoader(nullptr /* interceptor */,
                     {} /* single_request_handler */);
  }

  // |interceptor| is non-null if this is called by one of the interceptors
  // (via a LoaderCallback).
  // |single_request_handler| is the RequestHandler given by the |interceptor|,
  // non-null if the interceptor wants to handle the request.
  void MaybeStartLoader(
      NavigationLoaderInterceptor* interceptor,
      SingleRequestURLLoaderFactory::RequestHandler single_request_handler) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(started_);

    if (single_request_handler) {
      // |interceptor| wants to handle the request with
      // |single_request_handler|.
      DCHECK(interceptor);

      std::vector<std::unique_ptr<URLLoaderThrottle>> throttles =
          CreateURLLoaderThrottles();
      // Intercepted requests need MimeSniffingThrottle to do mime sniffing.
      // Non-intercepted requests usually go through the regular network
      // URLLoader, which does mime sniffing.
      throttles.push_back(std::make_unique<MimeSniffingThrottle>(
          base::ThreadTaskRunnerHandle::Get()));

      default_loader_used_ = false;
      url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
          base::MakeRefCounted<SingleRequestURLLoaderFactory>(
              std::move(single_request_handler)),
          std::move(throttles), frame_tree_node_id_,
          global_request_id_.request_id, network::mojom::kURLLoadOptionNone,
          resource_request_.get(), this, kNavigationUrlLoaderTrafficAnnotation,
          base::ThreadTaskRunnerHandle::Get());

      subresource_loader_params_ =
          interceptor->MaybeCreateSubresourceLoaderParams();

      return;
    }

    // Before falling back to the next interceptor, see if |interceptor| still
    // wants to give additional info to the frame for subresource loading. In
    // that case we will just fall back to the default loader (i.e. won't go on
    // to the next interceptors) but send the subresource_loader_params to the
    // child process. This is necessary for correctness in the cases where, e.g.
    // there's a controlling service worker that doesn't have a fetch event
    // handler so it doesn't intercept requests. In that case we still want to
    // skip AppCache.
    if (interceptor) {
      subresource_loader_params_ =
          interceptor->MaybeCreateSubresourceLoaderParams();

      // If non-null |subresource_loader_params_| is returned, make sure
      // we skip the next interceptors.
      if (subresource_loader_params_)
        interceptor_index_ = interceptors_.size();
    }

    // See if the next interceptor wants to handle the request.
    if (interceptor_index_ < interceptors_.size()) {
      auto* next_interceptor = interceptors_[interceptor_index_++].get();
      next_interceptor->MaybeCreateLoader(
          *resource_request_, resource_context_,
          base::BindOnce(&URLLoaderRequestController::MaybeStartLoader,
                         base::Unretained(this), next_interceptor),
          base::BindOnce(
              &URLLoaderRequestController::FallbackToNonInterceptedRequest,
              base::Unretained(this)));
      return;
    }

    // If we already have the default |url_loader_| we must come here after a
    // redirect. No interceptors wanted to intercept the redirected request, so
    // let the loader just follow the redirect.
    if (url_loader_) {
      DCHECK(!redirect_info_.new_url.is_empty());
      url_loader_->FollowRedirect(std::move(url_loader_removed_headers_),
                                  std::move(url_loader_modified_headers_));
      return;
    }

    // No interceptors wanted to handle this request.
    uint32_t options = network::mojom::kURLLoadOptionNone;
    scoped_refptr<network::SharedURLLoaderFactory> factory =
        PrepareForNonInterceptedRequest(&options);
    url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
        std::move(factory), CreateURLLoaderThrottles(), frame_tree_node_id_,
        global_request_id_.request_id, options, resource_request_.get(),
        this /* client */, kNavigationUrlLoaderTrafficAnnotation,
        base::ThreadTaskRunnerHandle::Get());
  }

  // This is the |fallback_callback| passed to
  // NavigationLoaderInterceptor::MaybeCreateLoader. It allows an interceptor
  // to initially elect to handle a request, and later decide to fallback to
  // the default behavior. This is needed for service worker network fallback
  // and signed exchange (SXG) fallback redirect.
  void FallbackToNonInterceptedRequest(bool reset_subresource_loader_params) {
    if (reset_subresource_loader_params)
      subresource_loader_params_.reset();

    // Non-NetworkService:
    // Cancel state on ResourceDispatcherHostImpl so it doesn't complain about
    // reusing the request_id after redirects. Otherwise the following sequence
    // can happen:
    // case 1. RDHI Start(request_id) -> Redirect -> SW interception -> SW
    //         fallback to network -> RDHI Start(request_id).
    // case 2. RDHI Start(request_id) -> SXG interception -> SXG fallback to
    //         network -> RDHI Start(request_id).
    if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      DCHECK(ResourceDispatcherHostImpl::Get());
      ResourceDispatcherHostImpl::Get()->CancelRequest(
          global_request_id_.child_id, global_request_id_.request_id);
    }

    uint32_t options = network::mojom::kURLLoadOptionNone;
    scoped_refptr<network::SharedURLLoaderFactory> factory =
        PrepareForNonInterceptedRequest(&options);
    if (url_loader_) {
      // |url_loader_| is using the factory for the interceptor that decided to
      // fallback, so restart it with the non-interceptor factory.
      url_loader_->RestartWithFactory(std::move(factory), options);
    } else {
      // In SXG cases we don't have |url_loader_| because it was reset when the
      // SXG interceptor intercepted the response in
      // MaybeCreateLoaderForResponse.
      DCHECK(response_loader_binding_);
      response_loader_binding_.Close();
      url_loader_ = ThrottlingURLLoader::CreateLoaderAndStart(
          std::move(factory), CreateURLLoaderThrottles(), frame_tree_node_id_,
          global_request_id_.request_id, options, resource_request_.get(),
          this /* client */, kNavigationUrlLoaderTrafficAnnotation,
          base::ThreadTaskRunnerHandle::Get());
    }
  }

  scoped_refptr<network::SharedURLLoaderFactory>
  PrepareForNonInterceptedRequest(uint32_t* out_options) {
    // If NetworkService is not enabled, use the default request handler instead
    // of going through the NetworkService path.
    if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      DCHECK(!interceptors_.empty());
      DCHECK(default_request_handler_factory_);

      default_loader_used_ = true;
      // Update |request_info_| when following a redirect.
      if (url_chain_.size() > 0) {
        request_info_ = CreateNavigationRequestInfoForRedirect(
            *request_info_, *resource_request_);
      }

      // When |subresource_loader_params_| has its value, the request should not
      // be intercepted by any other interceptors since it means that a request
      // interceptor already intercepted the request and it attached its info to
      // the request.
      bool was_request_intercepted = subresource_loader_params_.has_value();

      // TODO(falken): Determine whether GetURLLoaderOptions() can be called
      // here like below. It looks like |default_request_handler_factory_| just
      // calls that.
      *out_options = network::mojom::kURLLoadOptionNone;
      return base::MakeRefCounted<SingleRequestURLLoaderFactory>(
          default_request_handler_factory_.Run(was_request_intercepted));
    }

    // TODO(https://crbug.com/796425): We temporarily wrap raw
    // mojom::URLLoaderFactory pointers into SharedURLLoaderFactory. Need to
    // further refactor the factory getters to avoid this.
    scoped_refptr<network::SharedURLLoaderFactory> factory;

    if (!IsURLHandledByNetworkService(resource_request_->url)) {
      if (known_schemes_.find(resource_request_->url.scheme()) ==
          known_schemes_.end()) {
        network::mojom::URLLoaderFactory* external_protocol_factory = nullptr;
        bool handled = GetContentClient()->browser()->HandleExternalProtocol(
            resource_request_->url, web_contents_getter_,
            ChildProcessHost::kInvalidUniqueID, navigation_ui_data_.get(),
            resource_request_->resource_type ==
                static_cast<int>(ResourceType::kMainFrame),
            static_cast<ui::PageTransition>(resource_request_->transition_type),
            resource_request_->has_user_gesture, &proxied_factory_request_,
            external_protocol_factory);
        if (!handled) {
          handled = GetContentClient()->browser()->HandleExternalProtocol(
              web_contents_getter_, frame_tree_node_id_,
              navigation_ui_data_.get(), *resource_request_,
              &proxied_factory_request_, external_protocol_factory);
        }

        if (external_protocol_factory) {
          factory =
              base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                  external_protocol_factory);
        } else {
          factory = base::MakeRefCounted<SingleRequestURLLoaderFactory>(
              base::BindOnce(UnknownSchemeCallback, handled));
        }
      } else {
        network::mojom::URLLoaderFactoryPtr& non_network_factory =
            non_network_url_loader_factories_[resource_request_->url.scheme()];
        if (!non_network_factory.is_bound()) {
          base::PostTaskWithTraits(
              FROM_HERE, {BrowserThread::UI},
              base::BindOnce(&NavigationURLLoaderImpl ::
                                 BindNonNetworkURLLoaderFactoryRequest,
                             owner_, frame_tree_node_id_,
                             resource_request_->url,
                             mojo::MakeRequest(&non_network_factory)));
        }
        factory =
            base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                non_network_factory.get());
      }

      if (g_loader_factory_interceptor.Get()) {
        network::mojom::URLLoaderFactoryPtr factory_ptr;
        auto request = mojo::MakeRequest(&factory_ptr);
        g_loader_factory_interceptor.Get().Run(&request);
        factory->Clone(std::move(request));
        factory = base::MakeRefCounted<network::WrapperSharedURLLoaderFactory>(
            std::move(factory_ptr));
      }
    } else {
      default_loader_used_ = true;

      // NOTE: We only support embedders proxying network-service-bound requests
      // not handled by NavigationLoaderInterceptors above (e.g. Service Worker
      // or AppCache). Hence this code is only reachable when one of the above
      // interceptors isn't used and the URL is either a data URL or has a
      // scheme which is handled by the network service.
      if (proxied_factory_request_.is_pending()) {
        DCHECK(proxied_factory_info_.is_valid());
        // We don't worry about reconnection since it's a single navigation.
        network_loader_factory_->Clone(std::move(proxied_factory_request_));
        factory = base::MakeRefCounted<network::WrapperSharedURLLoaderFactory>(
            std::move(proxied_factory_info_));
      } else {
        factory = network_loader_factory_;
      }
    }
    url_chain_.push_back(resource_request_->url);
    *out_options =
        GetURLLoaderOptions(resource_request_->resource_type ==
                            static_cast<int>(ResourceType::kMainFrame));
    return factory;
  }

  void FollowRedirect(const std::vector<std::string>& removed_headers,
                      const net::HttpRequestHeaders& modified_headers,
                      PreviewsState new_previews_state,
                      base::Time ui_post_time) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(!redirect_info_.new_url.is_empty());
    ui_to_io_time_ += (base::Time::Now() - ui_post_time);
    if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
      auto* common_params =
          const_cast<CommonNavigationParams*>(&request_info_->common_params);
      common_params->url = redirect_info_.new_url;
      common_params->referrer.url = GURL(redirect_info_.new_referrer);
      common_params->method = redirect_info_.new_method;
    }

    // Update |resource_request_| and call Restart to give our |interceptors_| a
    // chance at handling the new location. If no interceptor wants to take
    // over, we'll use the existing url_loader to follow the redirect, see
    // MaybeStartLoader.
    // TODO(michaeln): This is still WIP and is based on URLRequest::Redirect,
    // there likely remains more to be done.
    // a. For subframe navigations, the Origin header may need to be modified
    //    differently?

    bool should_clear_upload = false;
    net::RedirectUtil::UpdateHttpRequest(
        resource_request_->url, resource_request_->method, redirect_info_,
        removed_headers, modified_headers, &resource_request_->headers,
        &should_clear_upload);
    if (should_clear_upload) {
      // The request body is no longer applicable.
      resource_request_->request_body.reset();
      blob_handles_.clear();
    }

    resource_request_->url = redirect_info_.new_url;
    resource_request_->method = redirect_info_.new_method;
    resource_request_->site_for_cookies = redirect_info_.new_site_for_cookies;
    resource_request_->top_frame_origin = redirect_info_.new_top_frame_origin;
    resource_request_->referrer = GURL(redirect_info_.new_referrer);
    resource_request_->referrer_policy = redirect_info_.new_referrer_policy;
    resource_request_->previews_state = new_previews_state;
    url_chain_.push_back(redirect_info_.new_url);

    // Need to cache modified headers for |url_loader_| since it doesn't use
    // |resource_request_| during redirect.
    url_loader_removed_headers_ = removed_headers;
    url_loader_modified_headers_ = modified_headers;

    // Don't send Accept: application/signed-exchange for fallback redirects.
    if (redirect_info_.is_signed_exchange_fallback_redirect) {
      url_loader_modified_headers_.SetHeader(network::kAcceptHeader,
                                             network::kFrameAcceptHeader);
      resource_request_->headers.SetHeader(network::kAcceptHeader,
                                           network::kFrameAcceptHeader);
    }

    Restart();
  }

  base::Optional<SubresourceLoaderParams> TakeSubresourceLoaderParams() {
    return std::move(subresource_loader_params_);
  }

 private:
  // network::mojom::URLLoaderClient implementation:
  void OnReceiveResponse(const network::ResourceResponseHead& head) override {
    received_response_ = true;

    // If the default loader (network) was used to handle the URL load request
    // we need to see if the interceptors want to potentially create a new
    // loader for the response. e.g. AppCache.
    if (MaybeCreateLoaderForResponse(head))
      return;

    network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints;

    if (url_loader_) {
      url_loader_client_endpoints = url_loader_->Unbind();
    } else {
      url_loader_client_endpoints =
          network::mojom::URLLoaderClientEndpoints::New(
              response_url_loader_.PassInterface(),
              response_loader_binding_.Unbind());
    }

    // 304 responses should abort the navigation, rather than display the page.
    // This needs to be after the URLLoader has been moved to
    // |url_loader_client_endpoints| in order to abort the request, to avoid
    // receiving unexpected call.
    if (head.headers &&
        head.headers->response_code() == net::HTTP_NOT_MODIFIED) {
      // Call CancelWithError instead of OnComplete so that if there is an
      // intercepting URLLoaderFactory it gets notified.
      url_loader_->CancelWithError(
          net::ERR_ABORTED,
          base::StringPiece(base::NumberToString(net::ERR_ABORTED)));
      return;
    }

    bool is_download;
    bool is_stream;

    std::unique_ptr<NavigationData> cloned_navigation_data;

    bool must_download =
        download_utils::MustDownload(url_, head.headers.get(), head.mime_type);
    bool known_mime_type = blink::IsSupportedMimeType(head.mime_type);

#if BUILDFLAG(ENABLE_PLUGINS)
    if (!head.intercepted_by_plugin && !must_download && !known_mime_type) {
      // No plugin throttles intercepted the response. Ask if the plugin
      // registered to PluginService wants to handle the request.
      CheckPluginAndContinueOnReceiveResponse(
          head, std::move(url_loader_client_endpoints),
          true /* is_download_if_not_handled_by_plugin */,
          std::vector<WebPluginInfo>());
      return;
    }
#endif

    // When a plugin intercepted the response, we don't want to download it.
    is_download =
        !head.intercepted_by_plugin && (must_download || !known_mime_type);
    is_stream = false;

    // If NetworkService is on, or an interceptor handled the request, the
    // request doesn't use ResourceDispatcherHost so
    // CallOnReceivedResponse and return here.
    if (base::FeatureList::IsEnabled(network::features::kNetworkService) ||
        !default_loader_used_) {
      CallOnReceivedResponse(head, std::move(url_loader_client_endpoints),
                             std::move(cloned_navigation_data), is_download,
                             is_stream);
      return;
    }

    // NetworkService is off and an interceptor didn't handle the request,
    // so it went to ResourceDispatcherHost.
    ResourceDispatcherHostImpl* rdh = ResourceDispatcherHostImpl::Get();
    net::URLRequest* url_request = rdh->GetURLRequest(global_request_id_);

    // The |url_request| maybe have been removed from the resource dispatcher
    // host during the time it took for OnReceiveResponse() to be received.
    if (url_request) {
      ResourceRequestInfoImpl* info =
          ResourceRequestInfoImpl::ForRequest(url_request);
      is_download = !head.intercepted_by_plugin && info->IsDownload();
      is_stream = info->is_stream();
      if (rdh->delegate()) {
        NavigationData* navigation_data =
            rdh->delegate()->GetNavigationData(url_request);

        // Clone the embedder's NavigationData before moving it to the UI
        // thread.
        if (navigation_data)
          cloned_navigation_data = navigation_data->Clone();
      }
    } else {
      is_download = is_stream = false;
    }

    CallOnReceivedResponse(head, std::move(url_loader_client_endpoints),
                           std::move(cloned_navigation_data), is_download,
                           is_stream);
  }

#if BUILDFLAG(ENABLE_PLUGINS)
  void CheckPluginAndContinueOnReceiveResponse(
      const network::ResourceResponseHead& head,
      network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints,
      bool is_download_if_not_handled_by_plugin,
      const std::vector<WebPluginInfo>& plugins) {
    bool stale;
    WebPluginInfo plugin;
    // It's ok to pass -1 for the render process and frame ID since that's
    // only used for plugin overridding. We don't actually care if we get an
    // overridden plugin or not, since all we care about is the presence of a
    // plugin. Note that this is what the MimeSniffingResourceHandler code
    // path does as well for navigations.
    bool has_plugin = PluginService::GetInstance()->GetPluginInfo(
        -1 /* render_process_id */, -1 /* render_frame_id */, resource_context_,
        resource_request_->url, true, url::Origin(), head.mime_type,
        false /* allow_wildcard */, &stale, &plugin, nullptr);

    if (stale) {
      // Refresh the plugins asynchronously.
      PluginService::GetInstance()->GetPlugins(base::BindOnce(
          &URLLoaderRequestController::CheckPluginAndContinueOnReceiveResponse,
          weak_factory_.GetWeakPtr(), head,
          std::move(url_loader_client_endpoints),
          is_download_if_not_handled_by_plugin));
      return;
    }

    bool is_download = !has_plugin && is_download_if_not_handled_by_plugin;

    CallOnReceivedResponse(head, std::move(url_loader_client_endpoints),
                           nullptr, is_download, false /* is_stream */);
  }
#endif

  void CallOnReceivedResponse(
      const network::ResourceResponseHead& head,
      network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints,
      std::unique_ptr<NavigationData> cloned_navigation_data,
      bool is_download,
      bool is_stream) {
    scoped_refptr<network::ResourceResponse> response(
        new network::ResourceResponse());
    response->head = head;

    // Make a copy of the ResourceResponse before it is passed to another
    // thread.
    //
    // TODO(davidben): This copy could be avoided if ResourceResponse weren't
    // reference counted and the loader stack passed unique ownership of the
    // response. https://crbug.com/416050
    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(
            &NavigationURLLoaderImpl::OnReceiveResponse, owner_,
            response->DeepCopy(), std::move(url_loader_client_endpoints),
            std::move(cloned_navigation_data), global_request_id_, is_download,
            is_stream, ui_to_io_time_, base::Time::Now()));
  }

  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         const network::ResourceResponseHead& head) override {
    if (base::FeatureList::IsEnabled(network::features::kNetworkService) &&
        !bypass_redirect_checks_ &&
        !IsRedirectSafe(url_, redirect_info.new_url, resource_context_)) {
      // Call CancelWithError instead of OnComplete so that if there is an
      // intercepting URLLoaderFactory (created through the embedder's
      // ContentBrowserClient::WillCreateURLLoaderFactory) it gets notified.
      url_loader_->CancelWithError(
          net::ERR_UNSAFE_REDIRECT,
          base::StringPiece(base::NumberToString(net::ERR_UNSAFE_REDIRECT)));
      return;
    }

    if (--redirect_limit_ == 0) {
      // Call CancelWithError instead of OnComplete so that if there is an
      // intercepting URLLoaderFactory it gets notified.
      url_loader_->CancelWithError(
          net::ERR_TOO_MANY_REDIRECTS,
          base::StringPiece(base::NumberToString(net::ERR_TOO_MANY_REDIRECTS)));
      return;
    }

    // Store the redirect_info for later use in FollowRedirect where we give
    // our interceptors_ a chance to intercept the request for the new location.
    redirect_info_ = redirect_info;

    scoped_refptr<network::ResourceResponse> response(
        new network::ResourceResponse());
    response->head = head;
    url_ = redirect_info.new_url;

    // Make a copy of the ResourceResponse before it is passed to another
    // thread.
    //
    // TODO(davidben): This copy could be avoided if ResourceResponse weren't
    // reference counted and the loader stack passed unique ownership of the
    // response. https://crbug.com/416050
    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(&NavigationURLLoaderImpl::OnReceiveRedirect, owner_,
                       redirect_info, response->DeepCopy(), base::Time::Now()));
  }

  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        OnUploadProgressCallback callback) override {}
  void OnReceiveCachedMetadata(mojo_base::BigBuffer data) override {}
  void OnTransferSizeUpdated(int32_t transfer_size_diff) override {}

  void OnStartLoadingResponseBody(mojo::ScopedDataPipeConsumerHandle) override {
    // Not reached. At this point, the loader and client endpoints must have
    // been unbound and forwarded to the renderer.
    CHECK(false);
  }

  void OnComplete(const network::URLLoaderCompletionStatus& status) override {
    UMA_HISTOGRAM_BOOLEAN(
        "Navigation.URLLoaderNetworkService.OnCompleteHasSSLInfo",
        status.ssl_info.has_value());

    // Successful load must have used OnResponseStarted first. In this case, the
    // URLLoaderClient has already been transferred to the renderer process and
    // OnComplete is not expected to be called here.
    if (status.error_code == net::OK) {
      base::debug::DumpWithoutCrashing();
      return;
    }

    if (status.ssl_info.has_value()) {
      UMA_HISTOGRAM_MEMORY_KB(
          "Navigation.URLLoaderNetworkService.OnCompleteCertificateChainsSize",
          GetCertificateChainsSizeInKB(status.ssl_info.value()));
    }

    // If the default loader (network) was used to handle the URL load request
    // we need to see if the interceptors want to potentially create a new
    // loader for the response. e.g. AppCache.
    //
    // Note: Despite having received a response, the HTTP_NOT_MODIFIED(304) ones
    //       are ignored using OnComplete(net::ERR_ABORTED). No interceptor must
    //       be used in this case.
    if (!received_response_ &&
        MaybeCreateLoaderForResponse(network::ResourceResponseHead())) {
      return;
    }

    status_ = status;
    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(&NavigationURLLoaderImpl::OnComplete, owner_, status));
  }

  // Returns true if an interceptor wants to handle the response, i.e. return a
  // different response. For e.g. AppCache may have fallback content.
  bool MaybeCreateLoaderForResponse(
      const network::ResourceResponseHead& response) {
    if (!default_loader_used_)
      return false;

    for (size_t i = 0u; i < interceptors_.size(); ++i) {
      NavigationLoaderInterceptor* interceptor = interceptors_[i].get();
      network::mojom::URLLoaderClientRequest response_client_request;
      bool skip_other_interceptors = false;
      if (interceptor->MaybeCreateLoaderForResponse(
              *resource_request_, response, &response_url_loader_,
              &response_client_request, url_loader_.get(),
              &skip_other_interceptors)) {
        if (response_loader_binding_.is_bound())
          response_loader_binding_.Close();
        response_loader_binding_.Bind(std::move(response_client_request));
        default_loader_used_ = false;
        url_loader_.reset();
        if (skip_other_interceptors) {
          std::vector<std::unique_ptr<NavigationLoaderInterceptor>>
              new_interceptors;
          new_interceptors.push_back(std::move(interceptors_[i]));
          new_interceptors.swap(interceptors_);
          if (service_worker_provider_host_) {
            // Reset the state of ServiceWorkerProviderHost.
            // Currently we don't support Service Worker in Signed Exchange
            // pages. The page will not be controlled by service workers. And
            // Service Worker related APIs will fail with NoDocumentURL error.
            // TODO(crbug/898733): Support SignedExchange loading and Service
            // Worker integration.
            service_worker_provider_host_->SetControllerRegistration(
                nullptr, false /* notify_controllerchange */);
            service_worker_provider_host_->UpdateUrls(GURL(), GURL());
          }
        }
        return true;
      }
    }
    return false;
  }

  std::vector<std::unique_ptr<URLLoaderThrottle>> CreateURLLoaderThrottles() {
    return GetContentClient()->browser()->CreateURLLoaderThrottles(
        *resource_request_, resource_context_, web_contents_getter_,
        navigation_ui_data_.get(), frame_tree_node_id_);
  }

  std::unique_ptr<SignedExchangeRequestHandler>
  CreateSignedExchangeRequestHandler(
      const NavigationRequestInfo& request_info,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      scoped_refptr<SignedExchangePrefetchMetricRecorder>
          signed_exchange_prefetch_metric_recorder,
      std::string accept_langs) {
    // It is safe to pass the callback of CreateURLLoaderThrottles with the
    // unretained |this|, because the passed callback will be used by a
    // SignedExchangeHandler which is indirectly owned by |this| until its
    // header is verified and parsed, that's where the getter is used.
    return std::make_unique<SignedExchangeRequestHandler>(
        GetURLLoaderOptions(request_info.is_main_frame),
        request_info.frame_tree_node_id, request_info.devtools_navigation_token,
        std::move(url_loader_factory),
        base::BindRepeating(
            &URLLoaderRequestController::CreateURLLoaderThrottles,
            base::Unretained(this)),
        std::move(signed_exchange_prefetch_metric_recorder),
        std::move(accept_langs));
  }

  std::vector<std::unique_ptr<NavigationLoaderInterceptor>> interceptors_;
  size_t interceptor_index_ = 0;

  std::unique_ptr<network::ResourceRequest> resource_request_;
  // Non-NetworkService: |request_info_| is updated along with
  // |resource_request_| on redirects.
  std::unique_ptr<NavigationRequestInfo> request_info_;
  int frame_tree_node_id_ = 0;
  GlobalRequestID global_request_id_;
  net::RedirectInfo redirect_info_;
  int redirect_limit_ = net::URLRequest::kMaxRedirects;
  ResourceContext* resource_context_;
  base::Callback<WebContents*()> web_contents_getter_;
  std::unique_ptr<NavigationUIData> navigation_ui_data_;
  scoped_refptr<network::SharedURLLoaderFactory> network_loader_factory_;

  std::unique_ptr<ThrottlingURLLoader> url_loader_;

  // Caches the modified request headers provided by clients during redirect,
  // will be consumed by next |url_loader_->FollowRedirect()|.
  std::vector<std::string> url_loader_removed_headers_;
  net::HttpRequestHeaders url_loader_modified_headers_;

  BlobHandles blob_handles_;
  std::vector<GURL> url_chain_;

  // Current URL that is being navigated, updated after redirection.
  GURL url_;

  // Currently used by the AppCache loader to pass its factory to the
  // renderer which enables it to handle subresources.
  base::Optional<SubresourceLoaderParams> subresource_loader_params_;

  // This is referenced only on the UI thread.
  base::WeakPtr<NavigationURLLoaderImpl> owner_;

  // Set to true if the default URLLoader (network service) was used for the
  // current navigation.
  bool default_loader_used_ = false;

  // URLLoaderClient binding for loaders created for responses received from the
  // network loader.
  mojo::Binding<network::mojom::URLLoaderClient> response_loader_binding_;

  // URLLoader instance for response loaders, i.e loaders created for handing
  // responses received from the network URLLoader.
  network::mojom::URLLoaderPtr response_url_loader_;

  // Set to true if we receive a valid response from a URLLoader, i.e.
  // URLLoaderClient::OnReceivedResponse() is called.
  bool received_response_ = false;

  bool started_ = false;

  // Lazily initialized and used in the case of non-network resource
  // navigations. Keyed by URL scheme.
  std::map<std::string, network::mojom::URLLoaderFactoryPtr>
      non_network_url_loader_factories_;

  // Non-NetworkService:
  // Generator of a request handler for sending request to the network. This
  // captures all of parameters to create a
  // SingleRequestURLLoaderFactory::RequestHandler. Used only when
  // NetworkService is disabled.
  // Set |was_request_intercepted| to true if the request was intercepted by an
  // interceptor and the request is falling back to the network. In that case,
  // any interceptors won't intercept the request.
  base::RepeatingCallback<SingleRequestURLLoaderFactory::RequestHandler(
      bool /* was_request_intercepted */)>
      default_request_handler_factory_;

  // The completion status if it has been received. This is needed to handle
  // the case that the response is intercepted by download, and OnComplete() is
  // already called while we are transferring the |url_loader_| and response
  // body to download code.
  base::Optional<network::URLLoaderCompletionStatus> status_;

  // Before creating this URLLoaderRequestController on UI thread, the embedder
  // may have elected to proxy the URLLoaderFactory request, in which case these
  // fields will contain input (info) and output (request) endpoints for the
  // proxy. If this controller is handling a request for which proxying is
  // supported, requests will be plumbed through these endpoints.
  //
  // Note that these are only used for requests that go to the Network Service.
  network::mojom::URLLoaderFactoryRequest proxied_factory_request_;
  network::mojom::URLLoaderFactoryPtrInfo proxied_factory_info_;

  // The schemes that this loader can use. For anything else we'll try external
  // protocol handlers.
  std::set<std::string> known_schemes_;

  // If true, redirect checks will be handled in a proxy, and not here.
  bool bypass_redirect_checks_;

  // Used to reset the state of ServiceWorkerProviderHost when
  // SignedExchangeRequestHandler will handle the response.
  base::WeakPtr<ServiceWorkerProviderHost> service_worker_provider_host_;

  // Counts the time overhead of all the hops from the UI to the IO threads.
  base::TimeDelta ui_to_io_time_;

  mutable base::WeakPtrFactory<URLLoaderRequestController> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLLoaderRequestController);
};

// TODO(https://crbug.com/790734): pass |navigation_ui_data| along with the
// request so that it could be modified.
NavigationURLLoaderImpl::NavigationURLLoaderImpl(
    ResourceContext* resource_context,
    StoragePartition* storage_partition,
    std::unique_ptr<NavigationRequestInfo> request_info,
    std::unique_ptr<NavigationUIData> navigation_ui_data,
    ServiceWorkerNavigationHandle* service_worker_navigation_handle,
    AppCacheNavigationHandle* appcache_handle,
    scoped_refptr<PrefetchedSignedExchangeCache>
        prefetched_signed_exchange_cache,
    NavigationURLLoaderDelegate* delegate,
    std::vector<std::unique_ptr<NavigationLoaderInterceptor>>
        initial_interceptors)
    : delegate_(delegate),
      download_policy_(request_info->common_params.download_policy),
      weak_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  int frame_tree_node_id = request_info->frame_tree_node_id;

  TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(
      "navigation", "Navigation timeToResponseStarted", this,
      request_info->common_params.navigation_start, "FrameTreeNode id",
      frame_tree_node_id);

  ServiceWorkerNavigationHandleCore* service_worker_navigation_handle_core =
      service_worker_navigation_handle
          ? service_worker_navigation_handle->core()
          : nullptr;

  AppCacheNavigationHandleCore* appcache_handle_core =
      appcache_handle ? appcache_handle->core() : nullptr;

  std::unique_ptr<network::ResourceRequest> new_request =
      CreateResourceRequest(request_info.get(), frame_tree_node_id,
                            download_policy_.IsDownloadAllowed());

  auto* partition = static_cast<StoragePartitionImpl*>(storage_partition);
  scoped_refptr<SignedExchangePrefetchMetricRecorder>
      signed_exchange_prefetch_metric_recorder =
          partition->GetPrefetchURLLoaderService()
              ->signed_exchange_prefetch_metric_recorder();

  std::string accept_langs = GetContentClient()->browser()->GetAcceptLangs(
      partition->browser_context());

  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    DCHECK(!request_controller_);
    request_controller_ = std::make_unique<URLLoaderRequestController>(
        /* initial_interceptors = */
        std::vector<std::unique_ptr<NavigationLoaderInterceptor>>(),
        std::move(new_request), resource_context,
        request_info->common_params.url, request_info->is_main_frame,
        /* proxied_url_loader_factory_request */ nullptr,
        /* proxied_url_loader_factory_info */ nullptr, std::set<std::string>(),
        /* bypass_redirect_checks */ false, weak_factory_.GetWeakPtr());

    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::IO},
        base::BindOnce(
            &URLLoaderRequestController::StartWithoutNetworkService,
            base::Unretained(request_controller_.get()),
            base::RetainedRef(storage_partition->GetURLRequestContext()),
            base::Unretained(storage_partition->GetFileSystemContext()),
            base::Unretained(service_worker_navigation_handle_core),
            base::Unretained(appcache_handle_core),
            std::move(prefetched_signed_exchange_cache),
            base::RetainedRef(signed_exchange_prefetch_metric_recorder),
            std::move(request_info), std::move(navigation_ui_data),
            std::move(accept_langs)));
    return;
  }

  // Check if a web UI scheme wants to handle this request.
  FrameTreeNode* frame_tree_node =
      FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  network::mojom::URLLoaderFactoryPtrInfo factory_for_webui;
  const auto& schemes = URLDataManagerBackend::GetWebUISchemes();
  std::string scheme = new_request->url.scheme();
  if (base::ContainsValue(schemes, scheme)) {
    factory_for_webui = CreateWebUIURLLoaderBinding(
                            frame_tree_node->current_frame_host(), scheme)
                            .PassInterface();
  }

  network::mojom::URLLoaderFactoryPtrInfo proxied_factory_info;
  network::mojom::URLLoaderFactoryRequest proxied_factory_request;
  network::mojom::TrustedURLLoaderHeaderClientPtrInfo header_client;
  bool bypass_redirect_checks = false;
  if (frame_tree_node) {
    // |frame_tree_node| may be null in some unit test environments.
    GetContentClient()
        ->browser()
        ->RegisterNonNetworkNavigationURLLoaderFactories(
            frame_tree_node_id, &non_network_url_loader_factories_);

    // Navigation requests are not associated with any particular
    // |network::ResourceRequest::request_initiator| origin - using an opaque
    // origin instead.
    url::Origin navigation_request_initiator = url::Origin();
    // The embedder may want to proxy all network-bound URLLoaderFactory
    // requests that it can. If it elects to do so, we'll pass its proxy
    // endpoints off to the URLLoaderRequestController where wthey will be
    // connected if the request type supports proxying.
    network::mojom::URLLoaderFactoryPtrInfo factory_info;
    auto factory_request = mojo::MakeRequest(&factory_info);
    bool use_proxy = GetContentClient()->browser()->WillCreateURLLoaderFactory(
        partition->browser_context(), frame_tree_node->current_frame_host(),
        frame_tree_node->current_frame_host()->GetProcess()->GetID(),
        true /* is_navigation */, false /* is_download */,
        navigation_request_initiator, &factory_request, &header_client,
        &bypass_redirect_checks);
    if (devtools_instrumentation::WillCreateURLLoaderFactory(
            frame_tree_node->current_frame_host(), true /* is_navigation */,
            false /* is_download */, &factory_request)) {
      use_proxy = true;
    }
    if (use_proxy) {
      proxied_factory_request = std::move(factory_request);
      proxied_factory_info = std::move(factory_info);
    }

    const std::string storage_domain;
    non_network_url_loader_factories_[url::kFileSystemScheme] =
        CreateFileSystemURLLoaderFactory(frame_tree_node->current_frame_host(),
                                         /*is_navigation=*/true,
                                         partition->GetFileSystemContext(),
                                         storage_domain);
  }

  non_network_url_loader_factories_[url::kAboutScheme] =
      std::make_unique<AboutURLLoaderFactory>();

  non_network_url_loader_factories_[url::kDataScheme] =
      std::make_unique<DataURLLoaderFactory>();

  std::unique_ptr<network::mojom::URLLoaderFactory> file_url_loader_factory =
      std::make_unique<FileURLLoaderFactory>(
          partition->browser_context()->GetPath(),
          partition->browser_context()->GetSharedCorsOriginAccessList(),
          base::CreateSequencedTaskRunnerWithTraits(
              {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
               base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));

  if (frame_tree_node) {  // May be nullptr in some unit tests.
    devtools_instrumentation::WillCreateURLLoaderFactory(
        frame_tree_node->current_frame_host(), true /* is_navigation */,
        false /* is_download */, &file_url_loader_factory);
  }

  non_network_url_loader_factories_[url::kFileScheme] =
      std::move(file_url_loader_factory);

#if defined(OS_ANDROID)
  non_network_url_loader_factories_[url::kContentScheme] =
      std::make_unique<ContentURLLoaderFactory>(
          base::CreateSequencedTaskRunnerWithTraits(
              {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
               base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
#endif

  std::set<std::string> known_schemes;
  for (auto& iter : non_network_url_loader_factories_)
    known_schemes.insert(iter.first);

  bool needs_loader_factory_interceptor = false;
  std::unique_ptr<network::SharedURLLoaderFactoryInfo> network_factory_info =
      partition->url_loader_factory_getter()->GetNetworkFactoryInfo();
  if (header_client) {
    needs_loader_factory_interceptor = true;
    network::mojom::URLLoaderFactoryPtrInfo factory_info;
    CreateURLLoaderFactoryWithHeaderClient(
        std::move(header_client), mojo::MakeRequest(&factory_info), partition);
    network_factory_info =
        std::make_unique<network::WrapperSharedURLLoaderFactoryInfo>(
            std::move(factory_info));
  }

  DCHECK(!request_controller_);
  request_controller_ = std::make_unique<URLLoaderRequestController>(
      std::move(initial_interceptors), std::move(new_request), resource_context,
      request_info->common_params.url, request_info->is_main_frame,
      std::move(proxied_factory_request), std::move(proxied_factory_info),
      std::move(known_schemes), bypass_redirect_checks,
      weak_factory_.GetWeakPtr());
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&URLLoaderRequestController::Start,
                     base::Unretained(request_controller_.get()),
                     std::move(network_factory_info),
                     service_worker_navigation_handle_core,
                     appcache_handle_core,
                     std::move(prefetched_signed_exchange_cache),
                     std::move(signed_exchange_prefetch_metric_recorder),
                     std::move(request_info), std::move(navigation_ui_data),
                     std::move(factory_for_webui), frame_tree_node_id,
                     needs_loader_factory_interceptor, base::Time::Now(),
                     std::move(accept_langs)));
}

NavigationURLLoaderImpl::~NavigationURLLoaderImpl() {
  BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE,
                            request_controller_.release());
}

void NavigationURLLoaderImpl::FollowRedirect(
    const std::vector<std::string>& removed_headers,
    const net::HttpRequestHeaders& modified_headers,
    PreviewsState new_previews_state) {
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&URLLoaderRequestController::FollowRedirect,
                     base::Unretained(request_controller_.get()),
                     removed_headers, modified_headers, new_previews_state,
                     base::Time::Now()));
}

void NavigationURLLoaderImpl::ProceedWithResponse() {}

void NavigationURLLoaderImpl::OnReceiveResponse(
    scoped_refptr<network::ResourceResponse> response,
    network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints,
    std::unique_ptr<NavigationData> navigation_data,
    const GlobalRequestID& global_request_id,
    bool is_download,
    bool is_stream,
    base::TimeDelta total_ui_to_io_time,
    base::Time io_post_time) {
  const base::TimeDelta kMinTime = base::TimeDelta::FromMicroseconds(1);
  const base::TimeDelta kMaxTime = base::TimeDelta::FromMilliseconds(100);
  const int kBuckets = 50;
  io_to_ui_time_ += (base::Time::Now() - io_post_time);
  UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
      "Navigation.NavigationURLLoaderImplIOPostTime",
      io_to_ui_time_ + total_ui_to_io_time, kMinTime, kMaxTime, kBuckets);

  TRACE_EVENT_ASYNC_END2("navigation", "Navigation timeToResponseStarted", this,
                         "&NavigationURLLoaderImpl", this, "success", true);

  if (is_download)
    download_policy_.RecordHistogram();

  // TODO(scottmg): This needs to do more of what
  // NavigationResourceHandler::OnResponseStarted() does.
  delegate_->OnResponseStarted(
      std::move(response), std::move(url_loader_client_endpoints),
      std::move(navigation_data), global_request_id, is_download,
      download_policy_, is_stream,
      request_controller_->TakeSubresourceLoaderParams());
}

void NavigationURLLoaderImpl::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    scoped_refptr<network::ResourceResponse> response,
    base::Time io_post_time) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  io_to_ui_time_ += (base::Time::Now() - io_post_time);
  delegate_->OnRequestRedirected(redirect_info, std::move(response));
}

void NavigationURLLoaderImpl::OnComplete(
    const network::URLLoaderCompletionStatus& status) {
  TRACE_EVENT_ASYNC_END2("navigation", "Navigation timeToResponseStarted", this,
                         "&NavigationURLLoaderImpl", this, "success", false);
  delegate_->OnRequestFailed(status);
}

// static
void NavigationURLLoaderImpl::SetBeginNavigationInterceptorForTesting(
    const BeginNavigationInterceptor& interceptor) {
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::IO) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!base::FeatureList::IsEnabled(network::features::kNetworkService));
  g_interceptor.Get() = interceptor;
}

// static
void NavigationURLLoaderImpl::SetURLLoaderFactoryInterceptorForTesting(
    const URLLoaderFactoryInterceptor& interceptor) {
  DCHECK(!BrowserThread::IsThreadInitialized(BrowserThread::IO) ||
         BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(base::FeatureList::IsEnabled(network::features::kNetworkService));
  g_loader_factory_interceptor.Get() = interceptor;
}

// static
void NavigationURLLoaderImpl::CreateURLLoaderFactoryWithHeaderClient(
    network::mojom::TrustedURLLoaderHeaderClientPtrInfo header_client,
    network::mojom::URLLoaderFactoryRequest factory_request,
    StoragePartitionImpl* partition) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  network::mojom::URLLoaderFactoryParamsPtr params =
      network::mojom::URLLoaderFactoryParams::New();
  params->header_client = std::move(header_client);
  params->process_id = network::mojom::kBrowserProcessId;
  params->is_corb_enabled = false;
  params->disable_web_security =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableWebSecurity);

  partition->GetNetworkContext()->CreateURLLoaderFactory(
      std::move(factory_request), std::move(params));
}

// static
GlobalRequestID NavigationURLLoaderImpl::MakeGlobalRequestID() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  // Start at -2 on the same lines as ResourceDispatcherHostImpl.
  static int s_next_request_id = -2;
  return GlobalRequestID(-1, s_next_request_id--);
}

void NavigationURLLoaderImpl::OnRequestStarted(base::TimeTicks timestamp) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  delegate_->OnRequestStarted(timestamp);
}

void NavigationURLLoaderImpl::BindNonNetworkURLLoaderFactoryRequest(
    int frame_tree_node_id,
    const GURL& url,
    network::mojom::URLLoaderFactoryRequest factory) {
  auto it = non_network_url_loader_factories_.find(url.scheme());
  if (it == non_network_url_loader_factories_.end()) {
    DVLOG(1) << "Ignoring request with unknown scheme: " << url.spec();
    return;
  }

  // Navigation requests are not associated with any particular
  // |network::ResourceRequest::request_initiator| origin - using an opaque
  // origin instead.
  url::Origin navigation_request_initiator = url::Origin();

  FrameTreeNode* frame_tree_node =
      FrameTreeNode::GloballyFindByID(frame_tree_node_id);
  auto* frame = frame_tree_node->current_frame_host();
  GetContentClient()->browser()->WillCreateURLLoaderFactory(
      frame->GetSiteInstance()->GetBrowserContext(), frame,
      frame->GetProcess()->GetID(), true /* is_navigation */,
      false /* is_download */, navigation_request_initiator, &factory,
      nullptr /* header_client */, nullptr /* bypass_redirect_checks */);
  it->second->Clone(std::move(factory));
}

}  // namespace content
