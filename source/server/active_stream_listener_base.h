#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/linked_object.h"
#include "source/server/active_listener_base.h"
#include "source/server/active_tcp_socket.h"

namespace Envoy {
namespace Server {

// The base class of the stream listener. It owns the the active sockets that drive itself through
// the listener filters. After the active socket passes all the listener filters, a server
// connection is created. The derived listener must override ``newActiveConnection`` to take the
// ownership of that server connection.
class ActiveStreamListenerBase : public ActiveListenerImplBase,
                                 protected Logger::Loggable<Logger::Id::conn_handler> {
public:
  ActiveStreamListenerBase(Network::ConnectionHandler& parent, Event::Dispatcher& dispatcher,
                           Network::ListenerPtr&& listener, Network::ListenerConfig& config);
  static void emitLogs(Network::ListenerConfig& config, StreamInfo::StreamInfo& stream_info);

  Event::Dispatcher& dispatcher() { return dispatcher_; }

  /**
   * Schedule to remove and destroy the active connections which are not tracked by listener
   * config. Caution: The connection are not destroyed yet when function returns.
   */
  void
  deferredRemoveFilterChains(const std::list<const Network::FilterChain*>& draining_filter_chains) {
    // Need to recover the original deleting state.
    const bool was_deleting = is_deleting_;
    is_deleting_ = true;
    for (const auto* filter_chain : draining_filter_chains) {
      deferRemoveFilterChain(filter_chain);
    }
    is_deleting_ = was_deleting;
  }

  virtual void incNumConnections() PURE;
  virtual void decNumConnections() PURE;

  /**
   * Create a new connection from a socket accepted by the listener.
   */
  void newConnection(Network::ConnectionSocketPtr&& socket,
                     std::unique_ptr<StreamInfo::StreamInfo> stream_info);
  /**
   * Schedule to remove and destroy the active connections owned by the filter chain.
   */
  virtual void deferRemoveFilterChain(const Network::FilterChain* filter_chain) PURE;

  virtual Network::BalancedConnectionHandlerOptRef
  getBalancedHandlerByAddress(const Network::Address::Instance& address) PURE;

  void onSocketAccepted(std::unique_ptr<ActiveTcpSocket> active_socket) {
    // Create and run the filters
    config_->filterChainFactory().createListenerFilterChain(*active_socket);
    active_socket->continueFilterChain(true);

    // Move active_socket to the sockets_ list if filter iteration needs to continue later.
    // Otherwise we let active_socket be destructed when it goes out of scope.
    if (active_socket->iter_ != active_socket->accept_filters_.end()) {
      active_socket->startTimer();
      LinkedList::moveIntoListBack(std::move(active_socket), sockets_);
    } else {
      if (!active_socket->connected_) {
        // If active_socket is about to be destructed, emit logs if a connection is not created.
        if (active_socket->stream_info_ != nullptr) {
          emitLogs(*config_, *active_socket->stream_info_);
        } else {
          // If the active_socket is not connected, this socket is not promoted to active
          // connection. Thus the stream_info_ is owned by this active socket.
          ENVOY_BUG(active_socket->stream_info_ != nullptr,
                    "the unconnected active socket must have stream info.");
        }
      }
    }
  }

  // Below members are open to access by ActiveTcpSocket.
  Network::ConnectionHandler& parent_;
  const std::chrono::milliseconds listener_filters_timeout_;
  const bool continue_on_listener_filters_timeout_;
  std::list<std::unique_ptr<ActiveTcpSocket>> sockets_;

protected:
  /**
   * Create the active connection from server connection. This active listener owns the created
   * active connection.
   *
   * @param filter_chain The network filter chain linking to the connection.
   * @param server_conn_ptr The server connection.
   * @param stream_info The stream info of the active connection.
   */
  virtual void newActiveConnection(const Network::FilterChain& filter_chain,
                                   Network::ServerConnectionPtr server_conn_ptr,
                                   std::unique_ptr<StreamInfo::StreamInfo> stream_info) PURE;
  Event::Dispatcher& dispatcher_;
  Network::ListenerPtr listener_;
  bool is_deleting_{false};
};

} // namespace Server
} // namespace Envoy
