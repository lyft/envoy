#!/bin/bash

echo "archive directory is not availabe on https://github.com/grailbio/bazel-compilation-database. Please check again!"
exit 1

RELEASE_VERSION=0.3.1

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/grailbio/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
