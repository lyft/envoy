#include "source/common/network/happy_eyeballs_connection_impl.h"

#include <vector>

namespace Envoy {
namespace Network {

HappyEyeballsConnectionImpl::HappyEyeballsConnectionImpl(
    Event::Dispatcher& dispatcher, const std::vector<Address::InstanceConstSharedPtr>& address_list,
    Address::InstanceConstSharedPtr source_address, TransportSocketFactory& socket_factory,
    TransportSocketOptionsConstSharedPtr transport_socket_options,
    const ConnectionSocket::OptionsSharedPtr options)
    // XXX: get a real id.
    : id_(42), dispatcher_(dispatcher), address_list_(address_list), source_address_(source_address),
      socket_factory_(socket_factory), transport_socket_options_(transport_socket_options),
      options_(options),
      next_attempt_timer_(dispatcher_.createTimer([this]() -> void { tryAnotherConnection(); })) {
  connections_.push_back(createNextConnection());
}

HappyEyeballsConnectionImpl::~HappyEyeballsConnectionImpl() = default;

void HappyEyeballsConnectionImpl::connect() {
  ENVOY_BUG(!connect_finished_, "connection already connected");
  connections_[0]->connect();
  maybeScheduleNextAttempt();
}

void HappyEyeballsConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addWriteFilter(filter);
    return;
  }
  post_connect_state_.write_filters_.push_back(filter);
}

void HappyEyeballsConnectionImpl::addFilter(FilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addFilter(filter);
    return;
  }
  post_connect_state_.filters_.push_back(filter);
}

void HappyEyeballsConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->addReadFilter(filter);
    return;
  }
  post_connect_state_.read_filters_.push_back(filter);
}

void HappyEyeballsConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  if (connect_finished_) {
    connections_[0]->removeReadFilter(filter);
    return;
  }
  auto i = post_connect_state_.read_filters_.begin();
  while (i != post_connect_state_.read_filters_.end()) {
    if (*i == filter) {
      post_connect_state_.read_filters_.erase(i);
      return;
    }
  }
  ASSERT(false);
}

bool HappyEyeballsConnectionImpl::initializeReadFilters() {
  if (connect_finished_) {
    return connections_[0]->initializeReadFilters();
  }
  if (!post_connect_state_.read_filters_.empty()) {
    return false;
  }
  post_connect_state_.initialize_read_filters_.value() = true;
  return true;

}

void HappyEyeballsConnectionImpl::addBytesSentCallback(Connection::BytesSentCb cb) {
  if (connect_finished_) {
    connections_[0]->addBytesSentCallback(cb);
    return;
  }
  post_connect_state_.bytes_sent_callbacks_.push_back(cb);
}

void HappyEyeballsConnectionImpl::enableHalfClose(bool enabled) {
  if (!connect_finished_) {
    per_connection_state_.enable_half_close_ = enabled;
  }
  for (auto& connection : connections_) {
    connection->enableHalfClose(enabled);
  }
}

bool HappyEyeballsConnectionImpl::isHalfCloseEnabled() {
  if (connect_finished_) {
    return connections_[0]->isHalfCloseEnabled();
  }
  return per_connection_state_.enable_half_close_.has_value() && per_connection_state_.enable_half_close_.value();
}

std::string HappyEyeballsConnectionImpl::nextProtocol() const {
  if (connect_finished_) {
    return connections_[0]->nextProtocol();
  }
  return "";
}

void HappyEyeballsConnectionImpl::noDelay(bool enable) {
  if (!connect_finished_) {
    per_connection_state_.no_delay_ = enable;
  }
  for (auto& connection : connections_) {
    connection->noDelay(enable);
  }
}

void HappyEyeballsConnectionImpl::readDisable(bool disable) {
  if (connect_finished_) {
    connections_[0]->readDisable(disable);
    return;
  }
  if (!post_connect_state_.read_disable_count_.has_value()) {
    post_connect_state_.read_disable_count_ = 0;
  }

  if (disable) {
    post_connect_state_.read_disable_count_.value()++;
  } else {
    ASSERT(post_connect_state_.read_disable_count_ != 0);
    post_connect_state_.read_disable_count_.value()--;
  }
}

void HappyEyeballsConnectionImpl::detectEarlyCloseWhenReadDisabled(bool value) {
  if (!connect_finished_) {
    per_connection_state_.detect_early_close_when_read_disabled_ = value;
  }
  for (auto& connection : connections_) {
    connection->detectEarlyCloseWhenReadDisabled(value);
  }
}

bool HappyEyeballsConnectionImpl::readEnabled() const {
  if (!connect_finished_) {
    return !post_connect_state_.read_disable_count_.has_value() ||
        post_connect_state_.read_disable_count_ == 0;

  }
  return connections_[0]->readEnabled();
}

const SocketAddressProvider& HappyEyeballsConnectionImpl::addressProvider() const {
  // Note, this might change before connect finishes.
  return connections_[0]->addressProvider();
}

SocketAddressProviderSharedPtr HappyEyeballsConnectionImpl::addressProviderSharedPtr() const {
  // Note, this might change before connect finishes.
  return connections_[0]->addressProviderSharedPtr();
}

absl::optional<Connection::UnixDomainSocketPeerCredentials>
HappyEyeballsConnectionImpl::unixSocketPeerCredentials() const {
  // Note, this might change before connect finishes.
  return connections_[0]->unixSocketPeerCredentials();
}

Ssl::ConnectionInfoConstSharedPtr HappyEyeballsConnectionImpl::ssl() const {
  // Note, this might change before connect finishes.
  return connections_[0]->ssl();
}

Connection::State HappyEyeballsConnectionImpl::state() const {
  if (!connect_finished_) {
    ASSERT(connections_[0]->state() == Connection::State::Open);
  }
  return connections_[0]->state();
}

bool HappyEyeballsConnectionImpl::connecting() const {
  ASSERT(connect_finished_ || connections_[0]->connecting());
  return connections_[0]->connecting();
}

void HappyEyeballsConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
  if (connect_finished_) {
    connections_[0]->write(data, end_stream);
    return;
  }

  post_connect_state_.write_buffer_ = dispatcher_.getWatermarkFactory().createBuffer(
      []() -> void { ASSERT(false); }, [this]() -> void { this->onWriteBufferHighWatermark(); },
      []() -> void { ASSERT(false); });
  if (per_connection_state_.buffer_limits_.has_value()) {
    post_connect_state_.write_buffer_.value()->setWatermarks(
        per_connection_state_.buffer_limits_.value());
  }
  post_connect_state_.write_buffer_.value()->move(data);
  post_connect_state_.end_stream_ = end_stream;
}

void HappyEyeballsConnectionImpl::setBufferLimits(uint32_t limit) {
  if (!connect_finished_) {
    ASSERT(!per_connection_state_.buffer_limits_.has_value());
    per_connection_state_.buffer_limits_ = limit;
    if (post_connect_state_.write_buffer_.has_value()) {
      post_connect_state_.write_buffer_.value()->setWatermarks(
          per_connection_state_.buffer_limits_.value());
    }
  }
  for (auto& connection : connections_) {
    connection->setBufferLimits(limit);
  }
}

uint32_t HappyEyeballsConnectionImpl::bufferLimit() const {
  if (!connect_finished_) {
    if (per_connection_state_.buffer_limits_.has_value()) {
      return per_connection_state_.buffer_limits_.value();
    }
    return 0;
  }
  return connections_[0]->bufferLimit();
}

bool HappyEyeballsConnectionImpl::aboveHighWatermark() const {
  if (!connect_finished_) {
    return above_write_high_water_mark_;
  }

  return connections_[0]->aboveHighWatermark();
}

const ConnectionSocket::OptionsSharedPtr& HappyEyeballsConnectionImpl::socketOptions() const {
  // Note, this might change before connect finishes.
  return connections_[0]->socketOptions();
}

absl::string_view HappyEyeballsConnectionImpl::requestedServerName() const {
  // Note, this might change before connect finishes.
  return connections_[0]->requestedServerName();
}

StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() {
  // Note, this might change before connect finishes.
  return connections_[0]->streamInfo();
}

const StreamInfo::StreamInfo& HappyEyeballsConnectionImpl::streamInfo() const {
  // Note, this might change before connect finishes.
  return connections_[0]->streamInfo();
}

absl::string_view HappyEyeballsConnectionImpl::transportFailureReason() const {
  // Note, this might change before connect finishes.
  return connections_[0]->transportFailureReason();
}

bool HappyEyeballsConnectionImpl::startSecureTransport() {
  if (!connect_finished_) {
    per_connection_state_.start_secure_transport_ = true;
  }
  bool ret = false;
  for (auto& connection : connections_) {
    ret = connection->startSecureTransport();
  }
  return ret;
}

absl::optional<std::chrono::milliseconds> HappyEyeballsConnectionImpl::lastRoundTripTime() const {
  // Note, this might change before connect finishes.
  return connections_[0]->lastRoundTripTime();
}

void HappyEyeballsConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->addConnectionCallbacks(cb);
    return;
  }
  post_connect_state_.connection_callbacks_.push_back(&cb);
}

void HappyEyeballsConnectionImpl::removeConnectionCallbacks(ConnectionCallbacks& cb) {
  if (connect_finished_) {
    connections_[0]->removeConnectionCallbacks(cb);
    return;
  }
  auto i = post_connect_state_.connection_callbacks_.begin();
  while (i != post_connect_state_.connection_callbacks_.end()) {
    if (*i == &cb) {
      post_connect_state_.connection_callbacks_.erase(i);
      return;
    }
  }
  ASSERT(false);
}

void HappyEyeballsConnectionImpl::close(ConnectionCloseType type) {
  if (connect_finished_) {
    connections_[0]->close(type);
    return;
  }

  connect_finished_ = true;
  next_attempt_timer_->disableTimer();
  for (size_t i = 0; i < connections_.size(); ++i) {
    connections_[i]->removeConnectionCallbacks(*callbacks_wrappers_[i]);
    if (i != 0) {
      // Wait to close the final connection until the post-connection callbacks
      // have been added.
      connections_[i]->close(ConnectionCloseType::NoFlush);
    }
  }
  connections_.resize(1);
  callbacks_wrappers_.clear();

  for (auto cb : post_connect_state_.connection_callbacks_) {
    if (cb) {
      connections_[0]->addConnectionCallbacks(*cb);
    }
  }
  connections_[0]->close(type);
}

Event::Dispatcher& HappyEyeballsConnectionImpl::dispatcher() {
  ASSERT(&dispatcher_ == &connections_[0]->dispatcher());
  return connections_[0]->dispatcher();
}

uint64_t HappyEyeballsConnectionImpl::id() const {
  return id_;
}

void HappyEyeballsConnectionImpl::hashKey(std::vector<uint8_t>& hash_key) const {
  // Pack the id into sizeof(id_) uint8_t entries in the hash_key vector.
  hash_key.reserve(hash_key.size() + sizeof(id_));
  for (unsigned i = 0; i < sizeof(id_); ++i) {
    hash_key.push_back(0xFF & (id_ >> (8 * i)));
  }
}

void HappyEyeballsConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  if (!connect_finished_) {
    per_connection_state_.connection_stats_ = stats;
  }
  for (auto& connection : connections_) {
    connection->setConnectionStats(stats);
  }
}

void HappyEyeballsConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  if (!connect_finished_) {
    per_connection_state_.delayed_close_timeout_ = timeout;
  }
  for (auto& connection : connections_) {
    connection->setDelayedCloseTimeout(timeout);
  }
}

void HappyEyeballsConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "HappyEyeballsConnectionImpl " << this << DUMP_MEMBER(id_) << DUMP_MEMBER(connect_finished_) << "\n";

  for (auto& connection : connections_) {
    DUMP_DETAILS(connection);
  }
}

ClientConnectionPtr HappyEyeballsConnectionImpl::createNextConnection() {
  ASSERT(next_address_ < address_list_.size());
  auto connection = dispatcher_.createClientConnection(
      address_list_[next_address_++], source_address_,
      socket_factory_.createTransportSocket(transport_socket_options_), options_);
  callbacks_wrappers_.push_back(std::make_unique<ConnectionCallbacksWrapper>(*this, *connection));
  connection->addConnectionCallbacks(*callbacks_wrappers_.back());

  if (per_connection_state_.detect_early_close_when_read_disabled_.has_value()) {
    connection->detectEarlyCloseWhenReadDisabled(
        per_connection_state_.detect_early_close_when_read_disabled_.value());
  }
  if (per_connection_state_.no_delay_.has_value()) {
    connection->noDelay(per_connection_state_.no_delay_.value());
  }
  if (per_connection_state_.connection_stats_.has_value()) {
    connection->setConnectionStats(*per_connection_state_.connection_stats_);
  }
  if (per_connection_state_.buffer_limits_.has_value()) {
    connection->setBufferLimits(per_connection_state_.buffer_limits_.value());
  }
  if (per_connection_state_.enable_half_close_.has_value()) {
    connection->enableHalfClose(per_connection_state_.enable_half_close_.value());
  }
  if (per_connection_state_.delayed_close_timeout_.has_value()) {
    connection->setDelayedCloseTimeout(per_connection_state_.delayed_close_timeout_.value());
  }
  if (per_connection_state_.start_secure_transport_.has_value()) {
    ASSERT(per_connection_state_.start_secure_transport_);
    connection->startSecureTransport();
  }

  return connection;
}

void HappyEyeballsConnectionImpl::tryAnotherConnection() {
  connections_.push_back(createNextConnection());
  connections_.back()->connect();
  maybeScheduleNextAttempt();
}

void HappyEyeballsConnectionImpl::maybeScheduleNextAttempt() {
  if (next_address_ >= address_list_.size()) {
    return;
  }
  next_attempt_timer_->enableTimer(std::chrono::milliseconds(300));
}

void HappyEyeballsConnectionImpl::onEvent(ConnectionEvent event,
                                          ConnectionCallbacksWrapper* wrapper) {
  wrapper->connection().removeConnectionCallbacks(*wrapper);
  if (event != ConnectionEvent::Connected) {
    if (next_address_ < address_list_.size()) {
      next_attempt_timer_->disableTimer();
      tryAnotherConnection();
    }
    if (connections_.size() > 1) {
      // Nuke this connection and associated callbacks and let a subsequent attempt proceed.
      cleanupWrapperAndConnection(wrapper);
      return;
    }
  }

  connect_finished_ = true;
  next_attempt_timer_->disableTimer();

  // Close and delete up other connections.
  auto it = connections_.begin();
  while (it != connections_.end()) {
    if (it->get() != &(wrapper->connection())) {
      (*it)->removeConnectionCallbacks(*wrapper);
      (*it)->close(ConnectionCloseType::NoFlush);
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
  ASSERT(connections_.size() == 1);
  callbacks_wrappers_.clear();

  // Apply post-connect state to the final socket.
  for (auto cb : post_connect_state_.connection_callbacks_) {
    if (cb) {
      connections_[0]->addConnectionCallbacks(*cb);
    }
  }

  for (auto cb : post_connect_state_.bytes_sent_callbacks_) {
    connections_[0]->addBytesSentCallback(cb);
  }

  if (event == ConnectionEvent::Connected) {
    for (auto& filter : post_connect_state_.filters_) {
      connections_[0]->addFilter(filter);
    }
    for (auto& filter : post_connect_state_.write_filters_) {
      connections_[0]->addWriteFilter(filter);
    }
    for (auto& filter : post_connect_state_.read_filters_) {
      connections_[0]->addReadFilter(filter);
    }
    if (post_connect_state_.initialize_read_filters_.has_value()  &&
        post_connect_state_.initialize_read_filters_.value()) {
      bool initialized = connections_[0]->initializeReadFilters();
      ASSERT(initialized);
    }
    if (post_connect_state_.read_disable_count_.has_value()) {
      for (int i = 0; i < post_connect_state_.read_disable_count_.value(); ++i) {
        connections_[0]->readDisable(true);
      }
    }

    if (post_connect_state_.write_buffer_.has_value()) {
      // write_buffer_ and end_stream_ are both set together in write().
      ASSERT(post_connect_state_.end_stream_.has_value());
      connections_[0]->write(*post_connect_state_.write_buffer_.value(),
                             post_connect_state_.end_stream_.value());
    }
  }

  std::vector<ConnectionCallbacks*> cbs;
  cbs.swap(post_connect_state_.connection_callbacks_);
}

void HappyEyeballsConnectionImpl::cleanupWrapperAndConnection(ConnectionCallbacksWrapper* wrapper) {
  for (auto it = connections_.begin(); it != connections_.end();) {
    if (it->get() == &(wrapper->connection())) {
      (*it)->close(ConnectionCloseType::NoFlush);
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = callbacks_wrappers_.begin(); it != callbacks_wrappers_.end();) {
    if (it->get() == wrapper) {
      it = callbacks_wrappers_.erase(it);
    } else {
      ++it;
    }
  }
}

void HappyEyeballsConnectionImpl::onAboveWriteBufferHighWatermark(
    ConnectionCallbacksWrapper* /*wrapper*/) {
  ASSERT(false);
}

void HappyEyeballsConnectionImpl::onBelowWriteBufferLowWatermark(
    ConnectionCallbacksWrapper* /*wrapper*/) {
  ASSERT(false);
}

void HappyEyeballsConnectionImpl::onWriteBufferHighWatermark() {
  ASSERT(!above_write_high_water_mark_);
  above_write_high_water_mark_ = true;
}

} // namespace Network
} // namespace Envoy
