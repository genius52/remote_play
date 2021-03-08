// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/appcache/appcache_backend_impl.h"

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "content/browser/appcache/appcache.h"
#include "content/browser/appcache/appcache_group.h"
#include "content/browser/appcache/appcache_navigation_handle_core.h"
#include "content/browser/appcache/appcache_service_impl.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/blink/public/mojom/appcache/appcache.mojom.h"

namespace content {

AppCacheBackendImpl::AppCacheBackendImpl(AppCacheServiceImpl* service,
                                         int process_id)
    : service_(service),
      process_id_(process_id) {
  DCHECK(service);
  service_->RegisterBackend(this);
}

AppCacheBackendImpl::~AppCacheBackendImpl() {
  service_->UnregisterBackend(this);
}

void AppCacheBackendImpl::RegisterHost(
    blink::mojom::AppCacheHostRequest host_request,
    blink::mojom::AppCacheFrontendPtr frontend,
    const base::UnguessableToken& host_id) {
  service_->RegisterHostInternal(std::move(host_request), std::move(frontend),
                                 host_id, MSG_ROUTING_NONE, process_id_,
                                 mojo::GetBadMessageCallback());
}

}  // namespace content
