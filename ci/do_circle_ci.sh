#!/bin/bash

set -e

# bazel uses jgit internally and the default circle-ci .gitconfig says to
# convert https://github.com to ssh://git@github.com, which jgit does not support.
if [[ -e "~/.gitconfig" ]]; then
  mv ~/.gitconfig ~/.gitconfig_save
fi

export ENVOY_SRCDIR="$(pwd)"

# xlarge resource_class.
# See note: https://circleci.com/docs/2.0/configuration-reference/#resource_class for why we
# hard code this (basically due to how docker works).
export LOCAL_RESOURCES=14336,8,1.0

# CircleCI doesn't support IPv6 by default, so we run all tests with IPv4, and
# a limited subset with IPv6 using "machine: true" and do_circle_ci_ipv6_tests.sh
# (see https://circleci.com/docs/2.0/executor-types/#using-machine)
export BAZEL_EXTRA_TEST_OPTIONS="--test_env=ENVOY_IP_TEST_VERSIONS=v4only"

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

ci/do_ci.sh "$1"
