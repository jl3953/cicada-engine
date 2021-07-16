//
// Created by jennifer on 7/16/21.
//

#ifndef MICA_DEMOTEHOTKEYS_CLIENT_H
#define MICA_DEMOTEHOTKEYS_CLIENT_H

#include <grpcpp/grpcpp.h>
#include "../build/demotehotkeys.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::Status;
using smdbrpc::DemoteHotkeysGateway;
using smdbrpc::KVVersion;
using smdbrpc::KVDemotionStatus;
using smdbrpc::HLCTimestamp;

class DemoteHotkeysGatewayClient {
 public:
  explicit DemoteHotkeysGatewayClient(const std::shared_ptr<Channel>& channel)
      : stub_(DemoteHotkeysGateway::NewStub(channel)) {}

  std::vector<KVDemotionStatus> DemoteHotkeys (const std::vector<KVVersion>& kvVersions);

  static KVVersion MakeKVVersion(const std::string& key,
                          const std::string& value,
                          int64_t walltime,
                          int32_t logicaltime,
                          int64_t hotness);

 private:
  std::unique_ptr<DemoteHotkeysGateway::Stub> stub_;

};

#endif  //MICA_DEMOTEHOTKEYS_CLIENT_H
