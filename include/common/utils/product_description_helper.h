/**
 * @file product_description_helper.h
 * @brief 商品描述助手 - 硬编码商品描述，根据金额自动匹配
 *
 * 功能特性：
 * - 硬编码商品描述数据，无需数据库
 * - 根据订单金额自动匹配价格区间
 * - 随机选择商品描述
 * - 格式化订单描述为：订单编号(商品描述)
 *
 * 使用方法：
 * - 启用功能：定义 ENABLE_PRODUCT_DESCRIPTION 宏
 * - 禁用功能：注释掉 ENABLE_PRODUCT_DESCRIPTION 宏，或删除相关代码即可
 *
 * 回退方案：
 * - 方案1：注释掉头文件中的 #define ENABLE_PRODUCT_DESCRIPTION
 * - 方案2：删除业务代码中的5行调用代码
 */

#ifndef PRODUCT_DESCRIPTION_HELPER_H
#define PRODUCT_DESCRIPTION_HELPER_H

#include <string>
#include <vector>
#include <map>
#include <random>
#include <functional>

namespace common {
namespace utils {

/**
 * @brief 商品描述信息结构
 */
struct ProductDescription {
    std::string name;           // 商品名称
    std::string category;       // 商品分类

    ProductDescription(const std::string& n, const std::string& c)
        : name(n), category(c) {}
};

/**
 * @brief 价格区间枚举
 */
enum class PriceRange {
    RANGE_0_50,      // 0-50元
    RANGE_50_100,    // 50-100元
    RANGE_100_200,   // 100-200元
    RANGE_200_500,   // 200-500元
    RANGE_500_1000,  // 500-1000元
    RANGE_1000_PLUS  // 1000元以上
};

/**
 * @brief 商品描述助手类
 * @details
 * 使用单例模式，提供商品描述匹配功能
 * 所有商品描述硬编码在代码中，无需数据库
 */
class ProductDescriptionHelper {
public:
    /**
     * @brief 获取单例实例
     */
    static ProductDescriptionHelper& getInstance();

    /**
     * @brief 根据金额匹配随机商品描述
     * @param amount 订单金额
     * @return 商品描述（如："一枝玫瑰花"）
     */
    std::string matchProductByAmount(double amount);

    /**
     * @brief 格式化订单描述
     * @param order_id 订单编号
     * @param amount 订单金额
     * @return 格式化后的描述（如："ORD20250115001(一枝玫瑰花)"）
     */
    std::string formatOrderDescription(const std::string& order_id, double amount);

    /**
     * @brief 设置自定义随机种子（用于测试）
     */
    void setRandomSeed(unsigned int seed);

private:
    ProductDescriptionHelper();
    ~ProductDescriptionHelper() = default;

    // 禁止拷贝
    ProductDescriptionHelper(const ProductDescriptionHelper&) = delete;
    ProductDescriptionHelper& operator=(const ProductDescriptionHelper&) = delete;

    /**
     * @brief 根据金额确定价格区间
     */
    PriceRange determinePriceRange(double amount);

    /**
     * @brief 从商品列表中随机选择一个
     */
    std::string selectRandomProduct(const std::vector<ProductDescription>& products);

private:
    // 价格区间到商品列表的映射（硬编码数据）
    std::map<PriceRange, std::vector<ProductDescription>> price_range_products_;

    // 随机数生成器
    std::random_device random_device_;
    std::mt19937 generator_;
};

// ==================== 功能开关宏 ====================

/**
 * @brief 功能开关
 * @details
 * - 定义此宏：启用商品描述自动匹配功能
 * - 注释此宏：禁用功能，保留原始订单描述
 *
 * 禁用方法：
 * 1. 注释掉下面这行：// #define ENABLE_PRODUCT_DESCRIPTION
 * 2. 或者在编译选项中添加：-DENABLE_PRODUCT_DESCRIPTION=0
 */
// ⭐⭐⭐ 启用/禁用功能的开关 ⭐⭐⭐
#define ENABLE_PRODUCT_DESCRIPTION

// ==================== 便捷宏定义 ====================

#ifdef ENABLE_PRODUCT_DESCRIPTION

/**
 * @brief 便捷宏：格式化订单描述
 * @details
 * 使用示例：
 *   pay_request.order_desc = FORMAT_ORDER_DESC(order_id, amount);
 *
 * 如果禁用了功能，此宏将返回原始描述
 */
#define FORMAT_ORDER_DESC(order_id, amount) \
    common::utils::ProductDescriptionHelper::getInstance().formatOrderDescription(order_id, amount)

#else

// 功能禁用时，直接返回原始描述
#define FORMAT_ORDER_DESC(order_id, amount) (order_id)

#endif // ENABLE_PRODUCT_DESCRIPTION

} // namespace utils
} // namespace common

#endif // PRODUCT_DESCRIPTION_HELPER_H
