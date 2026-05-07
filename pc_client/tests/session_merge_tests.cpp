#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "session.h"

namespace fs = std::filesystem;
using namespace powermonitor::client;

namespace {

Session::Config make_config() {
    Session::Config cfg;
    cfg.current_lsb_nA = 100000;
    return cfg;
}

Session::Sample make_sample(uint32_t seq) {
    Session::Sample s;
    s.seq = seq;
    s.host_timestamp_us = static_cast<uint64_t>(seq) * 1000;
    s.device_timestamp_us = static_cast<uint64_t>(seq) * 1000;
    s.device_timestamp_unix_us = static_cast<uint64_t>(seq) * 1000;
    s.power_raw = seq * 10;
    return s;
}

void write_chunk(const fs::path& dir, const std::string& name,
                 const std::vector<uint32_t>& seqs) {
    nlohmann::json chunk;
    chunk["meta"] = {{"schema_version", "1.0"}};
    chunk["samples"] = nlohmann::json::array();
    for (uint32_t seq : seqs) {
        nlohmann::json s;
        s["seq"] = seq;
        s["timestamp_us"] = seq * 1000;
        s["host_timestamp_us"] = seq * 1000;
        s["device_timestamp_us"] = seq * 1000;
        s["device_timestamp_unix_us"] = seq * 1000;
        s["flags"] = 0;
        s["engineering"]["power_w"] = seq * 0.01;
        s["engineering"]["vbus_v"] = 0.0;
        s["engineering"]["current_a"] = 0.0;
        s["engineering"]["temp_c"] = 0.0;
        s["engineering"]["energy_j"] = 0.0;
        s["engineering"]["charge_c"] = 0.0;
        chunk["samples"].push_back(s);
    }
    std::ofstream f(dir / name);
    f << chunk;
}

std::vector<uint32_t> collect_seqs(const nlohmann::json& samples) {
    std::vector<uint32_t> seqs;
    for (const auto& s : samples) {
        seqs.push_back(s.at("seq").get<uint32_t>());
    }
    return seqs;
}

}  // namespace

class SessionMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "session_merge_test";
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
        session_.set_config(make_config());
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    fs::path chunk_dir() const { return temp_dir_ / "chunks"; }

    void init_chunk_dir() {
        fs::create_directories(chunk_dir());
        session_.set_flush_dir(chunk_dir().string());
    }

    fs::path temp_dir_;
    Session session_;
};

// ── save_merged ───────────────────────────────────────────────────────────────

TEST_F(SessionMergeTest, SaveMerged_NoFlushDir_SavesInMemoryOnly) {
    session_.add_sample(make_sample(1));
    session_.add_sample(make_sample(2));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    ASSERT_TRUE(f.is_open());
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{1, 2}));
}

TEST_F(SessionMergeTest, SaveMerged_EmptyChunksDir_SavesInMemoryOnly) {
    init_chunk_dir();
    session_.add_sample(make_sample(10));
    session_.add_sample(make_sample(11));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    ASSERT_TRUE(f.is_open());
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{10, 11}));
}

TEST_F(SessionMergeTest, SaveMerged_SingleChunk_NoTail_ContainsChunkSamples) {
    init_chunk_dir();
    write_chunk(chunk_dir(), "chunk_20240101_120000.json", {1, 2, 3});

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{1, 2, 3}));
}

TEST_F(SessionMergeTest, SaveMerged_MultipleChunksAndTail_ContainsAll) {
    init_chunk_dir();
    write_chunk(chunk_dir(), "chunk_20240101_120000.json", {1, 2});
    write_chunk(chunk_dir(), "chunk_20240101_120001.json", {3, 4});
    session_.add_sample(make_sample(5));
    session_.add_sample(make_sample(6));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{1, 2, 3, 4, 5, 6}));
}

TEST_F(SessionMergeTest, SaveMerged_ChunksLoadedInFilenameOrder) {
    init_chunk_dir();
    // Write b before a to confirm sorting is by name, not creation order.
    write_chunk(chunk_dir(), "chunk_b.json", {10, 11});
    write_chunk(chunk_dir(), "chunk_a.json", {1, 2});

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{1, 2, 10, 11}));
}

TEST_F(SessionMergeTest, SaveMerged_CorruptChunkSkipped_OtherChunksPresent) {
    init_chunk_dir();
    write_chunk(chunk_dir(), "chunk_a.json", {1, 2});
    {
        std::ofstream corrupt(chunk_dir() / "chunk_b.json");
        corrupt << "not valid json {{{{";
    }
    write_chunk(chunk_dir(), "chunk_c.json", {3, 4});

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["samples"]), (std::vector<uint32_t>{1, 2, 3, 4}));
}

// Verify the bug scenario: flush_to_chunks() clears in-memory samples,
// but save_merged() must still reconstruct the full dataset from chunk files.
TEST_F(SessionMergeTest, SaveMerged_ViaFlushToChunks_FullDatasetRecovered) {
    init_chunk_dir();

    session_.add_sample(make_sample(1));
    session_.add_sample(make_sample(2));
    EXPECT_TRUE(session_.flush_to_chunks());
    EXPECT_EQ(session_.sample_count(), 0u);  // cleared after flush

    session_.add_sample(make_sample(3));     // tail written after last flush

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    // All 3 samples must appear; old code would produce only {3}
    EXPECT_EQ(root["samples"].size(), 3u);
    auto seqs = collect_seqs(root["samples"]);
    EXPECT_NE(std::find(seqs.begin(), seqs.end(), 1u), seqs.end());
    EXPECT_NE(std::find(seqs.begin(), seqs.end(), 2u), seqs.end());
    EXPECT_NE(std::find(seqs.begin(), seqs.end(), 3u), seqs.end());
}

TEST_F(SessionMergeTest, SaveMerged_TwoChunkFilesAndTail_AllFiveSamplesPresent) {
    init_chunk_dir();

    // Write chunk files directly to avoid same-second filename collision in flush_to_chunks().
    write_chunk(chunk_dir(), "chunk_20240101_120000.json", {1, 2});
    write_chunk(chunk_dir(), "chunk_20240101_120001.json", {3, 4});
    session_.add_sample(make_sample(5));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    // All 5 samples must appear; old code would produce only {5}
    EXPECT_EQ(root["samples"].size(), 5u);
}

// ── save_bundle_merged ────────────────────────────────────────────────────────

TEST_F(SessionMergeTest, SaveBundleMerged_NoFlushDir_BundleContainsInMemoryPicoSamples) {
    session_.add_sample(make_sample(1));
    session_.add_sample(make_sample(2));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_bundle_merged(out.string());

    std::ifstream f(out);
    ASSERT_TRUE(f.is_open());
    auto root = nlohmann::json::parse(f);
    ASSERT_TRUE(root["sources"].contains("pico"));
    EXPECT_EQ(collect_seqs(root["sources"]["pico"]["samples"]),
              (std::vector<uint32_t>{1, 2}));
}

TEST_F(SessionMergeTest, SaveBundleMerged_ChunksAndTail_AllPicoSamplesPresent) {
    init_chunk_dir();
    write_chunk(chunk_dir(), "chunk_20240101_120000.json", {1, 2});
    write_chunk(chunk_dir(), "chunk_20240101_120001.json", {3, 4});
    session_.add_sample(make_sample(5));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_bundle_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    EXPECT_EQ(collect_seqs(root["sources"]["pico"]["samples"]),
              (std::vector<uint32_t>{1, 2, 3, 4, 5}));
}

TEST_F(SessionMergeTest, SaveBundleMerged_SummaryCountsAllSamples) {
    init_chunk_dir();
    write_chunk(chunk_dir(), "chunk_a.json", {1, 2});
    write_chunk(chunk_dir(), "chunk_b.json", {3, 4});

    const fs::path out = temp_dir_ / "out.json";
    session_.save_bundle_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    ASSERT_TRUE(root["sources"]["pico"].contains("summary"));
    // Summary must cover all 4 samples from both chunks, not just the empty tail
    EXPECT_EQ(root["sources"]["pico"]["summary"]["sample_count"].get<size_t>(), 4u);
}

TEST_F(SessionMergeTest, SaveBundleMerged_ViaFlushToChunks_FullDatasetRecovered) {
    init_chunk_dir();

    session_.add_sample(make_sample(1));
    session_.add_sample(make_sample(2));
    EXPECT_TRUE(session_.flush_to_chunks());
    EXPECT_EQ(session_.sample_count(), 0u);

    session_.add_sample(make_sample(3));

    const fs::path out = temp_dir_ / "out.json";
    session_.save_bundle_merged(out.string());

    std::ifstream f(out);
    auto root = nlohmann::json::parse(f);
    // Old code produced only the tail {3}; correct code produces all 3
    EXPECT_EQ(root["sources"]["pico"]["samples"].size(), 3u);
    EXPECT_EQ(root["sources"]["pico"]["summary"]["sample_count"].get<size_t>(), 3u);
}
