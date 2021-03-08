// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/route_message_util.h"

#include "base/macros.h"

using media_router::mojom::RouteMessage;
using media_router::mojom::RouteMessagePtr;

namespace media_router {
namespace message_util {

RouteMessagePtr RouteMessageFromString(std::string message) {
  auto route_message = RouteMessage::New();
  route_message->type = RouteMessage::Type::TEXT;
  route_message->message = std::move(message);
  return route_message;
}

RouteMessagePtr RouteMessageFromData(std::vector<uint8_t> data) {
  auto route_message = RouteMessage::New();
  route_message->type = RouteMessage::Type::BINARY;
  route_message->data = std::move(data);
  return route_message;
}

blink::mojom::PresentationConnectionMessagePtr
PresentationConnectionFromRouteMessage(RouteMessagePtr route_message) {
  // NOTE: Makes a copy of |route_message| contents.  This can be eliminated
  // when media_router::mojom::RouteMessage is deleted.
  switch (route_message->type) {
    case RouteMessage::Type::TEXT:
      return blink::mojom::PresentationConnectionMessage::NewMessage(
          route_message->message.value());
    case RouteMessage::Type::BINARY:
      return blink::mojom::PresentationConnectionMessage::NewData(
          route_message->data.value());
    default:
      NOTREACHED() << "Unknown RouteMessageType " << route_message->type;
      return blink::mojom::PresentationConnectionMessage::New();
  }
}

}  // namespace message_util
}  // namespace media_router
