#include "extensions/filters/network/zookeeper_proxy/decoder.h"

#include "common/common/enum_to_int.h"

#include <string>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

constexpr uint32_t BOOL_LENGTH = 1;
constexpr uint32_t INT_LENGTH = 4;
constexpr uint32_t LONG_LENGTH = 8;
constexpr uint32_t XID_LENGTH = 4;
constexpr uint32_t OPCODE_LENGTH = 4;
constexpr uint32_t ZXID_LENGTH = 8;
constexpr uint32_t TIMEOUT_LENGTH = 4;
constexpr uint32_t SESSION_LENGTH = 8;
constexpr uint32_t MULTI_HEADER_LENGTH = 9;
constexpr uint32_t PROTO_VERSION_LENGTH = 4;
constexpr uint32_t SERVER_HEADER_LENGTH = 16;

const char* createFlagsToString(CreateFlags flags) {
  switch (flags) {
  case CreateFlags::PERSISTENT:
    return "persistent";
  case CreateFlags::PERSISTENT_SEQUENTIAL:
    return "persistent_sequential";
  case CreateFlags::EPHEMERAL:
    return "ephemeral";
  case CreateFlags::EPHEMERAL_SEQUENTIAL:
    return "ephemeral_sequential";
  case CreateFlags::CONTAINER:
    return "container";
  case CreateFlags::PERSISTENT_WITH_TTL:
    return "persistent_with_ttl";
  case CreateFlags::PERSISTENT_SEQUENTIAL_WITH_TTL:
    return "persistent_sequential_with_ttl";
  }

  return "unknown";
}

void DecoderImpl::decodeOnData(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "zookeeper_proxy: decoding request with {} bytes at offset {}", data.length(),
            offset);

  // Check message length.
  const int32_t len = helper_.peekInt32(data, offset);
  ensureMinLength(len, INT_LENGTH + XID_LENGTH);
  ensureMaxLength(len);

  // Control requests, with XIDs <= 0.
  //
  // These are meant to control the state of a session:
  // connect, keep-alive, authenticate and set initial watches.
  //
  // Note: setWatches is a command historically used to set watches
  //       right after connecting, typically used when roaming from one
  //       ZooKeeper server to the next. Thus, the special xid.
  //       However, some client implementations might expose setWatches
  //       as a regular data request, so we support that as well.
  const int32_t xid = helper_.peekInt32(data, offset);
  switch (static_cast<XidCodes>(xid)) {
  case XidCodes::CONNECT_XID:
    parseConnect(data, offset, len);
    return;
  case XidCodes::PING_XID:
    offset += OPCODE_LENGTH;
    callbacks_.onPing();
    return;
  case XidCodes::AUTH_XID:
    parseAuthRequest(data, offset, len);
    return;
  case XidCodes::SET_WATCHES_XID:
    offset += OPCODE_LENGTH;
    parseSetWatchesRequest(data, offset, len);
    return;
  default:
    // WATCH_XID is generated by the server, so that and everything
    // else can be ignored here.
    break;
  }

  // Data requests, with XIDs > 0.
  //
  // These are meant to happen after a successful control request, except
  // for two cases: auth requests can happen at any time and ping requests
  // must happen every 1/3 of the negotiated session timeout, to keep
  // the session alive.
  const auto opcode = static_cast<OpCodes>(helper_.peekInt32(data, offset));
  switch (opcode) {
  case OpCodes::GETDATA:
    parseGetDataRequest(data, offset, len);
    break;
  case OpCodes::CREATE:
  case OpCodes::CREATE2:
  case OpCodes::CREATECONTAINER:
  case OpCodes::CREATETTL:
    parseCreateRequest(data, offset, len, static_cast<OpCodes>(opcode));
    break;
  case OpCodes::SETDATA:
    parseSetRequest(data, offset, len);
    break;
  case OpCodes::GETCHILDREN:
    parseGetChildrenRequest(data, offset, len, false);
    break;
  case OpCodes::GETCHILDREN2:
    parseGetChildrenRequest(data, offset, len, true);
    break;
  case OpCodes::DELETE:
    parseDeleteRequest(data, offset, len);
    break;
  case OpCodes::EXISTS:
    parseExistsRequest(data, offset, len);
    break;
  case OpCodes::GETACL:
    parseGetAclRequest(data, offset, len);
    break;
  case OpCodes::SETACL:
    parseSetAclRequest(data, offset, len);
    break;
  case OpCodes::SYNC:
    callbacks_.onSyncRequest(pathOnlyRequest(data, offset, len));
    break;
  case OpCodes::CHECK:
    parseCheckRequest(data, offset, len);
    break;
  case OpCodes::MULTI:
    parseMultiRequest(data, offset, len);
    break;
  case OpCodes::RECONFIG:
    parseReconfigRequest(data, offset, len);
    break;
  case OpCodes::SETWATCHES:
    parseSetWatchesRequest(data, offset, len);
    break;
  case OpCodes::CHECKWATCHES:
    parseXWatchesRequest(data, offset, len, OpCodes::CHECKWATCHES);
    break;
  case OpCodes::REMOVEWATCHES:
    parseXWatchesRequest(data, offset, len, OpCodes::REMOVEWATCHES);
    break;
  case OpCodes::GETEPHEMERALS:
    callbacks_.onGetEphemeralsRequest(pathOnlyRequest(data, offset, len));
    break;
  case OpCodes::GETALLCHILDRENNUMBER:
    callbacks_.onGetAllChildrenNumberRequest(pathOnlyRequest(data, offset, len));
    break;
  case OpCodes::CLOSE:
    callbacks_.onCloseRequest();
    break;
  default:
    throw EnvoyException(fmt::format("Unknown opcode: {}", enumToSignedInt(opcode)));
  }

  requests_by_xid_[xid] = opcode;
}

void DecoderImpl::decodeOnWrite(Buffer::Instance& data, uint64_t& offset) {
  ENVOY_LOG(trace, "zookeeper_proxy: decoding response with {} bytes at offset {}", data.length(),
            offset);

  // Check message length.
  const int32_t len = helper_.peekInt32(data, offset);
  ensureMinLength(len, INT_LENGTH + XID_LENGTH);
  ensureMaxLength(len);

  const auto xid = helper_.peekInt32(data, offset);
  const auto xid_code = static_cast<XidCodes>(xid);

  // Connect responses are special, they have no full reply header
  // but just an XID with no zxid nor error fields like the ones
  // available for all other server generated messages.
  if (xid_code == XidCodes::CONNECT_XID) {
    parseConnectResponse(data, offset, len);
    return;
  }

  // Control responses that aren't connect, with XIDs <= 0.
  const auto zxid = helper_.peekInt64(data, offset);
  const auto error = helper_.peekInt32(data, offset);
  switch (xid_code) {
  case XidCodes::PING_XID:
    callbacks_.onResponse(OpCodes::PING, xid, zxid, error);
    return;
  case XidCodes::AUTH_XID:
    callbacks_.onResponse(OpCodes::SETAUTH, xid, zxid, error);
    return;
  case XidCodes::SET_WATCHES_XID:
    callbacks_.onResponse(OpCodes::SETWATCHES, xid, zxid, error);
    return;
  case XidCodes::WATCH_XID:
    parseWatchEvent(data, offset, len, zxid, error);
    return;
  default:
    break;
  }

  // Find the corresponding request for this XID.
  const auto it = requests_by_xid_.find(xid);
  if (it == requests_by_xid_.end()) {
    return;
  }

  const auto opcode = it->second;
  requests_by_xid_.erase(it);
  offset += (len - (XID_LENGTH + ZXID_LENGTH + INT_LENGTH));
  callbacks_.onResponse(opcode, xid, zxid, error);
}

void DecoderImpl::ensureMinLength(const int32_t len, const int32_t minlen) const {
  if (len < minlen) {
    throw EnvoyException("Packet is too small");
  }
}

void DecoderImpl::ensureMaxLength(const int32_t len) const {
  if (static_cast<uint32_t>(len) > max_packet_bytes_) {
    throw EnvoyException("Packet is too big");
  }
}

void DecoderImpl::parseConnect(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + ZXID_LENGTH + TIMEOUT_LENGTH + SESSION_LENGTH + INT_LENGTH);

  // Skip zxid, timeout, and session id.
  offset += ZXID_LENGTH + TIMEOUT_LENGTH + SESSION_LENGTH;

  // Skip password.
  skipString(data, offset);

  // Read readonly flag, if it's there.
  bool readonly{};
  if (data.length() >= offset + 1) {
    readonly = helper_.peekBool(data, offset);
  }

  callbacks_.onConnect(readonly);
}

void DecoderImpl::parseAuthRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + INT_LENGTH + INT_LENGTH);

  // Skip opcode + type.
  offset += OPCODE_LENGTH + INT_LENGTH;
  const std::string scheme = helper_.peekString(data, offset);
  // Skip credential.
  skipString(data, offset);

  callbacks_.onAuthRequest(scheme);
}

void DecoderImpl::parseGetDataRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = helper_.peekString(data, offset);
  const bool watch = helper_.peekBool(data, offset);

  callbacks_.onGetDataRequest(path, watch);
}

void DecoderImpl::skipAcls(Buffer::Instance& data, uint64_t& offset) {
  const int32_t count = helper_.peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    // Perms.
    helper_.peekInt32(data, offset);
    // Skip scheme.
    skipString(data, offset);
    // Skip cred.
    skipString(data, offset);
  }
}

void DecoderImpl::parseCreateRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                     OpCodes opcode) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);

  // Skip data.
  skipString(data, offset);
  skipAcls(data, offset);

  const CreateFlags flags = static_cast<CreateFlags>(helper_.peekInt32(data, offset));
  callbacks_.onCreateRequest(path, flags, opcode);
}

void DecoderImpl::parseSetRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);
  // Skip data.
  skipString(data, offset);
  // Ignore version.
  helper_.peekInt32(data, offset);

  callbacks_.onSetRequest(path);
}

void DecoderImpl::parseGetChildrenRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                          const bool two) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = helper_.peekString(data, offset);
  const bool watch = helper_.peekBool(data, offset);

  callbacks_.onGetChildrenRequest(path, watch, two);
}

void DecoderImpl::parseDeleteRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);
  const int32_t version = helper_.peekInt32(data, offset);

  callbacks_.onDeleteRequest(path, version);
}

void DecoderImpl::parseExistsRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH + BOOL_LENGTH);

  const std::string path = helper_.peekString(data, offset);
  const bool watch = helper_.peekBool(data, offset);

  callbacks_.onExistsRequest(path, watch);
}

void DecoderImpl::parseGetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH);

  const std::string path = helper_.peekString(data, offset);

  callbacks_.onGetAclRequest(path);
}

void DecoderImpl::parseSetAclRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);
  skipAcls(data, offset);
  const int32_t version = helper_.peekInt32(data, offset);

  callbacks_.onSetAclRequest(path, version);
}

std::string DecoderImpl::pathOnlyRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + INT_LENGTH);
  return helper_.peekString(data, offset);
}

void DecoderImpl::parseCheckRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, (2 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);
  const int32_t version = helper_.peekInt32(data, offset);

  callbacks_.onCheckRequest(path, version);
}

void DecoderImpl::parseMultiRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  // Treat empty transactions as a decoding error, there should be at least 1 header.
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + MULTI_HEADER_LENGTH);

  while (true) {
    const int32_t opcode = helper_.peekInt32(data, offset);
    const bool done = helper_.peekBool(data, offset);
    // Ignore error field.
    helper_.peekInt32(data, offset);

    if (done) {
      break;
    }

    switch (static_cast<OpCodes>(opcode)) {
    case OpCodes::CREATE:
      parseCreateRequest(data, offset, len, OpCodes::CREATE);
      break;
    case OpCodes::SETDATA:
      parseSetRequest(data, offset, len);
      break;
    case OpCodes::CHECK:
      parseCheckRequest(data, offset, len);
      break;
    default:
      throw EnvoyException(fmt::format("Unknown opcode within a transaction: {}", opcode));
    }
  }

  callbacks_.onMultiRequest();
}

void DecoderImpl::parseReconfigRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH) + LONG_LENGTH);

  // Skip joining.
  skipString(data, offset);
  // Skip leaving.
  skipString(data, offset);
  // Skip new members.
  skipString(data, offset);
  // Read config id.
  helper_.peekInt64(data, offset);

  callbacks_.onReconfigRequest();
}

void DecoderImpl::parseSetWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (3 * INT_LENGTH));

  // Data watches.
  skipStrings(data, offset);
  // Exist watches.
  skipStrings(data, offset);
  // Child watches.
  skipStrings(data, offset);

  callbacks_.onSetWatchesRequest();
}

void DecoderImpl::parseXWatchesRequest(Buffer::Instance& data, uint64_t& offset, uint32_t len,
                                       OpCodes opcode) {
  ensureMinLength(len, XID_LENGTH + OPCODE_LENGTH + (2 * INT_LENGTH));

  const std::string path = helper_.peekString(data, offset);
  const int32_t type = helper_.peekInt32(data, offset);

  if (opcode == OpCodes::CHECKWATCHES) {
    callbacks_.onCheckWatchesRequest(path, type);
  } else {
    callbacks_.onRemoveWatchesRequest(path, type);
  }
}

void DecoderImpl::skipString(Buffer::Instance& data, uint64_t& offset) {
  const int32_t slen = helper_.peekInt32(data, offset);
  helper_.skip(slen, offset);
}

void DecoderImpl::skipStrings(Buffer::Instance& data, uint64_t& offset) {
  const int32_t count = helper_.peekInt32(data, offset);

  for (int i = 0; i < count; ++i) {
    skipString(data, offset);
  }
}

void DecoderImpl::onData(Buffer::Instance& data) { decode(data, DecodeType::READ); }

void DecoderImpl::onWrite(Buffer::Instance& data) { decode(data, DecodeType::WRITE); }

void DecoderImpl::decode(Buffer::Instance& data, DecodeType dtype) {
  uint64_t offset = 0;

  try {
    while (offset < data.length()) {
      // Reset the helper's cursor, to ensure the current message stays within the
      // allowed max length, even when it's different than the declared length
      // by the message.
      //
      // Note: we need to keep two cursors — offset and helper_'s internal one — because
      //       a buffer may contain multiple messages, so offset is global and helper_'s
      //       internal cursor is reset for each individual message.
      helper_.reset();

      const uint64_t current = offset;
      switch (dtype) {
      case DecodeType::READ:
        decodeOnData(data, offset);
        callbacks_.onRequestBytes(offset - current);
        break;
      case DecodeType::WRITE:
        decodeOnWrite(data, offset);
        callbacks_.onResponseBytes(offset - current);
        break;
      }
    }
  } catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "zookeeper_proxy: decoding exception {}", e.what());
    callbacks_.onDecodeError();
  }
}

void DecoderImpl::parseConnectResponse(Buffer::Instance& data, uint64_t& offset, uint32_t len) {
  ensureMinLength(len, PROTO_VERSION_LENGTH + TIMEOUT_LENGTH + SESSION_LENGTH + INT_LENGTH);

  auto timeout = helper_.peekInt32(data, offset);

  // Skip session id + password.
  offset += SESSION_LENGTH;
  skipString(data, offset);

  // Read readonly flag, if it's there.
  bool readonly{};
  if (data.length() >= offset + 1) {
    readonly = helper_.peekBool(data, offset);
  }

  callbacks_.onConnectResponse(0, timeout, readonly);
}

void DecoderImpl::parseWatchEvent(Buffer::Instance& data, uint64_t& offset, const uint32_t len,
                                  const int64_t zxid, const int32_t error) {
  ensureMinLength(len, SERVER_HEADER_LENGTH + (3 * INT_LENGTH));

  const auto event_type = helper_.peekInt32(data, offset);
  const auto client_state = helper_.peekInt32(data, offset);
  const auto path = helper_.peekString(data, offset);

  callbacks_.onWatchEvent(event_type, client_state, path, zxid, error);
}

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
