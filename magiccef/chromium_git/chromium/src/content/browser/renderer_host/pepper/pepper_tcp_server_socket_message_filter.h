// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_TCP_SERVER_SOCKET_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_TCP_SERVER_SOCKET_MESSAGE_FILTER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/ip_endpoint.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/private/ppb_net_address_private.h"
#include "ppapi/host/resource_message_filter.h"
#include "services/network/public/mojom/tcp_socket.mojom.h"

#if defined(OS_CHROMEOS)
#include "chromeos/network/firewall_hole.h"
#include "content/public/browser/browser_thread.h"
#endif  // defined(OS_CHROMEOS)

namespace network {
namespace mojom {
class NetworkContext;
}
}  // namespace network

namespace ppapi {
namespace host {
class PpapiHost;
}
}

namespace content {

class BrowserPpapiHostImpl;
class ContentBrowserPepperHostFactory;

// TODO(yzshen): Remove this class entirely and let
// TCPServerSocketPrivateResource inherit TCPSocketResourceBase.
class CONTENT_EXPORT PepperTCPServerSocketMessageFilter
    : public ppapi::host::ResourceMessageFilter {
 public:
  PepperTCPServerSocketMessageFilter(ContentBrowserPepperHostFactory* factory,
                                     BrowserPpapiHostImpl* host,
                                     PP_Instance instance,
                                     bool private_api);

  // Sets a global NetworkContext object to be used instead of the real one for
  // doing all network operations.
  static void SetNetworkContextForTesting(
      network::mojom::NetworkContext* network_context);

  static size_t GetNumInstances();

 protected:
  ~PepperTCPServerSocketMessageFilter() override;

 private:
  enum State {
    STATE_BEFORE_LISTENING,
    STATE_LISTEN_IN_PROGRESS,
    STATE_LISTENING,
    STATE_ACCEPT_IN_PROGRESS,
    STATE_CLOSED
  };

  // ppapi::host::ResourceMessageFilter overrides.
  void OnFilterDestroyed() override;
  scoped_refptr<base::TaskRunner> OverrideTaskRunnerForMessage(
      const IPC::Message& message) override;
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

  int32_t OnMsgListen(const ppapi::host::HostMessageContext* context,
                      const PP_NetAddress_Private& addr,
                      int32_t backlog);
  int32_t OnMsgAccept(const ppapi::host::HostMessageContext* context);
  int32_t OnMsgStopListening(const ppapi::host::HostMessageContext* context);

  void DoListen(const ppapi::host::ReplyMessageContext& context,
                int32_t backlog);

  void OnListenCompleted(const ppapi::host::ReplyMessageContext& context,
                         int net_result,
                         const base::Optional<net::IPEndPoint>& local_addr);
  void OnAcceptCompleted(
      const ppapi::host::ReplyMessageContext& context,
      network::mojom::SocketObserverRequest socket_observer_request,
      int net_result,
      const base::Optional<net::IPEndPoint>& remote_addr,
      network::mojom::TCPConnectedSocketPtr connected_socket,
      mojo::ScopedDataPipeConsumerHandle receive_stream,
      mojo::ScopedDataPipeProducerHandle send_stream);
  void OnAcceptCompletedOnIOThread(
      const ppapi::host::ReplyMessageContext& context,
      network::mojom::TCPConnectedSocketPtrInfo connected_socket,
      network::mojom::SocketObserverRequest socket_observer_request,
      mojo::ScopedDataPipeConsumerHandle receive_stream,
      mojo::ScopedDataPipeProducerHandle send_stream,
      PP_NetAddress_Private pp_local_addr,
      PP_NetAddress_Private pp_remote_addr);

  // Closes the socket and FirewallHole, if they're open, and prevents
  // |this| from being used further, even with a new socket.
  void Close();

  void SendListenReply(const ppapi::host::ReplyMessageContext& context,
                       int32_t pp_result,
                       const PP_NetAddress_Private& local_addr);
  void SendListenError(const ppapi::host::ReplyMessageContext& context,
                       int32_t pp_result);
  void SendAcceptReply(const ppapi::host::ReplyMessageContext& context,
                       int32_t pp_result,
                       int pending_resource_id,
                       const PP_NetAddress_Private& local_addr,
                       const PP_NetAddress_Private& remote_addr);
  void SendAcceptError(const ppapi::host::ReplyMessageContext& context,
                       int32_t pp_result);

#if defined(OS_CHROMEOS)
  void OpenFirewallHole(const ppapi::host::ReplyMessageContext& context,
                        const net::IPEndPoint& local_addr);
  void OnFirewallHoleOpened(const ppapi::host::ReplyMessageContext& context,
                            std::unique_ptr<chromeos::FirewallHole> hole);
#endif  // defined(OS_CHROMEOS)

  // Following fields are initialized and used only on the IO thread.
  // Non-owning ptr.
  ppapi::host::PpapiHost* ppapi_host_;
  // Non-owning ptr.
  ContentBrowserPepperHostFactory* factory_;
  PP_Instance instance_;

  State state_;
  network::mojom::TCPServerSocketPtr socket_;

  PP_NetAddress_Private bound_addr_;

#if defined(OS_CHROMEOS)
  std::unique_ptr<chromeos::FirewallHole,
                  content::BrowserThread::DeleteOnUIThread>
      firewall_hole_;
#endif  // defined(OS_CHROMEOS)

  // Following fields are initialized on the IO thread but used only
  // on the UI thread.
  const bool external_plugin_;
  const bool private_api_;
  int render_process_id_;
  int render_frame_id_;

  // Used in place of the StoragePartition's NetworkContext when non-null.
  static network::mojom::NetworkContext* network_context_for_testing;

  // Vends weak pointers on the UI thread, for callbacks passed through Mojo
  // pipes not owned by |this|. All weak pointers released in Close().
  base::WeakPtrFactory<PepperTCPServerSocketMessageFilter> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PepperTCPServerSocketMessageFilter);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_TCP_SERVER_SOCKET_MESSAGE_FILTER_H_
