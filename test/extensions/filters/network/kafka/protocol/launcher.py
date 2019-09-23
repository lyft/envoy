#!/usr/bin/python

# Launcher for generating Kafka protocol tests.

import source.extensions.filters.network.kafka.protocol.generator as generator
import sys
import os


def main():
  """
  Kafka test generator script
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Generates tests from Kafka protocol specification.

  Usage:
    launcher.py MESSAGE_TYPE OUTPUT_FILES INPUT_FILES
  where:
  MESSAGE_TYPE : 'request' or 'response'
  OUTPUT_FILES : location of 'requests_test.cc'/'responses_test.cc',
                 'request_codec_request_test.cc' / 'response_codec_response_test.cc',
                 'filter_request_integration_test.cc' / 'filter_response_integration_test.cc'.
  INPUT_FILES: Kafka protocol json files to be processed.

  Kafka spec files are provided in Kafka clients jar file.

  Files created are:
    - ${MESSAGE_TYPE}s_test.cc - serialization/deserialization tests for kafka structures,
    - ${MESSAGE_TYPE}_codec_${MESSAGE_TYPE}_test.cc - integration tests involving codec for all
      request/response operations,
    - filter_${MESSAGE_TYPE}_integration_test.cc - integration tests involving whole broker-level
      filter for all request/response payloads.

  Templates used are:
  - to create '${MESSAGE_TYPE}s_test.cc': ${MESSAGE_TYPE}s_test_cc.j2,
  - to create '${MESSAGE_TYPE}_codec_${MESSAGE_TYPE}_test.cc' -
      ${MESSAGE_TYPE}_codec_${MESSAGE_TYPE}_test_cc.j2,
  - to create 'filter_${MESSAGE_TYPE}_integration_test.cc' -
      filter_${MESSAGE_TYPE}_integration_test_cc.j2
  """
  type = sys.argv[1]
  header_test_cc_file = os.path.abspath(sys.argv[2])
  codec_test_cc_file = os.path.abspath(sys.argv[3])
  filter_int_test_cc_file = os.path.abspath(sys.argv[4])
  input_files = sys.argv[5:]
  generator.generate_test_code(type, header_test_cc_file, codec_test_cc_file,
                               filter_int_test_cc_file, input_files)


if __name__ == "__main__":
  main()
