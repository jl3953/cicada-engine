/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <cstdio>
#include <thread>
#include <random>
#include "mica/transaction/db.h"
#include "mica/util/lcore.h"
//#include "mica/util/zipf.h"
#include "mica/util/rand.h"
#include "mica/test/test_tx_conf.h"

#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "../build/smdbrpc.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;
using smdbrpc::HotshardRequest;
using smdbrpc::HotshardReply;
using smdbrpc::HotshardGateway;
using smdbrpc::HLCTimestamp;

typedef DBConfig::Alloc Alloc;
typedef DBConfig::Logger Logger;
typedef DBConfig::Timestamp Timestamp;
typedef DBConfig::ConcurrentTimestamp ConcurrentTimestamp;
typedef DBConfig::Timing Timing;
typedef ::mica::transaction::PagePool<DBConfig> PagePool;
typedef ::mica::transaction::DB<DBConfig> DB;
typedef ::mica::transaction::Table<DBConfig> Table;
typedef DB::HashIndexUniqueU64 HashIndex;
//typedef DB::HashIndexNonuniqueU64 HashIndex;
typedef DB::BTreeIndexUniqueU64 BTreeIndex;
typedef ::mica::transaction::RowVersion<DBConfig> RowVersion;
typedef ::mica::transaction::RowAccessHandle<DBConfig> RowAccessHandle;
typedef ::mica::transaction::RowAccessHandlePeekOnly<DBConfig>
        RowAccessHandlePeekOnly;
typedef ::mica::transaction::Transaction<DBConfig> Transaction;
typedef ::mica::transaction::Result Result;

static ::mica::util::Stopwatch sw;

HashIndex* hash_idx = nullptr;
DB* db_ptr = nullptr;


// Logic and data behind the server's behavior.
//class HotshardGatewayServiceImpl final : public HotshardGateway::Service {
//  Status ContactHotshard(ServerContext* context, const HotshardRequest* request,
//                  HotshardReply* reply) override {
//
//      auto tbl = db_ptr->get_table("main");
//
//      ::mica::util::lcore.pin_thread(0);
//
//      db_ptr->activate(static_cast<uint16_t>(0));
//      Transaction tx(db_ptr->context(0));
//
//      //RowAccessHandle rah(&tx);
//      Timestamp assigned_ts = Timestamp::make(
//          0, static_cast<uint64_t>(request->hlctimestamp().walltime()), 0);
//      // bool began_successfully = tx.begin(false, nullptr, &assigned_ts);
//      if (!tx.begin(false, nullptr, &assigned_ts)) {
//          printf("jenndebug tx.begin() failed.\n");
//          reply->set_is_committed(false);
//          return Status::CANCELLED;
//      }
//
//      // reads
//      for (uint64_t key : request->read_keyset()) {
//
//        auto row_id = static_cast<uint64_t>(-1);
//        if (hash_idx->lookup(&tx, key, true /*skip_validation*/,
//                             [&row_id](auto &k, auto& v){
//                               (void) k;
//                               row_id = v;
//                               return true; /* jenndebug is this correct? */
//                             }) > 0) {
//          // value being read is found
//          RowAccessHandle rah(&tx);
//          if (!rah.peek_row(tbl, 0, row_id, true, true, false) ||
//              !rah.read_row()) {
//            // failed to read value for whatever reason
//            tx.abort();
//            reply->set_is_committed(false);
//            printf("jenndebug reads failed to peek/read row()\n");
//            return Status::CANCELLED;
//          }
//          smdbrpc::KVPair *kvPair = reply->add_read_valueset();
//          kvPair->set_key(key);
//          uint64_t val;
//          memcpy(&val, &rah.cdata()[0], sizeof(val));
//          kvPair->set_value(val);
//        }
//      }
//
//      // writes
//      for (int i = 0; i < request->write_keyset_size(); i++) {
//        uint64_t key = request->write_keyset(i).key();
//        uint64_t val = request->write_keyset(i).value();
//
//        RowAccessHandle rah(&tx);
//        auto row_id = static_cast<uint64_t>(-1);
//        if (hash_idx->lookup(&tx, key, true,
//                             [&row_id](auto &k, auto& v){
//                               (void) k;
//                               row_id = v;
//                               return true;
//                             }) > 0) {
//          // value already exists, just update it
//          if (!rah.peek_row(tbl, 0, row_id, true, false, true) ||
//              !rah.write_row()) {
//            // failed to write
//            tx.abort();
//            reply->set_is_committed(false);
//            printf("jenndebug failed to peek/write rows\n");
//            return Status::CANCELLED;
//          }
//          memcpy(&rah.data()[0], &val, sizeof(val));
//        } else {
//          // value does not exist yet. Create row and insert into index.
//
//          // make new row
//          if (!rah.new_row(tbl, 0, Transaction::kNewRowID, true, kDataSize)) {
//            tx.abort();
//            reply->set_is_committed(false);
//            printf("jenndebug failed to allocate new_row()\n");
//            return Status::CANCELLED;
//          }
//          //rah.data()[0] = static_cast<char>(val);
//          memcpy(&rah.data()[0], &val, sizeof(val));
//
//          // insert into index
//          row_id = rah.row_id();
//          if (hash_idx->insert(&tx, key, row_id) != 1) {
//            tx.abort();
//            reply->set_is_committed(false);
//            printf("jenndebug failed to insert new row into hash_index\n");
//            return Status::CANCELLED;
//          }
//        }
//      }
//
//      // commit
//      Result result;
//      if (!tx.commit(&result)){
//        tx.abort();
//        reply->set_is_committed(false);
//        printf("jenndebug failed to commit tx\n");
//        return Status::CANCELLED;
//      }
//      reply->set_is_committed(true);
//      return Status::OK;
//  }
//};

class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();

    for (auto& cq: cq_vec_)
      cq->Shutdown();
  }

  void Run(int concurrency) {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);

    for (int i = 0; i < concurrency; i++) {
      cq_vec_.emplace_back(builder.AddCompletionQueue().release());
    }

    server_ = builder.BuildAndStart().release();
    std::cout << "Server listening on " << server_address << std::endl;

    for (int i = 0; i < concurrency; i++) {
      server_threads_.emplace_back(std::thread([this, i]{HandleRpcs(i);}));
    }

    for (auto& thread: server_threads_)
      thread.join();
  }

 private:

  class CallData {
   public:
    CallData(HotshardGateway::AsyncService* service, ServerCompletionQueue* cq,
             int thread_id)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE),
          thread_id_(thread_id){
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        status_ = PROCESS;
        service_->RequestContactHotshard(&ctx_, &request_, &responder_,
                                         cq_, cq_, this);
      } else if (status_ == PROCESS) {

        status_ = FINISH;

        auto tbl = db_ptr->get_table("main");
        ::mica::util::lcore.pin_thread(static_cast<size_t>(thread_id_));

        db_ptr->activate(static_cast<uint16_t>(thread_id_));
        Transaction tx(db_ptr->context(static_cast<uint16_t>(thread_id_)));

        Timestamp assigned_ts = Timestamp::make(
            0, static_cast<uint64_t>(request_.hlctimestamp().walltime()), 0);
        if (!tx.begin(false, nullptr, &assigned_ts)) {
          const std::string& err_msg ="jenndebug tx.begin() failed";
          printf("%s\n", err_msg.c_str());
          //reply_.set_is_committed(false);
          responder_.FinishWithError(
              Status(Status::CANCELLED.error_code(), err_msg),
              this);
	  return;
        }

        // reads
        for (uint64_t key : request_.read_keyset()) {
          auto row_id = static_cast<uint64_t>(-1);
          if (hash_idx->lookup(&tx, key, true /*skip_validation*/,
                               [&row_id](auto& k, auto& v) {
                                 (void)k;
                                 row_id = v;
                                 return true; /* jenndebug is this correct? */
                               }) > 0) {
            // value being read is found
            RowAccessHandle rah(&tx);
            if (!rah.peek_row(tbl, 0, row_id, true, true, false) ||
                !rah.read_row()) {
              // failed to read value for whatever reason
              tx.abort();
              reply_.set_is_committed(false);
              const std::string& err_msg = "jenndebug reads failed to peek/read row()";
              printf("%s\n", err_msg.c_str());
              responder_.FinishWithError(
                  Status(Status::CANCELLED.error_code(), err_msg),
                  this);
              return;
            }
            smdbrpc::KVPair* kvPair = reply_.add_read_valueset();
            kvPair->set_key(key);
            uint64_t val;
            memcpy(&val, &rah.cdata()[0], sizeof(val));
            kvPair->set_value(val);
          }
        }

        // writes
        for (int i = 0; i < request_.write_keyset_size(); i++) {
          uint64_t key = request_.write_keyset(i).key();
          uint64_t val = request_.write_keyset(i).value();

          RowAccessHandle rah(&tx);
          auto row_id = static_cast<uint64_t>(-1);
          if (hash_idx->lookup(&tx, key, true, [&row_id](auto& k, auto& v) {
                (void)k;
                row_id = v;
                return true;
              }) > 0) {
            // value already exists, just update it
            if (!rah.peek_row(tbl, 0, row_id, true, false, true) ||
                !rah.write_row()) {
              // failed to write
              tx.abort();
              reply_.set_is_committed(false);
              const char* err_msg = "jenndebug failed to peek/write rows";
              printf("%s\n", err_msg);
              responder_.FinishWithError(
                  Status(Status::CANCELLED.error_code(), err_msg),
                  this);
	      return;
            }
            memcpy(&rah.data()[0], &val, sizeof(val));
          } else {
            // value does not exist yet. Create row and insert into index.

            // make new row
            if (!rah.new_row(tbl, 0, Transaction::kNewRowID, true, kDataSize)) {
              tx.abort();
              reply_.set_is_committed(false);
              const std::string& err_msg ="jenndebug failed to allocate new_row()";
              printf("%s\n", err_msg.c_str());
              responder_.FinishWithError(
                      Status(Status::CANCELLED.error_code(), err_msg),
                      this);
	      return;
            }
            memcpy(&rah.data()[0], &val, sizeof(val));

            // insert into index
            row_id = rah.row_id();
            if (hash_idx->insert(&tx, key, row_id) != 1) {
              tx.abort();
              reply_.set_is_committed(false);
              const std::string& err_msg = "jenndebug failed to insert new row into hash_index";
              printf("%s\n", err_msg.c_str());
              responder_.FinishWithError(
                  Status(Status::CANCELLED.error_code(), err_msg),
                  this);
	      return;
            }
          }
        }

        // commit
        Result result;
        if (!tx.commit(&result)) {
          tx.abort();
          reply_.set_is_committed(false);
          const std::string& err_msg ="jenndebug failed to commit tx";
          printf("%s\n", err_msg.c_str());
          responder_.FinishWithError(
              Status(Status::CANCELLED.error_code(), err_msg),
              this);
	  return;
        }

        reply_.set_is_committed(true);
        responder_.Finish(reply_, Status::OK, this);

      } else {
        new CallData(service_, cq_, thread_id_);
        delete this;
      }
    }

   private:
    int thread_id_;
    HotshardGateway::AsyncService* service_;
    ServerCompletionQueue* cq_;
    ServerContext ctx_;

    HotshardRequest request_;
    HotshardReply reply_;

    ServerAsyncResponseWriter<HotshardReply> responder_;

    enum CallStatus {CREATE, PROCESS, FINISH};
    CallStatus status_;
  };

  [[noreturn]] void HandleRpcs(int i) {
    new CallData(&service_, cq_vec_[i], i);
    void *tag;
    bool ok;
    while (true) {
      cq_vec_[i]->Next(&tag, &ok);
      static_cast<CallData *>(tag)->Proceed();
    }
  }

  std::vector<ServerCompletionQueue*> cq_vec_;
  HotshardGateway::AsyncService service_;
  Server* server_;
  std::list<std::thread> server_threads_;
};

void RunServer(int givenConcurrency) {

  int concurrency = static_cast<int>(std::thread::hardware_concurrency());
  if (givenConcurrency > 0)
    concurrency = givenConcurrency;

  std::cout << "concurrency " << concurrency << std::endl;

  ServerImpl server;
  server.Run(concurrency);
}

int main(int argc, char** argv) {
    auto config = ::mica::util::Config::load_file("test_tx.json");

    uint64_t num_rows = /* static_cast<uint64_t>(atol(argv[1]));*/ 100;
    uint64_t reqs_per_tx = /*static_cast<uint64_t>(atol(argv[2]));*/ 2;
    double read_ratio = /*atof(argv[3]);*/ 0.5;
    double zipf_theta = /*atof(argv[4]);*/ 0.9;
    uint64_t tx_count = /*static_cast<uint64_t>(atol(argv[5]));*/ 100;
    //uint64_t num_threads = /*static_cast<uint64_t>(atol(argv[6]));*/ 2;
    auto num_threads = static_cast<uint64_t>(atol(argv[1]));

    Alloc alloc(config.get("alloc"));
    auto page_pool_size = 24 * uint64_t(1073741824);
    PagePool* page_pools[2];
     if (num_threads == 1) {
       page_pools[0] = new PagePool(&alloc, page_pool_size, 0);
       page_pools[1] = nullptr;
     } else {
        page_pools[0] = new PagePool(&alloc, page_pool_size / 2, 0);
        page_pools[1] = new PagePool(&alloc, page_pool_size / 2, 1);
     }

    ::mica::util::lcore.pin_thread(0);

    sw.init_start();
    sw.init_end();

    if (num_rows == 0) {
        num_rows = SYNTH_TABLE_SIZE;
        reqs_per_tx = REQ_PER_QUERY;
        read_ratio = READ_PERC;
        zipf_theta = ZIPF_THETA;
        tx_count = MAX_TXN_PER_PART;
        num_threads = THREAD_CNT;
#ifndef NDEBUG
        printf("!NDEBUG\n");
    return EXIT_FAILURE;
#endif
    }

    printf("num_rows = %" PRIu64 "\n", num_rows);
    printf("reqs_per_tx = %" PRIu64 "\n", reqs_per_tx);
    printf("read_ratio = %lf\n", read_ratio);
    printf("zipf_theta = %lf\n", zipf_theta);
    printf("tx_count = %" PRIu64 "\n", tx_count);
    printf("num_threads = %" PRIu64 "\n", num_threads);
#ifndef NDEBUG
    printf("!NDEBUG\n");
#endif
    printf("\n");

    Logger logger;
    db_ptr = new DB(page_pools, &logger, &sw, static_cast<uint16_t>(num_threads));
    DB &db = *db_ptr;
    // DB db(page_pools, &logger, &sw, static_cast<uint16_t>(num_threads));


    const uint64_t data_sizes[] = {kDataSize};
    bool ret = db.create_table("main", 1, data_sizes);
    assert(ret);
    (void)ret;

    auto tbl = db.get_table("main");

    db.activate(0);

    // jenncomment hash_idx is on a certain table

    if (kUseHashIndex) {
        bool ret = db.create_hash_index_unique_u64("main_idx", tbl, num_rows);
        //bool ret = db.create_hash_index_nonunique_u64("main_idx", tbl, num_rows);
        assert(ret);
        (void)ret;

        hash_idx = db.get_hash_index_unique_u64("main_idx");
        //hash_idx = db.get_hash_index_nonunique_u64("main_idx");
        Transaction tx(db.context(0));
        hash_idx->init(&tx);
    }

    {
        printf("initializing table\n");

        std::vector<std::thread> threads;
        uint64_t init_num_threads = std::min(uint64_t(2), num_threads);
        for (uint64_t thread_id = 0; thread_id < init_num_threads; thread_id++) {
            threads.emplace_back([&, thread_id] {
                ::mica::util::lcore.pin_thread(thread_id);

                db.activate(static_cast<uint16_t>(thread_id));
                while (db.active_thread_count() < init_num_threads) {
                    ::mica::util::pause();
                    db.idle(static_cast<uint16_t>(thread_id));
                }

                // Randomize the data layout by shuffling row insert order.
                std::mt19937 g(thread_id);
                std::vector<uint64_t> row_ids;
                row_ids.reserve((num_rows + init_num_threads - 1) / init_num_threads);
                for (uint64_t i = thread_id; i < num_rows; i += init_num_threads)
                    row_ids.push_back(i);
                std::shuffle(row_ids.begin(), row_ids.end(), g);

                /** jennsection **/
                std::vector<uint64_t> keys;
                std::vector<uint64_t> values;
                /** end jennsection **/

                Transaction tx(db.context(static_cast<uint16_t>(thread_id)));
                const uint64_t kBatchSize = 16;
                for (uint64_t i = 0; i < row_ids.size(); i += kBatchSize) {
                    while (true) {
                        bool ret = tx.begin();
                        if (!ret) {
                            printf("failed to start a transaction\n");
                            continue;
                        }

                        bool aborted = false;
                        auto i_end = std::min(i + kBatchSize, row_ids.size());
                        for (uint64_t j = i; j < i_end; j++) {
                            RowAccessHandle rah(&tx);
                            if (!rah.new_row(tbl, 0, Transaction::kNewRowID, true,
                                             kDataSize)) {
                                printf("failed to insert rows at new_row(), row = %" PRIu64
                                       "\n",
                                       j);
                                aborted = true;
                                tx.abort();
                                break;
                            }

                            if (kUseHashIndex) {
                                auto row_id_jenn = row_ids[j];
                                auto value_jenn = rah.row_id();
                                keys.push_back(row_id_jenn); // jennsection
                                values.push_back(value_jenn); // jennsection
                                auto ret = hash_idx->insert(&tx, row_id_jenn, value_jenn);
                                if (ret != 1 || ret == HashIndex::kHaveToAbort) {
                                    printf("failed to update index row = %" PRIu64 "\n", j);
                                    aborted = true;
                                    tx.abort();
                                    break;
                                } else {
                                    printf("jenndebug inserted (%lu, %lu)\n", row_id_jenn, value_jenn);
                                }
                            }

                        }

                        if (aborted) continue;

                        Result result;
                        if (!tx.commit(&result)) {
                            printf("failed to insert rows at commit(), row = %" PRIu64
                                   "; result=%d\n",
                                   i_end - 1, static_cast<int>(result));
                            continue;
                        }
                        break;
                    }

                    /** jennsection **/
                    tx.begin();

                    uint64_t value = 0;
                    for (const auto& key : keys) {
                        auto lookup_result =
                                hash_idx->lookup(&tx, key, kSkipValidationForIndexAccess,
                                                 [&value](auto& k, auto& v) {
                                                     (void)k;
                                                     value = v;
                                                     return true;
                                                 });
                        printf("jenndebug found %lu, (%lu, %lu)\n", lookup_result, key, value);
                    }
                    Result result;
                    tx.commit(&result);
                    /** end jennsection **/

                }

                db.deactivate(static_cast<uint16_t>(thread_id));
                return 0;
            });
        }

        while (threads.size() > 0) {
            threads.back().join();
            threads.pop_back();
        }

        // TODO: Use multiple threads to renew rows for more balanced memory access.

        db.activate(0);
        {
            uint64_t i = 0;
            tbl->renew_rows(db.context(0), 0, i, static_cast<uint64_t>(-1), false);
        }
        if (hash_idx != nullptr) {
            uint64_t i = 0;
            hash_idx->index_table()->renew_rows(db.context(0), 0, i,
                                                static_cast<uint64_t>(-1), false);
        }

        db.deactivate(0);

        db.reset_stats();
        db.reset_backoff();

    }

    RunServer(num_threads);

  return 0;
}
