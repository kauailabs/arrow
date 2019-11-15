// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/dataset/discovery.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arrow/dataset/partition.h"
#include "arrow/dataset/test_util.h"
#include "arrow/filesystem/test_util.h"

namespace arrow {
namespace dataset {

class FileSystemDataSourceDiscoveryTest : public TestFileSystemBasedDataSource {
 public:
  void MakeDiscovery(const std::vector<fs::FileStats>& files) {
    MakeFileSystem(files);
    ASSERT_OK(FileSystemDataSourceDiscovery::Make(fs_.get(), selector_, format_,
                                                  discovery_options_, &discovery_));
  }

  void AssertFinishWithPaths(std::vector<std::string> paths) {
    ASSERT_OK(discovery_->Finish(&source_));
    AssertFragmentsAreFromPath(source_->GetFragments(options_), paths);
  }

 protected:
  fs::Selector selector_;
  FileSystemDiscoveryOptions discovery_options_;
  std::shared_ptr<DataSourceDiscovery> discovery_;
  std::shared_ptr<FileFormat> format_ = std::make_shared<DummyFileFormat>();
};

TEST_F(FileSystemDataSourceDiscoveryTest, Basic) {
  MakeDiscovery({fs::File("a"), fs::File("b")});

  AssertFinishWithPaths({"a", "b"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, Selector) {
  selector_.base_dir = "A";
  selector_.recursive = true;

  MakeDiscovery({fs::File("0"), fs::File("A/a"), fs::File("A/A/a")});
  // "0" doesn't match selector, so it has been dropped:
  AssertFinishWithPaths({"A/a", "A/A/a"});

  discovery_options_.partition_base_dir = "A/A";
  MakeDiscovery({fs::File("0"), fs::File("A/a"), fs::File("A/A/a")});
  // partition_base_dir should not affect filtered files, ony the applied
  // partition scheme.
  AssertFinishWithPaths({"A/a", "A/A/a"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, Partition) {
  selector_.base_dir = "a=ignored/base";
  MakeDiscovery(
      {fs::File(selector_.base_dir + "/a=1"), fs::File(selector_.base_dir + "/a=2")});

  auto partition_scheme =
      std::make_shared<HivePartitionScheme>(schema({field("a", int32())}));
  ASSERT_OK(discovery_->SetPartitionScheme(partition_scheme));

  AssertFinishWithPaths({selector_.base_dir + "/a=1", selector_.base_dir + "/a=2"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, OptionsIgnoredDefaultPrefixes) {
  MakeDiscovery({
      fs::File("."),
      fs::File("_"),
      fs::File("_$folder$"),
      fs::File("_SUCCESS"),
      fs::File("not_ignored_by_default"),
  });

  AssertFinishWithPaths({"not_ignored_by_default"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, OptionsIgnoredCustomPrefixes) {
  discovery_options_.ignore_prefixes = {"not_ignored"};
  MakeDiscovery({
      fs::File("."),
      fs::File("_"),
      fs::File("_$folder$"),
      fs::File("_SUCCESS"),
      fs::File("not_ignored_by_default"),
  });

  AssertFinishWithPaths({".", "_", "_$folder$", "_SUCCESS"});
}

TEST_F(FileSystemDataSourceDiscoveryTest, Inspect) {
  auto s = schema({field("f64", float64())});
  format_ = std::make_shared<DummyFileFormat>(s);

  MakeDiscovery({});
  std::shared_ptr<Schema> actual;

  // No files
  ASSERT_OK(discovery_->Inspect(&actual));
  EXPECT_EQ(actual, nullptr);

  MakeDiscovery({fs::File("test")});
  ASSERT_OK(discovery_->Inspect(&actual));
  EXPECT_EQ(actual, s);
}

}  // namespace dataset
}  // namespace arrow