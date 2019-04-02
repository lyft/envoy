#!/bin/bash

# Generate test/coverage/BUILD, which contains a single envoy_cc_test target
# that contains all C++ based tests suitable for performing the coverage run. A
# single binary (as opposed to multiple test targets) is require to work around
# the crazy in https://github.com/bazelbuild/bazel/issues/1118. This is used by
# the coverage runner script.

set -e

[ -z "${BAZEL_BIN}" ] && BAZEL_BIN=bazel
[ -z "${BUILDIFIER_BIN}" ] && BUILDIFIER_BIN=buildifier

# Path to the generated BUILD file for the coverage target.
[ -z "${BUILD_PATH}" ] && BUILD_PATH="$(dirname "$0")"/BUILD

# Extra repository information to include when generating coverage targets. This is useful for
# consuming projects. E.g., "@envoy".
[ -z "${REPOSITORY}" ] && REPOSITORY=""

# This is an extra bazel path to query for additional targets. This is useful for consuming projects
# that want to run coverage over the public envoy code as well as private extensions.
# E.g., "//envoy-lyft/test/..."
[ -z "${EXTRA_QUERY_PATHS}" ] && EXTRA_QUERY_PATHS=""

rm -f "${BUILD_PATH}"

TARGETS=$("${BAZEL_BIN}" query ${BAZEL_QUERY_OPTIONS} "attr('tags', 'coverage_test_lib', ${REPOSITORY}//test/...)" | grep "^//")
if [ -n "${EXTRA_QUERY_PATHS}" ]; then
  TARGETS="$TARGETS $("${BAZEL_BIN}" query ${BAZEL_QUERY_OPTIONS} "attr('tags', 'coverage_test_lib', ${EXTRA_QUERY_PATHS})" | grep "^//")"
fi

# gcov requires gcc
if [ "${NO_GCOV}" != 1 ]
then
  TARGETS="${TARGETS} ${REPOSITORY}//test/coverage/gcc_only_test:gcc_only_test_lib"
fi

(
  cat << EOF
# This file is generated by test/coverage/gen_build.sh automatically prior to
# coverage runs. It is under .gitignore. DO NOT EDIT, DO NOT CHECK IN.
load(
    "${REPOSITORY}//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "coverage_tests",
    repository = "${REPOSITORY}",
    deps = [
EOF
  for t in ${TARGETS}
  do
    echo "        \"$t\","
  done
  cat << EOF
    ],
    tags = ["manual"],
    coverage = False,
    # Needed when invoking external shell tests etc.
    local = True,
)
EOF

) > "${BUILD_PATH}"

echo "Generated coverage BUILD file at: ${BUILD_PATH}"
"${BUILDIFIER_BIN}" "${BUILD_PATH}"
