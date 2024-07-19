// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_generic_session.h"
#include "quiche/quic/moqt/moqt_known_track_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outgoing_queue.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/test_tools/moqt_simulator_harness.h"
#include "quiche/quic/moqt/tools/moqt_mock_visitor.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/test_harness.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace moqt::test {

namespace {

using ::quic::test::MemSliceFromString;
using ::testing::_;
using ::testing::Assign;
using ::testing::Return;

class MoqtIntegrationTest : public quiche::test::QuicheTest {
 public:
  void CreateDefaultEndpoints() {
    client_ = std::make_unique<MoqtClientEndpoint>(
        &test_harness_.simulator(), "Client", "Server", MoqtVersion::kDraft04);
    server_ = std::make_unique<MoqtServerEndpoint>(
        &test_harness_.simulator(), "Server", "Client", MoqtVersion::kDraft04);
    SetupCallbacks();
    test_harness_.set_client(client_.get());
    test_harness_.set_server(server_.get());
  }
  void SetupCallbacks() {
    client_->session()->callbacks() = client_callbacks_.AsSessionCallbacks();
    server_->session()->callbacks() = server_callbacks_.AsSessionCallbacks();
  }

  void WireUpEndpoints() { test_harness_.WireUpEndpoints(); }

  void EstablishSession() {
    CreateDefaultEndpoints();
    WireUpEndpoints();

    client_->quic_session()->CryptoConnect();
    bool client_established = false;
    bool server_established = false;
    EXPECT_CALL(client_callbacks_.session_established_callback, Call())
        .WillOnce(Assign(&client_established, true));
    EXPECT_CALL(server_callbacks_.session_established_callback, Call())
        .WillOnce(Assign(&server_established, true));
    bool success = test_harness_.RunUntilWithDefaultTimeout(
        [&]() { return client_established && server_established; });
    QUICHE_CHECK(success);
  }

 protected:
  quic::simulator::TestHarness test_harness_;

  MockSessionCallbacks client_callbacks_;
  MockSessionCallbacks server_callbacks_;
  std::unique_ptr<MoqtClientEndpoint> client_;
  std::unique_ptr<MoqtServerEndpoint> server_;
};

TEST_F(MoqtIntegrationTest, Handshake) {
  CreateDefaultEndpoints();
  WireUpEndpoints();

  client_->quic_session()->CryptoConnect();
  bool client_established = false;
  bool server_established = false;
  EXPECT_CALL(client_callbacks_.session_established_callback, Call())
      .WillOnce(Assign(&client_established, true));
  EXPECT_CALL(server_callbacks_.session_established_callback, Call())
      .WillOnce(Assign(&server_established, true));
  bool success = test_harness_.RunUntilWithDefaultTimeout(
      [&]() { return client_established && server_established; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, VersionMismatch) {
  client_ = std::make_unique<MoqtClientEndpoint>(
      &test_harness_.simulator(), "Client", "Server",
      MoqtVersion::kUnrecognizedVersionForTests);
  server_ = std::make_unique<MoqtServerEndpoint>(
      &test_harness_.simulator(), "Server", "Client", MoqtVersion::kDraft04);
  SetupCallbacks();
  test_harness_.set_client(client_.get());
  test_harness_.set_server(server_.get());
  WireUpEndpoints();

  client_->quic_session()->CryptoConnect();
  bool client_terminated = false;
  bool server_terminated = false;
  EXPECT_CALL(client_callbacks_.session_established_callback, Call()).Times(0);
  EXPECT_CALL(server_callbacks_.session_established_callback, Call()).Times(0);
  EXPECT_CALL(client_callbacks_.session_terminated_callback, Call(_))
      .WillOnce(Assign(&client_terminated, true));
  EXPECT_CALL(server_callbacks_.session_terminated_callback, Call(_))
      .WillOnce(Assign(&server_terminated, true));
  bool success = test_harness_.RunUntilWithDefaultTimeout(
      [&]() { return client_terminated && server_terminated; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, AnnounceSuccess) {
  EstablishSession();
  EXPECT_CALL(server_callbacks_.incoming_announce_callback, Call("foo"))
      .WillOnce(Return(std::nullopt));
  testing::MockFunction<void(
      absl::string_view track_namespace,
      std::optional<MoqtAnnounceErrorReason> error_message)>
      announce_callback;
  client_->session()->Announce("foo", announce_callback.AsStdFunction());
  bool matches = false;
  EXPECT_CALL(announce_callback, Call(_, _))
      .WillOnce([&](absl::string_view track_namespace,
                    std::optional<MoqtAnnounceErrorReason> error) {
        matches = true;
        EXPECT_EQ(track_namespace, "foo");
        EXPECT_FALSE(error.has_value());
      });
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return matches; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, AnnounceSuccessSubscribeInResponse) {
  EstablishSession();
  EXPECT_CALL(server_callbacks_.incoming_announce_callback, Call("foo"))
      .WillOnce(Return(std::nullopt));
  MockRemoteTrackVisitor server_visitor;
  testing::MockFunction<void(
      absl::string_view track_namespace,
      std::optional<MoqtAnnounceErrorReason> error_message)>
      announce_callback;
  client_->session()->Announce("foo", announce_callback.AsStdFunction());
  bool matches = false;
  EXPECT_CALL(announce_callback, Call(_, _))
      .WillOnce([&](absl::string_view track_namespace,
                    std::optional<MoqtAnnounceErrorReason> error) {
        EXPECT_EQ(track_namespace, "foo");
        EXPECT_FALSE(error.has_value());
        server_->session()->SubscribeCurrentGroup(track_namespace, "/catalog",
                                                  &server_visitor);
      });
  EXPECT_CALL(server_visitor, OnReply(_, _)).WillOnce([&]() {
    matches = true;
  });
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return matches; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, AnnounceSuccessSendDataInResponse) {
  EstablishSession();

  // Set up the server to subscribe to "data" track for the namespace announce
  // it receives.
  MockRemoteTrackVisitor server_visitor;
  EXPECT_CALL(server_callbacks_.incoming_announce_callback, Call(_))
      .WillOnce([&](absl::string_view track_namespace) {
        server_->session()->SubscribeAbsolute(
            track_namespace, "data", /*start_group=*/0,
            /*start_object=*/0, &server_visitor);
        return std::optional<MoqtAnnounceErrorReason>();
      });

  auto queue = std::make_shared<MoqtOutgoingQueue>(
      client_->session(), FullTrackName{"test", "data"},
      MoqtForwardingPreference::kGroup);
  MoqtKnownTrackPublisher known_track_publisher;
  known_track_publisher.Add(queue);
  client_->session()->set_publisher(&known_track_publisher);
  queue->AddObject(MemSliceFromString("object data"), /*key=*/true);
  bool received_subscribe_ok = false;
  EXPECT_CALL(server_visitor, OnReply(_, _)).WillOnce([&]() {
    received_subscribe_ok = true;
  });
  client_->session()->Announce(
      "test", [](absl::string_view, std::optional<MoqtAnnounceErrorReason>) {});

  bool received_object = false;
  EXPECT_CALL(server_visitor, OnObjectFragment(_, _, _, _, _, _, _, _))
      .WillOnce([&](const FullTrackName& full_track_name,
                    uint64_t group_sequence, uint64_t object_sequence,
                    uint64_t /*object_send_order*/, MoqtObjectStatus status,
                    MoqtForwardingPreference forwarding_preference,
                    absl::string_view object, bool end_of_message) {
        EXPECT_EQ(full_track_name.track_namespace, "test");
        EXPECT_EQ(full_track_name.track_name, "data");
        EXPECT_EQ(group_sequence, 0u);
        EXPECT_EQ(object_sequence, 0u);
        EXPECT_EQ(status, MoqtObjectStatus::kNormal);
        EXPECT_EQ(forwarding_preference, MoqtForwardingPreference::kGroup);
        EXPECT_EQ(object, "object data");
        EXPECT_TRUE(end_of_message);
        received_object = true;
      });
  bool success = test_harness_.RunUntilWithDefaultTimeout(
      [&]() { return received_object; });
  EXPECT_TRUE(received_subscribe_ok);
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, SendMultipleGroups) {
  EstablishSession();
  MoqtKnownTrackPublisher publisher;
  server_->session()->set_publisher(&publisher);

  for (MoqtForwardingPreference forwarding_preference :
       {MoqtForwardingPreference::kTrack, MoqtForwardingPreference::kGroup,
        MoqtForwardingPreference::kObject,
        MoqtForwardingPreference::kDatagram}) {
    SCOPED_TRACE(MoqtForwardingPreferenceToString(forwarding_preference));
    MockRemoteTrackVisitor client_visitor;
    std::string name =
        absl::StrCat("pref_", static_cast<int>(forwarding_preference));
    auto queue = std::make_shared<MoqtOutgoingQueue>(
        client_->session(), FullTrackName{"test", name},
        MoqtForwardingPreference::kObject);
    publisher.Add(queue);
    queue->AddObject(MemSliceFromString("object 1"), /*key=*/true);
    queue->AddObject(MemSliceFromString("object 2"), /*key=*/false);
    queue->AddObject(MemSliceFromString("object 3"), /*key=*/false);
    queue->AddObject(MemSliceFromString("object 4"), /*key=*/true);
    queue->AddObject(MemSliceFromString("object 5"), /*key=*/false);

    client_->session()->SubscribeCurrentGroup("test", name, &client_visitor);
    int received = 0;
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 1, 0, _, MoqtObjectStatus::kNormal, _,
                                 "object 4", true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 1, 1, _, MoqtObjectStatus::kNormal, _,
                                 "object 5", true))
        .WillOnce([&] { ++received; });
    bool success = test_harness_.RunUntilWithDefaultTimeout(
        [&]() { return received >= 2; });
    EXPECT_TRUE(success);

    queue->AddObject(MemSliceFromString("object 6"), /*key=*/false);
    queue->AddObject(MemSliceFromString("object 7"), /*key=*/true);
    queue->AddObject(MemSliceFromString("object 8"), /*key=*/false);
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 1, 2, _, MoqtObjectStatus::kNormal, _,
                                 "object 6", true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 1, 3, _, MoqtObjectStatus::kEndOfGroup, _,
                                 "", true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 2, 0, _, MoqtObjectStatus::kNormal, _,
                                 "object 7", true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor,
                OnObjectFragment(_, 2, 1, _, MoqtObjectStatus::kNormal, _,
                                 "object 8", true))
        .WillOnce([&] { ++received; });
    success = test_harness_.RunUntilWithDefaultTimeout(
        [&]() { return received >= 6; });
    EXPECT_TRUE(success);
  }
}

TEST_F(MoqtIntegrationTest, FetchItemsFromPast) {
  EstablishSession();
  MoqtKnownTrackPublisher publisher;
  server_->session()->set_publisher(&publisher);

  for (MoqtForwardingPreference forwarding_preference :
       {MoqtForwardingPreference::kTrack, MoqtForwardingPreference::kGroup,
        MoqtForwardingPreference::kObject,
        MoqtForwardingPreference::kDatagram}) {
    SCOPED_TRACE(MoqtForwardingPreferenceToString(forwarding_preference));
    MockRemoteTrackVisitor client_visitor;
    std::string name =
        absl::StrCat("pref_", static_cast<int>(forwarding_preference));
    auto queue = std::make_shared<MoqtOutgoingQueue>(
        client_->session(), FullTrackName{"test", name},
        MoqtForwardingPreference::kObject);
    publisher.Add(queue);
    for (int i = 0; i < 100; ++i) {
      queue->AddObject(MemSliceFromString("object"), /*key=*/true);
    }

    client_->session()->SubscribeAbsolute("test", name, 0, 0, &client_visitor);
    int received = 0;
    // Those won't arrive since they have expired.
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 0, 0, _, _, _, _, true))
        .Times(0);
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 0, 0, _, _, _, _, true))
        .Times(0);
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 96, 0, _, _, _, _, true))
        .Times(0);
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 96, 0, _, _, _, _, true))
        .Times(0);
    // Those are within the "last three groups" window.
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 97, 0, _, _, _, _, true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 97, 1, _, _, _, _, true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 98, 0, _, _, _, _, true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 98, 1, _, _, _, _, true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 99, 0, _, _, _, _, true))
        .WillOnce([&] { ++received; });
    EXPECT_CALL(client_visitor, OnObjectFragment(_, 99, 1, _, _, _, _, true))
        .Times(0);  // The current group should not be closed yet.
    bool success = test_harness_.RunUntilWithDefaultTimeout(
        [&]() { return received >= 5; });
    EXPECT_TRUE(success);
  }
}

TEST_F(MoqtIntegrationTest, AnnounceFailure) {
  EstablishSession();
  testing::MockFunction<void(
      absl::string_view track_namespace,
      std::optional<MoqtAnnounceErrorReason> error_message)>
      announce_callback;
  client_->session()->Announce("foo", announce_callback.AsStdFunction());
  bool matches = false;
  EXPECT_CALL(announce_callback, Call(_, _))
      .WillOnce([&](absl::string_view track_namespace,
                    std::optional<MoqtAnnounceErrorReason> error) {
        matches = true;
        EXPECT_EQ(track_namespace, "foo");
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->error_code,
                  MoqtAnnounceErrorCode::kAnnounceNotSupported);
      });
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return matches; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, SubscribeAbsoluteOk) {
  EstablishSession();
  FullTrackName full_track_name("foo", "bar");

  MoqtKnownTrackPublisher publisher;
  server_->session()->set_publisher(&publisher);
  auto track_publisher = std::make_shared<MockTrackPublisher>(full_track_name);
  publisher.Add(track_publisher);

  MockRemoteTrackVisitor client_visitor;
  std::optional<absl::string_view> expected_reason = std::nullopt;
  bool received_ok = false;
  EXPECT_CALL(client_visitor, OnReply(full_track_name, expected_reason))
      .WillOnce([&]() { received_ok = true; });
  client_->session()->SubscribeAbsolute(full_track_name.track_namespace,
                                        full_track_name.track_name, 0, 0,
                                        &client_visitor);
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return received_ok; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, SubscribeCurrentObjectOk) {
  EstablishSession();
  FullTrackName full_track_name("foo", "bar");

  MoqtKnownTrackPublisher publisher;
  server_->session()->set_publisher(&publisher);
  auto track_publisher = std::make_shared<MockTrackPublisher>(full_track_name);
  publisher.Add(track_publisher);

  MockRemoteTrackVisitor client_visitor;
  std::optional<absl::string_view> expected_reason = std::nullopt;
  bool received_ok = false;
  EXPECT_CALL(client_visitor, OnReply(full_track_name, expected_reason))
      .WillOnce([&]() { received_ok = true; });
  client_->session()->SubscribeCurrentObject(full_track_name.track_namespace,
                                             full_track_name.track_name,
                                             &client_visitor);
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return received_ok; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, SubscribeCurrentGroupOk) {
  EstablishSession();
  FullTrackName full_track_name("foo", "bar");

  MoqtKnownTrackPublisher publisher;
  server_->session()->set_publisher(&publisher);
  auto track_publisher = std::make_shared<MockTrackPublisher>(full_track_name);
  publisher.Add(track_publisher);

  MockRemoteTrackVisitor client_visitor;
  std::optional<absl::string_view> expected_reason = std::nullopt;
  bool received_ok = false;
  EXPECT_CALL(client_visitor, OnReply(full_track_name, expected_reason))
      .WillOnce([&]() { received_ok = true; });
  client_->session()->SubscribeCurrentGroup(full_track_name.track_namespace,
                                            full_track_name.track_name,
                                            &client_visitor);
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return received_ok; });
  EXPECT_TRUE(success);
}

TEST_F(MoqtIntegrationTest, SubscribeError) {
  EstablishSession();
  FullTrackName full_track_name("foo", "bar");
  MockRemoteTrackVisitor client_visitor;
  std::optional<absl::string_view> expected_reason = "No tracks published";
  bool received_ok = false;
  EXPECT_CALL(client_visitor, OnReply(full_track_name, expected_reason))
      .WillOnce([&]() { received_ok = true; });
  client_->session()->SubscribeCurrentObject(full_track_name.track_namespace,
                                             full_track_name.track_name,
                                             &client_visitor);
  bool success =
      test_harness_.RunUntilWithDefaultTimeout([&]() { return received_ok; });
  EXPECT_TRUE(success);
}

}  // namespace

}  // namespace moqt::test
