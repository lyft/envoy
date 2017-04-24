#!/usr/bin/env python

# This tool is a helper script that queries the admin address for all listener
# addresses after envoy startup. The script can then be used to update an
# exisiting json config file with updated listener addresses. This script is
# currently called in the hot restart integration test to update listener
# addresses bound to port 0 in the intial json config file. With the
# -n or -no_port_change option, this script does not update an exisiting json
# config file but checks that the listener addresses after envoy startup
# match the listener addresses in the initial json config file.

from collections import OrderedDict

import argparse
import httplib
import json
import os.path
import sys
import time

ADMIN_FILE_TIMEOUT_SECS = 20

def ReplaceListenerAddresses(original_json, admin_address, updated_json):
  # Get original listener addresses
  with open(original_json, 'r') as original_json_file:
    # Import original config file in order to get a deterministic output. This
    # allows us to diff the original config file and the updated config file
    # output from this script to check for any changes.
    parsed_json = json.load(original_json_file, object_pairs_hook=OrderedDict)
  original_listeners = parsed_json['listeners']

  sys.stdout.write('Admin address is ' + admin_address + '\n')
  try:
    admin_conn = httplib.HTTPConnection(admin_address)
    admin_conn.request('GET', '/listeners')
    admin_response = admin_conn.getresponse()
    if not admin_response.status == 200:
      return False
    discovered_listeners = json.loads(admin_response.read())
  except:
    sys.stderr.write('Cannot connect to admin.\n')
    return False
  else:
    if not len(discovered_listeners) == len(original_listeners):
      return False
    for discovered, original in zip(discovered_listeners, original_listeners):
      original['address'] = 'tcp://' + discovered
    with open(updated_json, 'w') as outfile:
      json.dump(OrderedDict(parsed_json), outfile, indent=2)
  finally:
    admin_conn.close()

  return True

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Replace listener addressses in json file.')
  parser.add_argument('-o', '--original_json', type=str, required=True,
                      help='Path of the original config json file')
  parser.add_argument('-a', '--admin_address_path', type=str, required=True,
                      help='Path of the admin address file')
  parser.add_argument('-u', '--updated_json', type=str, required=True,
                      help='Path to output updated json config file')
  args = parser.parse_args()
  admin_address_path = args.admin_address_path

  # Read admin address from file
  counter = 0;
  while not os.path.exists(admin_address_path):
    time.sleep(1)
    counter += 1
    if counter > ADMIN_FILE_TIMEOUT_SECS:
      break

  if not os.path.exists(admin_address_path):
    sys.exit(1)

  with open(admin_address_path, 'r') as admin_address_file:
    admin_address = admin_address_file.read()

  result = ReplaceListenerAddresses(args.original_json, admin_address, args.updated_json)

  if not result:
    sys.exit(1)
