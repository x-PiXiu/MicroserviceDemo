/**
 * @file distributed_leaderboard.h
 * @brief 分布式排行榜服务 - 支持跨服排行榜聚合和同步
 * @author AI Assistant
 * @date 2026-02-23
 * @version 1.0.0
 *
 * 职责：
 * - 跨服排行榜数据聚合
 * - 分片排行榜管理
 * - 数据同步和一致性
 * - 本地缓存优化
 */

#pragma once

#include "leaderboard_manager.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

/**
 * 服务器节点信息
 */
struct ServerNode {
    std::string node_id;                    // 节点ID
    std::string region;                     // 区域
    std::string endpoint;                   // API端点
    int priority = 0;                       // 优先级（用于选举主节点）
    bool is_active = false;                 // 是否活跃
    std::chrono::system_clock::time_point last_heartbeat;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServerNode, node_id, region, endpoint, priority, is_active)
};

/**
 * 分片配置
 */
struct ShardConfig {
    int shard_id;                           // 分片ID
    std::string shard_key;                  // 分片键（如用户ID前缀）
    std::string owner_node_id;              // 所属节点ID

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ShardConfig, shard_id, shard_key, owner_node_id)
};

/**
 * 分布式排行榜配置
 */
struct DistributedLeaderboardConfig {
    bool enabled = false;                   // 是否启用分布式模式

    // 本节点信息
    std::string local_node_id;              // 本节点ID
    std::string local_region;               // 本地区域

    // 集群配置
    int sync_interval_seconds = 60;         // 同步间隔
    int heartbeat_interval_seconds = 10;    // 心跳间隔
    int node_timeout_seconds = 30;          // 节点超时

    // 聚合配置
    int max_entries_per_shard = 1000;       // 每分片最大条目
    int global_max_entries = 10000;         // 全局最大条目
    bool enable_local_cache = true;         // 启用本地缓存
    int local_cache_ttl_seconds = 300;      // 本地缓存TTL

    // 一致性配置
    bool strong_consistency = false;        // 强一致性（影响性能）
    int consistency_timeout_ms = 5000;      // 一致性超时

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DistributedLeaderboardConfig,
        enabled, local_node_id, local_region,
        sync_interval_seconds, heartbeat_interval_seconds, node_timeout_seconds,
        max_entries_per_shard, global_max_entries,
        enable_local_cache, local_cache_ttl_seconds,
        strong_consistency, consistency_timeout_ms)
};

/**
 * 跨服排行榜条目
 */
struct DistributedLeaderboardEntry {
    int global_rank;                        // 全局排名
    std::string user_id;
    std::string display_name;
    std::string region;                     // 所属区域
    int64_t score;
    std::string tier_name;
    float win_rate;
    int games_played;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DistributedLeaderboardEntry,
        global_rank, user_id, display_name, region,
        score, tier_name, win_rate, games_played)
};

/**
 * 分布式排行榜结果
 */
struct DistributedLeaderboardResult {
    std::string type;                       // 排行榜类型
    std::string aggregated_from;            // 聚合来源（节点列表）
    std::chrono::system_clock::time_point aggregated_at;

    std::vector<DistributedLeaderboardEntry> entries;

    // 用户排名
    std::optional<DistributedLeaderboardEntry> my_rank;

    // 元数据
    int total_regions = 0;
    int total_players = 0;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        j["total_regions"] = total_regions;
        j["total_players"] = total_players;

        nlohmann::json entries_json = nlohmann::json::array();
        for (const auto& e : entries) {
            entries_json.push_back(nlohmann::json{
                {"global_rank", e.global_rank},
                {"user_id", e.user_id},
                {"display_name", e.display_name},
                {"region", e.region},
                {"score", e.score},
                {"tier_name", e.tier_name},
                {"win_rate", e.win_rate},
                {"games_played", e.games_played}
            });
        }
        j["entries"] = entries_json;

        if (my_rank.has_value()) {
            j["my_rank"] = nlohmann::json{
                {"global_rank", my_rank->global_rank},
                {"user_id", my_rank->user_id},
                {"score", my_rank->score}
            };
        }

        return j;
    }
};

/**
 * 同步数据包
 */
struct SyncDataPacket {
    std::string source_node_id;
    std::string target_node_id;
    LeaderboardType type;
    std::chrono::system_clock::time_point timestamp;
    std::vector<LeaderboardEntryDetail> entries;
    bool is_full_sync = false;              // 全量同步标志

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["source_node_id"] = source_node_id;
        j["target_node_id"] = target_node_id;
        j["type"] = static_cast<int>(type);

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()).count();
        j["timestamp"] = ms;
        j["is_full_sync"] = is_full_sync;
        j["entries"] = entries;
        return j;
    }
};

/**
 * 远程节点通信接口
 */
using RemoteFetchFunc = std::function<std::optional<DistributedLeaderboardResult>(
    const std::string& node_id,
    LeaderboardType type,
    int limit)>;

using SyncSendFunc = std::function<bool(
    const std::string& target_node_id,
    const SyncDataPacket& packet)>;

/**
 * 分布式排行榜服务
 */
class DistributedLeaderboardService {
public:
    /**
     * 构造函数
     */
    DistributedLeaderboardService(
        std::shared_ptr<LeaderboardManager> local_manager,
        const DistributedLeaderboardConfig& config = DistributedLeaderboardConfig());

    ~DistributedLeaderboardService();

    // ========== 生命周期 ==========

    /**
     * 启动服务
     */
    bool start();

    /**
     * 停止服务
     */
    void stop();

    /**
     * 是否运行中
     */
    bool isRunning() const { return running_.load(); }

    // ========== 配置 ==========

    /**
     * 更新配置
     */
    void updateConfig(const DistributedLeaderboardConfig& config);

    /**
     * 获取配置
     */
    const DistributedLeaderboardConfig& getConfig() const { return config_; }

    // ========== 节点管理 ==========

    /**
     * 注册远程节点
     */
    void registerNode(const ServerNode& node);

    /**
     * 注销节点
     */
    void unregisterNode(const std::string& node_id);

    /**
     * 更新节点心跳
     */
    void updateNodeHeartbeat(const std::string& node_id);

    /**
     * 获取所有活跃节点
     */
    std::vector<ServerNode> getActiveNodes() const;

    /**
     * 获取本节点信息
     */
    ServerNode getLocalNode() const;

    // ========== 通信接口设置 ==========

    /**
     * 设置远程获取函数
     */
    void setRemoteFetchFunc(RemoteFetchFunc func) {
        remote_fetch_func_ = std::move(func);
    }

    /**
     * 设置同步发送函数
     */
    void setSyncSendFunc(SyncSendFunc func) {
        sync_send_func_ = std::move(func);
    }

    // ========== 排行榜查询 ==========

    /**
     * 获取全局排行榜（聚合所有节点）
     */
    DistributedLeaderboardResult getGlobalLeaderboard(
        LeaderboardType type,
        int limit = 100,
        const std::string& user_id = "");

    /**
     * 获取区域排行榜
     */
    DistributedLeaderboardResult getRegionalLeaderboard(
        const std::string& region,
        LeaderboardType type,
        int limit = 100,
        const std::string& user_id = "");

    /**
     * 获取用户全局排名
     */
    int getUserGlobalRank(
        const std::string& user_id,
        LeaderboardType type);

    // ========== 数据同步 ==========

    /**
     * 处理接收到的同步数据
     */
    void handleSyncData(const SyncDataPacket& packet);

    /**
     * 触发主动同步
     */
    void triggerSync(LeaderboardType type, bool full_sync = false);

    /**
     * 广播本地更新
     */
    void broadcastLocalUpdate(
        LeaderboardType type,
        const LeaderboardEntryDetail& updated_entry);

    // ========== 统计和监控 ==========

    /**
     * 获取集群状态
     */
    nlohmann::json getClusterStatus() const;

    /**
     * 获取同步统计
     */
    nlohmann::json getSyncStatistics() const;

private:
    std::shared_ptr<LeaderboardManager> local_manager_;
    DistributedLeaderboardConfig config_;

    // 节点管理
    mutable std::mutex nodes_mutex_;
    std::unordered_map<std::string, ServerNode> nodes_;

    // 远程通信
    RemoteFetchFunc remote_fetch_func_;
    SyncSendFunc sync_send_func_;

    // 本地缓存
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, DistributedLeaderboardResult> global_cache_;
    std::unordered_map<std::string, DistributedLeaderboardResult> cache_;  // 简化缓存
    std::unordered_map<std::string, std::chrono::system_clock::time_point> cache_times_;

    // 运行状态
    std::atomic<bool> running_{false};
    std::thread sync_thread_;
    std::thread heartbeat_thread_;

    // 统计
    mutable std::mutex stats_mutex_;
    int64_t total_syncs_{0};
    int64_t successful_syncs_{0};
    int64_t cache_hits_{0};
    int64_t remote_fetches_{0};

    // ========== 内部方法 ==========

    /**
     * 同步线程
     */
    void syncLoop();

    /**
     * 心跳线程
     */
    void heartbeatLoop();

    /**
     * 聚合多节点排行榜
     */
    DistributedLeaderboardResult aggregateLeaderboards(
        LeaderboardType type,
        int limit,
        const std::string& user_id);

    /**
     * 从远程节点获取排行榜
     */
    std::optional<DistributedLeaderboardResult> fetchFromRemote(
        const std::string& node_id,
        LeaderboardType type,
        int limit);

    /**
     * 缓存操作
     */
    std::optional<DistributedLeaderboardResult> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, const DistributedLeaderboardResult& result);
    void invalidateCache(const std::string& pattern = "");

    /**
     * 检查节点是否活跃
     */
    bool isNodeActive(const ServerNode& node) const;

    /**
     * 生成缓存键
     */
    static std::string buildCacheKey(LeaderboardType type, int limit);
};

} // namespace game_service
} // namespace core_services
