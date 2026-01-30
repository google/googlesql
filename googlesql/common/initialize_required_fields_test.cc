//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/common/initialize_required_fields.h"

#include <memory>

#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "googlesql/common/initialize_required_fields_test.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

using ::googlesql_test::extension_msg;
using ::googlesql_test::extension_opt_i32;
using ::googlesql_test::InitializeRequiredFieldsTestMessage;
using ::googlesql_test::repeated_extension_msg;

using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::DynamicMessageFactory;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::FieldDescriptorProto;
using ::google::protobuf::FileDescriptorProto;
using ::google::protobuf::Reflection;

TEST(InitializeRequiredFieldsTest, ComplexMessageWorks) {
  InitializeRequiredFieldsTestMessage message;

  // Add two repeated fields to test looping.
  message.add_rep_msg1();
  message.add_rep_msg1();

  // Add an optional message
  message.mutable_opt_msg1();

  // And add a required message
  message.mutable_req_msg1();

  InitializeRequiredFields(&message);

  ASSERT_TRUE(message.IsInitialized());

  // Make sure we didn't set anything non-required
  EXPECT_FALSE(message.has_opt_msg2());
  EXPECT_EQ(0, message.rep_msg2_size());
  EXPECT_FALSE(message.has_opt_val());
  EXPECT_EQ(0, message.rep_val_size());

  // Test the basic defaults setting code.
  EXPECT_EQ(0, message.i32());
  EXPECT_EQ(0, message.i64());
  EXPECT_EQ(0, message.u32());
  EXPECT_EQ(0, message.u64());
  EXPECT_EQ(0.0, message.d());
  EXPECT_EQ(0.0, message.f());
  EXPECT_EQ(false, message.b());
  EXPECT_EQ("", message.s());
  EXPECT_EQ(InitializeRequiredFieldsTestMessage::ONE_VAL, message.e());

  // Test the recursion.
  EXPECT_EQ(0, message.req_msg1().req());
  EXPECT_EQ(0, message.opt_msg1().req());
  EXPECT_EQ(0, message.rep_msg1(0).req());
  EXPECT_EQ(0, message.rep_msg1(1).req());
  EXPECT_EQ(0, message.req_msg2().req());

  // Test that optional extensions are not set and that repeated extensions have
  // zero instances (required extensions are disallowed).
  EXPECT_FALSE(message.HasExtension(extension_opt_i32));
  EXPECT_FALSE(message.HasExtension(extension_msg));
  EXPECT_EQ(0, message.ExtensionSize(repeated_extension_msg));
}

TEST(InitializeRequiredFieldsTest, ExtensionMessageWorks) {
  InitializeRequiredFieldsTestMessage message;

  // This will fill the optional message and add repeated messages. The function
  // should descend into them and fill the required fields.
  message.MutableExtension(extension_msg);
  message.AddExtension(repeated_extension_msg);
  message.AddExtension(repeated_extension_msg);

  InitializeRequiredFields(&message);

  ASSERT_TRUE(message.IsInitialized());

  // Make sure we didn't set anything non-required.
  EXPECT_FALSE(message.HasExtension(extension_opt_i32));
  EXPECT_FALSE(message.GetExtension(extension_msg).has_non_req());
  ASSERT_EQ(2, message.ExtensionSize(repeated_extension_msg));
  EXPECT_FALSE(message.GetExtension(repeated_extension_msg, 0).has_non_req());
  EXPECT_FALSE(message.GetExtension(repeated_extension_msg, 1).has_non_req());

  // Test the recursion.
  EXPECT_EQ(0, message.GetExtension(extension_msg).req());
  EXPECT_EQ(0, message.GetExtension(repeated_extension_msg, 0).req());
  EXPECT_EQ(0, message.GetExtension(repeated_extension_msg, 1).req());
}

TEST(InitializeRequiredFieldsTest, EmptyMessageWorks) {
  googlesql_test::EmptyMessage message;
  InitializeRequiredFields(&message);
  ASSERT_TRUE(message.IsInitialized());
}

TEST(InitializeRequiredFieldsTest, DynamicMessageWorks) {
  InitializeRequiredFieldsTestMessage original_message;
  FileDescriptorProto file;
  original_message.GetDescriptor()->file()->CopyTo(&file);

  // Add another extension.
  FieldDescriptorProto* field = file.add_extension();
  field->set_name("dynamic_extension_msg");
  field->set_number(200);
  field->set_label(FieldDescriptorProto::LABEL_OPTIONAL);
  field->set_type(FieldDescriptorProto::TYPE_MESSAGE);
  field->set_type_name("InitializeRequiredFieldsTestMessage.InnerMessage");
  field->set_extendee("InitializeRequiredFieldsTestMessage");

  DescriptorPool pool;
  ASSERT_TRUE(pool.BuildFile(file));
  const Descriptor* message_descriptor = pool.FindMessageTypeByName(
      "googlesql_test.InitializeRequiredFieldsTestMessage");
  ASSERT_TRUE(message_descriptor);
  const FieldDescriptor* extension_msg_descriptor =
      pool.FindExtensionByName("googlesql_test.extension_msg");
  ASSERT_TRUE(extension_msg_descriptor);
  const FieldDescriptor* dynamic_extension_msg_descriptor =
      pool.FindExtensionByName("googlesql_test.dynamic_extension_msg");
  ASSERT_TRUE(dynamic_extension_msg_descriptor);

  DynamicMessageFactory factory;
  std::unique_ptr<google::protobuf::Message> message(
      factory.GetPrototype(message_descriptor)->New());

  // This will fill the original and newly added optional messages. The
  // function should descend into them and fill the required fields.
  const Reflection* reflection = message->GetReflection();
  reflection->MutableMessage(message.get(), extension_msg_descriptor, &factory);
  reflection->MutableMessage(message.get(), dynamic_extension_msg_descriptor,
                             &factory);

  InitializeRequiredFields(message.get());

  ASSERT_TRUE(message->IsInitialized());
}

}  // namespace
}  // namespace googlesql
