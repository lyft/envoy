#include "extensions/tracers/zipkin/span_context.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

#include "extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

SpanContext::SpanContext(const Span& span) {
  trace_id_ = span.traceId();
  id_ = span.id();
  parent_id_ = span.isSetParentId() ? span.parentId() : 0;
  sampled_ = span.sampled();
  is_initialized_ = true;
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
