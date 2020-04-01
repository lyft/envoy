#include "extensions/filters/http/aws_lambda/config.h"

#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/fmt.h"

#include "extensions/common/aws/credentials_provider_impl.h"
#include "extensions/common/aws/signer_impl.h"
#include "extensions/common/aws/utility.h"
#include "extensions/filters/http/aws_lambda/aws_lambda_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {
constexpr auto service_name = "lambda";
namespace {
std::string extractRegionFromArn(absl::string_view arn) {
  auto parsed_arn = parseArn(arn);
  if (parsed_arn.has_value()) {
    return parsed_arn->region();
  }
  throw EnvoyException(fmt::format("Invalid ARN: {}", arn));
}
} // namespace

Http::FilterFactoryCb AwsLambdaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  using namespace envoy::extensions::filters::http::aws_lambda::v3;

  auto credentials_provider =
      std::make_shared<Extensions::Common::Aws::DefaultCredentialsProviderChain>(
          context.api(), Extensions::Common::Aws::Utility::metadataFetcher);

  const std::string region = extractRegionFromArn(proto_config.arn());
  auto signer = std::make_shared<Extensions::Common::Aws::SignerImpl>(
      service_name, region, std::move(credentials_provider), context.dispatcher().timeSource());

  InvocationMode invocation_mode;
  switch (proto_config.invocation_mode()) {
  case Config_InvocationMode_ASYNCHRONOUS:
    invocation_mode = InvocationMode::Asynchronous;
    break;
  case Config_InvocationMode_SYNCHRONOUS:
    invocation_mode = InvocationMode::Synchronous;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  FilterSettings filter_settings{proto_config.arn(), invocation_mode,
                                 proto_config.payload_passthrough()};

  return [signer, filter_settings](Http::FilterChainFactoryCallbacks& cb) {
    auto filter = std::make_shared<Filter>(filter_settings, signer);
    cb.addStreamFilter(filter);
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
AwsLambdaFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  InvocationMode invocation_mode;
  switch (proto_config.invoke_config().invocation_mode()) {
    using namespace envoy::extensions::filters::http::aws_lambda::v3;
  case Config_InvocationMode_ASYNCHRONOUS:
    invocation_mode = InvocationMode::Asynchronous;
    break;
  case Config_InvocationMode_SYNCHRONOUS:
    invocation_mode = InvocationMode::Synchronous;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return std::make_shared<const FilterSettings>(
      FilterSettings{proto_config.invoke_config().arn(), invocation_mode,
                     proto_config.invoke_config().payload_passthrough()});
}
/*
 * Static registration for the AWS Lambda filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AwsLambdaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
