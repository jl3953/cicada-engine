
#include "mica/grpcsrc/demotehotkeys_client.h"

using grpc::Channel;

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection ot an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).

  std::string target_str;
  std::string arg_str("--target");
  if (argc > 1) {
    std::string arg_val = argv[1];
    size_t start_pos = arg_val.find(arg_str);
    if (start_pos != std::string::npos) {
      start_pos += arg_str.size();
      if (arg_val[start_pos] == '=') {
        target_str = arg_val.substr(start_pos + 1);
      } else {
        std::cout << "The only correct argument syntax is --target="
                  << std::endl;
        return 0;
      }
    } else {
      std::cout << "The only acceptable argument is --target=" << std::endl;
      return 0;
    }
  } else {
    target_str = "localhost:50052";
  }
  DemoteHotkeysGatewayClient client(grpc::CreateChannel(
      target_str, grpc::InsecureChannelCredentials()));

  auto now_since_epoch = std::chrono::high_resolution_clock::now().time_since_epoch();
  auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_since_epoch);
  int64_t now = now_ns.count();

  std::vector<KVVersion> kvVersions {
      DemoteHotkeysGatewayClient::MakeKVVersion("1994214", "jennifer", now, 0, 100),
      DemoteHotkeysGatewayClient::MakeKVVersion("1994812", "jeff", now, 0, 200),
  };
  const std::vector<KVDemotionStatus> demotionStatuses = client.DemoteHotkeys(kvVersions);
  for (const KVDemotionStatus& demotionStatus : demotionStatuses) {
    std::cout << demotionStatus.key()
              << " " << demotionStatus.is_successfully_demoted()
              << std::endl;
  }

  return 0;
}
