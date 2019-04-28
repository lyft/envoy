#!/bin/bash

set -e
set -x

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${BAZEL_COVERAGE}" ]] && BAZEL_COVERAGE=bazel
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
# TODO(htuch): Today we use a single test binary for performance reasons. This
# should ideally be //test/... in the future for parallelization, but the trace
# merger cost is too high today.
[[ -z "${COVERAGE_TARGET}" ]] && COVERAGE_TARGET="//test/coverage:coverage_tests"

# TODO(htuch): Nuke these lines and the test/coverage tree once we no longer
# need a single test binary.
# Make sure ${COVERAGE_TARGET} is up-to-date.
SCRIPT_DIR="$(realpath "$(dirname "$0")")"
(BAZEL_BIN="${BAZEL_COVERAGE}" "${SCRIPT_DIR}"/coverage/gen_build.sh)

rm -f /tmp/coverage.perf.data

# Generate coverage data.
"${BAZEL_COVERAGE}" coverage -s ${BAZEL_TEST_OPTIONS} \
  "${COVERAGE_TARGET}"  \
  --experimental_cc_coverage \
  --instrumentation_filter=//source/...,//include/... \
  --coverage_report_generator=@bazel_tools//tools/test/CoverageOutputGenerator/java/com/google/devtools/coverageoutputgenerator:Main \
  --combined_report=lcov \
  --profile=/tmp/bazel-prof \
  --cache_test_results=no \
  --coverage_support=//coverage_support \
  --test_env=CC_CODE_COVERAGE_SCRIPT=coverage_support/collect_coverage.sh \
  --test_output=all \
  --define ENVOY_CONFIG_COVERAGE=1 --cxxopt="-DENVOY_CONFIG_COVERAGE=1" --copt=-DNDEBUG --test_timeout=6000

# Generate HTML
declare -r COVERAGE_DIR="${SRCDIR}"/generated/coverage
declare -r COVERAGE_SUMMARY="${COVERAGE_DIR}/coverage_summary.txt"
mkdir -p "${COVERAGE_DIR}"
genhtml bazel-out/_coverage/_coverage_report.dat --output-directory="${COVERAGE_DIR}" | tee "${COVERAGE_SUMMARY}"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"

if [ "$VALIDATE_COVERAGE" == "true" ]
then
  COVERAGE_VALUE=$(grep -Po 'lines\.*: \K(\d|\.)*' "${COVERAGE_SUMMARY}")
  COVERAGE_THRESHOLD=97.5
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
  if test ${COVERAGE_FAILED} -eq 1; then
      echo Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}
      exit 1
  else
      echo Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}
  fi
  echo "HTML coverage report is in ${COVERAGE_DIR}/coverage.html"
fi
