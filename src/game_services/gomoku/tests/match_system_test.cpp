/**
 * @file match_system_test.cpp
 * @brief 自动匹配系统单元测试
 * @details 测试匹配池、匹配策略、匹配管理器的核心功能
 * @author Game Server Team
 * @date 2025-01-23
 */

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "../include/match_pool.h"
#include "../include/match_strategy.h"
#include "../include/match_making_manager.h"

using namespace game_services::gomoku;

// ==================== MatchRequest 测试 ====================

class MatchRequestTest : public ::testing::Test {
protected:
    void SetUp() override {
        request1 = MatchRequest("user1", "Player1", 1500, MatchMode::RANKED);
        request1.game_mode = GameMode::FREESTYLE;

        request2 = MatchRequest("user2", "Player2", 1480, MatchMode::RANKED);
        request2.game_mode = GameMode::FREESTYLE;
    }

    MatchRequest request1;
    MatchRequest request2;
};

TEST_F(MatchRequestTest, ConstructorInitializesCorrectly) {
    EXPECT_EQ(request1.user_id, "user1");
    EXPECT_EQ(request1.username, "Player1");
    EXPECT_EQ(request1.rating, 1500);
    EXPECT_EQ(request1.mode, MatchMode::RANKED);
    EXPECT_EQ(request1.status, MatchStatus::WAITING);
    EXPECT_FALSE(request1.request_id.empty());
}

TEST_F(MatchRequestTest, RatingInRangeInitially) {
    // 初始评分范围为 ±100
    EXPECT_TRUE(request1.isRatingInRange(1400));
    EXPECT_TRUE(request1.isRatingInRange(1500));
    EXPECT_TRUE(request1.isRatingInRange(1600));
    EXPECT_FALSE(request1.isRatingInRange(1399));
    EXPECT_FALSE(request1.isRatingInRange(1601));
}

TEST_F(MatchRequestTest, RatingDifferenceCalculation) {
    int diff = request1.getRatingDifference(request2);
    EXPECT_EQ(diff, 20);  // |1500 - 1480| = 20
}

TEST_F(MatchRequestTest, MatchQualityEvaluation) {
    // 测试匹配质量评估
    MatchRequest high_rating("user3", "Player3", 1550, MatchMode::RANKED);
    MatchRequest low_rating("user4", "Player4", 1300, MatchMode::RANKED);

    // 评分差 50 = EXCELLENT
    MatchRequest rating_1450("user5", "Player5", 1450, MatchMode::RANKED);
    EXPECT_EQ(request1.evaluateMatchQuality(rating_1450), MatchQuality::EXCELLENT);

    // 评分差 100 = GOOD
    MatchRequest rating_1400("user6", "Player6", 1400, MatchMode::RANKED);
    EXPECT_EQ(request1.evaluateMatchQuality(rating_1400), MatchQuality::GOOD);

    // 评分差 200 = FAIR
    EXPECT_EQ(request1.evaluateMatchQuality(low_rating), MatchQuality::FAIR);
}

TEST_F(MatchRequestTest, ExpandRatingRange) {
    int initial_min = request1.min_rating;
    int initial_max = request1.max_rating;

    request1.expandRatingRange(50);

    EXPECT_EQ(request1.min_rating, initial_min - 50);
    EXPECT_EQ(request1.max_rating, initial_max + 50);
    EXPECT_EQ(request1.rating_range_expansion, 50);
}

TEST_F(MatchRequestTest, ToJsonProducesValidJson) {
    nlohmann::json json = request1.toJson();

    EXPECT_EQ(json["user_id"], "user1");
    EXPECT_EQ(json["username"], "Player1");
    EXPECT_EQ(json["rating"], 1500);
    EXPECT_EQ(json["mode"], "ranked");
}

// ==================== RatingMatchStrategy 测试 ====================

class RatingMatchStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy = std::make_unique<RatingMatchStrategy>();
        config.initial_rating_range = 100;
        config.rating_expansion_per_interval = 25;
        config.max_rating_expansion = 500;
        config.excellent_quality_threshold = 50;
        config.good_quality_threshold = 100;
        config.fair_quality_threshold = 200;
        config.rating_weight = 0.6;
        config.wait_time_weight = 0.3;
        config.tier_weight = 0.1;
    }

    std::unique_ptr<RatingMatchStrategy> strategy;
    MatchPoolConfig config;
};

TEST_F(RatingMatchStrategyTest, CanMatchValidPair) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1520, MatchMode::RANKED);

    EXPECT_TRUE(strategy->canMatch(request1, request2, config));
}

TEST_F(RatingMatchStrategyTest, CannotMatchSameUser) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user1", "Player1", 1500, MatchMode::RANKED);

    EXPECT_FALSE(strategy->canMatch(request1, request2, config));
}

TEST_F(RatingMatchStrategyTest, CannotMatchDifferentModes) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1520, MatchMode::CASUAL);

    EXPECT_FALSE(strategy->canMatch(request1, request2, config));
}

TEST_F(RatingMatchStrategyTest, FindBestMatchReturnsClosest) {
    MatchRequest seeker("seeker", "Seeker", 1500, MatchMode::RANKED);

    std::vector<MatchRequest> candidates = {
        MatchRequest("far", "Far", 1700, MatchMode::RANKED),    // 差200
        MatchRequest("close", "Close", 1510, MatchMode::RANKED), // 差10
        MatchRequest("mid", "Mid", 1550, MatchMode::RANKED)      // 差50
    };

    auto best = strategy->findBestMatch(seeker, candidates, config);

    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->user_id, "close");  // 应该选择评分最接近的
}

TEST_F(RatingMatchStrategyTest, CalculateMatchScore) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1520, MatchMode::RANKED);

    MatchScore score = strategy->calculateMatchScore(request1, request2, config);

    EXPECT_GE(score.total_score, 0.0);
    EXPECT_LE(score.total_score, 1.0);
    EXPECT_GT(score.rating_score, 0.0);  // 评分差小，应该有较高评分
}

TEST_F(RatingMatchStrategyTest, NoMatchWhenOutOfRange) {
    MatchRequest seeker("seeker", "Seeker", 1500, MatchMode::RANKED);
    seeker.min_rating = 1450;
    seeker.max_rating = 1550;

    std::vector<MatchRequest> candidates = {
        MatchRequest("far", "Far", 1800, MatchMode::RANKED)  // 超出范围
    };

    auto best = strategy->findBestMatch(seeker, candidates, config);

    EXPECT_FALSE(best.has_value());
}

// ==================== CasualMatchStrategy 测试 ====================

class CasualMatchStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        strategy = std::make_unique<CasualMatchStrategy>();
    }

    std::unique_ptr<CasualMatchStrategy> strategy;
    MatchPoolConfig config;
};

TEST_F(CasualMatchStrategyTest, PrioritizesWaitTime) {
    MatchRequest seeker("seeker", "Seeker", 1500, MatchMode::CASUAL);
    seeker.wait_seconds = 10;

    std::vector<MatchRequest> candidates = {
        MatchRequest("new", "New", 1500, MatchMode::CASUAL),   // 等待时间短
        MatchRequest("old", "Old", 1500, MatchMode::CASUAL)    // 等待时间长
    };
    candidates[0].wait_seconds = 5;
    candidates[1].wait_seconds = 60;  // 等待更久

    auto best = strategy->findBestMatch(seeker, candidates, config);

    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->user_id, "old");  // 应该选择等待时间最长的
}

// ==================== MatchMakingManager 测试 ====================

class MatchMakingManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        MatchPoolConfig config;
        config.max_pool_size = 100;
        config.max_wait_seconds = 300;
        config.match_interval_ms = 50;  // 快速匹配用于测试

        manager = std::make_unique<MatchMakingManager>(config);
    }

    void TearDown() override {
        if (manager) {
            manager->stop();
        }
    }

    std::unique_ptr<MatchMakingManager> manager;
};

TEST_F(MatchMakingManagerTest, AddRequestSuccess) {
    MatchRequest request("user1", "Player1", 1500, MatchMode::RANKED);

    EXPECT_TRUE(manager->addRequest(request));
    EXPECT_TRUE(manager->isInPool("user1"));
}

TEST_F(MatchMakingManagerTest, CannotAddDuplicateRequest) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user1", "Player1", 1500, MatchMode::RANKED);

    EXPECT_TRUE(manager->addRequest(request1));
    EXPECT_FALSE(manager->addRequest(request2));  // 重复添加应该失败
}

TEST_F(MatchMakingManagerTest, CancelRequestSuccess) {
    MatchRequest request("user1", "Player1", 1500, MatchMode::RANKED);
    manager->addRequest(request);

    EXPECT_TRUE(manager->cancelRequest("user1"));
    EXPECT_FALSE(manager->isInPool("user1"));
}

TEST_F(MatchMakingManagerTest, CancelNonExistentFails) {
    EXPECT_FALSE(manager->cancelRequest("nonexistent"));
}

TEST_F(MatchMakingManagerTest, GetWaitTime) {
    MatchRequest request("user1", "Player1", 1500, MatchMode::RANKED);
    manager->addRequest(request);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int wait_time = manager->getWaitTime("user1");
    EXPECT_GE(wait_time, 0);
}

TEST_F(MatchMakingManagerTest, GetPoolStatus) {
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1400, MatchMode::CASUAL);

    manager->addRequest(request1);
    manager->addRequest(request2);

    MatchPoolStatus status = manager->getPoolStatus();

    EXPECT_EQ(status.total_requests, 2);
    EXPECT_EQ(status.waiting_requests, 2);
    EXPECT_EQ(status.ranked_count, 1);
    EXPECT_EQ(status.casual_count, 1);
}

TEST_F(MatchMakingManagerTest, MatchingCallbackInvoked) {
    std::vector<MatchResult> results;
    manager->setMatchResultCallback([&results](const MatchResult& result) {
        results.push_back(result);
    });

    ASSERT_TRUE(manager->start());

    // 添加两个匹配的请求
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1510, MatchMode::RANKED);

    manager->addRequest(request1);
    manager->addRequest(request2);

    // 等待匹配完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(results.size(), 1);
    EXPECT_TRUE(results[0].isValid());
    EXPECT_EQ(results[0].players.size(), 2);
}

TEST_F(MatchMakingManagerTest, Statistics) {
    ASSERT_TRUE(manager->start());

    // 添加两个请求并等待匹配
    MatchRequest request1("user1", "Player1", 1500, MatchMode::RANKED);
    MatchRequest request2("user2", "Player2", 1510, MatchMode::RANKED);

    manager->addRequest(request1);
    manager->addRequest(request2);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nlohmann::json stats = manager->getStatistics();

    EXPECT_TRUE(stats.contains("total_matches"));
    EXPECT_TRUE(stats.contains("pool_status"));
}

// ==================== MatchResult 测试 ====================

class MatchResultTest : public ::testing::Test {
protected:
    void SetUp() override {
        MatchRequest player1("user1", "Player1", 1500, MatchMode::RANKED);
        MatchRequest player2("user2", "Player2", 1520, MatchMode::RANKED);

        result.match_id = "match_123";
        result.players = {player1, player2};
        result.quality = MatchQuality::EXCELLENT;
        result.rating_difference = 20;
        result.total_wait_seconds = 30;
    }

    MatchResult result;
};

TEST_F(MatchResultTest, IsValidWithTwoPlayers) {
    EXPECT_TRUE(result.isValid());
}

TEST_F(MatchResultTest, IsInvalidWithEmptyPlayers) {
    MatchResult empty_result;
    EXPECT_FALSE(empty_result.isValid());
}

TEST_F(MatchResultTest, GetOpponentId) {
    EXPECT_EQ(result.getOpponentId("user1"), "user2");
    EXPECT_EQ(result.getOpponentId("user2"), "user1");
}

TEST_F(MatchResultTest, ToJsonProducesValidJson) {
    nlohmann::json json = result.toJson();

    EXPECT_EQ(json["match_id"], "match_123");
    EXPECT_EQ(json["quality"], "excellent");
    EXPECT_EQ(json["rating_difference"], 20);
    EXPECT_TRUE(json.contains("players"));
    EXPECT_EQ(json["players"].size(), 2);
}

// ==================== MatchPoolConfig 测试 ====================

TEST(MatchPoolConfigTest, ToJsonContainsAllFields) {
    MatchPoolConfig config;
    config.max_pool_size = 500;
    config.max_wait_seconds = 600;
    config.initial_rating_range = 150;

    nlohmann::json json = config.toJson();

    EXPECT_EQ(json["max_pool_size"], 500);
    EXPECT_EQ(json["max_wait_seconds"], 600);
    EXPECT_EQ(json["initial_rating_range"], 150);
    EXPECT_TRUE(json.contains("quality_thresholds"));
    EXPECT_TRUE(json.contains("weights"));
}

// ==================== 集成测试 ====================

class MatchSystemIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        MatchPoolConfig config;
        config.match_interval_ms = 20;  // 快速匹配

        manager = std::make_unique<MatchMakingManager>(config);
        ASSERT_TRUE(manager->start());
    }

    void TearDown() override {
        manager->stop();
    }

    std::unique_ptr<MatchMakingManager> manager;
};

TEST_F(MatchSystemIntegrationTest, FullMatchCycle) {
    std::vector<MatchResult> match_results;
    manager->setMatchResultCallback([&match_results](const MatchResult& result) {
        match_results.push_back(result);
    });

    // 模拟两个玩家请求匹配
    MatchRequest player1("player1", "Alice", 1500, MatchMode::RANKED);
    MatchRequest player2("player2", "Bob", 1510, MatchMode::RANKED);

    EXPECT_TRUE(manager->addRequest(player1));
    EXPECT_TRUE(manager->addRequest(player2));

    // 等待匹配
    for (int i = 0; i < 50 && match_results.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_EQ(match_results.size(), 1);

    const auto& result = match_results[0];
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.players.size(), 2);
    EXPECT_LE(result.rating_difference, 100);  // 应该是优质匹配

    // 验证两个玩家都从池中移除
    EXPECT_FALSE(manager->isInPool("player1"));
    EXPECT_FALSE(manager->isInPool("player2"));
}

TEST_F(MatchSystemIntegrationTest, MatchPoolSize) {
    // 添加多个玩家
    for (int i = 0; i < 10; ++i) {
        MatchRequest request(
            "player" + std::to_string(i),
            "Player" + std::to_string(i),
            1200 + i * 50,  // 评分范围: 1200-1650
            MatchMode::RANKED
        );
        manager->addRequest(request);
    }

    EXPECT_EQ(manager->getPoolSize(), 10);

    // 等待匹配完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 池大小应该减少（玩家被匹配）
    EXPECT_LT(manager->getPoolSize(), 10);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
