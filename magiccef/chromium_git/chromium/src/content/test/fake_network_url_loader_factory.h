// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_FAKE_NETWORK_URL_LOADER_FACTORY_H_
#define CONTENT_TEST_FAKE_NETWORK_URL_LOADER_FACTORY_H_

#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace content {

// A URLLoaderFactory that returns 200 OK with a simple body to any request.
// Any request path that ends in ".js" gets a body of
// "/*this body came from the network*/". Other requests get
// "this body came from the network".
class FakeNetworkURLLoaderFactory final
    : public network::mojom::URLLoaderFactory {
 public:
  FakeNetworkURLLoaderFactory();
  ~FakeNetworkURLLoaderFactory() override;

  // network::mojom::URLLoaderFactory implementation.
  void CreateLoaderAndStart(network::mojom::URLLoaderRequest request,
                            int32_t routing_id,
                            int32_t request_id,
                            uint32_t options,
                            const network::ResourceRequest& url_request,
                            network::mojom::URLLoaderClientPtr client,
                            const net::MutableNetworkTrafficAnnotationTag&
                                traffic_annotation) override;

  void Clone(network::mojom::URLLoaderFactoryRequest request) override;

 private:
  mojo::BindingSet<network::mojom::URLLoaderFactory> bindings_;

  DISALLOW_COPY_AND_ASSIGN(FakeNetworkURLLoaderFactory);
};

}  // namespace content

#endif  // CONTENT_TEST_FAKE_NETWORK_URL_LOADER_FACTORY_H_
