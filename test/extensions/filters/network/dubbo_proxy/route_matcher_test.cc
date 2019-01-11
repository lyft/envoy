#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/route.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/route.pb.validate.h"

#include "common/protobuf/protobuf.h"

#include "extensions/filters/network/dubbo_proxy/router/route_matcher.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

namespace {

envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration
parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration route_config;
  MessageUtil::loadFromYaml(yaml, route_config);
  MessageUtil::validate(route_config);
  return route_config;
}

envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy
parseDubboProxyFromV2Yaml(const std::string& yaml) {
  envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy config;
  MessageUtil::loadFromYaml(yaml, config);
  MessageUtil::validate(config);
  return config;
}

} // namespace

TEST(RouteMatcherTest, WildcardMatchTest) {
  std::string input("add123");
  std::string pattern("add*");
  EXPECT_EQ(true, Utility::wildcardMatch(input.c_str(), pattern.c_str()));

  input = "add123test";
  pattern = "add*test";
  EXPECT_EQ(true, Utility::wildcardMatch(input.c_str(), pattern.c_str()));

  input = "123testadd";
  pattern = "*test*";
  EXPECT_EQ(true, Utility::wildcardMatch(input.c_str(), pattern.c_str()));

  input = "123testadd";
  pattern = "*test";
  EXPECT_EQ(false, Utility::wildcardMatch(input.c_str(), pattern.c_str()));

  pattern = "test*";
  EXPECT_EQ(false, Utility::wildcardMatch(input.c_str(), pattern.c_str()));
}

TEST(RouteMatcherTest, RouteByServiceNameWithAnyMethod) {
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: "*"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    RouteMatcher matcher(config);
    MessageMetadata metadata;
    metadata.setMethodName("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceName("unknown");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceVersion("1.0.0");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    // Ignore version matches if there is no version field in the configuration information.
    metadata.setServiceVersion("1.0.1");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    metadata.setServiceGroup("test_one");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with optional(version and group) matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
version: 1.0.0
group: test
routes:
  - match:
      method:
        name: "*"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    RouteMatcher matcher(config);
    MessageMetadata metadata;
    metadata.setMethodName("test");
    metadata.setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceVersion("1.0.0");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with version matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
version: 1.0.0
routes:
  - match:
      method:
        name: "*"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    RouteMatcher matcher(config);
    MessageMetadata metadata;
    metadata.setMethodName("test");
    metadata.setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceVersion("1.0.0");
    EXPECT_NE(nullptr, matcher.route(metadata, 0));
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    // Ignore group matches if there is no group field in the configuration information.
    metadata.setServiceGroup("test_1");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with group matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
group: HSF
routes:
  - match:
      method:
        name: "*"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    RouteMatcher matcher(config);
    MessageMetadata metadata;
    metadata.setMethodName("test");
    metadata.setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceVersion("1.0.0");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    metadata.setServiceGroup("HSF");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }
}

TEST(RouteMatcherTest, RouteByMethod) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: add
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");

  RouteMatcher matcher(config);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("sub");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("add");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, RouteByMethodWithWildcard) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: add*test
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");

  RouteMatcher matcher(config);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("sub");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  metadata.setMethodName("add123test");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, RouteByParameterWithRangeMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: add
        params_match:
        - index: 0
          type: int
          range_match:
            start: 100
            end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");
  metadata.setMethodName("add");
  metadata.addParameterValue(0, "150");

  RouteMatcher matcher(config);
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, RouteByParameterWithExactMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: add
        params_match:
        - index: 1
          type: string
          exact_match: "user_id:94562"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");
  metadata.setMethodName("add");
  metadata.addParameterValue(1, "user_id:94562");

  RouteMatcher matcher(config);
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, RouteWithHeaders) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name: add
      headers:
      - name: custom
        exact_match: "123"
      - name: custom1
        exact_match: "123"
        invert_match: true
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");
  metadata.setMethodName("add");
  metadata.addHeader("custom", "123");
  std::string test_value("123");

  Envoy::Http::LowerCaseString test_key("custom1");
  metadata.addHeaderReference(test_key, test_value);

  RouteMatcher matcher(config);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  test_value = "456";
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

  test_value = "123";
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(MultiRouteMatcherTest, Route) {
  const std::string yaml = R"EOF(
stat_prefix: dubbo_incomming_stats
protocol_type: Dubbo
serialization_type: Hessian2
route_config:
  - name: test1
    interface: org.apache.dubbo.demo.DemoService
    routes:
      - match:
          method:
            name: add
            params_match:
            - index: 1
              type: string
              exact_match: "user_id"
        route:
            cluster: user_service_dubbo_server
  - name: test2
    interface: org.apache.dubbo.demo.FormatService
    routes:
      - match:
          method:
            name: format
        route:
            cluster: format_service
)EOF";

  envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy config =
      parseDubboProxyFromV2Yaml(yaml);
  MessageMetadata metadata;
  metadata.setServiceName("org.apache.dubbo.demo.DemoService");
  metadata.setMethodName("add");
  metadata.addParameterValue(1, "user_id");

  MultiRouteMatcher matcher(config.route_config());
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
