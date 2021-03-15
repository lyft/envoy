#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/stderr/v3/stderr.pb.h"
#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/common/file_access_log_impl.h"
#include "extensions/access_loggers/stderr/config.h"
#include "extensions/access_loggers/well_known_names.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {
namespace {

class StderrAccessLogTest : public testing::Test {
public:
  StderrAccessLogTest() = default;

  void runTest(const std::string& yaml, absl::string_view expected, bool is_json) {
    envoy::extensions::access_loggers::stderr::v3::StdErrorAccessLog fal_config;
    TestUtility::loadFromYaml(yaml, fal_config);

    envoy::config::accesslog::v3::AccessLog config;
    config.mutable_typed_config()->PackFrom(fal_config);

    auto file = std::make_shared<AccessLog::MockAccessLogFile>();
    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::Stderr, ""};
    EXPECT_CALL(context_.access_log_manager_, createAccessLog(file_info)).WillOnce(Return(file));

    AccessLog::InstanceSharedPtr logger = AccessLog::AccessLogFactory::fromProto(config, context_);

    absl::Time abslStartTime =
        TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
    stream_info_.start_time_ = absl::ToChronoTime(abslStartTime);
    EXPECT_CALL(stream_info_, upstreamHost()).WillRepeatedly(Return(nullptr));
    stream_info_.response_code_ = 200;

    EXPECT_CALL(*file, write(_)).WillOnce(Invoke([expected, is_json](absl::string_view got) {
      if (is_json) {
        EXPECT_TRUE(TestUtility::jsonStringEqual(std::string(got), std::string(expected)));
      } else {
        EXPECT_EQ(got, expected);
      }
    }));
    logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(StderrAccessLogTest, EmptyFormat) {
  runTest(
      "{}",
      "[2018-12-18T01:50:34.000Z] \"GET /bar/foo -\" 200 - 0 0 - - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      false);
}

TEST_F(StderrAccessLogTest, LogFormatText) {
  runTest(
      R"(
  log_format:
    text_format_source:
      inline_string: "plain_text - %REQ(:path)% - %RESPONSE_CODE%"
)",
      "plain_text - /bar/foo - 200", false);
}

TEST_F(StderrAccessLogTest, LogFormatJson) {
  runTest(
      R"(
  log_format:
    json_format:
      text: "plain text"
      path: "%REQ(:path)%"
      code: "%RESPONSE_CODE%"
)",
      R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})",
      true);
}

} // namespace
} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy