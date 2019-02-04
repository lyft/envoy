#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST_F(TestBase, FramedTransportTest_Name) {
  FramedTransportImpl transport;
  EXPECT_EQ(transport.name(), "framed");
}

TEST_F(TestBase, FramedTransportTest_Type) {
  FramedTransportImpl transport;
  EXPECT_EQ(transport.type(), TransportType::Framed);
}

TEST_F(TestBase, FramedTransportTest_NotEnoughData) {
  Buffer::OwnedImpl buffer;
  FramedTransportImpl transport;
  MessageMetadata metadata;

  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());

  addRepeated(buffer, 3, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
}

TEST_F(TestBase, FramedTransportTest_InvalidFrameSize) {
  FramedTransportImpl transport;

  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(-1);

    MessageMetadata metadata;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift framed transport frame size -1");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(0x7fffffff);

    MessageMetadata metadata;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "invalid thrift framed transport frame size 2147483647");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }
}

TEST_F(TestBase, FramedTransportTest_DecodeFrameStart) {
  FramedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<int32_t>(100);

  EXPECT_EQ(buffer.length(), 4);

  MessageMetadata metadata;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, HasOnlyFrameSize(100U));
  EXPECT_EQ(buffer.length(), 0);
}

TEST_F(TestBase, FramedTransportTest_DecodeFrameEnd) {
  FramedTransportImpl transport;

  Buffer::OwnedImpl buffer;

  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST_F(TestBase, FramedTransportTest_EncodeFrame) {
  FramedTransportImpl transport;

  {
    MessageMetadata metadata;
    Buffer::OwnedImpl message;
    message.add("fake message");

    Buffer::OwnedImpl buffer;
    transport.encodeFrame(buffer, metadata, message);

    EXPECT_EQ(0, message.length());
    EXPECT_EQ(std::string("\0\0\0\xC"
                          "fake message",
                          16),
              buffer.toString());
  }

  {
    MessageMetadata metadata;
    Buffer::OwnedImpl message;
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, metadata, message), EnvoyException,
                              "invalid thrift framed transport frame size 0");
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
