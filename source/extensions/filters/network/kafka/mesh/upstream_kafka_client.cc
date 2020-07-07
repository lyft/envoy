#include "extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

KafkaProducerWrapper::KafkaProducerWrapper(Event::Dispatcher& dispatcher,
                                           Thread::ThreadFactory& thread_factory,
                                           const RawKafkaProducerConfig& configuration)
    : dispatcher_{dispatcher} {

  std::unique_ptr<RdKafka::Conf> conf =
      std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  std::string errstr;
  for (const auto& e : configuration) {
    if (conf->set(e.first, e.second, errstr) != RdKafka::Conf::CONF_OK) {
      throw EnvoyException(absl::StrCat("Could not set producer property [", e.first, "] to [",
                                        e.second, "]:", errstr));
    }
  }

  if (conf->set("dr_cb", this, errstr) != RdKafka::Conf::CONF_OK) {
    ENVOY_LOG(warn, "dr_cb", errstr);
    throw EnvoyException(absl::StrCat("Could not set producer callback:", errstr));
  }

  producer_ = std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf.get(), errstr));
  if (!producer_) {
    throw EnvoyException(absl::StrCat("Could not set create producer:", errstr));
  }

  poller_thread_active_ = true;
  std::function<void()> thread_routine = [this]() -> void { checkDeliveryReports(); };
  poller_thread_ = thread_factory.createThread(thread_routine);
}

KafkaProducerWrapper::~KafkaProducerWrapper() {
  ENVOY_LOG(warn, "Shutting down worker thread");
  // Impl note: this could be optimized by having flag flipped by owning Facade for all of its
  // clients so shutdowns happen in parallel.
  poller_thread_active_ = false;
  poller_thread_->join();
  ENVOY_LOG(warn, "Worker thread shut down successfully");
}

void KafkaProducerWrapper::send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
                                const int32_t partition, const absl::string_view key,
                                const absl::string_view value) {
  {
    ENVOY_LOG(warn, "Sending {} value-bytes to [{}/{}] (data = {})", value.size(), topic, partition,
              reinterpret_cast<long>(value.data()));
    // No flags, we leave all the memory management to Envoy, as we will use address of value data
    // in message callback to tell apart the requests.
    const int flags = 0;
    void* value_data = const_cast<char*>(value.data());
    RdKafka::ErrorCode ec = producer_->produce(topic, partition, flags, value_data, value.size(),
                                               key.data(), key.size(), 0, nullptr, nullptr);
    if (RdKafka::ERR_NO_ERROR == ec) {
      // We have succeeded with submitting data to producer, so we register a callback.
      unfinished_produce_requests_.push_back(origin);
    } else {
      ENVOY_LOG(warn, "Produce failure [{}] while sending {} value-bytes to [{}/{}]", ec,
                value.size(), topic, partition);
      const DeliveryMemento memento = {value_data, ec, 0};
      origin->accept(memento);
    }
  }
}

void KafkaProducerWrapper::checkDeliveryReports() {
  while (poller_thread_active_) {
    // We are going to wait for 1000ms, returning when an event (message delivery) happens or
    // producer is closed. Unfortunately we do not have any ability to interrupt this call, so every
    // destructor is going to take up to this much time.
    producer_->poll(1000);
  }
  ENVOY_LOG(warn, "Poller thread finished");
}

void KafkaProducerWrapper::dr_cb(RdKafka::Message& message) {
  ENVOY_LOG(warn, "KafkaProducerWrapper - dr_cb: [{}] {}/{} -> {} (data = {})", message.err(),
            message.topic_name(), message.partition(), message.offset(),
            reinterpret_cast<long>(message.payload()));
  const DeliveryMemento memento = {message.payload(), message.err(), message.offset()};
  const Event::PostCb callback = [this, memento]() -> void { processDelivery(memento); };
  dispatcher_.post(callback);
}

void KafkaProducerWrapper::processDelivery(const DeliveryMemento& memento) {
  ENVOY_LOG(trace, "KafkaProducerWrapper - processDelivery - entry [{}]",
            reinterpret_cast<long>(memento.data_));
  for (auto it = unfinished_produce_requests_.begin(); it != unfinished_produce_requests_.end();) {
    bool accepted = (*it)->accept(memento);
    if (accepted) {
      ENVOY_LOG(trace, "KafkaProducerWrapper - processDelivery - accepted [{}]",
                reinterpret_cast<long>(memento.data_));
      unfinished_produce_requests_.erase(it);
      break; // This is important - a single request can be mapped into multiple callbacks here.
             // (awkward)
    } else {
      ++it;
    }
  }
}

void KafkaProducerWrapper::markFinished() { poller_thread_active_ = false; }

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
