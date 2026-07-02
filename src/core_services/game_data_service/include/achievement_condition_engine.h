/**
 * @file achievement_condition_engine.h
 * @brief 成就条件检查引擎 - 支持复杂条件表达式的解析和评估
 * @author AI Assistant
 * @date 2026-02-23
 * @version 1.0.0
 *
 * 职责：
 * - 解析复杂的条件表达式
 * - 支持多种比较运算符
 * - 支持逻辑运算符（AND、OR）
 * - 支持嵌套条件
 * - 支持自定义条件函数
 */

#pragma once

#include "game_models.h"
#include "achievement_manager.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

/**
 * 条件值类型
 */
using ConditionValue = std::variant<int, float, bool, std::string>;

/**
 * 条件评估结果
 */
struct ConditionResult {
    bool satisfied = false;
    std::string message;                // 评估消息（用于调试）
    int current_value = 0;              // 当前值
    int target_value = 0;               // 目标值
    float progress_percent = 0.0f;      // 进度百分比

    nlohmann::json toJson() const {
        return {
            {"satisfied", satisfied},
            {"message", message},
            {"current_value", current_value},
            {"target_value", target_value},
            {"progress_percent", progress_percent}
        };
    }
};

/**
 * 比较运算符
 */
enum class ComparisonOperator {
    EQUAL,              // ==
    NOT_EQUAL,          // !=
    GREATER_THAN,       // >
    GREATER_EQUAL,      // >=
    LESS_THAN,          // <
    LESS_EQUAL,         // <=
    IN_RANGE,           // in [a, b]
    NOT_IN_RANGE,       // not in [a, b]
    CONTAINS,           // contains (for strings/lists)
    STARTS_WITH,        // starts with
    ENDS_WITH           // ends with
};

/**
 * 逻辑运算符
 */
enum class LogicalOperator {
    AND,
    OR,
    NOT
};

/**
 * 条件节点 - AST节点
 */
struct ConditionNode {
    virtual ~ConditionNode() = default;
    virtual ConditionResult evaluate(const AchievementContext& context) const = 0;
    virtual nlohmann::json toJson() const = 0;
};

/**
 * 比较条件节点
 */
struct ComparisonCondition : public ConditionNode {
    std::string field;                  // 字段名
    ComparisonOperator op;              // 比较运算符
    ConditionValue value;               // 比较值
    ConditionValue value2;              // 第二个值（用于范围比较）

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 逻辑条件节点
 */
struct LogicalCondition : public ConditionNode {
    LogicalOperator op;
    std::vector<std::unique_ptr<ConditionNode>> children;

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 进度条件节点 - 检查进度是否达到目标
 */
struct ProgressCondition : public ConditionNode {
    std::string field;                  // 字段名
    int target_value;                   // 目标值
    bool check_exact = false;           // 是否检查精确值

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 时间条件节点 - 检查时间相关条件
 */
struct TimeCondition : public ConditionNode {
    std::string type;                   // "daily_first_win", "weekly_games", "season_rank"
    int value = 0;                      // 阈值

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 游戏特定条件节点
 */
struct GameSpecificCondition : public ConditionNode {
    std::string game_type;              // 游戏类型
    std::string condition_type;         // "perfect_game", "comeback", "quick_win"
    std::unordered_map<std::string, ConditionValue> params;

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 自定义条件函数类型
 */
using CustomConditionFunc = std::function<ConditionResult(const AchievementContext&)>;

/**
 * 自定义条件节点
 */
struct CustomCondition : public ConditionNode {
    std::string name;
    CustomConditionFunc func;

    ConditionResult evaluate(const AchievementContext& context) const override;
    nlohmann::json toJson() const override;
};

/**
 * 成就条件定义
 */
struct AchievementConditionDef {
    std::string achievement_id;
    std::string name;
    std::string description;
    std::unique_ptr<ConditionNode> condition;
    std::string raw_expression;         // 原始表达式（用于显示）

    nlohmann::json toJson() const {
        return {
            {"achievement_id", achievement_id},
            {"name", name},
            {"description", description},
            {"raw_expression", raw_expression},
            {"condition", condition ? condition->toJson() : nlohmann::json()}
        };
    }
};

/**
 * 条件引擎配置
 */
struct ConditionEngineConfig {
    bool enable_caching = true;
    int cache_ttl_seconds = 300;
    bool strict_mode = false;           // 严格模式：未知字段返回错误
    bool enable_debug_output = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ConditionEngineConfig,
        enable_caching, cache_ttl_seconds, strict_mode, enable_debug_output)
};

/**
 * 成就条件检查引擎
 *
 * 支持的条件语法：
 * 1. 简单比较: "total_wins >= 10"
 * 2. 逻辑组合: "total_wins >= 10 AND current_win_streak >= 3"
 * 3. 范围检查: "rating IN [1500, 2000]"
 * 4. 进度检查: "total_games PROGRESS 100"
 * 5. 游戏特定: "GAME:gomoku:perfect_game"
 * 6. 时间条件: "TIME:daily_first_win"
 * 7. 自定义函数: "CUSTOM:comeback_king"
 */
class AchievementConditionEngine {
public:
    /**
     * 构造函数
     */
    AchievementConditionEngine(const ConditionEngineConfig& config = ConditionEngineConfig());

    ~AchievementConditionEngine() = default;

    // ========== 配置 ==========

    /**
     * 更新配置
     */
    void updateConfig(const ConditionEngineConfig& config);

    /**
     * 获取配置
     */
    const ConditionEngineConfig& getConfig() const { return config_; }

    // ========== 条件解析 ==========

    /**
     * 解析条件表达式
     * @param expression 条件表达式
     * @return 条件AST根节点（失败返回nullptr）
     */
    std::unique_ptr<ConditionNode> parseCondition(const std::string& expression);

    /**
     * 解析并创建成就条件定义
     */
    std::optional<AchievementConditionDef> parseAchievementCondition(
        const std::string& achievement_id,
        const std::string& name,
        const std::string& description,
        const std::string& expression);

    // ========== 条件评估 ==========

    /**
     * 评估条件
     * @param condition 条件节点
     * @param context 成就上下文
     * @return 评估结果
     */
    ConditionResult evaluate(
        const ConditionNode& condition,
        const AchievementContext& context) const;

    /**
     * 评估条件表达式（解析+评估）
     */
    ConditionResult evaluateExpression(
        const std::string& expression,
        const AchievementContext& context);

    /**
     * 批量评估多个成就条件
     */
    std::vector<std::pair<std::string, ConditionResult>> evaluateBatch(
        const std::vector<std::pair<std::string, std::string>>& achievement_conditions,
        const AchievementContext& context);

    // ========== 自定义条件注册 ==========

    /**
     * 注册自定义条件函数
     * @param name 条件名称
     * @param func 条件函数
     */
    void registerCustomCondition(
        const std::string& name,
        CustomConditionFunc func);

    /**
     * 注销自定义条件
     */
    void unregisterCustomCondition(const std::string& name);

    /**
     * 检查自定义条件是否存在
     */
    bool hasCustomCondition(const std::string& name) const;

    // ========== 字段值提取 ==========

    /**
     * 从上下文提取字段值
     */
    std::optional<ConditionValue> extractFieldValue(
        const std::string& field,
        const AchievementContext& context) const;

    /**
     * 注册自定义字段提取器
     */
    void registerFieldExtractor(
        const std::string& field,
        std::function<ConditionValue(const AchievementContext&)> extractor);

    // ========== 内置条件 ==========

    /**
     * 获取内置条件列表
     */
    std::vector<std::string> getBuiltinConditions() const;

    /**
     * 初始化内置条件
     */
    void initializeBuiltinConditions();

    // ========== 调试和分析 ==========

    /**
     * 验证条件表达式语法
     */
    std::optional<std::string> validateExpression(const std::string& expression);

    /**
     * 获取条件中引用的所有字段
     */
    std::vector<std::string> extractReferencedFields(const std::string& expression);

    /**
     * 获取引擎统计信息
     */
    nlohmann::json getStatistics() const;

private:
    ConditionEngineConfig config_;

    // 自定义条件注册表
    std::unordered_map<std::string, CustomConditionFunc> custom_conditions_;

    // 自定义字段提取器
    std::unordered_map<std::string, std::function<ConditionValue(const AchievementContext&)>> field_extractors_;

    // 条件缓存
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::unique_ptr<ConditionNode>> condition_cache_;

    // 统计
    mutable std::mutex stats_mutex_;
    mutable int64_t total_evaluations_{0};
    mutable int64_t cache_hits_{0};
    mutable int64_t parse_errors_{0};

    // ========== 解析辅助方法 ==========

    /**
     * 词法分析 - 分词
     */
    std::vector<std::string> tokenize(const std::string& expression);

    /**
     * 解析逻辑表达式
     */
    std::unique_ptr<ConditionNode> parseLogicalExpression(
        std::vector<std::string>& tokens);

    /**
     * 解析比较表达式
     */
    std::unique_ptr<ConditionNode> parseComparisonExpression(
        std::vector<std::string>& tokens);

    /**
     * 解析原子表达式
     */
    std::unique_ptr<ConditionNode> parseAtom(std::vector<std::string>& tokens);

    /**
     * 解析运算符
     */
    std::optional<ComparisonOperator> parseComparisonOperator(const std::string& token);

    /**
     * 解析逻辑运算符
     */
    std::optional<LogicalOperator> parseLogicalOperator(const std::string& token);

    /**
     * 解析值
     */
    std::optional<ConditionValue> parseValue(const std::string& token);

    /**
     * 跳过空白
     */
    static std::string trim(const std::string& str);
};

// ========== 内置条件函数 ==========

namespace builtin_conditions {

/**
 * 完美对局：在30步内获胜
 */
ConditionResult perfectGame(const AchievementContext& context);

/**
 * 绝地反击：在评分劣势200分以上时获胜
 */
ConditionResult comebackKing(const AchievementContext& context);

/**
 * 速战速决：在5分钟内获胜
 */
ConditionResult quickWin(const AchievementContext& context);

/**
 * 持久战：游戏时长超过30分钟
 */
ConditionResult endurance(const AchievementContext& context);

/**
 * 连胜大师：连续获胜10场
 */
ConditionResult streakMaster(const AchievementContext& context);

/**
 * 全能选手：在所有游戏模式中都获得胜利
 */
ConditionResult versatile(AchievementContext& context);

} // namespace builtin_conditions

} // namespace game_service
} // namespace core_services
