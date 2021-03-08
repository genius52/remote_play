// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/protocol/service_worker_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/containers/flat_set.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "content/browser/background_sync/background_sync_context_impl.h"
#include "content/browser/background_sync/background_sync_manager.h"
#include "content/browser/devtools/service_worker_devtools_agent_host.h"
#include "content/browser/devtools/service_worker_devtools_manager.h"
#include "content/browser/devtools/shared_worker_devtools_manager.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/service_worker/embedded_worker_status.h"
#include "content/browser/service_worker/service_worker_context_watcher.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/browser/storage_partition_impl_map.h"
#include "content/common/service_worker/service_worker_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging_status.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_object.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_provider_type.mojom.h"
#include "url/gurl.h"

namespace content {
namespace protocol {

namespace {

const std::string GetVersionRunningStatusString(
    EmbeddedWorkerStatus running_status) {
  switch (running_status) {
    case EmbeddedWorkerStatus::STOPPED:
      return ServiceWorker::ServiceWorkerVersionRunningStatusEnum::Stopped;
    case EmbeddedWorkerStatus::STARTING:
      return ServiceWorker::ServiceWorkerVersionRunningStatusEnum::Starting;
    case EmbeddedWorkerStatus::RUNNING:
      return ServiceWorker::ServiceWorkerVersionRunningStatusEnum::Running;
    case EmbeddedWorkerStatus::STOPPING:
      return ServiceWorker::ServiceWorkerVersionRunningStatusEnum::Stopping;
    default:
      NOTREACHED();
  }
  return std::string();
}

const std::string GetVersionStatusString(
    content::ServiceWorkerVersion::Status status) {
  switch (status) {
    case content::ServiceWorkerVersion::NEW:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::New;
    case content::ServiceWorkerVersion::INSTALLING:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::Installing;
    case content::ServiceWorkerVersion::INSTALLED:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::Installed;
    case content::ServiceWorkerVersion::ACTIVATING:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::Activating;
    case content::ServiceWorkerVersion::ACTIVATED:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::Activated;
    case content::ServiceWorkerVersion::REDUNDANT:
      return ServiceWorker::ServiceWorkerVersionStatusEnum::Redundant;
    default:
      NOTREACHED();
  }
  return std::string();
}

void StopServiceWorkerOnIO(scoped_refptr<ServiceWorkerContextWrapper> context,
                           int64_t version_id) {
  if (content::ServiceWorkerVersion* version =
          context->GetLiveVersion(version_id)) {
    version->StopWorker(base::DoNothing());
  }
}

void GetDevToolsRouteInfoOnIO(
    scoped_refptr<ServiceWorkerContextWrapper> context,
    int64_t version_id,
    const base::Callback<void(int, int)>& callback) {
  if (content::ServiceWorkerVersion* version =
          context->GetLiveVersion(version_id)) {
    base::PostTaskWithTraits(
        FROM_HERE, {BrowserThread::UI},
        base::BindOnce(
            callback, version->embedded_worker()->process_id(),
            version->embedded_worker()->worker_devtools_agent_route_id()));
  }
}

Response CreateDomainNotEnabledErrorResponse() {
  return Response::Error("ServiceWorker domain not enabled");
}

Response CreateContextErrorResponse() {
  return Response::Error("Could not connect to the context");
}

Response CreateInvalidVersionIdErrorResponse() {
  return Response::InvalidParams("Invalid version ID");
}

void DidFindRegistrationForDispatchSyncEventOnIO(
    scoped_refptr<BackgroundSyncContextImpl> sync_context,
    const std::string& tag,
    bool last_chance,
    blink::ServiceWorkerStatusCode status,
    scoped_refptr<content::ServiceWorkerRegistration> registration) {
  if (status != blink::ServiceWorkerStatusCode::kOk ||
      !registration->active_version())
    return;
  BackgroundSyncManager* background_sync_manager =
      sync_context->background_sync_manager();
  scoped_refptr<content::ServiceWorkerVersion> version(
      registration->active_version());
  // Keep the registration while dispatching the sync event.
  background_sync_manager->EmulateDispatchSyncEvent(
      tag, std::move(version), last_chance,
      base::BindOnce(base::DoNothing::Once<
                         scoped_refptr<content::ServiceWorkerRegistration>,
                         blink::ServiceWorkerStatusCode>(),
                     std::move(registration)));
}

void DispatchSyncEventOnIO(
    scoped_refptr<ServiceWorkerContextWrapper> context,
    scoped_refptr<BackgroundSyncContextImpl> sync_context,
    const GURL& origin,
    int64_t registration_id,
    const std::string& tag,
    bool last_chance) {
  context->FindReadyRegistrationForId(
      registration_id, origin,
      base::BindOnce(&DidFindRegistrationForDispatchSyncEventOnIO, sync_context,
                     tag, last_chance));
}

}  // namespace

ServiceWorkerHandler::ServiceWorkerHandler()
    : DevToolsDomainHandler(ServiceWorker::Metainfo::domainName),
      enabled_(false),
      browser_context_(nullptr),
      storage_partition_(nullptr),
      weak_factory_(this) {}

ServiceWorkerHandler::~ServiceWorkerHandler() {
}

void ServiceWorkerHandler::Wire(UberDispatcher* dispatcher) {
  frontend_.reset(new ServiceWorker::Frontend(dispatcher->channel()));
  ServiceWorker::Dispatcher::wire(dispatcher, this);
}

void ServiceWorkerHandler::SetRenderer(int process_host_id,
                                       RenderFrameHostImpl* frame_host) {
  RenderProcessHost* process_host = RenderProcessHost::FromID(process_host_id);
  // Do not call UpdateHosts yet, wait for load to commit.
  if (!process_host) {
    ClearForceUpdate();
    context_ = nullptr;
    return;
  }

  storage_partition_ =
      static_cast<StoragePartitionImpl*>(process_host->GetStoragePartition());
  DCHECK(storage_partition_);
  browser_context_ = process_host->GetBrowserContext();
  context_ = static_cast<ServiceWorkerContextWrapper*>(
      storage_partition_->GetServiceWorkerContext());
}

Response ServiceWorkerHandler::Enable() {
  if (enabled_)
    return Response::OK();
  if (!context_)
    return CreateContextErrorResponse();
  enabled_ = true;

  context_watcher_ = new ServiceWorkerContextWatcher(
      context_, base::Bind(&ServiceWorkerHandler::OnWorkerRegistrationUpdated,
                           weak_factory_.GetWeakPtr()),
      base::Bind(&ServiceWorkerHandler::OnWorkerVersionUpdated,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ServiceWorkerHandler::OnErrorReported,
                 weak_factory_.GetWeakPtr()));
  context_watcher_->Start();

  return Response::OK();
}

Response ServiceWorkerHandler::Disable() {
  if (!enabled_)
    return Response::OK();
  enabled_ = false;

  ClearForceUpdate();
  DCHECK(context_watcher_);
  context_watcher_->Stop();
  context_watcher_ = nullptr;
  return Response::OK();
}

Response ServiceWorkerHandler::Unregister(const std::string& scope_url) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();
  context_->UnregisterServiceWorker(GURL(scope_url), base::DoNothing());
  return Response::OK();
}

Response ServiceWorkerHandler::StartWorker(const std::string& scope_url) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();
  context_->StartServiceWorker(GURL(scope_url), base::DoNothing());
  return Response::OK();
}

Response ServiceWorkerHandler::SkipWaiting(const std::string& scope_url) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();
  context_->SkipWaitingWorker(GURL(scope_url));
  return Response::OK();
}

Response ServiceWorkerHandler::StopWorker(const std::string& version_id) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();
  int64_t id = 0;
  if (!base::StringToInt64(version_id, &id))
    return CreateInvalidVersionIdErrorResponse();
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&StopServiceWorkerOnIO, context_, id));
  return Response::OK();
}

void ServiceWorkerHandler::StopAllWorkers(
    std::unique_ptr<StopAllWorkersCallback> callback) {
  if (!enabled_) {
    callback->sendFailure(CreateDomainNotEnabledErrorResponse());
    return;
  }
  if (!context_) {
    callback->sendFailure(CreateContextErrorResponse());
    return;
  }
  context_->StopAllServiceWorkers(base::BindOnce(
      &StopAllWorkersCallback::sendSuccess, std::move(callback)));
}

Response ServiceWorkerHandler::UpdateRegistration(
    const std::string& scope_url) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();
  context_->UpdateRegistration(GURL(scope_url));
  return Response::OK();
}

Response ServiceWorkerHandler::InspectWorker(const std::string& version_id) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!context_)
    return CreateContextErrorResponse();

  int64_t id = blink::mojom::kInvalidServiceWorkerVersionId;
  if (!base::StringToInt64(version_id, &id))
    return CreateInvalidVersionIdErrorResponse();
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&GetDevToolsRouteInfoOnIO, context_, id,
                     base::Bind(&ServiceWorkerHandler::OpenNewDevToolsWindow,
                                weak_factory_.GetWeakPtr())));
  return Response::OK();
}

Response ServiceWorkerHandler::SetForceUpdateOnPageLoad(
    bool force_update_on_page_load) {
  if (!context_)
    return CreateContextErrorResponse();
  context_->SetForceUpdateOnPageLoad(force_update_on_page_load);
  return Response::OK();
}

Response ServiceWorkerHandler::DeliverPushMessage(
    const std::string& origin,
    const std::string& registration_id,
    const std::string& data) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!browser_context_)
    return CreateContextErrorResponse();
  int64_t id = 0;
  if (!base::StringToInt64(registration_id, &id))
    return CreateInvalidVersionIdErrorResponse();
  base::Optional<std::string> payload;
  if (data.size() > 0)
    payload = data;
  BrowserContext::DeliverPushMessage(
      browser_context_, GURL(origin), id, /* push_message_id= */ std::string(),
      std::move(payload),
      base::BindRepeating([](blink::mojom::PushDeliveryStatus status) {}));

  return Response::OK();
}

Response ServiceWorkerHandler::DispatchSyncEvent(
    const std::string& origin,
    const std::string& registration_id,
    const std::string& tag,
    bool last_chance) {
  if (!enabled_)
    return CreateDomainNotEnabledErrorResponse();
  if (!storage_partition_)
    return CreateContextErrorResponse();
  int64_t id = 0;
  if (!base::StringToInt64(registration_id, &id))
    return CreateInvalidVersionIdErrorResponse();

  BackgroundSyncContextImpl* sync_context =
      storage_partition_->GetBackgroundSyncContext();

  base::PostTaskWithTraits(FROM_HERE, {BrowserThread::IO},
                           base::BindOnce(&DispatchSyncEventOnIO, context_,
                                          base::WrapRefCounted(sync_context),
                                          GURL(origin), id, tag, last_chance));
  return Response::OK();
}

void ServiceWorkerHandler::OpenNewDevToolsWindow(int process_id,
                                                 int devtools_agent_route_id) {
  scoped_refptr<DevToolsAgentHostImpl> agent_host(
      ServiceWorkerDevToolsManager::GetInstance()
          ->GetDevToolsAgentHostForWorker(process_id, devtools_agent_route_id));
  if (!agent_host.get())
    return;
  agent_host->Inspect();
}

void ServiceWorkerHandler::OnWorkerRegistrationUpdated(
    const std::vector<ServiceWorkerRegistrationInfo>& registrations) {
  using Registration = ServiceWorker::ServiceWorkerRegistration;
  std::unique_ptr<protocol::Array<Registration>> result =
      protocol::Array<Registration>::create();
  for (const auto& registration : registrations) {
    result->addItem(Registration::Create()
                        .SetRegistrationId(
                            base::NumberToString(registration.registration_id))
                        .SetScopeURL(registration.scope.spec())
                        .SetIsDeleted(registration.delete_flag ==
                                      ServiceWorkerRegistrationInfo::IS_DELETED)
                        .Build());
  }
  frontend_->WorkerRegistrationUpdated(std::move(result));
}

void ServiceWorkerHandler::OnWorkerVersionUpdated(
    const std::vector<ServiceWorkerVersionInfo>& versions) {
  using Version = ServiceWorker::ServiceWorkerVersion;
  std::unique_ptr<protocol::Array<Version>> result =
      protocol::Array<Version>::create();
  for (const auto& version : versions) {
    base::flat_set<std::string> client_set;

    for (const auto& client : version.clients) {
      if (client.second.type ==
          blink::mojom::ServiceWorkerProviderType::kForWindow) {
        // PlzNavigate: a navigation may not yet be associated with a
        // RenderFrameHost. Use the |web_contents_getter| instead.
        WebContents* web_contents =
            client.second.web_contents_getter
                ? client.second.web_contents_getter.Run()
                : WebContents::FromRenderFrameHost(RenderFrameHostImpl::FromID(
                      client.second.process_id, client.second.route_id));
        // There is a possibility that the frame is already deleted
        // because of the thread hopping.
        if (!web_contents)
          continue;
        client_set.insert(
            DevToolsAgentHost::GetOrCreateFor(web_contents)->GetId());
      }
    }
    std::unique_ptr<protocol::Array<std::string>> clients =
        protocol::Array<std::string>::create();
    for (auto& c : client_set)
      clients->addItem(c);

    std::unique_ptr<Version> version_value =
        Version::Create()
            .SetVersionId(base::NumberToString(version.version_id))
            .SetRegistrationId(base::NumberToString(version.registration_id))
            .SetScriptURL(version.script_url.spec())
            .SetRunningStatus(
                GetVersionRunningStatusString(version.running_status))
            .SetStatus(GetVersionStatusString(version.status))
            .SetScriptLastModified(version.script_last_modified.ToDoubleT())
            .SetScriptResponseTime(version.script_response_time.ToDoubleT())
            .SetControlledClients(std::move(clients))
            .Build();
    scoped_refptr<DevToolsAgentHostImpl> host(
        ServiceWorkerDevToolsManager::GetInstance()
            ->GetDevToolsAgentHostForWorker(
                version.process_id,
                version.devtools_agent_route_id));
    if (host)
      version_value->SetTargetId(host->GetId());
    result->addItem(std::move(version_value));
  }
  frontend_->WorkerVersionUpdated(std::move(result));
}

void ServiceWorkerHandler::OnErrorReported(
    int64_t registration_id,
    int64_t version_id,
    const ServiceWorkerContextCoreObserver::ErrorInfo& info) {
  frontend_->WorkerErrorReported(
      ServiceWorker::ServiceWorkerErrorMessage::Create()
          .SetErrorMessage(base::UTF16ToUTF8(info.error_message))
          .SetRegistrationId(base::NumberToString(registration_id))
          .SetVersionId(base::NumberToString(version_id))
          .SetSourceURL(info.source_url.spec())
          .SetLineNumber(info.line_number)
          .SetColumnNumber(info.column_number)
          .Build());
}

void ServiceWorkerHandler::ClearForceUpdate() {
  if (context_)
    context_->SetForceUpdateOnPageLoad(false);
}

}  // namespace protocol
}  // namespace content
