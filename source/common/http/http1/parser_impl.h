#pragma once

#include <memory>

#include "source/common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class HttpParserImpl : public Parser {
public:
  HttpParserImpl(MessageType type, ParserCallbacks* data);
  ~HttpParserImpl() override;

  // Http1::Parser
  RcVal execute(const char* data, int len) override;
  void resume() override;
  ParserStatus pause() override;
  bool isOk() override;
  bool isPaused() override;
  uint16_t statusCode() const override;
  int httpMajor() const override;
  int httpMinor() const override;
  absl::optional<uint64_t> contentLength() const override;
  void setHasContentLength(bool val) override;
  bool isChunked() const override;
  absl::string_view methodName() const override;
  absl::string_view errnoName(int rc) const override;
  int hasTransferEncoding() const override;
  int statusToInt(const ParserStatus code) const override;

private:
  // TODO(5155): This secondary layer with a private class can be removed after http-parser is
  // removed. This layer avoids colliding symbols between the two libraries by isolating the
  // libraries in separate compilation units.
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
