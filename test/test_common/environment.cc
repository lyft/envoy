#include "test/test_common/environment.h"

#include <sys/un.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"

#include "server/options_impl.h"

#include "spdlog/spdlog.h"

namespace {

std::string getCheckedEnvVar(const std::string& var) {
  // Bazel style temp dirs. Should be set by test runner or Bazel.
  const char* path = ::getenv(var.c_str());
  RELEASE_ASSERT(path != nullptr);
  return std::string(path);
}

std::string getOrCreateUnixDomainSocketDirectory() {
  const char* path = ::getenv("TEST_UDSDIR");
  if (path != nullptr) {
    return std::string(path);
  }
  // Generate temporary path for Unix Domain Sockets only. This is a workaround
  // for the sun_path limit on sockaddr_un, since TEST_TMPDIR as generated by
  // Bazel may be too long.
  char test_udsdir[] = "/tmp/envoy_test_uds.XXXXXX";
  RELEASE_ASSERT(::mkdtemp(test_udsdir) != nullptr);
  return std::string(test_udsdir);
}

// Allow initializeOptions() to remember CLI args for getOptions().
int argc_;
char** argv_;

} // namespace

void TestEnvironment::initializeOptions(int argc, char** argv) {
  argc_ = argc;
  argv_ = argv;
}

Server::Options& TestEnvironment::getOptions() {
  static OptionsImpl* options = new OptionsImpl(argc_, argv_, "1", spdlog::level::err);
  return *options;
}

const std::string& TestEnvironment::temporaryDirectory() {
  static const std::string* temporary_directory = new std::string(getCheckedEnvVar("TEST_TMPDIR"));
  return *temporary_directory;
}

const std::string& TestEnvironment::runfilesDirectory() {
  static const std::string* runfiles_directory =
      new std::string(getCheckedEnvVar("TEST_SRCDIR") + "/" + getCheckedEnvVar("TEST_WORKSPACE"));
  return *runfiles_directory;
}

const std::string TestEnvironment::unixDomainSocketDirectory() {
  static const std::string* uds_directory = new std::string(getOrCreateUnixDomainSocketDirectory());
  return *uds_directory;
}

std::string TestEnvironment::substitute(const std::string str) {
  const std::unordered_map<std::string, std::string> path_map = {
      {"test_tmpdir", TestEnvironment::temporaryDirectory()},
      {"test_udsdir", TestEnvironment::unixDomainSocketDirectory()},
      {"test_rundir", TestEnvironment::runfilesDirectory()},
  };
  std::string out_json_string = str;
  for (auto it : path_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, it.second);
  }
  return out_json_string;
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const PortMap& port_map) {
  // Load the entire file as a string, regex replace one at a time and write it back out. Proper
  // templating might be better one day, but this works for now.
  const std::string json_path = TestEnvironment::runfilesPath(path);
  std::string out_json_string;
  {
    std::ifstream file(json_path);
    if (file.fail()) {
      std::cerr << "failed to open: " << json_path << std::endl;
      RELEASE_ASSERT(false);
    }

    std::stringstream file_string_stream;
    file_string_stream << file.rdbuf();
    out_json_string = file_string_stream.str();
  }
  // Substitute ports.
  for (auto it : port_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, std::to_string(it.second));
  }
  // Substitute paths.
  out_json_string = substitute(out_json_string);
  const std::string out_json_path = TestEnvironment::temporaryPath(path + ".with.ports.json");
  RELEASE_ASSERT(::system(("mkdir -p $(dirname " + out_json_path + ")").c_str()) == 0);
  {
    std::ofstream out_json_file(out_json_path);
    out_json_file << out_json_string;
  }
  return out_json_path;
}

Json::ObjectPtr TestEnvironment::jsonLoadFromString(const std::string& json) {
  return Json::Factory::loadFromString(substitute(json));
}

void TestEnvironment::exec(const std::vector<std::string>& args) {
  std::stringstream cmd;
  // Symlinked args[0] can confuse Python when importing module relative, so we let Python know
  // where it can find its module relative files.
  cmd << "PYTHONPATH=$(dirname " << args[0] << ") ";
  for (auto& arg : args) {
    cmd << arg << " ";
  }
  if (::system(cmd.str().c_str()) != 0) {
    std::cerr << "Failed " << cmd.str() << "\n";
    RELEASE_ASSERT(false);
  }
}
