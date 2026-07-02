# 第三方依赖说明

此目录包含编译所需的第三方库源码。

## 目录结构

构建前需要确保以下目录存在：

```
docker/shared/third_party/
├── jwt-cpp/          # jwt-cpp 源码目录
│   ├── include/
│   ├── CMakeLists.txt
│   └── ...
├── libbcrypt/        # libbcrypt 源码目录
│   ├── src/
│   ├── CMakeLists.txt
│   └── ...
└── README.md
```

## 快速设置

```bash
# 进入此目录
cd docker/shared/third_party

# 方式1: 解压已有的压缩包（如果有）
tar -xzf jwt-cpp_R7YGx.tar.gz
tar -xzf libbcrypt_R7YGx.tar.gz

# 方式2: 从 GitHub 下载
git clone --depth 1 https://github.com/Thalhammer/jwt-cpp.git
git clone --depth 1 https://github.com/trusch/libbcrypt.git

# 方式3: 使用国内镜像（推荐）
git clone --depth 1 https://gitee.com/mirrors/jwt-cpp.git
git clone --depth 1 https://gitee.com/mirrors/libbcrypt.git
```

## 依赖版本

- **jwt-cpp**: master 分支 (最新版)
- **libbcrypt**: master 分支 (最新版)
