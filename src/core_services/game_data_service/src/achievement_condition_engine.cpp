/**
 * @file achievement_condition_engine.cpp
 * @brief 成就条件检查引擎实现
 * @author AI Assistant
 * @date 2026-02-23
 */

#include "achievement_condition_engine.h"
#include <common/logger/logger.h>
#include <algorithm>
#include <sstream>
#include <regex>
#include <stdexcept>
#include <unordered_set>
#include <variant>

namespace core_services {
namespace game_service {

// ========== ComparisonCondition ==========

ConditionResult ComparisonCondition::evaluate(const AchievementContext& context) const {
    ConditionResult result;

    // 获取字段值
    auto field_val_opt = AchievementConditionEngine(ConditionEngineConfig()).extractFieldValue(field, context);
    if (!field_val_opt.has_value()) {
        result.satisfied = false;
        result.message = "Unknown field: " + field;
        return result;
    }

    int field_val = std::holds_alternative<int>(*field_val_opt)
        ? std::get<int>(*field_val_opt)
        : std::get<float>(*field_val_opt);

    int compare_val = std::holds_alternative<int>(value)
        ? std::get<int>(value)
        : static_cast<int>(std::get<float>(value));

    result.current_value = field_val;
    result.target_value = compare_val;

    switch (op) {
        case ComparisonOperator::EQUAL:
            result.satisfied = (field_val == compare_val);
            break;
        case ComparisonOperator::NOT_EQUAL:
            result.satisfied = (field_val != compare_val);
            break;
        case ComparisonOperator::GREATER_THAN:
            result.satisfied = (field_val > compare_val);
            break;
        case ComparisonOperator::GREATER_EQUAL:
            result.satisfied = (field_val >= compare_val);
            break;
        case ComparisonOperator::LESS_THAN:
            result.satisfied = (field_val < compare_val);
            break;
        case ComparisonOperator::LESS_EQUAL:
            result.satisfied = (field_val <= compare_val);
            break;
        case ComparisonOperator::IN_RANGE: {
            int val2 = std::holds_alternative<int>(value2)
                ? std::get<int>(value2)
                : static_cast<int>(std::get<float>(value2));
            result.satisfied = (field_val >= compare_val && field_val <= val2);
            break;
        }
        default:
            result.satisfied = false;
            result.message = "Unsupported operator";
            return result;
    }

    result.progress_percent = compare_val > 0
        ? std::min(100.0f, (float)field_val / compare_val * 100.0f)
        : 100.0f;

    result.message = result.satisfied ? "Condition satisfied" : "Condition not met";
    return result;
}

nlohmann::json ComparisonCondition::toJson() const {
    std::string op_str;
    switch (op) {
        case ComparisonOperator::EQUAL: op_str = "=="; break;
        case ComparisonOperator::NOT_EQUAL: op_str = "!="; break;
        case ComparisonOperator::GREATER_THAN: op_str = ">"; break;
        case ComparisonOperator::GREATER_EQUAL: op_str = ">="; break;
        case ComparisonOperator::LESS_THAN: op_str = "<"; break;
        case ComparisonOperator::LESS_EQUAL: op_str = "<="; break;
        default: op_str = "unknown"; break;
    }

    return {
        {"type", "comparison"},
        {"field", field},
        {"operator", op_str},
        {"value", std::holds_alternative<int>(value) ? std::get<int>(value) : std::get<float>(value)}
    };
}

// ========== LogicalCondition ==========

ConditionResult LogicalCondition::evaluate(const AchievementContext& context) const {
    ConditionResult result;

    switch (op) {
        case LogicalOperator::AND: {
            bool all_satisfied = true;
            std::string combined_message;
            float min_progress = 100.0f;

            for (const auto& child : children) {
                auto child_result = child->evaluate(context);
                if (!child_result.satisfied) {
                    all_satisfied = false;
                    combined_message = child_result.message;
                }
                min_progress = std::min(min_progress, child_result.progress_percent);
            }

            result.satisfied = all_satisfied;
            result.progress_percent = min_progress;
            result.message = all_satisfied ? "All conditions satisfied" : combined_message;
            break;
        }

        case LogicalOperator::OR: {
            bool any_satisfied = false;
            float max_progress = 0.0f;

            for (const auto& child : children) {
                auto child_result = child->evaluate(context);
                if (child_result.satisfied) {
                    any_satisfied = true;
                    result.message = child_result.message;
                    result.current_value = child_result.current_value;
                    result.target_value = child_result.target_value;
                }
                max_progress = std::max(max_progress, child_result.progress_percent);
            }

            result.satisfied = any_satisfied;
            result.progress_percent = max_progress;
            if (!any_satisfied) {
                result.message = "No condition satisfied";
            }
            break;
        }

        case LogicalOperator::NOT: {
            if (children.size() != 1) {
                result.satisfied = false;
                result.message = "NOT operator requires exactly one child";
            } else {
                auto child_result = children[0]->evaluate(context);
                result.satisfied = !child_result.satisfied;
                result.progress_percent = 100.0f - child_result.progress_percent;
                result.message = result.satisfied ? "NOT condition satisfied" : "NOT condition not met";
            }
            break;
        }
    }

    return result;
}

nlohmann::json LogicalCondition::toJson() const {
    std::string op_str = (op == LogicalOperator::AND) ? "AND" :
                         (op == LogicalOperator::OR) ? "OR" : "NOT";

    nlohmann::json children_json = nlohmann::json::array();
    for (const auto& child : children) {
        children_json.push_back(child->toJson());
    }

    return {
        {"type", "logical"},
        {"operator", op_str},
        {"children", children_json}
    };
}

// ========== ProgressCondition ==========

ConditionResult ProgressCondition::evaluate(const AchievementContext& context) const {
    ConditionResult result;

    auto field_val_opt = AchievementConditionEngine(ConditionEngineConfig()).extractFieldValue(field, context);
    if (!field_val_opt.has_value()) {
        result.satisfied = false;
        result.message = "Unknown field: " + field;
        return result;
    }

    result.current_value = std::holds_alternative<int>(*field_val_opt)
        ? std::get<int>(*field_val_opt)
        : static_cast<int>(std::get<float>(*field_val_opt));
    result.target_value = target_value;

    if (check_exact) {
        result.satisfied = (result.current_value == target_value);
    } else {
        result.satisfied = (result.current_value >= target_value);
    }

    result.progress_percent = target_value > 0
        ? std::min(100.0f, (float)result.current_value / target_value * 100.0f)
        : 100.0f;

    result.message = result.satisfied ? "Progress target reached" : "Progress target not reached";
    return result;
}

nlohmann::json ProgressCondition::toJson() const {
    return {
        {"type", "progress"},
        {"field", field},
        {"target_value", target_value},
        {"check_exact", check_exact}
    };
}

// ========== TimeCondition ==========

ConditionResult TimeCondition::evaluate(const AchievementContext& context) const {
    ConditionResult result;

    if (type == "daily_first_win") {
        // 检查是否当日首胜
        if (context.custom_data.contains("is_first_win_today")) {
            result.satisfied = context.custom_data["is_first_win_today"].get<bool>();
        } else {
            result.satisfied = (context.last_game_result == GameResult::WIN);
        }
        result.message = result.satisfied ? "Daily first win achieved" : "Not daily first win";
    } else if (type == "weekly_games") {
        // 检查本周游戏数
        int weekly_games = context.custom_data.value("weekly_games", 0);
        result.current_value = weekly_games;
        result.target_value = value;
        result.satisfied = (weekly_games >= value);
        result.progress_percent = value > 0 ? (float)weekly_games / value * 100.0f : 100.0f;
    } else if (type == "season_rank") {
        // 检查赛季排名
        int season_rank = context.custom_data.value("season_rank", 999);
        result.current_value = season_rank;
        result.target_value = value;
        result.satisfied = (season_rank > 0 && season_rank <= value);
        result.progress_percent = season_rank > 0 ? 100.0f : 0.0f;
    } else {
        result.satisfied = false;
        result.message = "Unknown time condition type: " + type;
    }

    return result;
}

nlohmann::json TimeCondition::toJson() const {
    return {
        {"type", "time"},
        {"condition_type", type},
        {"value", value}
    };
}

// ========== GameSpecificCondition ==========

ConditionResult GameSpecificCondition::evaluate(const AchievementContext& context) const {
    ConditionResult result;

    // 检查游戏类型
    if (!game_type.empty() && context.game_type != game_type) {
        result.satisfied = false;
        result.message = "Game type mismatch";
        return result;
    }

    if (condition_type == "perfect_game") {
        // 完美对局：30步内获胜
        result = builtin_conditions::perfectGame(context);
    } else if (condition_type == "comeback") {
        // 绝地反击
        result = builtin_conditions::comebackKing(context);
    } else if (condition_type == "quick_win") {
        // 速战速决
        result = builtin_conditions::quickWin(context);
    } else if (condition_type == "endurance") {
        // 持久战
        result = builtin_conditions::endurance(context);
    } else {
        result.satisfied = false;
        result.message = "Unknown game-specific condition: " + condition_type;
    }

    return result;
}

nlohmann::json GameSpecificCondition::toJson() const {
    nlohmann::json j;
    j["type"] = "game_specific";
    j["game_type"] = game_type;
    j["condition_type"] = condition_type;

    // 手动转换 params
    nlohmann::json params_json = nlohmann::json::object();
    for (const auto& [key, val] : params) {
        std::visit([&params_json, &key](auto&& arg) {
            params_json[key] = arg;
        }, val);
    }
    j["params"] = params_json;

    return j;
}

// ========== CustomCondition ==========

ConditionResult CustomCondition::evaluate(const AchievementContext& context) const {
    if (func) {
        return func(context);
    }
    ConditionResult result;
    result.satisfied = false;
    result.message = "Custom condition function not set: " + name;
    return result;
}

nlohmann::json CustomCondition::toJson() const {
    return {
        {"type", "custom"},
        {"name", name}
    };
}

// ========== AchievementConditionEngine ==========

AchievementConditionEngine::AchievementConditionEngine(const ConditionEngineConfig& config)
    : config_(config) {
    initializeBuiltinConditions();
}

void AchievementConditionEngine::updateConfig(const ConditionEngineConfig& config) {
    config_ = config;
}

std::unique_ptr<ConditionNode> AchievementConditionEngine::parseCondition(const std::string& expression) {
    std::string trimmed = trim(expression);
    if (trimmed.empty()) {
        return nullptr;
    }

    // 检查缓存
    if (config_.enable_caching) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = condition_cache_.find(trimmed);
        if (it != condition_cache_.end()) {
            // 返回缓存的副本
            // 注意：这里简化处理，实际应该深拷贝
            return parseCondition(trimmed); // 重新解析
        }
    }

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) {
        return nullptr;
    }

    try {
        auto node = parseLogicalExpression(tokens);

        // 缓存结果
        if (config_.enable_caching && node) {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            condition_cache_[trimmed] = parseCondition(expression); // 递归缓存
        }

        return node;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse condition expression");
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            parse_errors_++;
        }
        return nullptr;
    }
}

std::optional<AchievementConditionDef> AchievementConditionEngine::parseAchievementCondition(
    const std::string& achievement_id,
    const std::string& name,
    const std::string& description,
    const std::string& expression) {

    auto condition = parseCondition(expression);
    if (!condition) {
        return std::nullopt;
    }

    AchievementConditionDef def;
    def.achievement_id = achievement_id;
    def.name = name;
    def.description = description;
    def.raw_expression = expression;
    def.condition = std::move(condition);

    return def;
}

ConditionResult AchievementConditionEngine::evaluate(
    const ConditionNode& condition,
    const AchievementContext& context) const {

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_evaluations_++;
    }

    return condition.evaluate(context);
}

ConditionResult AchievementConditionEngine::evaluateExpression(
    const std::string& expression,
    const AchievementContext& context) {

    auto condition = parseCondition(expression);
    if (!condition) {
        ConditionResult result;
        result.satisfied = false;
        result.message = "Failed to parse condition: " + expression;
        return result;
    }

    return evaluate(*condition, context);
}

std::vector<std::pair<std::string, ConditionResult>> AchievementConditionEngine::evaluateBatch(
    const std::vector<std::pair<std::string, std::string>>& achievement_conditions,
    const AchievementContext& context) {

    std::vector<std::pair<std::string, ConditionResult>> results;
    results.reserve(achievement_conditions.size());

    for (const auto& [id, expr] : achievement_conditions) {
        results.emplace_back(id, evaluateExpression(expr, context));
    }

    return results;
}

void AchievementConditionEngine::registerCustomCondition(
    const std::string& name,
    CustomConditionFunc func) {

    custom_conditions_[name] = std::move(func);
    LOG_DEBUG("Registered custom condition");
}

void AchievementConditionEngine::unregisterCustomCondition(const std::string& name) {
    custom_conditions_.erase(name);
}

bool AchievementConditionEngine::hasCustomCondition(const std::string& name) const {
    return custom_conditions_.find(name) != custom_conditions_.end();
}

std::optional<ConditionValue> AchievementConditionEngine::extractFieldValue(
    const std::string& field,
    const AchievementContext& context) const {

    // 检查自定义提取器
    auto it = field_extractors_.find(field);
    if (it != field_extractors_.end()) {
        return it->second(context);
    }

    // 内置字段
    if (field == "total_games") return context.total_games;
    if (field == "total_wins") return context.total_wins;
    if (field == "total_losses") return context.total_losses;
    if (field == "total_draws") return context.total_draws;
    if (field == "current_win_streak") return context.current_win_streak;
    if (field == "best_win_streak") return context.best_win_streak;
    if (field == "current_rating" || field == "rating") return context.current_rating;
    if (field == "peak_rating") return context.peak_rating;
    if (field == "level") return context.level;
    if (field == "last_game_moves") return context.last_game_moves;
    if (field == "last_game_duration") return context.last_game_duration;
    if (field == "opponent_rating") return context.opponent_rating;
    if (field == "was_underdog") return context.was_underdog;

    // 尝试从custom_data获取
    if (context.custom_data.contains(field)) {
        const auto& val = context.custom_data[field];
        if (val.is_number_integer()) return val.get<int>();
        if (val.is_number_float()) return val.get<float>();
        if (val.is_boolean()) return val.get<bool>();
        if (val.is_string()) return val.get<std::string>();
    }

    return std::nullopt;
}

void AchievementConditionEngine::registerFieldExtractor(
    const std::string& field,
    std::function<ConditionValue(const AchievementContext&)> extractor) {

    field_extractors_[field] = std::move(extractor);
}

std::vector<std::string> AchievementConditionEngine::getBuiltinConditions() const {
    return {
        "perfect_game", "comeback", "quick_win", "endurance",
        "streak_master", "versatile",
        "daily_first_win", "weekly_games", "season_rank"
    };
}

void AchievementConditionEngine::initializeBuiltinConditions() {
    // 注册内置游戏特定条件
    registerCustomCondition("perfect_game", builtin_conditions::perfectGame);
    registerCustomCondition("comeback", builtin_conditions::comebackKing);
    registerCustomCondition("quick_win", builtin_conditions::quickWin);
    registerCustomCondition("endurance", builtin_conditions::endurance);
    registerCustomCondition("streak_master", builtin_conditions::streakMaster);
}

std::optional<std::string> AchievementConditionEngine::validateExpression(const std::string& expression) {
    try {
        auto node = parseCondition(expression);
        if (!node) {
            return "Failed to parse expression";
        }
        return std::nullopt;
    } catch (const std::exception& e) {
        return e.what();
    }
}

std::vector<std::string> AchievementConditionEngine::extractReferencedFields(const std::string& expression) {
    std::vector<std::string> fields;

    // 简单的正则匹配字段名
    std::regex field_regex(R"(\b([a-z_][a-z0-9_]*)\b)");
    auto words_begin = std::sregex_iterator(expression.begin(), expression.end(), field_regex);
    auto words_end = std::sregex_iterator();

    std::unordered_set<std::string> seen;
    for (auto it = words_begin; it != words_end; ++it) {
        std::string word = it->str();

        // 排除运算符和关键字
        static const std::unordered_set<std::string> keywords = {
            "and", "or", "not", "in", "progress", "game", "time", "custom"
        };

        if (seen.find(word) == seen.end() && keywords.find(word) == keywords.end()) {
            // 检查是否是数字
            bool is_number = true;
            for (char c : word) {
                if (!std::isdigit(c) && c != '.' && c != '-') {
                    is_number = false;
                    break;
                }
            }

            if (!is_number) {
                fields.push_back(word);
                seen.insert(word);
            }
        }
    }

    return fields;
}

nlohmann::json AchievementConditionEngine::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return {
        {"total_evaluations", total_evaluations_},
        {"cache_hits", cache_hits_},
        {"parse_errors", parse_errors_},
        {"custom_conditions_count", custom_conditions_.size()},
        {"field_extractors_count", field_extractors_.size()},
        {"cached_conditions_count", condition_cache_.size()}
    };
}

// ========== 解析辅助方法 ==========

std::vector<std::string> AchievementConditionEngine::tokenize(const std::string& expression) {
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < expression.size(); ++i) {
        char c = expression[i];

        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        // 检查多字符运算符
        if (i + 1 < expression.size()) {
            std::string two_char = expression.substr(i, 2);
            if (two_char == ">=" || two_char == "<=" || two_char == "==" || two_char == "!=") {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(two_char);
                i++; // 跳过下一个字符
                continue;
            }
        }

        // 单字符运算符和括号
        if (c == '>' || c == '<' || c == '(' || c == ')' || c == '[' || c == ']' || c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::string(1, c));
            continue;
        }

        current += c;
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

std::unique_ptr<ConditionNode> AchievementConditionEngine::parseLogicalExpression(
    std::vector<std::string>& tokens) {

    if (tokens.empty()) {
        return nullptr;
    }

    auto left = parseComparisonExpression(tokens);
    if (!left) {
        return nullptr;
    }

    while (!tokens.empty()) {
        auto op = parseLogicalOperator(tokens[0]);
        if (!op.has_value()) {
            break;
        }

        tokens.erase(tokens.begin()); // 消费运算符

        auto right = parseComparisonExpression(tokens);
        if (!right) {
            return nullptr;
        }

        auto logical = std::make_unique<LogicalCondition>();
        logical->op = op.value();
        logical->children.push_back(std::move(left));
        logical->children.push_back(std::move(right));
        left = std::move(logical);
    }

    return left;
}

std::unique_ptr<ConditionNode> AchievementConditionEngine::parseComparisonExpression(
    std::vector<std::string>& tokens) {

    if (tokens.empty()) {
        return nullptr;
    }

    // 检查括号
    if (tokens[0] == "(") {
        tokens.erase(tokens.begin()); // 消费 "("
        auto node = parseLogicalExpression(tokens);
        if (tokens.empty() || tokens[0] != ")") {
            return nullptr;
        }
        tokens.erase(tokens.begin()); // 消费 ")"
        return node;
    }

    // 检查NOT
    if (tokens[0] == "NOT" || tokens[0] == "not") {
        tokens.erase(tokens.begin());
        auto child = parseComparisonExpression(tokens);
        if (!child) {
            return nullptr;
        }

        auto logical = std::make_unique<LogicalCondition>();
        logical->op = LogicalOperator::NOT;
        logical->children.push_back(std::move(child));
        return logical;
    }

    // 检查特殊条件类型
    if (tokens[0] == "PROGRESS" || tokens[0] == "progress") {
        tokens.erase(tokens.begin());
        if (tokens.size() < 2) {
            return nullptr;
        }

        auto progress = std::make_unique<ProgressCondition>();
        progress->field = tokens[0];
        tokens.erase(tokens.begin());

        auto target_val = parseValue(tokens[0]);
        if (!target_val.has_value()) {
            return nullptr;
        }
        progress->target_value = std::holds_alternative<int>(*target_val)
            ? std::get<int>(*target_val)
            : static_cast<int>(std::get<float>(*target_val));
        tokens.erase(tokens.begin());

        return progress;
    }

    if (tokens[0] == "TIME" || tokens[0] == "time") {
        tokens.erase(tokens.begin());
        if (tokens.empty()) {
            return nullptr;
        }

        auto time_cond = std::make_unique<TimeCondition>();
        time_cond->type = tokens[0];
        tokens.erase(tokens.begin());

        if (!tokens.empty()) {
            auto val = parseValue(tokens[0]);
            if (val.has_value()) {
                time_cond->value = std::holds_alternative<int>(*val)
                    ? std::get<int>(*val)
                    : static_cast<int>(std::get<float>(*val));
                tokens.erase(tokens.begin());
            }
        }

        return time_cond;
    }

    if (tokens[0] == "GAME" || tokens[0] == "game") {
        tokens.erase(tokens.begin());
        if (tokens.size() < 2) {
            return nullptr;
        }

        auto game_cond = std::make_unique<GameSpecificCondition>();
        // 格式: GAME:gomoku:perfect_game 或 GAME gomoku perfect_game
        if (tokens[0].find(':') != std::string::npos) {
            std::string combined = tokens[0];
            size_t pos1 = combined.find(':');
            size_t pos2 = combined.find(':', pos1 + 1);
            game_cond->game_type = combined.substr(0, pos1);
            game_cond->condition_type = combined.substr(pos1 + 1, pos2 - pos1 - 1);
            tokens.erase(tokens.begin());
        } else {
            game_cond->game_type = tokens[0];
            tokens.erase(tokens.begin());
            game_cond->condition_type = tokens[0];
            tokens.erase(tokens.begin());
        }

        return game_cond;
    }

    if (tokens[0] == "CUSTOM" || tokens[0] == "custom") {
        tokens.erase(tokens.begin());
        if (tokens.empty()) {
            return nullptr;
        }

        auto custom = std::make_unique<CustomCondition>();
        custom->name = tokens[0];
        tokens.erase(tokens.begin());

        if (hasCustomCondition(custom->name)) {
            custom->func = custom_conditions_[custom->name];
        }

        return custom;
    }

    // 普通比较表达式
    if (tokens.size() < 3) {
        return nullptr;
    }

    auto comparison = std::make_unique<ComparisonCondition>();
    comparison->field = tokens[0];
    tokens.erase(tokens.begin());

    auto op = parseComparisonOperator(tokens[0]);
    if (!op.has_value()) {
        return nullptr;
    }
    comparison->op = op.value();
    tokens.erase(tokens.begin());

    auto value = parseValue(tokens[0]);
    if (!value.has_value()) {
        return nullptr;
    }
    comparison->value = *value;
    tokens.erase(tokens.begin());

    // 处理范围比较 [min, max]
    if (op.value() == ComparisonOperator::IN_RANGE) {
        if (tokens.size() < 2 || tokens[0] != ",") {
            return nullptr;
        }
        tokens.erase(tokens.begin()); // 消费 ","

        auto value2 = parseValue(tokens[0]);
        if (!value2.has_value()) {
            return nullptr;
        }
        comparison->value2 = *value2;
        tokens.erase(tokens.begin());
    }

    return comparison;
}

std::optional<ComparisonOperator> AchievementConditionEngine::parseComparisonOperator(const std::string& token) {
    if (token == "==" || token == "=") return ComparisonOperator::EQUAL;
    if (token == "!=" || token == "<>") return ComparisonOperator::NOT_EQUAL;
    if (token == ">") return ComparisonOperator::GREATER_THAN;
    if (token == ">=") return ComparisonOperator::GREATER_EQUAL;
    if (token == "<") return ComparisonOperator::LESS_THAN;
    if (token == "<=") return ComparisonOperator::LESS_EQUAL;
    if (token == "IN" || token == "in") return ComparisonOperator::IN_RANGE;
    return std::nullopt;
}

std::optional<LogicalOperator> AchievementConditionEngine::parseLogicalOperator(const std::string& token) {
    if (token == "AND" || token == "and" || token == "&&") return LogicalOperator::AND;
    if (token == "OR" || token == "or" || token == "||") return LogicalOperator::OR;
    return std::nullopt;
}

std::optional<ConditionValue> AchievementConditionEngine::parseValue(const std::string& token) {
    try {
        // 检查是否是整数
        size_t pos;
        int int_val = std::stoi(token, &pos);
        if (pos == token.size()) {
            return int_val;
        }

        // 检查是否是浮点数
        float float_val = std::stof(token, &pos);
        if (pos == token.size()) {
            return float_val;
        }
    } catch (...) {
        // 不是数字，作为字符串
    }

    // 布尔值
    if (token == "true" || token == "TRUE") return true;
    if (token == "false" || token == "FALSE") return false;

    // 字符串值（去掉引号）
    if ((token.front() == '"' && token.back() == '"') ||
        (token.front() == '\'' && token.back() == '\'')) {
        return token.substr(1, token.size() - 2);
    }

    return std::nullopt;
}

std::string AchievementConditionEngine::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

// ========== 内置条件函数 ==========

namespace builtin_conditions {

ConditionResult perfectGame(const AchievementContext& context) {
    ConditionResult result;
    result.current_value = context.last_game_moves;
    result.target_value = 30;
    result.satisfied = (context.last_game_result == GameResult::WIN &&
                       context.last_game_moves > 0 &&
                       context.last_game_moves <= 30);
    result.progress_percent = context.last_game_moves > 0
        ? std::min(100.0f, 30.0f / context.last_game_moves * 100.0f)
        : 0.0f;
    result.message = result.satisfied ? "Perfect game achieved" : "Perfect game not achieved";
    return result;
}

ConditionResult comebackKing(const AchievementContext& context) {
    ConditionResult result;
    int rating_diff = context.opponent_rating - context.current_rating;
    result.current_value = rating_diff;
    result.target_value = 200;
    result.satisfied = (context.last_game_result == GameResult::WIN &&
                       context.was_underdog &&
                       rating_diff >= 200);
    result.progress_percent = rating_diff >= 200 ? 100.0f : (float)rating_diff / 200.0f * 100.0f;
    result.message = result.satisfied ? "Comeback achieved" : "Comeback not achieved";
    return result;
}

ConditionResult quickWin(const AchievementContext& context) {
    ConditionResult result;
    result.current_value = context.last_game_duration;
    result.target_value = 300; // 5分钟
    result.satisfied = (context.last_game_result == GameResult::WIN &&
                       context.last_game_duration > 0 &&
                       context.last_game_duration <= 300);
    result.progress_percent = context.last_game_duration > 0
        ? std::min(100.0f, (float)context.last_game_duration / 300.0f * 100.0f)
        : 0.0f;
    result.message = result.satisfied ? "Quick win achieved" : "Quick win not achieved";
    return result;
}

ConditionResult endurance(const AchievementContext& context) {
    ConditionResult result;
    result.current_value = context.last_game_duration;
    result.target_value = 1800; // 30分钟
    result.satisfied = (context.last_game_duration >= 1800);
    result.progress_percent = std::min(100.0f, (float)context.last_game_duration / 1800.0f * 100.0f);
    result.message = result.satisfied ? "Endurance achievement achieved" : "Endurance not achieved";
    return result;
}

ConditionResult streakMaster(const AchievementContext& context) {
    ConditionResult result;
    result.current_value = context.current_win_streak;
    result.target_value = 10;
    result.satisfied = (context.current_win_streak >= 10);
    result.progress_percent = std::min(100.0f, (float)context.current_win_streak / 10.0f * 100.0f);
    result.message = result.satisfied ? "Streak master achieved" : "Streak master not achieved";
    return result;
}

} // namespace builtin_conditions

} // namespace game_service
} // namespace core_services
