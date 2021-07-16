#include <iostream>
#include <thread>

#include "mica/grpcsrc/demotehotkeys_client.h"

using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::Status;
using smdbrpc::DemoteHotkeysGateway;
using smdbrpc::KVVersion;
using smdbrpc::KVDemotionStatus;
using smdbrpc::HLCTimestamp;


// Assembles the client's payload, sends it and presents the response back
// from the server.
/*void DemoteHotkeys() {

  ClientContext context;

  std::shared_ptr<ClientReaderWriter<KVVersion, KVDemotionStatus> > stream(
      stub_->DemoteHotkeys(&context));

  std::cout << "std::shared_ptr stream" << std::endl;

  std::thread writer([stream]() {
    std::vector<KVVersion> kvVersions{
        MakeKVVersion("1994214", "jennifer", 1994214, 1, 214),
        MakeKVVersion("1994812", "jeff", 1994812, 2, 812)
    };

    std::cout << "make vector" << std::endl;
    for (const KVVersion& kvVersion : kvVersions) {
      std::cout << "Sending message " << kvVersion.key()
                << ", " << kvVersion.value()
                << " at " << kvVersion.timestamp().walltime()
                << std::endl;
      stream->Write(kvVersion);
    }
    stream->WritesDone();
  });


  std::cout << "make thread function" << std::endl;

  KVDemotionStatus demotionStatus;
  while (stream->Read(&demotionStatus)) {
    std::cout << "Replied for key " << demotionStatus.key()
              << " with return code " << demotionStatus.is_successfully_demoted()
              << std::endl;
  }
  writer.join();

  Status status = stream->Finish();
  if (!status.ok()) {
    std::cout << "DemoteHotkeys rpc failed" << std::endl;
  }
} */


std::vector<KVDemotionStatus> DemoteHotkeysGatewayClient::DemoteHotkeys(
    const std::vector<KVVersion>& kvVersions) {
  ClientContext context;

  std::shared_ptr<ClientReaderWriter<KVVersion, KVDemotionStatus> > stream(
      stub_->DemoteHotkeys(&context));

  std::thread writer([stream, kvVersions]() {

    for (const KVVersion& kvVersion : kvVersions) {
      stream->Write(kvVersion);
    }
    stream->WritesDone();
  });


  std::vector<KVDemotionStatus> demotionStatuses;
  KVDemotionStatus demotionStatus;
  while (stream->Read(&demotionStatus)) {
    demotionStatuses.push_back(demotionStatus);
  }
  writer.join();

  Status status = stream->Finish();
  if (!status.ok()) {
    std::cout << "DemoteHotkeys rpc failed" << std::endl;
  }

  return demotionStatuses;

}
KVVersion DemoteHotkeysGatewayClient::MakeKVVersion(const std::string& key,
                                                    const std::string& value,
                                                    int64_t walltime,
                                                    int32_t logicaltime,
                                                    int64_t hotness) {
  KVVersion kvVersion;
  kvVersion.set_key(key);
  kvVersion.set_value(value);

  auto* timestamp = new HLCTimestamp();
  timestamp->set_walltime(walltime);
  timestamp->set_logicaltime(logicaltime);
  kvVersion.set_allocated_timestamp(timestamp);

  kvVersion.set_hotness(hotness);

  return kvVersion;
}

