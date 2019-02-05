#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

#include "common/common/hex.h"
#include "absl/types/optional.h"
#include "common/common/logger.h"

#include "extensions/tracers/xray/tracer_interface.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Base class to be inherited by all classes that represent XRay-related concepts, namely:
                 * endpoint, annotation, binary annotation, and span.
                 */
                class XRayBase {
                public:
                    /**
                     * Destructor.
                     */
                    virtual ~XRayBase() {}

                    /**
                     * All classes defining XRay abstractions need to implement this method to convert
                     * the corresponding abstraction to a XRay-compliant JSON.
                     */
                    virtual const std::string toJson() PURE;
                };

                class BinaryAnnotation : public XRayBase {
                public:
                    /**
                     * Copy constructor.
                     */
                    BinaryAnnotation(const BinaryAnnotation&);

                    /**
                     * Assignment operator.
                     */
                    BinaryAnnotation& operator=(const BinaryAnnotation&);

                    /**
                     * Default constructor. Creates an empty binary annotation.
                     */
                    BinaryAnnotation() : key_(), value_() {}

                    /**
                     * Constructor that creates a binary annotation based on the given parameters.
                     *
                     * @param key The key name of the annotation.
                     * @param value The value associated with the key.
                     */
                    BinaryAnnotation(const std::string& key, const std::string& value) : key_(key), value_(value) {}

                    /**
                     * @return the key attribute.
                     */
                    const std::string& key() const { return key_; }

                    /**
                     * Sets the key attribute.
                     */
                    void setKey(const std::string& key) { key_ = key; }

                    /**
                     * @return the value attribute.
                     */
                    const std::string& value() const { return value_; }

                    /**
                     * Sets the value attribute.
                     */
                    void setValue(const std::string& value) { value_ = value; }

                    /**
                     * Serializes the binary annotation as JSON representation as a string.
                     *
                     * @return a stringified JSON.
                     */
                    const std::string toJson() override;

                private:
                    std::string key_;
                    std::string value_;
                };

                class ChildSpan : public XRayBase {
                public:
                    ChildSpan(const ChildSpan&);

                    ChildSpan() : name_(), id_(0), start_time_(0) {}

                    void setName(const std::string& val) { name_ = val; }

                    void setId(const uint64_t val) { id_ = val; }

                    void setBinaryAnnotations(const std::vector<BinaryAnnotation>& val) { binary_annotations_ = val; }

                    void addBinaryAnnotation(const BinaryAnnotation& bann) { binary_annotations_.push_back(bann); }

                    void addBinaryAnnotation(const BinaryAnnotation&& bann) { binary_annotations_.push_back(bann); }

                    const std::vector<BinaryAnnotation>& binaryAnnotations() const { return binary_annotations_; }

                    void setStartTime(const double time) { start_time_ = time; }

                    uint64_t id() const { return id_; }

                    const std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

                    const std::string& name() const { return name_; }

                    double startTime() const { return start_time_; }

                    const std::string toJson() override;

                private:
                    std::string name_;
                    uint64_t id_;
                    std::vector<BinaryAnnotation> binary_annotations_;
                    double start_time_;
                };

                typedef std::unique_ptr<Span> SpanPtr;

                class Span : public XRayBase, Logger::Loggable<Logger::Id::tracing> {
                public:
                    /**
                     * Copy constructor.
                     */
                    Span(const Span &);

                    /**
                     * Default constructor. Creates an empty span.
                     */
                    Span() : trace_id_(), name_(), id_(0), sampled_(false), start_time_(0) {}

                    /**
                     * Sets the span's trace id attribute.
                     */
                    void setTraceId(const std::string& val) { trace_id_ = val; }

                    /**
                     * Sets the span's name attribute.
                     */
                    void setName(const std::string& val) { name_ = val; }

                    /**
                     * Sets the span's id.
                     */
                    void setId(const uint64_t val) { id_ = val; }

                    /**
                     * Sets the span's parent id.
                     */
                    void setParentId(const uint64_t val) { parent_id_ = val; }

                    /**
                     * @return Whether or not the parent_id attribute is set.
                     */
                    bool isSetParentId() const { return parent_id_.has_value(); }

                    /**
                     * Set the span's sampled flag.
                     */
                    void setSampled(bool val) { sampled_ = val; }

                    /**
                     * Sets the span's binary annotations all at once.
                     */
                    void setBinaryAnnotations(const std::vector<BinaryAnnotation>& val) { binary_annotations_ = val; }

                    /**
                     * Adds a binary annotation to the span (copy semantics).
                     */
                    void addBinaryAnnotation(const BinaryAnnotation& bann) { binary_annotations_.push_back(bann); }

                    /**
                     * Adds a binary annotation to the span (move semantics).
                     */
                    void addBinaryAnnotation(const BinaryAnnotation&& bann) { binary_annotations_.push_back(bann); }

                    const std::vector<BinaryAnnotation>& binaryAnnotations() const { return binary_annotations_; }

                    void setChildSpans(const std::vector<ChildSpan>& val) { child_span_ = val; }

                    void addChildSpan(const ChildSpan& child) {
                        child_span_.push_back(child);
                    }

                    void addChildSpan(const ChildSpan&& child) {
                        child_span_.push_back(child);
                    }

                    const std::vector<ChildSpan>& childSpans() const { return child_span_; }

                    /**
                     * Sets the span's timestamp attribute.
                     */
                    void setTimestamp(const int64_t val) { timestamp_ = val; }

                    /**
                     * @return Whether or not the timestamp attribute is set.
                     */
                    bool isSetTimestamp() const { return timestamp_.has_value(); }

                    /**
                     * Sets the span start-time attribute (monotonic, used to calculate duration).
                     */
                    void setStartTime(const double time) { start_time_ = time; }

                    void setServiceName(const std::string& service_name);

                    /**
                     * @return the span's id as an integer.
                     */
                    uint64_t id() const { return id_; }

                    /**
                     * @return the span's id as a hexadecimal string.
                     */
                    const std::string idAsHexString() const { return Hex::uint64ToHex(id_); }

                    const std::string parentIdAsHexString() const {
                        return parent_id_ ? Hex::uint64ToHex(parent_id_.value()) : EMPTY_HEX_STRING_;
                    }

                    /**
                     * @return the span's name.
                     */
                    const std::string& name() const { return name_; }

                    /**
                     * @return the span's parent id as an integer.
                     */
                    uint64_t parentId() const { return parent_id_.value(); }

                    /**
                     * @return whether or not the sampled attribute is set
                     */
                    bool sampled() const { return sampled_; }

                    /**
                     * @return the span's timestamp (clock time for user presentation: microseconds since epoch).
                     */
                    int64_t timestamp() const { return timestamp_.value(); }

                    /**
                     * @return the span's trace id as a string.
                     */
                    const std::string traceId() const { return trace_id_; }

                    /**
                     * @return the span's start time (monotonic, used to calculate duration).
                     */
                    double startTime() const { return start_time_; }

                    /**
                     * Serializes the span as a XRay-compliant JSON representation as a string.
                     *
                     * @return a stringified JSON.
                     */
                    const std::string toJson() override;

                    /**
                     * Associates a Tracer object with the span. The tracer's reportSpan() method is invoked
                     * by the span's finish() method so that the tracer can decide what to do with the span
                     * when it is finished.
                     *
                     * @param tracer Represents the Tracer object to be associated with the span.
                     */
                    void setTracer(TracerInterface* tracer) { tracer_ = tracer; }

                    /**
                     * @return the Tracer object associated with the span.
                     */
                    TracerInterface* tracer() const { return tracer_; }

                    /**
                     * Marks a successful end of the span. This method will: invoke the tracer's reportSpan() method if a tracer has been associated with the span.
                     */
                    void finish();

                    void setTag(const std::string& name, const std::string& value);

                private:
                    static const std::string EMPTY_HEX_STRING_;
                    static const std::string VERSION_;
                    static const std::string FORMAT_;

                    std::string trace_id_;
                    std::string name_;
                    uint64_t id_;
                    absl::optional<uint64_t> parent_id_;
                    bool sampled_;
                    std::vector<BinaryAnnotation> binary_annotations_;
                    absl::optional<int64_t> timestamp_;
                    double start_time_;
                    TracerInterface* tracer_;
                    std::vector<ChildSpan> child_span_;
                };
            }
        }
    }
}
