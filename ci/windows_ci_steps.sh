#!/usr/bin/bash.exe

set -e

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

. "$(dirname "$0")"/setup_cache.sh

# Set up TMPDIR so bash and non-bash can access
# e.g. TMPDIR=/d/tmp, make a link from /d/d to /d so both bash and Windows programs resolve the
# same path
# This is due to this issue: https://github.com/bazelbuild/rules_foreign_cc/issues/334
# rules_foreign_cc does not currently use bazel output/temp directories by default, it uses mktemp
# which respects the value of the TMPDIR environment variable
drive="$(readlink -f $TMPDIR | cut -d '/' -f2)"
/c/windows/system32/cmd.exe "/c if not exist $drive:\\$drive mklink /d $drive:\\$drive $drive:\\"

# Set up PATH to ensure executables from installed software and system to not conflict with those
# from MSVC (e.g. link.exe from mingw64 or find.exe from C:\windows\system32 do not conflict with
# desired executables with those same names).
# export PATH=$(echo :$PATH: | sed "s#::#:#g;s#:/usr/bin:#:#g;s#:/mingw64/bin:#:#ig;s#:/c/windows/system32:#:#ig;s/$/\/usr\/bin:\/mingw64\/bin:\/c\/windows\/system32/;s/^://")

BAZEL_STARTUP_OPTIONS="--noworkspace_rc --bazelrc=windows/.bazelrc --output_base=c:/_eb"
BAZEL_BUILD_OPTIONS="-c opt --config=msvc-cl --show_task_finish --verbose_failures \
  --test_output=all ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

# With all envoy-static and //test/ tree building, no need to test compile externals
# bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //bazel/... --build_tag_filters=-skip_on_windows

bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //source/exe:envoy-static --build_tag_filters=-skip_on_windows

# Test compilation of known MSVC-compatible test sources
bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //test/... --test_tag_filters=-skip_on_windows --build_tests_only

# Test invocations of known-working tests on Windows
# bazel ${BAZEL_STARTUP_OPTIONS} test ${BAZEL_BUILD_OPTIONS} //test/... --test_tag_filters=-skip_on_windows,-fails_on_windows --build_tests_only --test_summary=terse --test_output=errors

