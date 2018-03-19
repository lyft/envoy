#include "common/buffer/buffer_impl.h"
#include "common/lua/wrappers.h"

#include "test/test_common/lua_wrappers.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Lua {

class LuaBufferWrapperTest : public LuaWrappersTestBase<BufferWrapper> {};

class LuaMetadataMapWrapperTest : public Envoy::Lua::LuaWrappersTestBase<MetadataMapWrapper> {
public:
  virtual void setup(const std::string& script) {
    Envoy::Lua::LuaWrappersTestBase<MetadataMapWrapper>::setup(script);
    state_->registerType<MetadataMapIterator>();
  }

  envoy::api::v2::core::Metadata parseMetadataFromYaml(const std::string& yaml_string) {
    envoy::api::v2::core::Metadata metadata;
    MessageUtil::loadFromYaml(yaml_string, metadata);
    return metadata;
  }
};

// Basic buffer wrapper methods test.
TEST_F(LuaBufferWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:length())
      testPrint(object:getBytes(0, 2))
      testPrint(object:getBytes(6, 5))
    end
  )EOF"};

  setup(SCRIPT);
  Buffer::OwnedImpl data("hello world");
  BufferWrapper::create(coroutine_->luaState(), data);
  EXPECT_CALL(*this, testPrint("11"));
  EXPECT_CALL(*this, testPrint("he"));
  EXPECT_CALL(*this, testPrint("world"));
  start("callMe");
}

// Invalid params for the buffer wrapper getBytes() call.
TEST_F(LuaBufferWrapperTest, GetBytesInvalidParams) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:getBytes(100, 100)
    end
  )EOF"};

  setup(SCRIPT);
  Buffer::OwnedImpl data("hello world");
  BufferWrapper::create(coroutine_->luaState(), data);
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), LuaException,
      "[string \"...\"]:3: index/length must be >= 0 and (index + length) must be <= buffer size");
}

// Basic methods test for the metadata wrapper.
TEST_F(LuaMetadataMapWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value["name"]))
      end

      recipe = object:get("make.delicious.bread")

      testPrint(recipe["name"])
      testPrint(recipe["origin"])

      testPrint(tostring(recipe["lactose"]))
      testPrint(tostring(recipe["nut"]))

      testPrint(tostring(recipe["portion"]))
      testPrint(tostring(recipe["minutes"]))

      testPrint(recipe["butter"]["type"])
      testPrint(tostring(recipe["butter"]["expensive"]))

      for i, ingredient in ipairs(recipe["ingredients"]) do
        testPrint(ingredient)
      end
    end
    )EOF"};

  testing::InSequence s;
  setup(SCRIPT);

  const std::string yaml = R"EOF(
    filter_metadata:
      envoy.lua:
        make.delicious.bread:
          name: pulla
          origin: finland
          lactose: true
          nut: false
          portion: 5
          minutes: 30.5
          butter:
            type: grass_fed
            expensive: false
          ingredients:
            - flour
            - milk
        make.delicious.cookie:
          name: chewy
    )EOF";

  envoy::api::v2::core::Metadata metadata = parseMetadataFromYaml(yaml);
  const auto filter_metadata = metadata.filter_metadata().at("envoy.lua");
  MetadataMapWrapper::create(coroutine_->luaState(), filter_metadata);

  EXPECT_CALL(*this, testPrint("'make.delicious.bread' 'pulla'"));
  EXPECT_CALL(*this, testPrint("'make.delicious.cookie' 'chewy'"));

  EXPECT_CALL(*this, testPrint("pulla"));
  EXPECT_CALL(*this, testPrint("finland"));

  EXPECT_CALL(*this, testPrint("true"));
  EXPECT_CALL(*this, testPrint("false"));

  EXPECT_CALL(*this, testPrint("5"));
  EXPECT_CALL(*this, testPrint("30.5"));

  EXPECT_CALL(*this, testPrint("grass_fed"));
  EXPECT_CALL(*this, testPrint("false"));

  EXPECT_CALL(*this, testPrint("flour"));
  EXPECT_CALL(*this, testPrint("milk"));

  start("callMe");
}

} // namespace Lua
} // namespace Envoy
