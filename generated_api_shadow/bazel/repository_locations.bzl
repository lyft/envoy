# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.0.3",
        sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        release_date = "2020-08-27",
        use_category = ["api"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/envoyproxy/protoc-gen-validate",
        version = "872b28c457822ed9c2a5405da3c33f386ac0e86f",
        sha256 = "388ea2261bc1d2c6ef6ec01bfaa3aec451aedb245e23514033ccc9b5cc10c4ab",
        strip_prefix = "protoc-gen-validate-{version}",
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/{version}.tar.gz"],
        release_date = "2021-01-05",
        use_category = ["api"],
        implied_untracked_deps = [
            "com_github_iancoleman_strcase",
            "com_github_lyft_protoc_gen_star",
            "com_github_spf13_afero",
            "org_golang_google_genproto",
            "org_golang_x_text",
        ],
    ),
    com_github_cncf_udpa = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/udpa",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "cc1b757b3eddccaaaf0743cbb107742bb7e3ee4f",
        sha256 = "822a007cf155855d0c08a2e753a39e222e5816b904436196244066a818a8a230",
        strip_prefix = "udpa-{version}",
        urls = ["https://github.com/cncf/udpa/archive/{version}.tar.gz"],
        release_date = "2020-12-11",
        use_category = ["api"],
    ),
    com_github_openzipkin_zipkinapi = dict(
        project_name = "Zipkin API",
        project_desc = "Zipkin's language independent model and HTTP Api Definitions",
        project_url = "https://github.com/openzipkin/zipkin-api",
        version = "0.2.2",
        sha256 = "688c4fe170821dd589f36ec45aaadc03a618a40283bc1f97da8fa11686fc816b",
        strip_prefix = "zipkin-api-{version}",
        urls = ["https://github.com/openzipkin/zipkin-api/archive/{version}.tar.gz"],
        release_date = "2019-08-23",
        use_category = ["api"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        project_name = "Google APIs",
        project_desc = "Public interface definitions of Google APIs",
        project_url = "https://github.com/googleapis/googleapis",
        version = "82944da21578a53b74e547774cf62ed31a05b841",
        sha256 = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
        release_date = "2019-12-02",
        use_category = ["api"],
    ),
    opencensus_proto = dict(
        project_name = "OpenCensus Proto",
        project_desc = "Language Independent Interface Types For OpenCensus",
        project_url = "https://github.com/census-instrumentation/opencensus-proto",
        version = "0.3.0",
        sha256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0",
        strip_prefix = "opencensus-proto-{version}/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v{version}.tar.gz"],
        release_date = "2020-07-21",
        use_category = ["api"],
    ),
    prometheus_metrics_model = dict(
        project_name = "Prometheus client model",
        project_desc = "Data model artifacts for Prometheus",
        project_url = "https://github.com/prometheus/client_model",
        version = "60555c9708c786597e6b07bf846d0dc5c2a46f54",
        sha256 = "6748b42f6879ad4d045c71019d2512c94be3dd86f60965e9e31e44a3f464323e",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/{version}.tar.gz"],
        release_date = "2020-06-23",
        use_category = ["api"],
    ),
    rules_proto = dict(
        project_name = "Protobuf Rules for Bazel",
        project_desc = "Protocol buffer rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_proto",
        version = "40298556293ae502c66579620a7ce867d5f57311",
        sha256 = "aa1ee19226f707d44bee44c720915199c20c84a23318bb0597ed4e5c873ccbd5",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/{version}.tar.gz"],
        release_date = "2020-08-17",
        use_category = ["api"],
    ),
    opentelemetry_proto = dict(
        project_name = "OpenTelemetry Proto",
        project_desc = "Language Independent Interface Types For OpenTelemetry",
        project_url = "https://github.com/open-telemetry/opentelemetry-proto",
        version = "0.7.0",
        sha256 = "39cc1fb45039c7687354ca497aff8a55c71d0f1e484f6b81124ba9d821c36441",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
        release_date = "2020-12-09",
        use_category = ["api"],
    ),
)
