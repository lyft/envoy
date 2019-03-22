#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quiche_epoll_impl.h"

#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_clock.h"

namespace quic {
extern bool quic_monotonic_epoll_clock;

#define GetQuicReloadableFlag(flag) ({ flag; })
#define SetQuicReloadableFlag(flag, value)                                                         \
  do {                                                                                             \
    flag = value;                                                                                  \
  } while (0)

// Clock to efficiently retrieve an approximately accurate time from an
// net::EpollServer.
class QuicEpollClock : public QuicClock {
public:
  explicit QuicEpollClock(quiche::EpollServer* epoll_server);

  QuicEpollClock(const QuicEpollClock&) = delete;
  QuicEpollClock& operator=(const QuicEpollClock&) = delete;

  ~QuicEpollClock() override {}

  // Returns the approximate current time as a QuicTime object.
  QuicTime ApproximateNow() const override;

  // Returns the current time as a QuicTime object.
  // Note: this uses significant resources, please use only if needed.
  QuicTime Now() const override;

  // Returns the current time as a QuicWallTime object.
  // Note: this uses significant resources, please use only if needed.
  QuicWallTime WallNow() const override;

  // Override to do less work in this implementation. The epoll clock is
  // already based on system (unix epoch) time, no conversion required.
  QuicTime ConvertWallTimeToQuicTime(const QuicWallTime& walltime) const override;

protected:
  quiche::EpollServer* epoll_server_;
  // Largest time returned from Now() so far.
  mutable QuicTime largest_time_;
};

} // namespace quic
