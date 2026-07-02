/**
 * @file distributed_leaderboard.cpp
 * @brief 分布式排行榜服务实现
 * @author AI Assistant
 * @date 2026-02-23
 */

#include "distributed_leaderboard.h"
#include <common/logger/logger.h>
#include <algorithm>
#include <sstream>

namespace core_services {
namespace game_service {

DistributedLeaderboardService::DistributedLeaderboardService(
    std::shared_ptr<LeaderboardManager> local_manager,
    const DistributedLeaderboardConfig& config)
    : local_manager_(std::move(local_manager))
    , config_(config) {
}

DistributedLeaderboardService::~DistributedLeaderboardService() {
    stop();
}

bool DistributedLeaderboardService::start() {
    if (running_.exchange(true)) {
        return true; // Already running
    }

    // 启动同步线程
    sync_thread_ = std::thread(&DistributedLeaderboardService::syncLoop, this);

    // 启动心跳线程
    heartbeat_thread_ = std::thread(&DistributedLeaderboardService::heartbeatLoop, this);

    LOG_INFO("DistributedLeaderboardService started");
    return true;
}

void DistributedLeaderboardService::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }

    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    LOG_INFO("DistributedLeaderboardService stopped");
}

void DistributedLeaderboardService::updateConfig(const DistributedLeaderboardConfig& config) {
    config_ = config;
}

void DistributedLeaderboardService::registerNode(const ServerNode& node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_[node.node_id] = node;
    LOG_INFO("Registered node");
}

void DistributedLeaderboardService::unregisterNode(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_.erase(node_id);
    LOG_INFO("Unregistered node");
}

void DistributedLeaderboardService::updateNodeHeartbeat(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_heartbeat = std::chrono::system_clock::now();
        it->second.is_active = true;
    }
}

std::vector<ServerNode> DistributedLeaderboardService::getActiveNodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<ServerNode> active;
    for (const auto& [id, node] : nodes_) {
        if (isNodeActive(node)) {
            active.push_back(node);
        }
    }
    return active;
}

ServerNode DistributedLeaderboardService::getLocalNode() const {
    ServerNode node;
    node.node_id = config_.local_node_id;
    node.region = config_.local_region;
    node.is_active = running_.load();
    return node;
}

DistributedLeaderboardResult DistributedLeaderboardService::getGlobalLeaderboard(
    LeaderboardType type,
    int limit,
    const std::string& user_id) {

    if (!config_.enabled) {
        // 非分布式模式，返回本地数据
        DistributedLeaderboardResult result;
        result.type = LeaderboardManager::typeToString(type);
        result.total_regions = 1;
        result.aggregated_at = std::chrono::system_clock::now();

        auto local_result = local_manager_->getLeaderboard(type, LeaderboardScope::GLOBAL, "", 0, limit, user_id);
        for (const auto& entry : local_result.entries) {
            DistributedLeaderboardEntry dist_entry;
            dist_entry.global_rank = entry.rank;
            dist_entry.user_id = entry.user_id;
            dist_entry.display_name = entry.display_name;
            dist_entry.region = config_.local_region;
            dist_entry.score = entry.score;
            dist_entry.tier_name = entry.tier_name;
            dist_entry.win_rate = entry.win_rate;
            dist_entry.games_played = entry.games_played;
            result.entries.push_back(dist_entry);
        }

        result.total_players = local_result.total_players;

        if (local_result.my_rank.has_value()) {
            DistributedLeaderboardEntry my_entry;
            my_entry.global_rank = local_result.my_rank->rank;
            my_entry.user_id = local_result.my_rank->user_id;
            my_entry.score = local_result.my_rank->score;
            result.my_rank = my_entry;
        }

        return result;
    }

    // 检查缓存
    std::string cache_key = buildCacheKey(type, limit);
    auto cached = getFromCache(cache_key);
    if (cached.has_value()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        cache_hits_++;
        return cached.value();
    }

    // 聚合多节点数据
    auto result = aggregateLeaderboards(type, limit, user_id);

    // 缓存结果
    if (config_.enable_local_cache) {
        saveToCache(cache_key, result);
    }

    return result;
}

DistributedLeaderboardResult DistributedLeaderboardService::getRegionalLeaderboard(
    const std::string& region,
    LeaderboardType type,
    int limit,
    const std::string& user_id) {

    // 如果是本地区域，直接返回本地数据
    if (region == config_.local_region) {
        return getGlobalLeaderboard(type, limit, user_id);
    }

    // 否则从远程节点获取
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    for (const auto& [node_id, node] : nodes_) {
        if (node.region == region && isNodeActive(node)) {
            auto result = fetchFromRemote(node_id, type, limit);
            if (result.has_value()) {
                return result.value();
            }
        }
    }

    // 没有找到该区域的活跃节点
    DistributedLeaderboardResult empty_result;
    empty_result.type = LeaderboardManager::typeToString(type);
    return empty_result;
}

int DistributedLeaderboardService::getUserGlobalRank(
    const std::string& user_id,
    LeaderboardType type) {

    auto result = getGlobalLeaderboard(type, 1, user_id);
    if (result.my_rank.has_value()) {
        return result.my_rank->global_rank;
    }
    return 0; // 未上榜
}

void DistributedLeaderboardService::handleSyncData(const SyncDataPacket& packet) {
    if (!config_.enabled) {
        return;
    }

    LOG_DEBUG("Received sync data from node");

    // 更新本地缓存
    // 实际实现中需要合并数据
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_syncs_++;
        successful_syncs_++;
    }
}

void DistributedLeaderboardService::triggerSync(LeaderboardType type, bool full_sync) {
    if (!config_.enabled || !sync_send_func_) {
        return;
    }

    auto active_nodes = getActiveNodes();

    // 获取本地排行榜数据
    auto local_result = local_manager_->getLeaderboard(type, LeaderboardScope::GLOBAL, "", 0, config_.max_entries_per_shard);

    SyncDataPacket packet;
    packet.source_node_id = config_.local_node_id;
    packet.type = type;
    packet.timestamp = std::chrono::system_clock::now();
    packet.is_full_sync = full_sync;
    packet.entries = local_result.entries;

    for (const auto& node : active_nodes) {
        if (node.node_id != config_.local_node_id) {
            packet.target_node_id = node.node_id;
            sync_send_func_(node.node_id, packet);
        }
    }
}

void DistributedLeaderboardService::broadcastLocalUpdate(
    LeaderboardType type,
    const LeaderboardEntryDetail& updated_entry) {

    // 使相关缓存失效
    invalidateCache("leaderboard_" + LeaderboardManager::typeToString(type));

    // 触发增量同步
    SyncDataPacket packet;
    packet.source_node_id = config_.local_node_id;
    packet.type = type;
    packet.timestamp = std::chrono::system_clock::now();
    packet.is_full_sync = false;
    packet.entries.push_back(updated_entry);

    auto active_nodes = getActiveNodes();
    for (const auto& node : active_nodes) {
        if (node.node_id != config_.local_node_id && sync_send_func_) {
            packet.target_node_id = node.node_id;
            sync_send_func_(node.node_id, packet);
        }
    }
}

nlohmann::json DistributedLeaderboardService::getClusterStatus() const {
    auto active_nodes = getActiveNodes();

    nlohmann::json nodes_json = nlohmann::json::array();
    for (const auto& node : active_nodes) {
        nodes_json.push_back(nlohmann::json{
            {"node_id", node.node_id},
            {"region", node.region},
            {"is_active", node.is_active}
        });
    }

    return {
        {"local_node_id", config_.local_node_id},
        {"local_region", config_.local_region},
        {"enabled", config_.enabled},
        {"running", running_.load()},
        {"total_nodes", nodes_.size()},
        {"active_nodes", active_nodes.size()},
        {"nodes", nodes_json}
    };
}

nlohmann::json DistributedLeaderboardService::getSyncStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return {
        {"total_syncs", total_syncs_},
        {"successful_syncs", successful_syncs_},
        {"cache_hits", cache_hits_},
        {"remote_fetches", remote_fetches_}
    };
}

void DistributedLeaderboardService::syncLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(config_.sync_interval_seconds));

        if (!running_) break;

        // 执行定期同步
        triggerSync(LeaderboardType::RATING, false);
        triggerSync(LeaderboardType::WIN_STREAK, false);
    }
}

void DistributedLeaderboardService::heartbeatLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(config_.heartbeat_interval_seconds));

        if (!running_) break;

        // 检查节点状态
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto now = std::chrono::system_clock::now();
        for (auto& [id, node] : nodes_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - node.last_heartbeat).count();
            node.is_active = (elapsed < config_.node_timeout_seconds);
        }
    }
}

DistributedLeaderboardResult DistributedLeaderboardService::aggregateLeaderboards(
    LeaderboardType type,
    int limit,
    const std::string& user_id) {

    DistributedLeaderboardResult result;
    result.type = LeaderboardManager::typeToString(type);
    result.aggregated_at = std::chrono::system_clock::now();

    // 收集所有节点的数据
    std::vector<DistributedLeaderboardEntry> all_entries;

    // 本地数据
    auto local_result = local_manager_->getLeaderboard(type, LeaderboardScope::GLOBAL, "", 0, config_.max_entries_per_shard);
    for (const auto& entry : local_result.entries) {
        DistributedLeaderboardEntry dist_entry;
        dist_entry.global_rank = 0; // 待排序后分配
        dist_entry.user_id = entry.user_id;
        dist_entry.display_name = entry.display_name;
        dist_entry.region = config_.local_region;
        dist_entry.score = entry.score;
        dist_entry.tier_name = entry.tier_name;
        dist_entry.win_rate = entry.win_rate;
        dist_entry.games_played = entry.games_played;
        all_entries.push_back(dist_entry);
    }
    result.total_regions = 1;

    // 远程数据
    auto active_nodes = getActiveNodes();
    for (const auto& node : active_nodes) {
        if (node.node_id != config_.local_node_id) {
            auto remote_result = fetchFromRemote(node.node_id, type, config_.max_entries_per_shard);
            if (remote_result.has_value()) {
                for (const auto& entry : remote_result->entries) {
                    all_entries.push_back(entry);
                }
                result.total_regions++;
            }
        }
    }

    // 按分数排序
    std::sort(all_entries.begin(), all_entries.end(),
              [](const DistributedLeaderboardEntry& a, const DistributedLeaderboardEntry& b) {
                  return a.score > b.score;
              });

    // 分配全局排名并取前N个
    int rank = 1;
    for (size_t i = 0; i < all_entries.size() && rank <= limit; ++i) {
        if (i > 0 && all_entries[i].score < all_entries[i-1].score) {
            rank = i + 1;
        }
        all_entries[i].global_rank = rank;
        result.entries.push_back(all_entries[i]);

        // 检查是否是查询的用户
        if (!user_id.empty() && all_entries[i].user_id == user_id) {
            result.my_rank = all_entries[i];
        }
    }

    result.total_players = all_entries.size();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        remote_fetches_ += (active_nodes.size() > 1) ? active_nodes.size() - 1 : 0;
    }

    return result;
}

std::optional<DistributedLeaderboardResult> DistributedLeaderboardService::fetchFromRemote(
    const std::string& node_id,
    LeaderboardType type,
    int limit) {

    if (!remote_fetch_func_) {
        return std::nullopt;
    }

    return remote_fetch_func_(node_id, type, limit);
}

std::optional<DistributedLeaderboardResult> DistributedLeaderboardService::getFromCache(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_times_.find(key);
    if (it == cache_times_.end()) {
        return std::nullopt;
    }

    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second).count();

    if (elapsed > config_.local_cache_ttl_seconds) {
        cache_.erase(key);
        cache_times_.erase(key);
        return std::nullopt;
    }

    auto cache_it = cache_.find(key);
    if (cache_it != cache_.end()) {
        return cache_it->second;
    }

    return std::nullopt;
}

void DistributedLeaderboardService::saveToCache(const std::string& key, const DistributedLeaderboardResult& result) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[key] = result;
    cache_times_[key] = std::chrono::system_clock::now();
}

void DistributedLeaderboardService::invalidateCache(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    if (pattern.empty()) {
        cache_.clear();
        cache_times_.clear();
        return;
    }

    // 简单的前缀匹配
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (it->first.find(pattern) == 0) {
            cache_times_.erase(it->first);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

bool DistributedLeaderboardService::isNodeActive(const ServerNode& node) const {
    if (!node.is_active) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - node.last_heartbeat).count();

    return elapsed < config_.node_timeout_seconds;
}

std::string DistributedLeaderboardService::buildCacheKey(LeaderboardType type, int limit) {
    return "leaderboard_" + LeaderboardManager::typeToString(type) + "_" + std::to_string(limit);
}

} // namespace game_service
} // namespace core_services
