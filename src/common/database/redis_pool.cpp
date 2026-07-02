//
// Created by 29108 on 2025/7/6.
//
#include "common/database/redis_pool.h"

#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <future>

#include "common/logger/logger.h"

#ifdef HIREDIS_STUB
// Stub implementation when hiredis is not available
namespace {
    struct redisContext {
        int err = 0;
        char errstr[128] = {0};
    };

    struct redisReply {
        int type = 0;
        long long integer = 0;
        size_t len = 0;
        char* str = nullptr;
    };

    const int REDIS_REPLY_STRING = 1;
    const int REDIS_REPLY_INTEGER = 3;
    const int REDIS_REPLY_NIL = 4;
    const int REDIS_REPLY_STATUS = 5;

    redisContext* redisConnectWithTimeout(const char*, int, struct timeval) { return new redisContext(); }
    void redisFree(redisContext* c) { delete c; }
    int redisSetTimeout(redisContext*, struct timeval) { return 0; }
    redisReply* redisCommand(redisContext*, const char*, ...) { return new redisReply(); }
    int redisAppendCommand(redisContext*, const char*, ...) { return 0; }
    int redisGetReply(redisContext*, void**) { return 0; }
    int redisvFormatCommand(char**, const char*, va_list) { return 0; }
    void* redisvCommand(redisContext*, const char*, va_list) { return new redisReply(); }
    void freeReplyObject(void* reply) { delete static_cast<redisReply*>(reply); }
}
#else
#include <hiredis/hiredis.h>
#endif

namespace common {
    namespace database {

        // 生成唯一的连接ID
        std::string generateConnectionId() {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, 15);

            std::stringstream ss;
            ss << "redis_conn_";
            for (int i = 0; i < 8; ++i) {
                ss << std::hex << dis(gen);
            }
            return ss.str();
        }

        RedisConnection::RedisConnection(const std::string& host, int port, const std::string& password)
        : context_(nullptr), host_(host), port_(port), password_(password),
            connection_id_(generateConnectionId()), in_transaction_(false), in_pipeline_(false) {
            last_used_ = std::chrono::system_clock::now();
        }

        RedisConnection::~RedisConnection() {
            disconnect();
        }

        bool RedisConnection::connect() {
            std::lock_guard<std::mutex> lock(connection_mutex_);



            if (context_) {
                LOG_DEBUG("Existing connection found, disconnecting first");
                // 直接清理现有连接，避免递归锁
                redisFree(context_);
                context_ = nullptr;
            }

            // 创建Redis连接，使用超时设置
            struct timeval timeout = { 5, 0 }; // 5秒超时
            context_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);

            if (!context_ || context_->err) {
                std::string error_msg = "Failed to connect to Redis server: " + host_ + ":" + std::to_string(port_);
                if (context_) {
                    if (context_->errstr[0] != '\0') {
                        error_msg += " - " + std::string(context_->errstr);
                    }
                    LOG_ERROR(error_msg);
                    redisFree(context_);
                    context_ = nullptr;
                } else {
                    error_msg += " - Can't allocate redis context";
                    LOG_ERROR(error_msg);
                }
                return false;
            }

            LOG_DEBUG("TCP connection to Redis server established successfully" );

            // 设置命令超时
            struct timeval cmd_timeout = { 3, 0 }; // 3秒超时
            redisSetTimeout(context_, cmd_timeout);

            // 如果有密码，进行认证
            if (!password_.empty() && !authenticateIfNeeded()) {
                LOG_ERROR("Redis authentication failed" );
                // 直接清理连接，避免递归锁
                redisFree(context_);
                context_ = nullptr;
                return false;
            }

            updateLastUsed();

            return true;
        }

        void RedisConnection::disconnect() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            // 🎯 安全检查：避免重复释放
            if (context_) {
                redisContext* temp_context = context_;
                context_ = nullptr;  // 先置空，防止重复释放
                
                redisFree(temp_context);  // 然后释放
                
                // 避免在析构时使用日志系统，使用标准输出
                LOG_DEBUG("[~RedisConnection] Redis connection closed successfully");
            } else {
                // 🔧 修复：静默处理重复断开连接的情况，避免日志干扰
                // std::cout << "[RedisConnection] Disconnect called but no active connection found" << std::endl;
            }

            // 清理状态
            if (in_transaction_) {
                std::cout << "[RedisConnection] Disconnecting while in transaction mode - transaction will be lost" << std::endl;
                in_transaction_ = false;
            }

            if (in_pipeline_) {
                std::cout << "[RedisConnection] Disconnecting while in pipeline mode - " << pipeline_commands_.size() << " commands will be lost" << std::endl;
                in_pipeline_ = false;
                pipeline_commands_.clear();
            }
        }

        bool RedisConnection::isConnected() const {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            return context_ && !context_->err;
        }

        bool RedisConnection::ping() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            LOG_DEBUG("Pinging Redis server for health check" );

            // 🔧 根本修复：增强连接状态检查
            if (!context_) {
                LOG_DEBUG("Cannot ping: Redis context is null" );
                return false;
            }

            // 检查context错误状态
            if (context_->err) {
                LOG_DEBUG("Cannot ping: Redis context has error: " + std::string(context_->errstr));
                return false;
            }

            // 🔧 根本修复：检查套接字状态
            if (context_->fd <= 0) {
                LOG_DEBUG("Cannot ping: Invalid socket file descriptor: " + std::to_string(context_->fd));
                return false;
            }

            // 🔧 根本修复：使用更安全的ping方法
            try {
                // 方法1：首先尝试检查socket可写性（非阻塞方式）
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(context_->fd, &write_fds);
                
                struct timeval timeout = {0, 100000}; // 100ms超时
                int select_result = select(context_->fd + 1, nullptr, &write_fds, nullptr, &timeout);
                
                if (select_result <= 0) {
                    LOG_DEBUG("Socket not ready for writing, connection may be broken");
                    return false;
                }

                // 方法2：使用原子性更好的redisAppendCommand + redisGetReply
                int append_result = redisAppendCommand(context_, "PING");
                if (append_result != REDIS_OK) {
                    LOG_WARNING("Failed to append PING command: " + std::string(context_->errstr));
                    return false;
                }

                redisReply* reply = nullptr;
                int get_result = redisGetReply(context_, (void**)&reply);
                
                if (get_result != REDIS_OK || !reply) {
                    LOG_WARNING("Failed to get PING reply");
                    if (context_->err) {
                        LOG_WARNING("Redis context error: " + std::string(context_->errstr));
                    }
                    return false;
                }

                bool success = (reply->type == REDIS_REPLY_STATUS &&
                               reply->str && strcmp(reply->str, "PONG") == 0);

                if (success) {
                    LOG_DEBUG("Redis server responded to PING with PONG" );
                } else {
                    std::string error_msg = "PING command failed - unexpected response";
                    if (reply->str) {
                        error_msg += ": " + std::string(reply->str);
                    }
                    LOG_WARNING(error_msg );
                }

                freeReply(reply);
                updateLastUsed();
                return success;
                
            } catch (const std::system_error& e) {
                LOG_ERROR("🚨 Redis PING系统错误: " + std::string(e.what()) + " (errno: " + std::to_string(e.code().value()) + ")");
                
                // 🔧 根本修复：当出现系统错误时，重新创建连接
                if (e.code().value() == 22 || e.code().value() == 9) { // EINVAL or EBADF
                    LOG_ERROR("🚨 检测到严重的连接错误，标记连接为需要重建");
                    if (context_) {
                        context_->err = 1; // 标记context为错误状态
                        strncpy(context_->errstr, "Connection marked for rebuild due to system error", sizeof(context_->errstr) - 1);
                    }
                }
                return false;
            } catch (const std::exception& e) {
                LOG_ERROR("🚨 Redis PING异常: " + std::string(e.what()));
                return false;
            } catch (...) {
                LOG_ERROR("🚨 Redis PING未知异常");
                return false;
            }
        }

        std::string RedisConnection::getConnectionId() const {
            return connection_id_;
        }

        // 基本操作实现
        std::string RedisConnection::get(const std::string& key) {
            LOG_DEBUG("Getting value for key: " + key );

            redisReply* reply = executeCommand("GET %s", key.c_str());
            if (!reply) {
                LOG_ERROR("GET command failed for key: " + key );
                return "";
            }

            std::string result;
            if (reply->type == REDIS_REPLY_STRING) {
                result = std::string(reply->str, reply->len);
                LOG_DEBUG("Retrieved value for key '" + key + "' (length: " + std::to_string(result.length()) + ")" );
            } else if (reply->type == REDIS_REPLY_NIL) {
                 LOG_WARNING("Key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("GET command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        bool RedisConnection::set(const std::string& key, const std::string& value, int ttl) {
            std::string log_msg = "Setting key '" + key + "' with value (length: " + std::to_string(value.length()) + ")";
            if (ttl > 0) {
                log_msg += " and TTL: " + std::to_string(ttl) + " seconds";
            }
            LOG_DEBUG(log_msg );

            redisReply* reply;

            if (ttl > 0) {
                reply = executeCommand("SETEX %s %d %s", key.c_str(), ttl, value.c_str());
            } else {
                reply = executeCommand("SET %s %s", key.c_str(), value.c_str());
            }

            if (!reply) {
                LOG_ERROR("SET command failed for key: " + key );
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_STATUS &&
                            std::strcmp(reply->str, "OK") == 0);

            if (success) {
                LOG_DEBUG("Successfully set key '" + key + "'" );
            } else {
                std::string error_msg = "SET command failed for key '" + key + "'";
                if (reply->type == REDIS_REPLY_ERROR && reply->str) {
                    error_msg += ": " + std::string(reply->str);
                }
                LOG_ERROR(error_msg );
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::del(const std::string& key) {
            LOG_DEBUG("Deleting key: " + key );

            redisReply* reply = executeCommand("DEL %s", key.c_str());
            if (!reply) {
                LOG_ERROR("DEL command failed for key: " + key );
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);

            if (success) {
                LOG_DEBUG("Successfully deleted key '" + key + "' (deleted " + std::to_string(reply->integer) + " key(s))" );
            } else {
                if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0) {
                    LOG_DEBUG("Key '" + key + "' not found for deletion" );
                } else {
                    LOG_ERROR("DEL command failed for key '" + key + "' - unexpected reply type: " + std::to_string(reply->type) );
                }
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::exists(const std::string& key) {
            LOG_DEBUG("Checking existence of key: " + key );

            redisReply* reply = executeCommand("EXISTS %s", key.c_str());
            if (!reply) {
                LOG_ERROR("EXISTS command failed for key: " + key );
                return false;
            }

            bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);

            LOG_DEBUG("Key '" + key + "' " + (exists ? "exists" : "does not exist") );

            freeReply(reply);
            return exists;
        }

        int64_t RedisConnection::incr(const std::string& key) {
            redisReply* reply = executeCommand("INCR %s", key.c_str());
            if (!reply || reply->type != REDIS_REPLY_INTEGER) {
                if (reply) freeReply(reply);
                return -1;
            }

            int64_t result = reply->integer;
            freeReply(reply);
            return result;
        }

        int64_t RedisConnection::decr(const std::string& key) {
            redisReply* reply = executeCommand("DECR %s", key.c_str());
            if (!reply || reply->type != REDIS_REPLY_INTEGER) {
                if (reply) freeReply(reply);
                return -1;
            }

            int64_t result = reply->integer;
            freeReply(reply);
            return result;
        }

        bool RedisConnection::expire(const std::string& key, int seconds) {
            redisReply* reply = executeCommand("EXPIRE %s %d", key.c_str(), seconds);
            if (!reply) {
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
            freeReply(reply);
            return success;
        }

        int64_t RedisConnection::ttl(const std::string& key) {
            redisReply* reply = executeCommand("TTL %s", key.c_str());
            if (!reply || reply->type != REDIS_REPLY_INTEGER) {
                if (reply) freeReply(reply);
                return -1;
            }

            int64_t result = reply->integer;
            freeReply(reply);
            return result;
        }

        // Hash操作实现
        std::string RedisConnection::hget(const std::string& key, const std::string& field) {
            redisReply* reply = executeCommand("HGET %s %s", key.c_str(), field.c_str());
            if (!reply) {
                return "";
            }

            std::string result;
            if (reply->type == REDIS_REPLY_STRING) {
                result = std::string(reply->str, reply->len);
            }

            freeReply(reply);
            return result;
        }

        bool RedisConnection::hset(const std::string& key, const std::string& field, const std::string& value) {
            redisReply* reply = executeCommand("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
            if (!reply) {
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_INTEGER);
            freeReply(reply);
            return success;
        }

        bool RedisConnection::hdel(const std::string& key, const std::string& field) {
            redisReply* reply = executeCommand("HDEL %s %s", key.c_str(), field.c_str());
            if (!reply) {
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
            freeReply(reply);
            return success;
        }

        std::map<std::string, std::string> RedisConnection::hgetall(const std::string& key) {
            std::map<std::string, std::string> result;

            redisReply* reply = executeCommand("HGETALL %s", key.c_str());
            if (!reply || reply->type != REDIS_REPLY_ARRAY) {
                if (reply) freeReply(reply);
                return result;
            }

            // HGETALL返回的是field-value对的数组
            for (size_t i = 0; i < reply->elements; i += 2) {
                if (i + 1 < reply->elements &&
                    reply->element[i]->type == REDIS_REPLY_STRING &&
                    reply->element[i + 1]->type == REDIS_REPLY_STRING) {

                    std::string field(reply->element[i]->str, reply->element[i]->len);
                    std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
                    result[field] = value;
                    }
            }

            freeReply(reply);
            return result;
        }

        bool RedisConnection::hexists(const std::string& key, const std::string& field) {
            LOG_DEBUG("Checking if hash field exists: " + key + "." + field );

            redisReply* reply = executeCommand("HEXISTS %s %s", key.c_str(), field.c_str());
            if (!reply) {
                LOG_ERROR("HEXISTS command failed for key: " + key + ", field: " + field );
                return false;
            }

            bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);

            LOG_DEBUG("Hash field '" + key + "." + field + "' " + (exists ? "exists" : "does not exist") );

            freeReply(reply);
            return exists;
        }

        std::vector<std::string> RedisConnection::hkeys(const std::string& key) {
            LOG_DEBUG("Getting all hash keys for: " + key );

            std::vector<std::string> result;

            redisReply* reply = executeCommand("HKEYS %s", key.c_str());
            if (!reply) {
                LOG_ERROR("HKEYS command failed for key: " + key );
                return result;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
                    }
                }
                LOG_DEBUG("Retrieved " + std::to_string(result.size()) + " hash keys for: " + key );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Hash key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("HKEYS command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        std::vector<std::string> RedisConnection::hvals(const std::string& key) {
            LOG_DEBUG("Getting all hash values for: " + key );

            std::vector<std::string> result;

            redisReply* reply = executeCommand("HVALS %s", key.c_str());
            if (!reply) {
                LOG_ERROR("HVALS command failed for key: " + key );
                return result;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
                    }
                }
                LOG_DEBUG("Retrieved " + std::to_string(result.size()) + " hash values for: " + key );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Hash key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("HVALS command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        // List操作实现
        int64_t RedisConnection::lpush(const std::string &key, const std::string &value) {
            LOG_DEBUG("Left pushing value to list: " + key + " (value length: " + std::to_string(value.length()) + ")" );

            redisReply* reply = executeCommand("LPUSH %s %s", key.c_str(), value.c_str());
            if (!reply) {
                LOG_ERROR("LPUSH command failed for key: " + key );
                return -1;
            }

            int64_t result = -1;
            if (reply->type == REDIS_REPLY_INTEGER) {
                result = reply->integer;
                LOG_DEBUG("LPUSH successful for key '" + key + "', new list length: " + std::to_string(result) );
            } else {
                LOG_ERROR("LPUSH command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        int64_t RedisConnection::rpush(const std::string &key, const std::string &value) {
            LOG_DEBUG("Right pushing value to list: " + key + " (value length: " + std::to_string(value.length()) + ")" );

            redisReply* reply = executeCommand("RPUSH %s %s", key.c_str(), value.c_str());
            if (!reply) {
                LOG_ERROR("RPUSH command failed for key: " + key );
                return -1;
            }

            int64_t result = -1;
            if (reply->type == REDIS_REPLY_INTEGER) {
                result = reply->integer;
                LOG_DEBUG("RPUSH successful for key '" + key + "', new list length: " + std::to_string(result) );
            } else {
                LOG_ERROR("RPUSH command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        std::string RedisConnection::lpop(const std::string& key) {
            LOG_DEBUG("Left popping value from list: " + key );

            redisReply* reply = executeCommand("LPOP %s", key.c_str());
            if (!reply) {
                LOG_ERROR("LPOP command failed for key: " + key );
                return "";
            }

            std::string result;
            if (reply->type == REDIS_REPLY_STRING) {
                result = std::string(reply->str, reply->len);
                LOG_DEBUG("LPOP successful for key '" + key + "', retrieved value length: " + std::to_string(result.length()) );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("LPOP for key '" + key + "' returned NIL (empty list)" );
            } else {
                LOG_WARNING("LPOP command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        std::string RedisConnection::rpop(const std::string& key) {
            LOG_DEBUG("Right popping value from list: " + key );

            redisReply* reply = executeCommand("RPOP %s", key.c_str());
            if (!reply) {
                LOG_ERROR("RPOP command failed for key: " + key );
                return "";
            }

            std::string result;
            if (reply->type == REDIS_REPLY_STRING) {
                result = std::string(reply->str, reply->len);
                LOG_DEBUG("RPOP successful for key '" + key + "', retrieved value length: " + std::to_string(result.length()) );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("RPOP for key '" + key + "' returned NIL (empty list)" );
            } else {
                LOG_WARNING("RPOP command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        int64_t RedisConnection::llen(const std::string& key) {
            LOG_DEBUG("Getting list length for key: " + key );

            redisReply* reply = executeCommand("LLEN %s", key.c_str());
            if (!reply) {
                LOG_ERROR("LLEN command failed for key: " + key );
                return -1;
            }

            int64_t result = -1;
            if (reply->type == REDIS_REPLY_INTEGER) {
                result = reply->integer;
                LOG_DEBUG("List length for key '" + key + "': " + std::to_string(result) );
            } else {
                LOG_ERROR("LLEN command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        // Set操作实现
        bool RedisConnection::sadd(const std::string& key, const std::string& member) {
            LOG_DEBUG("Adding member to set: " + key + " (member: " + member + ")" );

            redisReply* reply = executeCommand("SADD %s %s", key.c_str(), member.c_str());
            if (!reply) {
                LOG_ERROR("SADD command failed for key: " + key + ", member: " + member );
                return false;
            }

            bool success = false;
            if (reply->type == REDIS_REPLY_INTEGER) {
                success = (reply->integer > 0);
                if (success) {
                    LOG_DEBUG("SADD successful for key '" + key + "', added " + std::to_string(reply->integer) + " new member(s)" );
                } else {
                    LOG_DEBUG("SADD for key '" + key + "' - member already exists" );
                }
            } else {
                LOG_ERROR("SADD command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::srem(const std::string& key, const std::string& member) {
            LOG_DEBUG("Removing member from set: " + key + " (member: " + member + ")" );

            redisReply* reply = executeCommand("SREM %s %s", key.c_str(), member.c_str());
            if (!reply) {
                LOG_ERROR("SREM command failed for key: " + key + ", member: " + member );
                return false;
            }

            bool success = false;
            if (reply->type == REDIS_REPLY_INTEGER) {
                success = (reply->integer > 0);
                if (success) {
                    LOG_DEBUG("SREM successful for key '" + key + "', removed " + std::to_string(reply->integer) + " member(s)" );
                } else {
                    LOG_DEBUG("SREM for key '" + key + "' - member not found" );
                }
            } else {
                LOG_ERROR("SREM command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::sismember(const std::string& key, const std::string& member) {
            LOG_DEBUG("Checking set membership: " + key + " (member: " + member + ")" );

            redisReply* reply = executeCommand("SISMEMBER %s %s", key.c_str(), member.c_str());
            if (!reply) {
                LOG_ERROR("SISMEMBER command failed for key: " + key + ", member: " + member );
                return false;
            }

            bool is_member = false;
            if (reply->type == REDIS_REPLY_INTEGER) {
                is_member = (reply->integer == 1);
                LOG_DEBUG("Member '" + member + "' " + (is_member ? "is" : "is not") + " in set '" + key + "'" );
            } else {
                LOG_ERROR("SISMEMBER command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return is_member;
        }

        std::vector<std::string> RedisConnection::smembers(const std::string& key) {
            LOG_DEBUG("Getting all set members for key: " + key );

            std::vector<std::string> result;

            redisReply* reply = executeCommand("SMEMBERS %s", key.c_str());
            if (!reply) {
                LOG_ERROR("SMEMBERS command failed for key: " + key );
                return result;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
                    }
                }
                LOG_DEBUG("Retrieved " + std::to_string(result.size()) + " set members for key: " + key );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Set key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("SMEMBERS command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        int64_t RedisConnection::scard(const std::string& key) {
            LOG_DEBUG("Getting set cardinality for key: " + key );

            redisReply* reply = executeCommand("SCARD %s", key.c_str());
            if (!reply) {
                LOG_ERROR("SCARD command failed for key: " + key );
                return -1;
            }

            int64_t result = -1;
            if (reply->type == REDIS_REPLY_INTEGER) {
                result = reply->integer;
                LOG_DEBUG("Set cardinality for key '" + key + "': " + std::to_string(result) );
            } else {
                LOG_ERROR("SCARD command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        // Sorted Set操作实现
        bool RedisConnection::zadd(const std::string& key, double score, const std::string& member) {
            LOG_DEBUG("Adding member to sorted set: " + key + " (score: " + std::to_string(score) + ", member: " + member + ")" );

            redisReply* reply = executeCommand("ZADD %s %f %s", key.c_str(), score, member.c_str());
            if (!reply) {
                LOG_ERROR("ZADD command failed for key: " + key + ", member: " + member );
                return false;
            }

            bool success = false;
            if (reply->type == REDIS_REPLY_INTEGER) {
                success = (reply->integer > 0);
                if (success) {
                    LOG_DEBUG("ZADD successful for key '" + key + "', added " + std::to_string(reply->integer) + " new member(s)" );
                } else {
                    LOG_DEBUG("ZADD for key '" + key + "' - member already exists, score updated" );
                }
            } else {
                LOG_ERROR("ZADD command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::zrem(const std::string& key, const std::string& member) {
            LOG_DEBUG("Removing member from sorted set: " + key + " (member: " + member + ")" );

            redisReply* reply = executeCommand("ZREM %s %s", key.c_str(), member.c_str());
            if (!reply) {
                LOG_ERROR("ZREM command failed for key: " + key + ", member: " + member );
                return false;
            }

            bool success = false;
            if (reply->type == REDIS_REPLY_INTEGER) {
                success = (reply->integer > 0);
                if (success) {
                    LOG_DEBUG("ZREM successful for key '" + key + "', removed " + std::to_string(reply->integer) + " member(s)" );
                } else {
                    LOG_DEBUG("ZREM for key '" + key + "' - member not found" );
                }
            } else {
                LOG_ERROR("ZREM command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return success;
        }

        std::vector<std::string> RedisConnection::zrange(const std::string& key, int64_t start, int64_t stop, bool withscores) {
            LOG_DEBUG("Getting sorted set range for key: " + key + " (start: " + std::to_string(start) + ", stop: " + std::to_string(stop) + ", withscores: " + (withscores ? "true" : "false") + ")" );

            std::vector<std::string> result;

            redisReply* reply;
            if (withscores) {
                reply = executeCommand("ZRANGE %s %lld %lld WITHSCORES", key.c_str(), start, stop);
            } else {
                reply = executeCommand("ZRANGE %s %lld %lld", key.c_str(), start, stop);
            }

            if (!reply) {
                LOG_ERROR("ZRANGE command failed for key: " + key );
                return result;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
                    }
                }
                LOG_DEBUG("Retrieved " + std::to_string(result.size()) + " elements from sorted set range for key: " + key );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Sorted set key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("ZRANGE command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        std::vector<std::string> RedisConnection::zrevrange(const std::string& key, int64_t start, int64_t stop, bool withscores) {
            LOG_DEBUG("Getting sorted set reverse range for key: " + key + " (start: " + std::to_string(start) + ", stop: " + std::to_string(stop) + ", withscores: " + (withscores ? "true" : "false") + ")" );

            std::vector<std::string> result;

            redisReply* reply;
            if (withscores) {
                reply = executeCommand("ZREVRANGE %s %lld %lld WITHSCORES", key.c_str(), start, stop);
            } else {
                reply = executeCommand("ZREVRANGE %s %lld %lld", key.c_str(), start, stop);
            }

            if (!reply) {
                LOG_ERROR("ZREVRANGE command failed for key: " + key );
                return result;
            }

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    if (reply->element[i]->type == REDIS_REPLY_STRING) {
                        result.emplace_back(reply->element[i]->str, reply->element[i]->len);
                    }
                }
                LOG_DEBUG("Retrieved " + std::to_string(result.size()) + " elements from sorted set reverse range for key: " + key );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Sorted set key '" + key + "' not found (NIL reply)" );
            } else {
                LOG_WARNING("ZREVRANGE command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        int64_t RedisConnection::zcard(const std::string& key) {
            LOG_DEBUG("Getting sorted set cardinality for key: " + key );

            redisReply* reply = executeCommand("ZCARD %s", key.c_str());
            if (!reply) {
                LOG_ERROR("ZCARD command failed for key: " + key );
                return -1;
            }

            int64_t result = -1;
            if (reply->type == REDIS_REPLY_INTEGER) {
                result = reply->integer;
                LOG_DEBUG("Sorted set cardinality for key '" + key + "': " + std::to_string(result) );
            } else {
                LOG_ERROR("ZCARD command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        double RedisConnection::zscore(const std::string& key, const std::string& member) {
            LOG_DEBUG("Getting score for member in sorted set: " + key + " (member: " + member + ")" );

            redisReply* reply = executeCommand("ZSCORE %s %s", key.c_str(), member.c_str());
            if (!reply) {
                LOG_ERROR("ZSCORE command failed for key: " + key + ", member: " + member );
                return std::numeric_limits<double>::quiet_NaN();
            }

            double result = std::numeric_limits<double>::quiet_NaN();
            if (reply->type == REDIS_REPLY_STRING) {
                try {
                    result = std::stod(std::string(reply->str, reply->len));
                    LOG_DEBUG("Score for member '" + member + "' in sorted set '" + key + "': " + std::to_string(result) );
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to parse score for member '" + member + "' in sorted set '" + key + "': " + std::string(e.what()) );
                }
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_DEBUG("Member '" + member + "' not found in sorted set '" + key + "'" );
            } else {
                LOG_ERROR("ZSCORE command for key '" + key + "' returned unexpected reply type: " + std::to_string(reply->type) );
            }

            freeReply(reply);
            return result;
        }

        // 事务操作实现
        bool RedisConnection::multi() {
            LOG_DEBUG("Starting Redis transaction (MULTI)" );

            if (in_transaction_) {
                LOG_WARNING("Already in transaction mode, cannot start new transaction" );
                return false; // 已经在事务中
            }

            redisReply* reply = executeCommand("MULTI");
            if (!reply) {
                LOG_ERROR("MULTI command failed - no reply received" );
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_STATUS &&
                           strcmp(reply->str, "OK") == 0);

            if (success) {
                in_transaction_ = true;
                LOG_INFO("Redis transaction started successfully" );
            } else {
                std::string error_msg = "MULTI command failed";
                if (reply->type == REDIS_REPLY_ERROR && reply->str) {
                    error_msg += ": " + std::string(reply->str);
                }
                LOG_ERROR(error_msg );
            }

            freeReply(reply);
            return success;
        }

        std::vector<redisReply*> RedisConnection::exec() {
            std::vector<redisReply*> results;

            LOG_DEBUG("Executing Redis transaction (EXEC)" );

            if (!in_transaction_) {
                LOG_WARNING("Not in transaction mode, cannot execute EXEC" );
                return results; // 不在事务中
            }

            redisReply* reply = executeCommand("EXEC");
            if (!reply) {
                LOG_ERROR("EXEC command failed - no reply received" );
                in_transaction_ = false;
                return results;
            }

            in_transaction_ = false;

            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    results.push_back(reply->element[i]);
                }
                LOG_INFO("Transaction executed successfully with " + std::to_string(reply->elements) + " results" );
            } else if (reply->type == REDIS_REPLY_NIL) {
                LOG_WARNING("Transaction was discarded (EXEC returned nil) - likely due to WATCH key modification" );
            } else {
                LOG_ERROR("EXEC command returned unexpected reply type: " + std::to_string(reply->type) );
            }

            // 注意：这里不释放reply，因为results中包含了reply的元素
            // 调用者需要负责释放
            return results;
        }

        bool RedisConnection::discard() {
            LOG_DEBUG("Discarding Redis transaction (DISCARD)" );

            if (!in_transaction_) {
                LOG_WARNING("Not in transaction mode, cannot execute DISCARD" );
                return false; // 不在事务中
            }

            redisReply* reply = executeCommand("DISCARD");
            if (!reply) {
                LOG_ERROR("DISCARD command failed - no reply received" );
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_STATUS &&
                           strcmp(reply->str, "OK") == 0);

            if (success) {
                in_transaction_ = false;
                LOG_INFO("Redis transaction discarded successfully" );
            } else {
                std::string error_msg = "DISCARD command failed";
                if (reply->type == REDIS_REPLY_ERROR && reply->str) {
                    error_msg += ": " + std::string(reply->str);
                }
                LOG_ERROR(error_msg );
            }

            freeReply(reply);
            return success;
        }

        // 管道操作实现
        void RedisConnection::pipelineStart() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            // 直接检查连接状态，避免递归锁
            if (!context_ || context_->err) {
                LOG_ERROR("Cannot start pipeline: Redis connection is not established." );
                return;
            }

            // 如果已经在管道模式，先清理之前的状态
            if (in_pipeline_) {
                LOG_WARNING("Already in pipeline mode. Clearing previous pipeline commands." );
                pipeline_commands_.clear();
            }

            // 启动管道模式
            in_pipeline_ = true;
            pipeline_commands_.clear();

            LOG_DEBUG("Pipeline mode started." );
        }

        void RedisConnection::pipelineAdd(const std::string &command) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            // 检查是否在管道模式
            if (!in_pipeline_) {
                LOG_ERROR("Not in pipeline mode. Call pipelineStart() first." );
                return;
            }

            // 直接检查连接状态，避免递归锁
            if (!context_ || context_->err) {
                LOG_ERROR("Redis connection is not established." );
                return;
            }

            // 验证命令不为空
            if (command.empty()) {
                LOG_ERROR("Empty command cannot be added to pipeline." );
                return;
            }

            // 验证命令格式（基本检查）
            if (command.find_first_not_of(" \t\r\n") == std::string::npos) {
                LOG_ERROR("Command contains only whitespace characters." );
                return;
            }

            try {
                // 添加命令到管道队列
                pipeline_commands_.push_back(command);

                // 记录调试信息
                LOG_DEBUG("Added command to pipeline: " + command + " (total commands: " +
                         std::to_string(pipeline_commands_.size()) + ")" );

                // 可选：检查管道命令数量限制，防止内存过度使用
                const size_t MAX_PIPELINE_COMMANDS = 1000;
                if (pipeline_commands_.size() > MAX_PIPELINE_COMMANDS) {
                    LOG_WARNING("Pipeline contains " + std::to_string(pipeline_commands_.size()) +
                            " commands, consider executing to avoid memory issues." );
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to add command to pipeline: " + std::string(e.what()) );
            }
        }

        std::vector<redisReply*> RedisConnection::pipelineExec() {
            std::vector<redisReply*> results;
            std::lock_guard<std::mutex> lock(connection_mutex_);

            // 检查管道状态
            if (!in_pipeline_) {
                LOG_ERROR("Not in pipeline mode. Call pipelineStart() first." );
                return results;
            }

            // 直接检查连接状态，避免递归锁
            if (!context_ || context_->err) {
                LOG_ERROR("Redis connection is not established." );
                in_pipeline_ = false;
                pipeline_commands_.clear();
                return results;
            }

            // 检查是否有命令要执行
            if (pipeline_commands_.empty()) {
                LOG_WARNING("Pipeline is empty, no commands to execute." );
                in_pipeline_ = false;
                return results;
            }

            try {
                LOG_DEBUG("Executing pipeline with " + std::to_string(pipeline_commands_.size()) + " commands" );

                // 执行所有管道命令
                for (const auto& cmd : pipeline_commands_) {
                    if (redisAppendCommand(context_, cmd.c_str()) != REDIS_OK) {
                        LOG_ERROR("Failed to append command to pipeline: " + cmd );
                        // 继续处理其他命令，但记录错误
                    }
                }

                // 获取所有结果
                size_t expected_replies = pipeline_commands_.size();
                for (size_t i = 0; i < expected_replies; ++i) {
                    redisReply* reply = nullptr;
                    int status = redisGetReply(context_, reinterpret_cast<void**>(&reply));

                    if (status == REDIS_OK && reply != nullptr) {
                        results.push_back(reply);
                    } else {
                        LOG_ERROR("Failed to get reply for command " + std::to_string(i) +
                                 " in pipeline. Status: " + std::to_string(status) );

                        // 如果reply不为空但状态不对，仍需要释放内存
                        if (reply != nullptr) {
                            freeReply(reply);
                        }
                    }
                }

                LOG_DEBUG("Pipeline execution completed. Got " + std::to_string(results.size()) +
                         " replies out of " + std::to_string(expected_replies) + " commands" );

            } catch (const std::exception& e) {
                LOG_ERROR("Exception during pipeline execution: " + std::string(e.what()) );

                // 清理已获取的结果
                for (auto* reply : results) {
                    if (reply != nullptr) {
                        freeReply(reply);
                    }
                }
                results.clear();
            }

            // 清理管道状态
            in_pipeline_ = false;
            pipeline_commands_.clear();
            updateLastUsed();

            return results;
        }

        void RedisConnection::PipelineDiscard() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!in_pipeline_) {
                LOG_WARNING("Not in pipeline mode, nothing to discard." );
                return;
            }

            size_t discarded_commands = pipeline_commands_.size();

            // 清理管道状态
            in_pipeline_ = false;
            pipeline_commands_.clear();

            LOG_DEBUG("Pipeline discarded. " + std::to_string(discarded_commands) +
                     " commands were removed." );
        }

        // 工具方法实现
        redisReply* RedisConnection::executeCommand(const char* format, ...) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            // 直接检查连接状态，避免递归锁
            if (!context_ || context_->err) {
                LOG_ERROR("Cannot execute command: Redis connection is not established" );
                return nullptr;
            }

            va_list args;
            va_start(args, format);

            // 为调试目的格式化命令字符串
            char* cmd_str = nullptr;
            int cmd_len = redisvFormatCommand(&cmd_str, format, args);
            std::string command_log;
            if (cmd_str && cmd_len > 0) {
                command_log = std::string(cmd_str, cmd_len);
                // 移除敏感信息（如AUTH命令中的密码）
                if (command_log.find("AUTH") == 0) {
                    command_log = "AUTH [password hidden]";
                }
            }

            redisReply* reply = nullptr;

            if (in_pipeline_) {
                // 如果在管道模式，添加到命令列表
                if (cmd_len > 0) {
                    pipeline_commands_.emplace_back(cmd_str, cmd_len);
                    LOG_DEBUG("Added command to pipeline: " + command_log );
                } else {
                    LOG_ERROR("Failed to format command for pipeline" );
                }
                if (cmd_str) free(cmd_str);
                va_end(args);
                return nullptr; // 管道模式不立即返回结果
            } else {
                // 正常执行命令
                LOG_DEBUG("Executing Redis command: " + command_log );

                // 重新设置va_list，因为之前用过了
                va_end(args);
                va_start(args, format);
                reply = static_cast<redisReply*>(redisvCommand(context_, format, args));

                if (!reply) {
                    LOG_ERROR("Command execution failed - no reply received: " + command_log );
                } else if (reply->type == REDIS_REPLY_ERROR) {
                    std::string error_msg = "Redis command error: " + command_log;
                    if (reply->str) {
                        error_msg += " - " + std::string(reply->str);
                    }
                    LOG_ERROR(error_msg );
                } else {
                    LOG_DEBUG("Command executed successfully: " + command_log );
                }
            }

            if (cmd_str) free(cmd_str);
            va_end(args);
            updateLastUsed();

            return reply;
        }

        void RedisConnection::freeReply(redisReply* reply) {
            if (reply) {
                ::freeReplyObject(reply);
            }
        }

        bool RedisConnection::authenticateIfNeeded() {
            if (password_.empty()) {
                LOG_DEBUG("No password configured, skipping authentication" );
                return true;
            }

            LOG_DEBUG("Authenticating with Redis server using password" );
            redisReply* reply = static_cast<redisReply*>(
                redisCommand(context_, "AUTH %s", password_.c_str()));

            if (!reply) {
                LOG_ERROR("Authentication command failed - no reply received" );
                return false;
            }

            bool success = (reply->type == REDIS_REPLY_STATUS &&
                           strcmp(reply->str, "OK") == 0);

            if (success) {
                LOG_DEBUG("Redis authentication successful" );
            } else {
                std::string error_msg = "Redis authentication failed";
                if (reply->type == REDIS_REPLY_ERROR && reply->str) {
                    error_msg += ": " + std::string(reply->str);
                }
                LOG_ERROR(error_msg );
            }

            freeReply(reply);
            return success;
        }

        bool RedisConnection::isPipelineActive() const {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            return in_pipeline_;
        }

        size_t RedisConnection::getPipelineCommandCount() const {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            return pipeline_commands_.size();
        }

        void RedisConnection::pipelineAddSet(const std::string& key, const std::string& value) {
            if (key.empty()) {
                LOG_ERROR("Key cannot be empty for SET command." );
                return;
            }

            // 对包含空格的值进行简单的引号处理
            std::string escaped_value = value;
            if (value.find(' ') != std::string::npos) {
                escaped_value = "\"" + value + "\"";
            }

            std::string command = "SET " + key + " " + escaped_value;
            pipelineAdd(command);
        }

        void RedisConnection::pipelineAddGet(const std::string& key) {
            if (key.empty()) {
                LOG_ERROR("Key cannot be empty for GET command." );
                return;
            }

            std::string command = "GET " + key;
            pipelineAdd(command);
        }

        void RedisConnection::pipelineAddDel(const std::string& key) {
            if (key.empty()) {
                LOG_ERROR("Key cannot be empty for DEL command." );
                return;
            }

            std::string command = "DEL " + key;
            pipelineAdd(command);
        }

        // ==================== RedisPool 实现 ====================

        RedisPool::RedisPool(const common::config::RedisConfig& config)
            : manager_config_(config), running_(false) {
            manager_config_.validate(); // 验证配置
        }

        RedisPool::RedisPool() : RedisPool(common::config::RedisConfig::fromConfigManager()) {
        }

        RedisPool::~RedisPool() {
            try {
                // 避免在析构时使用日志系统，使用标准输出
                std::cout << "[RedisPool] Destroying Redis connection pool" << std::endl;

                // 🎯 安全析构顺序：先停止运行标志
                running_.store(false);
                
                // 先关闭线程池，再关闭连接池
                shutdownThreadPool();
                
                // 🎯 安全停止：带超时的停止机制
                try {
                    stop();
                } catch (const std::exception& e) {
                    std::cerr << "[RedisPool] Exception during stop: " << e.what() << std::endl;
                }

                // 🎯 健康检查线程安全清理（带超时）
                if (health_check_thread_.joinable()) {
                    std::atomic<bool> thread_finished{false};
                    std::thread wait_thread([&]() {
                        try {
                            health_check_thread_.join();
                            thread_finished.store(true);
                        } catch (...) {
                            // 忽略join异常
                        }
                    });
                    
                    // 等待最多1秒
                    auto start_time = std::chrono::steady_clock::now();
                    while (!thread_finished.load() && 
                           (std::chrono::steady_clock::now() - start_time) < std::chrono::seconds(1)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    
                    if (thread_finished.load()) {
                        wait_thread.join();
                        std::cout << "[RedisPool] Health check thread stopped safely" << std::endl;
                    } else {
                        wait_thread.detach();
                        health_check_thread_.detach();
                        std::cout << "[RedisPool] Health check thread detached due to timeout" << std::endl;
                    }
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[RedisPool] Exception in destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[RedisPool] Unknown exception in destructor" << std::endl;
            }
        }

        void RedisPool::start() {
            if (running_.exchange(true)) {
                LOG_WARNING("Redis pool is already running" );
                return;
            }

            try {

                {
                    // 创建初始连接
                    std::lock_guard<std::mutex> lock(pool_mutex_);

                    for (size_t i = 0; i < manager_config_.redis_pool_size; ++i) {
                        LOG_DEBUG("Creating initial connection " + std::to_string(i + 1) + "/" + std::to_string(manager_config_.redis_pool_size) );
                        auto conn = createConnection();
                        if (conn) {
                            LOG_DEBUG("Connection object created, attempting to connect..." );
                            if (conn->connect()) {
                                idle_connections_.push(conn);
                                LOG_DEBUG("Created initial connection " + std::to_string(i + 1) + "/" +
                                         std::to_string(manager_config_.redis_pool_size) );
                            } else {
                                LOG_ERROR("Failed to connect initial connection " + std::to_string(i + 1) );
                            }
                        } else {
                            LOG_ERROR("Failed to create initial connection object " + std::to_string(i + 1) );
                        }
                    }
                }

                // 初始化线程池（如果启用异步操作）
                if (manager_config_.enable_async_operations) {
                    initializeThreadPool();
                }

                // 🔧 根本修复：启动改进的健康检查线程（适用于所有服务）
                health_check_thread_ = std::thread(&RedisPool::healthCheck, this);
                LOG_INFO("✅ Redis健康检查线程已启动（使用增强的错误处理）");



            } catch (const std::exception& e) {
                running_ = false;
                LOG_ERROR("Failed to start Redis connection pool: " + std::string(e.what()) );
                throw;
            }
        }

        void RedisPool::stop() {
            if (!running_.exchange(false)) {
                LOG_DEBUG("Redis pool is already stopped" );
                return;
            }



            // 先关闭线程池
            shutdownThreadPool();

            // 停止健康检查线程，使用激进的超时机制
            if (health_check_thread_.joinable()) {
                try {


                    // 使用很短的超时时间，快速放弃等待
                    std::atomic<bool> join_completed{false};

                    std::thread join_thread([&]() {
                        try {
                            health_check_thread_.join();
                            join_completed.store(true);
                        } catch (...) {
                            // 忽略join异常
                        }
                    });

                    // 只等待1秒
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                    if (join_completed.load()) {
                        std::cout << "[RedisPool] Redis健康检查线程已正常结束" << std::endl;
                        if (join_thread.joinable()) {
                            join_thread.join();
                        }
                    } else {
                        std::cout << "[RedisPool] Redis健康检查线程结束超时，强制继续" << std::endl;
                        if (join_thread.joinable()) {
                            join_thread.detach();
                        }
                        // 不等待健康检查线程，让析构函数处理
                    }

                } catch (const std::exception& e) {
                    std::cerr << "[RedisPool] 等待Redis健康检查线程结束时发生异常: " << e.what() << std::endl;
                }
            }

            std::lock_guard<std::mutex> lock(pool_mutex_);

            // 清理空闲连接
            size_t idle_count = idle_connections_.size();
            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();
                if (conn) {
                    conn->disconnect();
                }
            }

            // 记录活跃连接（这些连接会在归还时自动清理）
            size_t active_count = active_connections_.size();
            if (active_count > 0) {
                std::cout << "[RedisPool] Redis pool stopped with " << active_count <<
                        " active connections still in use" << std::endl;
            }

            std::cout << "[RedisPool] Redis connection pool stopped. Cleaned up " << idle_count <<
                    " idle connections" << std::endl;
        }

        void RedisPool::enableConfigHotReload() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            // 监听Redis主机地址变化
            config_manager.addChangeListener("database.redis.host",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_WARNING("Redis host changed from " + old_val + " to " + new_val +
                               ". Pool restart required for changes to take effect." );
                    this->markForRestart();
                });

            // 监听Redis端口变化
            config_manager.addChangeListener("database.redis.port",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_WARNING("Redis port changed from " + old_val + " to " + new_val +
                               ". Pool restart required for changes to take effect." );
                    this->markForRestart();
                });

            // 监听Redis密码变化
            config_manager.addChangeListener("database.redis.password",
                [this](const auto& /*key*/, const auto& /*old_val*/, const auto& /*new_val*/) {
                    LOG_WARNING("Redis password changed. Pool restart required for changes to take effect." );
                    this->markForRestart();
                });

            // 监听Redis数据库编号变化
            config_manager.addChangeListener("database.redis.database",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis database changed from " + old_val + " to " + new_val +
                            ". Pool restart required for changes to take effect." );
                    this->markForRestart();
                });

            // 监听连接池最大大小变化（可以动态调整）
            config_manager.addChangeListener("database.redis.pool.max_size",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis pool max_size changed from " + old_val + " to " + new_val );
                    try {
                        size_t new_max_size = std::stoull(new_val);
                        this->adjustPoolSize(new_max_size);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to adjust Redis pool size: " + std::string(e.what()) );
                    }
                });

            // 监听空闲超时变化（可以动态调整）
            config_manager.addChangeListener("database.redis.pool.idle_timeout",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis pool idle_timeout changed from " + old_val + " to " + new_val );
                    try {
                        int new_timeout = std::stoi(new_val);
                        this->updateIdleTimeout(std::chrono::seconds(new_timeout));
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to update Redis idle timeout: " + std::string(e.what()) );
                    }
                });

            // 监听健康检查间隔变化（可以动态调整）
            config_manager.addChangeListener("database.redis.pool.health_check_interval",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis pool health_check_interval changed from " + old_val + " to " + new_val );
                    try {
                        int new_interval = std::stoi(new_val);
                        this->updateHealthCheckInterval(std::chrono::seconds(new_interval));
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to update Redis health check interval: " + std::string(e.what()) );
                    }
                });

            // ==================== 线程池配置监听 ====================

            // 监听异步操作启用状态变化
            config_manager.addChangeListener("database.redis.thread_pool.enable_async_operations",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis async operations changed from " + old_val + " to " + new_val);
                    bool enable_async = (new_val == "true" || new_val == "1");

                    if (enable_async && !thread_pool_initialized_.load()) {
                        // 启用异步操作，初始化线程池
                        try {
                            this->initializeThreadPool();
                            LOG_INFO("线程池已启用并初始化");
                        } catch (const std::exception& e) {
                            LOG_ERROR("启用线程池失败: " + std::string(e.what()));
                        }
                    } else if (!enable_async && thread_pool_initialized_.load()) {
                        // 禁用异步操作，关闭线程池
                        this->shutdownThreadPool();
                        LOG_INFO("线程池已禁用并关闭");
                    }
                });

            // 监听线程池核心线程数变化
            config_manager.addChangeListener("database.redis.thread_pool.core_size",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis thread pool core size changed from " + old_val + " to " + new_val);
                    try {
                        int new_core_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustCorePoolSize(new_core_size);
                            manager_config_.thread_pool_core_size = new_core_size;
                            LOG_INFO("线程池核心大小已动态调整为: " + std::to_string(new_core_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整线程池核心大小失败: " + std::string(e.what()));
                    }
                });

            // 监听线程池最大线程数变化
            config_manager.addChangeListener("database.redis.thread_pool.max_size",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Redis thread pool max size changed from " + old_val + " to " + new_val);
                    try {
                        int new_max_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustMaximumPoolSize(new_max_size);
                            manager_config_.thread_pool_max_size = new_max_size;
                            LOG_INFO("线程池最大大小已动态调整为: " + std::to_string(new_max_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整线程池最大大小失败: " + std::string(e.what()));
                    }
                });

            // 监听线程池队列容量变化（需要重启线程池）
            config_manager.addChangeListener("database.redis.thread_pool.queue_capacity",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_WARNING("Redis thread pool queue capacity changed from " + old_val + " to " + new_val +
                               ". Thread pool restart required for changes to take effect.");
                    // 队列容量变化需要重启线程池
                    if (restart_thread_pool_) {
                        restart_thread_pool_->submit([this]() {
                            try {
                                this->shutdownThreadPool();
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                this->initializeThreadPool();
                                LOG_INFO("线程池已重启以应用新的队列容量配置");
                            } catch (const std::exception& e) {
                                LOG_ERROR("重启线程池失败: " + std::string(e.what()));
                            }
                        });
                    }
                });

            LOG_INFO("Redis连接池配置热更新监听已启用，包括线程池配置");
        }

        std::shared_ptr<RedisConnection> RedisPool::getConnection(std::chrono::milliseconds timeout) {
            if (!running_.load()) {
                LOG_ERROR("Cannot get connection: Redis pool is not running" );
                return nullptr;
            }



            LOG_DEBUG("Attempting to get Redis connection with timeout: " + std::to_string(timeout.count()) + "ms" );
            auto start_time = std::chrono::steady_clock::now();

            int retry_count = 0;
            while (std::chrono::steady_clock::now() - start_time < timeout) {
                std::unique_lock<std::mutex> lock(pool_mutex_);
                retry_count++;

                if (retry_count % 100 == 0) { // 每100次重试记录一次状态
                    LOG_WARNING("Redis连接获取重试 " + std::to_string(retry_count) + " 次 - 活跃: " +
                               std::to_string(active_connections_.size()) + ", 空闲: " +
                               std::to_string(idle_connections_.size()));
                }

                // 尝试从空闲连接池获取连接
                LOG_DEBUG("Idle connections available: " + std::to_string(idle_connections_.size()) );
                if (!idle_connections_.empty()) {
                    auto conn = idle_connections_.front();
                    idle_connections_.pop();
                    LOG_DEBUG("Retrieved connection from idle pool, validating..." );

                    // 简化连接验证 - 只检查连接状态，不执行ping
                    if (conn && conn->isConnected()) {
                        active_connections_.insert(conn);
                        LOG_DEBUG("Retrieved connection from idle pool. Active: " +
                                 std::to_string(active_connections_.size()) +
                                 ", Idle: " + std::to_string(idle_connections_.size()) );
                        return conn;
                    } else {
                        LOG_WARNING("Found invalid connection in idle pool, discarding" );
                        // 连接无效，尝试重新连接
                        if (conn) {
                            if (conn->connect()) {
                                active_connections_.insert(conn);
                                LOG_INFO("重新连接成功，使用修复的连接");
                                return conn;
                            }
                        }
                        // 连接无效且无法修复，丢弃并继续
                    }
                }

                // 如果没有空闲连接且未达到最大连接数，创建新连接
                size_t current_total = active_connections_.size() + idle_connections_.size();
                if (current_total < manager_config_.redis_max_pool_size) {
                    auto conn = createConnection();
                    if (conn && conn->connect()) {
                        active_connections_.insert(conn);
                        LOG_DEBUG("Created new connection. Active: " +
                                 std::to_string(active_connections_.size()) +
                                 ", Idle: " + std::to_string(idle_connections_.size()) );
                        return conn;
                    } else {
                        LOG_ERROR("Failed to create new connection" );
                    }
                } else {
                    // 连接池已满，检查是否有僵死连接需要清理
                    if (retry_count > 200) { // 重试超过200次，强制清理可能的僵死连接
                        LOG_WARNING("连接池可能存在僵死连接，尝试清理活跃连接");
                        // 这里可以添加更激进的连接清理逻辑
                        // 但要小心不要影响正在使用的连接
                    }
                }

                lock.unlock();

                // 等待一段时间后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // 超时前记录最终状态
            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                LOG_ERROR("Redis连接获取超时 (" + std::to_string(timeout.count()) + "ms) - " +
                         "活跃连接: " + std::to_string(active_connections_.size()) +
                         ", 空闲连接: " + std::to_string(idle_connections_.size()) +
                         ", 最大连接数: " + std::to_string(manager_config_.redis_max_pool_size) +
                         ", 重试次数: " + std::to_string(retry_count));
            }
            return nullptr;
        }


        void RedisPool::returnConnection(std::shared_ptr<RedisConnection> conn) {
            if (!conn) {
                LOG_WARNING("Attempted to return null connection to pool" );
                return;
            }

            std::lock_guard<std::mutex> lock(pool_mutex_);

            // 从活跃连接中移除
            auto it = active_connections_.find(conn);
            if (it == active_connections_.end()) {
                LOG_WARNING("Attempted to return connection that was not in active pool - 活跃: " +
                           std::to_string(active_connections_.size()) + ", 空闲: " +
                           std::to_string(idle_connections_.size()));
                return;
            }

            active_connections_.erase(it);

            LOG_DEBUG("归还连接 - 活跃连接数: " + std::to_string(active_connections_.size()) +
                     ", 空闲连接数: " + std::to_string(idle_connections_.size()));

            if (!running_.load()) {
                LOG_DEBUG("Pool is stopped, discarding returned connection" );
                conn->disconnect();
                return;
            }

            // 简化连接有效性检查，减少ping开销
            if (conn->isConnected()) {
                // 连接有效，放回空闲池
                idle_connections_.push(conn);
                LOG_DEBUG("Returned connection to idle pool. Active: " +
                         std::to_string(active_connections_.size()) +
                         ", Idle: " + std::to_string(idle_connections_.size()) );
            } else {
                LOG_WARNING("Returned connection is invalid, discarding" );
                conn->disconnect();
            }
        }

        void RedisPool::healthCheck() {
            LOG_INFO("🔧 启动智能Redis健康检查线程");

            int interval_ms = manager_config_.redis_pool_validation_interval_ms;

            LOG_INFO("🔧 Redis健康检查配置验证:");
            LOG_INFO("  - 原始间隔: " + std::to_string(interval_ms) + "毫秒 (" +
                     std::to_string(interval_ms / 1000) + "秒)");

            if (interval_ms <= 0 || interval_ms < 1000) {
                interval_ms = 60000;
                LOG_WARNING("⚠️ Redis健康检查间隔无效(<1000ms)，使用安全默认值: 60秒");
            } else if (interval_ms > 3600000) {
                interval_ms = 60000;
                LOG_WARNING("⚠️ Redis健康检查间隔过大(>1小时)，使用安全默认值: 60秒");
            } else {
                LOG_INFO("✅ Redis健康检查间隔有效: " + std::to_string(interval_ms / 1000) + "秒");
            }

            auto safe_health_check_interval = std::chrono::milliseconds(interval_ms);

            while (running_.load()) {
                // 阶段1: 连接池维护（独立 try-catch，隔离 mutex 错误）
                try {
                    std::lock_guard<std::mutex> lock(pool_mutex_);
                    removeExpiredConnections();
                    ensureMinimumConnections();
                } catch (const std::system_error& e) {
                    // pthread_mutex_lock EINVAL: mutex 可能被其他线程的异常操作暂时破坏
                    // 跳过本轮维护，下轮重试即可恢复
                    LOG_WARNING("Redis连接池维护跳过(mutex暂时不可用): " + std::string(e.what()) +
                               " (errno: " + std::to_string(e.code().value()) + ")");
                } catch (const std::exception& e) {
                    LOG_WARNING("Redis连接池维护异常: " + std::string(e.what()));
                } catch (...) {
                    LOG_WARNING("Redis连接池维护未知异常");
                }

                // 阶段2: 等待下一轮（sleep_for 在 libstdc++ 下不抛异常）
                if (!running_.load()) break;
                std::this_thread::sleep_for(safe_health_check_interval);
            }
            
            LOG_INFO("🔧 Redis智能健康检查线程正常退出");
        }

        bool RedisPool::lightweightValidateConnection(std::shared_ptr<RedisConnection> conn) {
            if (!conn) {
                return false;
            }

            // 🔧 轻量级验证：只检查基本状态，不进行网络操作
            try {
                // 第一层：检查shared_ptr引用计数
                if (conn.use_count() <= 1) {
                    return false;
                }

                // 第二层：检查基本连接状态（只检查context指针状态）
                if (!conn->isConnected()) {
                    return false;
                }

                // 第三层：检查空闲时间
                auto now = std::chrono::system_clock::now();
                auto idle_time = (now - conn->getLastUsed()).count();
                if (idle_time > manager_config_.redis_pool_max_idle_time_ms) {
                    return false;
                }

                return true;

            } catch (const std::exception& e) {
                LOG_DEBUG("轻量级连接验证异常: " + std::string(e.what()));
                return false;
            }
        }

        std::shared_ptr<RedisConnection> RedisPool::createConnection() {
            try {
                auto conn = std::make_shared<RedisConnection>(manager_config_.redis_host, manager_config_.redis_port, manager_config_.redis_password);
                LOG_DEBUG("Created new Redis connection object" );
                return conn;
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to create Redis connection object: " + std::string(e.what()) );
                return nullptr;
            }
        }

        void RedisPool::removeExpiredConnections() {
            auto now = std::chrono::system_clock::now();

            // 🔧 在healthCheck中使用轻量级验证，避免ping测试
            std::queue<std::shared_ptr<RedisConnection>> valid_connections;

            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();

                if (!conn) {
                    LOG_DEBUG("Removed null idle connection");
                    continue;
                }

                // 🔧 使用轻量级验证，不进行ping测试
                try {
                    if (lightweightValidateConnection(conn)) {
                        // 检查连接是否过期（基于时间，不需要网络调用）
                        auto last_used = conn->getLastUsed();
                        auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(now - last_used);

                        if (idle_time.count() < 30) { // 30分钟内的连接保留
                            valid_connections.push(conn);
                            LOG_DEBUG("Kept valid idle connection (idle for " + std::to_string(idle_time.count()) + " minutes)");
                        } else {
                            LOG_DEBUG("Removed expired idle connection (idle for " + std::to_string(idle_time.count()) + " minutes)");
                            // 安全地断开过期连接
                            try {
                                conn->disconnect();
                            } catch (...) {
                                // 忽略断开连接时的任何异常
                            }
                        }
                    } else {
                        LOG_DEBUG("Removed invalid idle connection (lightweight check)");
                    }

                } catch (const std::exception& e) {
                    LOG_WARNING("🚨 检查空闲连接时出现异常: " + std::string(e.what()) + "，移除该连接");
                }
            }

            // 将有效连接放回队列
            idle_connections_ = std::move(valid_connections);

            // 🔧 简化活跃连接检查
            auto it = active_connections_.begin();
            while (it != active_connections_.end()) {
                if (!(*it) || !lightweightValidateConnection(*it)) {
                    LOG_DEBUG("Removed invalid active connection (lightweight check)");
                    it = active_connections_.erase(it);
                } else {
                    // 检查活跃连接的空闲时间
                    try {
                        auto last_used = (*it)->getLastUsed();
                        auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(now - last_used);
                        
                        if (idle_time.count() > 60) { // 活跃连接1小时未使用算过期
                            LOG_DEBUG("Removed long-idle active connection (idle for " + std::to_string(idle_time.count()) + " minutes)");
                            try {
                                (*it)->disconnect();
                            } catch (...) {
                                // 忽略断开时的异常
                            }
                            it = active_connections_.erase(it);
                        } else {
                            ++it;
                        }
                    } catch (const std::exception& e) {
                        LOG_WARNING("🚨 检查活跃连接时出现异常: " + std::string(e.what()) + "，移除该连接");
                        it = active_connections_.erase(it);
                    }
                }
            }
        }

        void RedisPool::ensureMinimumConnections() {
            size_t current_total = active_connections_.size() + idle_connections_.size();

            if (current_total < manager_config_.redis_min_pool_size) {
                size_t needed = manager_config_.redis_min_pool_size - current_total;

                for (size_t i = 0; i < needed && current_total < manager_config_.redis_max_pool_size; ++i) {
                    try {
                        auto conn = createConnection();
                        if (conn && conn->connect()) {
                            idle_connections_.push(conn);
                            current_total++;
                            LOG_DEBUG("Added new connection to maintain minimum pool size");
                        } else {
                            LOG_WARNING("Failed to create/connect Redis connection for minimum pool size");
                            break;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("🚨 创建最小连接数时出现异常: " + std::string(e.what()));
                        break;
                    }
                }
            }
        }

        bool RedisPool::validateConnection(std::shared_ptr<RedisConnection> conn) {
            if (!conn) {
                return false;
            }

            // 🔧 根本修复：增强连接验证逻辑
            
            // 第一层：基本连接状态检查
            if (!conn->isConnected()) {
                LOG_DEBUG("Connection validation failed: not connected");
                return false;
            }

            // 第二层：空闲时间检查
            auto now = std::chrono::system_clock::now();
            auto idle_time = (now - conn->getLastUsed()).count();
            if (idle_time > manager_config_.redis_pool_max_idle_time_ms) {
                LOG_DEBUG("Connection validation failed: idle timeout exceeded");
                return false;
            }

            // 🔧 根本修复：在healthCheck中避免ping测试，只在实际使用时进行
            // healthCheck应该使用轻量级检查，ping测试留给实际使用时
            
            // 🔧 智能ping测试：使用频率限制避免过于频繁的ping
            static std::chrono::steady_clock::time_point last_ping_time;
            static std::mutex ping_time_mutex;
            
            {
                std::lock_guard<std::mutex> lock(ping_time_mutex);
                auto now = std::chrono::steady_clock::now();
                auto time_since_last_ping = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time);
                
                // 如果距离上次ping很短（<3秒），跳过ping测试，使用基本连接状态
                if (time_since_last_ping.count() < 3) {
                    LOG_DEBUG("跳过频繁ping测试（距离上次ping仅" + std::to_string(time_since_last_ping.count()) + "秒）");
                    return true;
                }
                
                last_ping_time = now;
            }
            
            // 第三层：在实际使用上下文中进行完整的ping测试
            try {
                return conn->ping();
            } catch (const std::system_error& e) {
                LOG_WARNING("🚨 Connection validation ping failed with system error (errno: " + 
                           std::to_string(e.code().value()) + "): " + std::string(e.what()));
                if (e.code().value() == 22) { // EINVAL
                    // 标记连接为无效，让连接池清理它
                    return false;
                }
                return false;
            } catch (const std::exception& e) {
                LOG_DEBUG("Connection validation ping failed: " + std::string(e.what()));
                return false;
            }
        }

        void RedisPool::markForRestart() {
            restart_required_ = true;
            LOG_INFO("Redis pool marked for restart" );

            // 立即触发重启检查（异步执行）
            scheduleRestartCheck();
        }

        void RedisPool::adjustPoolSize(size_t new_max_size) {
            std::lock_guard<std::mutex> lock(pool_mutex_);

            if (new_max_size > manager_config_.redis_max_pool_size) {
                size_t old_max_size = manager_config_.redis_max_pool_size;
                manager_config_.redis_max_pool_size = new_max_size;

                LOG_INFO("Redis pool max_size increased from " + std::to_string(old_max_size) +
                        " to " + std::to_string(new_max_size) );

                // 可以预创建一些新连接
                preCreateConnections(new_max_size - old_max_size);
            } else if (new_max_size < manager_config_.redis_max_pool_size) {
                LOG_WARNING("Cannot decrease Redis pool max_size from " + std::to_string(manager_config_.redis_max_pool_size) +
                           " to " + std::to_string(new_max_size) + " at runtime. Restart required." );
            } else {
                LOG_DEBUG("Redis pool max_size unchanged: " + std::to_string(new_max_size) );
            }
        }

        void RedisPool::updateIdleTimeout(std::chrono::seconds new_timeout) {
            manager_config_.redis_pool_max_idle_time_ms = new_timeout.count();
            LOG_INFO("Redis pool idle timeout updated to " + std::to_string(new_timeout.count()) + " seconds" );

            // 触发空闲连接检查
            triggerIdleConnectionCheck();
        }

        void RedisPool::updateHealthCheckInterval(std::chrono::seconds new_interval) {
            manager_config_.redis_pool_validation_interval_ms = new_interval.count();
            LOG_INFO("Redis pool health check interval updated to " + std::to_string(new_interval.count()) + " seconds" );

            // 重新调度健康检查任务
            rescheduleHealthCheck();
        }

        void RedisPool::preCreateConnections(size_t count) {
            for (size_t i = 0; i < count; ++i) {
                try {
                    auto conn = createConnection();
                    if (conn && conn->connect()) {
                        idle_connections_.push(conn);
                        LOG_DEBUG("Pre-created Redis connection " + std::to_string(i + 1) + "/" + std::to_string(count) );
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Failed to pre-create Redis connection: " + std::string(e.what()) );
                    break; // 如果创建失败，停止预创建
                }
            }
        }

        void RedisPool::triggerIdleConnectionCheck() {
            // 这里可以实现立即检查空闲连接的逻辑
            LOG_DEBUG("Triggered idle connection check" );
        }

        void RedisPool::rescheduleHealthCheck() {
            // 这里可以实现重新调度健康检查的逻辑
            LOG_DEBUG("Rescheduled health check with new interval" );
        }

        void RedisPool::scheduleRestartCheck() {
            if (restart_thread_pool_) {
                restart_thread_pool_->submit([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 延迟500ms，避免频繁重启
                    checkAndHandleRestart();
                });
            } else {
                // 如果没有线程池，创建临时线程
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    checkAndHandleRestart();
                }).detach();
            }
        }

        void RedisPool::checkAndHandleRestart() {
            if (!restart_required_.load()) {
                return; // 不需要重启
            }

            LOG_INFO("Starting Redis pool restart process" );

            try {
                // 1. 停止接受新的连接请求
                setAcceptingNewConnections(false);

                // 2. 等待现有连接完成当前操作
                waitForActiveConnectionsToFinish();

                // 3. 关闭所有现有连接
                closeAllConnections();

                // 4. 重新加载配置
                reloadConfiguration();

                // 5. 重新初始化连接池
                if (!reinitializePool()) {
                    LOG_ERROR("Failed to reinitialize Redis pool" );
                    return;
                }

                // 6. 恢复接受新连接
                setAcceptingNewConnections(true);

                // 7. 重置重启标志
                restart_required_ = false;

                LOG_INFO("Redis pool restart completed successfully" );

            } catch (const std::exception& e) {
                LOG_ERROR("Redis pool restart failed: " + std::string(e.what()) );

                // 重启失败，稍后重试
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (restart_required_.load()) {
                    LOG_INFO("Retrying Redis pool restart" );
                    checkAndHandleRestart();
                }
            }
        }

        void RedisPool::setAcceptingNewConnections(bool accepting) {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            accepting_new_connections_ = accepting;

            if (accepting) {
                LOG_INFO("Redis pool now accepting new connections" );
            } else {
                LOG_INFO("Redis pool stopped accepting new connections" );
            }
        }

        void RedisPool::waitForActiveConnectionsToFinish() {
            LOG_INFO("Waiting for active Redis connections to finish" );

            const int max_wait_seconds = 30; // 最多等待30秒
            const int check_interval_ms = 100; // 每100ms检查一次

            for (int i = 0; i < max_wait_seconds * 1000 / check_interval_ms; ++i) {
                {
                    std::lock_guard<std::mutex> lock(pool_mutex_);
                    if (active_connections_.empty()) {
                        LOG_INFO("All active Redis connections finished" );
                        return;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            }

            LOG_WARNING("Timeout waiting for active Redis connections to finish, proceeding with restart" );
        }

        void RedisPool::closeAllConnections() {
            std::lock_guard<std::mutex> lock(pool_mutex_);

            LOG_INFO("Closing all Redis connections" );

            // 🎯 安全关闭空闲连接（避免重复释放）
            std::vector<std::shared_ptr<RedisConnection>> idle_conns_to_close;
            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();
                if (conn) {
                    idle_conns_to_close.push_back(conn);
                }
            }
            
            // 在锁外关闭连接，避免死锁
            {
                // 临时释放锁
                std::unique_lock<std::mutex> temp_lock(pool_mutex_);
                temp_lock.unlock();
                
                for (auto& conn : idle_conns_to_close) {
                    try {
                        if (conn && conn.use_count() > 1) {  // 确保不是最后一个引用
                            conn->disconnect();
                        }
                    } catch (const std::exception& e) {
                        std::cout << "[RedisPool] Error closing idle connection: " << e.what() << std::endl;
                    }
                }
                
                temp_lock.lock();
            }

            // 🎯 安全关闭活跃连接
            std::vector<std::shared_ptr<RedisConnection>> active_conns_to_close;
            for (auto& conn : active_connections_) {
                if (conn) {
                    active_conns_to_close.push_back(conn);
                }
            }
            active_connections_.clear();
            
            // 在锁外关闭连接
            {
                std::unique_lock<std::mutex> temp_lock(pool_mutex_);
                temp_lock.unlock();
                
                for (auto& conn : active_conns_to_close) {
                    try {
                        if (conn && conn.use_count() > 1) {  // 确保不是最后一个引用
                            conn->disconnect();
                        }
                    } catch (const std::exception& e) {
                        std::cout << "[RedisPool] Error closing active connection: " << e.what() << std::endl;
                    }
                }
                
                temp_lock.lock();
            }

            LOG_INFO("All Redis connections closed safely" );
        }

        void RedisPool::reloadConfiguration() {
            LOG_INFO("Reloading Redis configuration" );

            try {
                auto new_config = common::config::RedisConfig::fromConfigManager();
                new_config.validate();

                manager_config_ = new_config;

                LOG_INFO("Redis configuration reloaded successfully" );
                LOG_DEBUG("New Redis host: " + manager_config_.redis_host + ":" + std::to_string(manager_config_.redis_port) );

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to reload Redis configuration: " + std::string(e.what()) );
                throw;
            }
        }

        bool RedisPool::reinitializePool() {
            std::lock_guard<std::mutex> lock(pool_mutex_);

            try {
                LOG_INFO("Reinitializing Redis pool with new configuration" );

                // 创建初始连接
                for (size_t i = 0; i < manager_config_.redis_pool_size; ++i) {
                    auto conn = createConnection();
                    if (!conn || !conn->connect()) {
                        LOG_ERROR("Failed to create initial Redis connection " + std::to_string(i + 1) );
                        return false;
                    }
                    idle_connections_.push(conn);
                }

                LOG_INFO("Redis pool reinitialized with " + std::to_string(manager_config_.redis_pool_size) + " connections" );
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception while reinitializing Redis pool: " + std::string(e.what()) );
                return false;
            }
        }

        size_t RedisPool::getActiveConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return active_connections_.size();
        }

        size_t RedisPool::getIdleConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return idle_connections_.size();
        }

        size_t RedisPool::getTotalConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return active_connections_.size() + idle_connections_.size();
        }

        // ==================== 线程池集成实现 ====================

        void RedisPool::initializeThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (thread_pool_initialized_.load()) {
                LOG_WARNING("Redis连接池线程池已经初始化，跳过重复初始化");
                return;
            }

            if (!manager_config_.enable_async_operations) {
                LOG_INFO("Redis连接池异步操作未启用，跳过线程池初始化");
                return;
            }

            try {
                // 创建线程池配置
                common::config::ThreadPoolConfig thread_config;
                thread_config.core_pool_size = manager_config_.thread_pool_core_size;
                thread_config.maximum_pool_size = manager_config_.thread_pool_max_size;
                thread_config.keep_alive_time_ms = manager_config_.thread_pool_keep_alive_ms;
                thread_config.queue_capacity = manager_config_.thread_pool_queue_capacity;
                thread_config.enable_monitoring = manager_config_.thread_pool_enable_monitoring;
                thread_config.thread_name_prefix = "redis-pool-";

                // 验证配置
                thread_config.validate();

                // 创建主线程池
                main_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(thread_config);

                // 创建重启线程池（较小的线程池，专门用于重启操作）
                common::config::ThreadPoolConfig restart_config;
                restart_config.core_pool_size = 1;
                restart_config.maximum_pool_size = 2;
                restart_config.keep_alive_time_ms = 30000;
                restart_config.queue_capacity = 100;
                restart_config.enable_monitoring = false;
                restart_config.thread_name_prefix = "redis-restart-";

                restart_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(restart_config);

                thread_pool_initialized_.store(true);

                LOG_INFO("Redis连接池线程池初始化成功 - 主线程池(" +
                        std::to_string(manager_config_.thread_pool_core_size) + "/" +
                        std::to_string(manager_config_.thread_pool_max_size) + "), 重启线程池(1/2)");

            } catch (const std::exception& e) {
                LOG_ERROR("Redis连接池线程池初始化失败: " + std::string(e.what()));
                thread_pool_initialized_.store(false);
                throw;
            }
        }

        void RedisPool::shutdownThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load()) {
                return;
            }

            // 避免在关闭时使用日志系统，使用标准输出
            std::cout << "[RedisPool] 开始关闭Redis连接池线程池" << std::endl;

            try {
                // 关闭主线程池
                if (main_thread_pool_) {
                    main_thread_pool_->shutdown();
                    main_thread_pool_.reset();
                }

                // 关闭重启线程池
                if (restart_thread_pool_) {
                    restart_thread_pool_->shutdown();
                    restart_thread_pool_.reset();
                }

                thread_pool_initialized_.store(false);
                std::cout << "[RedisPool] Redis连接池线程池已关闭" << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "[RedisPool] 关闭Redis连接池线程池时发生异常: " << e.what() << std::endl;
            }
        }

        std::string RedisPool::getThreadPoolStatus() const {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                return "线程池未初始化或已关闭";
            }

            std::ostringstream status;
            status << "Redis连接池线程池状态:\n";
            status << "  异步操作: " << (manager_config_.enable_async_operations ? "启用" : "禁用") << "\n";
            status << "  主线程池:\n";
            status << "    活跃线程数: " << main_thread_pool_->getActiveThreadCount() << "\n";
            status << "    总线程数: " << main_thread_pool_->getTotalThreadCount() << "\n";
            status << "    队列大小: " << main_thread_pool_->getQueueSize() << "\n";
            status << "    核心线程数: " << manager_config_.thread_pool_core_size << "\n";
            status << "    最大线程数: " << manager_config_.thread_pool_max_size << "\n";

            if (restart_thread_pool_) {
                status << "  重启线程池:\n";
                status << "    活跃线程数: " << restart_thread_pool_->getActiveThreadCount() << "\n";
                status << "    总线程数: " << restart_thread_pool_->getTotalThreadCount() << "\n";
                status << "    队列大小: " << restart_thread_pool_->getQueueSize() << "\n";
            }

            return status.str();
        }

        std::future<std::vector<std::shared_ptr<RedisConnection>>> RedisPool::createConnectionsAsync(size_t count) {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                // 如果线程池未初始化，返回一个立即完成的future
                std::promise<std::vector<std::shared_ptr<RedisConnection>>> promise;
                promise.set_value(std::vector<std::shared_ptr<RedisConnection>>());
                return promise.get_future();
            }

            return main_thread_pool_->submit([this, count]() -> std::vector<std::shared_ptr<RedisConnection>> {
                std::vector<std::shared_ptr<RedisConnection>> connections;
                connections.reserve(count);

                LOG_DEBUG("异步创建 " + std::to_string(count) + " 个Redis连接");

                for (size_t i = 0; i < count; ++i) {
                    try {
                        auto conn = createConnection();
                        if (conn && conn->connect()) {
                            connections.push_back(conn);
                            LOG_DEBUG("异步创建连接成功 " + std::to_string(i + 1) + "/" + std::to_string(count));
                        } else {
                            LOG_WARNING("异步创建连接失败 " + std::to_string(i + 1) + "/" + std::to_string(count));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("异步创建连接异常 " + std::to_string(i + 1) + "/" + std::to_string(count) +
                                 ": " + std::string(e.what()));
                    }
                }

                LOG_INFO("异步连接创建完成，成功创建 " + std::to_string(connections.size()) + "/" + std::to_string(count) + " 个连接");
                return connections;
            });
        }

        void RedisPool::performAsyncHealthCheck() {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("线程池未初始化，跳过异步健康检查");
                return;
            }

            main_thread_pool_->submit([this]() {
                try {
                    LOG_DEBUG("开始异步健康检查");

                    std::vector<std::shared_ptr<RedisConnection>> connections_to_check;

                    // 收集需要检查的连接
                    {
                        std::lock_guard<std::mutex> lock(pool_mutex_);

                        // 检查空闲连接
                        auto temp_queue = idle_connections_;
                        while (!temp_queue.empty()) {
                            connections_to_check.push_back(temp_queue.front());
                            temp_queue.pop();
                        }

                        // 检查活跃连接（复制一份，避免长时间持锁）
                        for (const auto& conn : active_connections_) {
                            connections_to_check.push_back(conn);
                        }
                    }

                    // 执行健康检查
                    size_t healthy_count = 0;
                    size_t unhealthy_count = 0;

                    for (auto& conn : connections_to_check) {
                        if (conn && validateConnection(conn)) {
                            healthy_count++;
                        } else {
                            unhealthy_count++;
                        }
                    }

                    LOG_DEBUG("异步健康检查完成 - 健康连接: " + std::to_string(healthy_count) +
                             ", 不健康连接: " + std::to_string(unhealthy_count));

                } catch (const std::exception& e) {
                    LOG_ERROR("异步健康检查异常: " + std::string(e.what()));
                }
            });
        }

        void RedisPool::cleanupExpiredConnectionsAsync() {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("线程池未初始化，跳过异步连接清理");
                return;
            }

            main_thread_pool_->submit([this]() {
                try {
                    LOG_DEBUG("开始异步清理过期连接");

                    auto now = std::chrono::system_clock::now();
                    size_t cleaned_count = 0;

                    std::lock_guard<std::mutex> lock(pool_mutex_);

                    // 清理空闲连接中的过期连接
                    std::queue<std::shared_ptr<RedisConnection>> temp_queue;

                    while (!idle_connections_.empty()) {
                        auto conn = idle_connections_.front();
                        idle_connections_.pop();

                        if (conn) {
                            auto last_used = conn->getLastUsed();
                            auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_used).count();

                            if (idle_duration > manager_config_.redis_pool_max_idle_time_ms) {
                                // 连接已过期，断开并丢弃
                                conn->disconnect();
                                cleaned_count++;
                                LOG_DEBUG("清理过期空闲连接: " + conn->getConnectionId());
                            } else {
                                // 连接仍然有效，放回队列
                                temp_queue.push(conn);
                            }
                        }
                    }

                    // 将有效连接放回队列
                    idle_connections_ = std::move(temp_queue);

                    if (cleaned_count > 0) {
                        LOG_INFO("异步清理完成，清理了 " + std::to_string(cleaned_count) + " 个过期连接");
                    } else {
                        LOG_DEBUG("异步清理完成，没有发现过期连接");
                    }

                    // 确保最小连接数
                    ensureMinimumConnections();

                } catch (const std::exception& e) {
                    LOG_ERROR("异步连接清理异常: " + std::string(e.what()));
                }
            });
        }

        void RedisPool::warmupPoolAsync(size_t target_size) {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("线程池未初始化，跳过异步预热");
                return;
            }

            main_thread_pool_->submit([this, target_size]() {
                try {
                    LOG_INFO("开始异步预热Redis连接池，目标连接数: " + std::to_string(target_size));

                    size_t current_total = getTotalConnections();
                    if (current_total >= target_size) {
                        LOG_INFO("当前连接数(" + std::to_string(current_total) +
                                ")已达到目标(" + std::to_string(target_size) + ")，无需预热");
                        return;
                    }

                    size_t connections_to_create = target_size - current_total;
                    if (connections_to_create > manager_config_.redis_max_pool_size - current_total) {
                        connections_to_create = manager_config_.redis_max_pool_size - current_total;
                        LOG_WARNING("预热连接数受最大连接数限制，实际创建: " + std::to_string(connections_to_create));
                    }

                    size_t created_count = 0;
                    for (size_t i = 0; i < connections_to_create; ++i) {
                        try {
                            auto conn = createConnection();
                            if (conn && conn->connect()) {
                                std::lock_guard<std::mutex> lock(pool_mutex_);
                                idle_connections_.push(conn);
                                created_count++;
                                LOG_DEBUG("预热创建连接 " + std::to_string(i + 1) + "/" + std::to_string(connections_to_create));
                            }
                        } catch (const std::exception& e) {
                            LOG_WARNING("预热创建连接失败: " + std::string(e.what()));
                        }
                    }

                    LOG_INFO("异步预热完成，成功创建 " + std::to_string(created_count) + "/" +
                            std::to_string(connections_to_create) + " 个连接");

                } catch (const std::exception& e) {
                    LOG_ERROR("异步预热异常: " + std::string(e.what()));
                }
            });
        }


    }
}