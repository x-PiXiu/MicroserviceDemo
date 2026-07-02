/**
 * @file product_description_helper.cpp
 * @brief 商品描述助手实现
 */

#include "common/utils/product_description_helper.h"
#include <algorithm>

namespace common {
namespace utils {

// ==================== 单例实现 ====================

ProductDescriptionHelper& ProductDescriptionHelper::getInstance() {
    static ProductDescriptionHelper instance;
    return instance;
}

// ==================== 构造函数：初始化硬编码商品数据 ====================

ProductDescriptionHelper::ProductDescriptionHelper()
    : generator_(random_device_()) {

    // ⭐⭐⭐ 所有商品描述硬编码在这里 ⭐⭐⭐
    // 如需修改商品描述，直接编辑下面的数据即可

    // ============ 0-50元区间 ============
    price_range_products_[PriceRange::RANGE_0_50] = {
        {"一枝玫瑰花", "鲜花"},
        {"小礼品套装", "礼品"},
        {"精美文具套装", "文具"},
        {"畅销图书一本", "图书"},
        {"手机支架", "电子配件"},
        {"USB数据线", "电子配件"},
        {"创意钥匙扣", "日用品"},
        {"明信片套装", "文创"},
        {"小型绿植盆栽", "绿植"},
        {"零食大礼包", "食品"},
        {"水杯", "日用品"},
        {"笔记本", "文具"},
        {"笔袋", "文具"},
        {"书签", "文创"},
        {"便利贴", "文具"}
    };

    // ============ 50-100元区间 ============
    price_range_products_[PriceRange::RANGE_50_100] = {
        {"纯棉T恤一件", "服装"},
        {"精装图书套装", "图书"},
        {"蓝牙耳机", "电子配件"},
        {"保温杯", "日用品"},
        {"运动毛巾", "体育用品"},
        {"手机保护壳", "电子配件"},
        {"商务笔记本", "文具"},
        {"咖啡豆礼盒", "食品"},
        {"护手霜套装", "美妆"},
        {"便携小风扇", "小家电"},
        {"无线鼠标", "电子用品"},
        {"收纳盒套装", "日用品"},
        {"茶叶礼盒", "食品"},
        {"瑜伽垫", "体育用品"},
        {"保温饭盒", "日用品"}
    };

    // ============ 100-200元区间 ============
    price_range_products_[PriceRange::RANGE_100_200] = {
        {"品牌运动鞋", "鞋靴"},
        {"无线鼠标", "电子用品"},
        {"机械键盘", "电子用品"},
        {"智能运动手环", "智能设备"},
        {"意式咖啡机", "小家电"},
        {"双肩背包", "箱包"},
        {"运动手表", "体育用品"},
        {"护肤套装", "美妆"},
        {"家用工具套装", "工具"},
        {"蓝牙音箱", "电子用品"},
        {"电动剃须刀", "个人护理"},
        {"空气加湿器", "家用电器"},
        {"电子书阅读器", "数码产品"},
        {"移动电源", "数码配件"},
        {"品牌太阳镜", "配饰"}
    };

    // ============ 200-500元区间 ============
    price_range_products_[PriceRange::RANGE_200_500] = {
        {"品牌羽绒服", "服装"},
        {"智能空气炸锅", "小家电"},
        {"多功能电压力锅", "小家电"},
        {"扫地机器人", "家用电器"},
        {"平板电脑支架", "数码配件"},
        {"品牌运动服套装", "服装"},
        {"声波电动牙刷", "个人护理"},
        {"智能空气加湿器", "家用电器"},
        {"品牌商务背包", "箱包"},
        {"无线充电器", "数码配件"},
        {"智能家居套装", "智能设备"},
        {"品牌运动手表", "体育用品"},
        {"高端蓝牙耳机", "音频设备"},
        {"美容仪", "个人护理"},
        {"咖啡机套装", "小家电"}
    };

    // ============ 500-1000元区间 ============
    price_range_products_[PriceRange::RANGE_500_1000] = {
        {"智能手表", "智能设备"},
        {"专业跑鞋", "鞋靴"},
        {"全自动咖啡机", "小家电"},
        {"空气净化器", "家用电器"},
        {"高端降噪耳机", "音频设备"},
        {"游戏手柄", "游戏设备"},
        {"智能护眼台灯", "照明"},
        {"智能电饭煲", "小家电"},
        {"品牌香水瓶", "美妆"},
        {"大容量移动电源", "数码配件"},
        {"智能门锁", "智能家居"},
        {"品牌保温杯套装", "日用品"},
        {"专业运动装备", "体育用品"},
        {"智能家居中控", "智能设备"},
        {"高端蓝牙音箱", "音频设备"}
    };

    // ============ 1000元以上区间 ============
    price_range_products_[PriceRange::RANGE_1000_PLUS] = {
        {"品牌笔记本电脑", "电脑"},
        {"旗舰智能手机", "手机"},
        {"品牌平板电脑", "平板"},
        {"奢侈品手表", "奢侈品"},
        {"品牌珠宝首饰", "珠宝"},
        {"专业数码相机", "相机"},
        {"品牌奢侈包包", "奢侈品"},
        {"高端游戏主机", "游戏设备"},
        {"品牌无线耳机", "音频设备"},
        {"智能电视", "家用电器"},
        {"品牌家用投影仪", "数码产品"},
        {"高端智能手表", "智能设备"},
        {"品牌香水套装", "美妆"},
        {"专业级电竞显示器", "电脑"},
        {"智能家居套装", "智能设备"}
    };
}

// ==================== 公共方法实现 ====================

std::string ProductDescriptionHelper::matchProductByAmount(double amount) {
    // 确定价格区间
    PriceRange range = determinePriceRange(amount);

    // 获取该区间的商品列表
    auto it = price_range_products_.find(range);
    if (it == price_range_products_.end() || it->second.empty()) {
        return "商品";  // 兜底返回
    }

    // 随机选择一个商品
    return selectRandomProduct(it->second);
}

std::string ProductDescriptionHelper::formatOrderDescription(
    const std::string& order_id,
    double amount) {

    std::string product_desc = matchProductByAmount(amount);
    return order_id + "(" + product_desc + ")";
}

void ProductDescriptionHelper::setRandomSeed(unsigned int seed) {
    generator_.seed(seed);
}

// ==================== 私有方法实现 ====================

PriceRange ProductDescriptionHelper::determinePriceRange(double amount) {
    if (amount < 50) return PriceRange::RANGE_0_50;
    if (amount < 100) return PriceRange::RANGE_50_100;
    if (amount < 200) return PriceRange::RANGE_100_200;
    if (amount < 500) return PriceRange::RANGE_200_500;
    if (amount < 1000) return PriceRange::RANGE_500_1000;
    return PriceRange::RANGE_1000_PLUS;
}

std::string ProductDescriptionHelper::selectRandomProduct(
    const std::vector<ProductDescription>& products) {

    if (products.empty()) {
        return "商品";
    }

    std::uniform_int_distribution<size_t> dist(0, products.size() - 1);
    size_t index = dist(generator_);

    return products[index].name;
}

} // namespace utils
} // namespace common
