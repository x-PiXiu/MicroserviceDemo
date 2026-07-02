/**
 * @file query_engine.cpp
 * @brief 查询引擎实现
 * @details 实现多维度服务查询和推荐功能
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "query_engine.h"
#include <algorithm>
#include <random>

namespace core_services {
namespace service_registry {

QueryEngine::QueryEngine(
    std::shared_ptr<RedisStorage> storage,
    std::shared_ptr<IndexManager> index_manager,
    const RegistryConfig& config
) : storage_(storage)
  , index_manager_(index_manager)
  , config_(config)
  , rng_(std::random_device{}()) {
}

// ==================== 查询操作 ====================

std::vector<ServiceInfo> QueryEngine::query(const ServiceFilter& filter) {
    std::vector<ServiceInfo> results;

    try {
        // 1. 确定查询范围（使用索引优化）
        std::vector<ServiceInfo> candidates;

        if (filter.service_name) {
            candidates = storage_->getServiceInstances(*filter.service_name);
        } else if (filter.game_type) {
            // 从游戏类型索引获取实例键
            auto instance_ids = index_manager_->getInstancesByGameType(*filter.game_type);
            // 解析并获取服务信息
            for (const auto& id : instance_ids) {
                // id 格式: service_name:host:port
                size_t first_colon = id.find(':');
                size_t last_colon = id.rfind(':');
                if (first_colon != std::string::npos && last_colon != std::string::npos) {
                    std::string service_name = id.substr(0, first_colon);
                    std::string host = id.substr(first_colon + 1, last_colon - first_colon - 1);
                    int port = std::stoi(id.substr(last_colon + 1));

                    auto service = storage_->get(service_name, host, port);
                    if (service) {
                        candidates.push_back(*service);
                    }
                }
            }
        } else if (filter.region) {
            // 从区域索引获取实例键
            auto instance_ids = index_manager_->getInstancesByRegion(*filter.region);
            for (const auto& id : instance_ids) {
                size_t first_colon = id.find(':');
                size_t last_colon = id.rfind(':');
                if (first_colon != std::string::npos && last_colon != std::string::npos) {
                    std::string service_name = id.substr(0, first_colon);
                    std::string host = id.substr(first_colon + 1, last_colon - first_colon - 1);
                    int port = std::stoi(id.substr(last_colon + 1));

                    auto service = storage_->get(service_name, host, port);
                    if (service) {
                        candidates.push_back(*service);
                    }
                }
            }
        } else {
            // 全量扫描
            candidates = storage_->getAllInstances();
        }

        // 2. 应用过滤条件
        results = applyFilter(candidates, filter);

        // 3. 排序
        sortServices(results, filter.sort_by);

        // 4. 分页
        results = applyPagination(results, filter.limit, filter.offset);

    } catch (const std::exception& e) {
        LOG_ERROR("Query failed: " + std::string(e.what()));
    }

    return results;
}

std::vector<ServiceInfo> QueryEngine::queryByName(
    const std::string& service_name,
    bool healthy_only
) {
    ServiceFilter filter;
    filter.service_name = service_name;
    filter.healthy_only = healthy_only;
    return query(filter);
}

std::vector<ServiceInfo> QueryEngine::queryByGameType(
    const std::string& game_type,
    bool healthy_only
) {
    ServiceFilter filter;
    filter.game_type = game_type;
    filter.healthy_only = healthy_only;
    return query(filter);
}

std::vector<ServiceInfo> QueryEngine::queryByRegion(
    const std::string& region,
    bool healthy_only
) {
    ServiceFilter filter;
    filter.region = region;
    filter.healthy_only = healthy_only;
    return query(filter);
}

// ==================== 推荐操作 ====================

ServiceRecommendation QueryEngine::recommend(
    const ServiceFilter& filter,
    RecommendStrategy strategy
) {
    ServiceRecommendation recommendation;

    // 获取候选服务
    auto candidates = query(filter);

    if (candidates.empty()) {
        recommendation.reason = "No available services found";
        return recommendation;
    }

    // 根据策略选择推荐
    switch (strategy) {
        case RecommendStrategy::LEAST_LOAD: {
            // 计算负载分数并排序
            std::vector<std::pair<ServiceInfo, double>> scored;
            for (const auto& service : candidates) {
                double score = calculateLoadScore(service);
                scored.emplace_back(service, score);
            }

            // 按负载分数升序排序（分数越低负载越低）
            std::sort(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) {
                    return a.second < b.second;
                });

            recommendation.recommended = scored[0].first;
            recommendation.load_score = scored[0].second;
            recommendation.reason = "Least load instance";

            // 保存候选列表
            int max_candidates = std::min(
                config_.max_recommendation_candidates,
                static_cast<int>(scored.size())
            );
            for (int i = 0; i < max_candidates; ++i) {
                recommendation.candidates.push_back(scored[i]);
            }
            break;
        }

        case RecommendStrategy::RANDOM: {
            std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
            size_t idx = dist(rng_);

            recommendation.recommended = candidates[idx];
            recommendation.load_score = calculateLoadScore(candidates[idx]);
            recommendation.reason = "Random selection";

            // 保存前 N 个候选
            int max_candidates = std::min(
                config_.max_recommendation_candidates,
                static_cast<int>(candidates.size())
            );
            for (int i = 0; i < max_candidates; ++i) {
                recommendation.candidates.emplace_back(
                    candidates[i],
                    calculateLoadScore(candidates[i])
                );
            }
            break;
        }

        case RecommendStrategy::ROUND_ROBIN: {
            // 简单实现：选择第一个
            recommendation.recommended = candidates[0];
            recommendation.load_score = calculateLoadScore(candidates[0]);
            recommendation.reason = "Round-robin selection";

            int max_candidates = std::min(
                config_.max_recommendation_candidates,
                static_cast<int>(candidates.size())
            );
            for (int i = 0; i < max_candidates; ++i) {
                recommendation.candidates.emplace_back(
                    candidates[i],
                    calculateLoadScore(candidates[i])
                );
            }
            break;
        }

        case RecommendStrategy::WEIGHTED: {
            // 按权重加权随机选择
            int total_weight = 0;
            for (const auto& service : candidates) {
                total_weight += service.weight;
            }

            if (total_weight > 0) {
                std::uniform_int_distribution<int> dist(1, total_weight);
                int random_weight = dist(rng_);

                int accumulated = 0;
                for (const auto& service : candidates) {
                    accumulated += service.weight;
                    if (accumulated >= random_weight) {
                        recommendation.recommended = service;
                        recommendation.load_score = calculateLoadScore(service);
                        break;
                    }
                }
            } else {
                recommendation.recommended = candidates[0];
                recommendation.load_score = calculateLoadScore(candidates[0]);
            }

            recommendation.reason = "Weighted random selection";

            int max_candidates = std::min(
                config_.max_recommendation_candidates,
                static_cast<int>(candidates.size())
            );
            for (int i = 0; i < max_candidates; ++i) {
                recommendation.candidates.emplace_back(
                    candidates[i],
                    calculateLoadScore(candidates[i])
                );
            }
            break;
        }
    }

    return recommendation;
}

ServiceRecommendation QueryEngine::recommendForGame(
    const std::string& game_type,
    RecommendStrategy strategy
) {
    ServiceFilter filter;
    filter.game_type = game_type;
    filter.healthy_only = true;
    return recommend(filter, strategy);
}

// ==================== 排序操作 ====================

void QueryEngine::sortServices(
    std::vector<ServiceInfo>& services,
    ServiceFilter::SortBy sort_by
) {
    switch (sort_by) {
        case ServiceFilter::SortBy::HEALTH_SCORE:
            std::sort(services.begin(), services.end(),
                [](const ServiceInfo& a, const ServiceInfo& b) {
                    return a.health_score > b.health_score;
                });
            break;

        case ServiceFilter::SortBy::LOAD_ASC:
            std::sort(services.begin(), services.end(),
                [this](const ServiceInfo& a, const ServiceInfo& b) {
                    return calculateLoadScore(a) < calculateLoadScore(b);
                });
            break;

        case ServiceFilter::SortBy::LOAD_DESC:
            std::sort(services.begin(), services.end(),
                [this](const ServiceInfo& a, const ServiceInfo& b) {
                    return calculateLoadScore(a) > calculateLoadScore(b);
                });
            break;

        case ServiceFilter::SortBy::RANDOM:
            std::shuffle(services.begin(), services.end(), rng_);
            break;

        case ServiceFilter::SortBy::REGISTER_TIME:
            std::sort(services.begin(), services.end(),
                [](const ServiceInfo& a, const ServiceInfo& b) {
                    return a.register_time < b.register_time;
                });
            break;
    }
}

// ==================== 私有方法 ====================

double QueryEngine::calculateLoadScore(const ServiceInfo& service) {
    double score = 0.0;

    // 1. CPU 使用率 (0-40%)
    auto cpu = service.getMetadataDouble("cpu_usage");
    if (cpu) {
        score += (*cpu / 100.0) * 0.4;
    }

    // 2. 内存使用率 (0-30%)
    auto memory = service.getMetadataDouble("memory_usage");
    if (memory) {
        score += (*memory / 100.0) * 0.3;
    }

    // 3. 玩家负载 (0-20%)
    auto current_players = service.getMetadataInt("current_players");
    auto max_players = service.getMetadataInt("max_players");
    if (current_players && max_players && *max_players > 0) {
        score += (static_cast<double>(*current_players) / *max_players) * 0.2;
    }

    // 4. 健康分数反向 (0-10%)
    score += (1.0 - service.health_score) * 0.1;

    return score;
}

std::vector<ServiceInfo> QueryEngine::applyFilter(
    const std::vector<ServiceInfo>& services,
    const ServiceFilter& filter
) {
    std::vector<ServiceInfo> results;

    for (const auto& service : services) {
        if (filter.matches(service)) {
            results.push_back(service);
        }
    }

    return results;
}

std::vector<ServiceInfo> QueryEngine::applyPagination(
    const std::vector<ServiceInfo>& services,
    int limit,
    int offset
) {
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = static_cast<int>(services.size());

    if (offset >= static_cast<int>(services.size())) {
        return {};
    }

    int end = std::min(offset + limit, static_cast<int>(services.size()));
    return std::vector<ServiceInfo>(
        services.begin() + offset,
        services.begin() + end
    );
}

} // namespace service_registry
} // namespace core_services
