#pragma once

#include <leveldb/db.h>
#include <leveldb/options.h> // Include for WriteOptions
#include <leveldb/slice.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <memory>
#include <mutex>
#include <string>

namespace native_ac {
class LevelDBService {
public:
    // 获取单例实例
    static LevelDBService& GetInstance();

    // 初始化数据库
    // db_path: 数据库文件存放路径 (例如 "data/leveldb")
    // 返回 leveldb::Status 指示操作结果。如果目录不存在，会尝试创建。
    leveldb::Status Init(const std::string& db_path);

    // 关闭数据库
    void Shutdown();

    // 获取键值
    // key: 要获取的键
    // value: 用于存储获取到的值的指针
    // 返回 leveldb::Status 指示操作结果 (例如 status.IsNotFound())
    leveldb::Status Get(const leveldb::Slice& key, std::string* value);

    // 写入键值对 (使用默认写入选项)
    // key: 要写入的键
    // value: 要写入的值
    // 返回 leveldb::Status 指示操作结果
    leveldb::Status Put(const leveldb::Slice& key, const leveldb::Slice& value);

    // 删除键 (使用默认写入选项)
    // key: 要删除的键
    // 返回 leveldb::Status 指示操作结果
    leveldb::Status Delete(const leveldb::Slice& key);

    // 执行批量写入/删除操作
    // options: 写入选项 (例如设置 sync)
    // updates: 包含 Put 和 Delete 操作的 WriteBatch
    // 返回 leveldb::Status 指示操作结果
    leveldb::Status Write(const leveldb::WriteOptions& options, leveldb::WriteBatch* updates);

    // 检查服务是否已初始化
    bool IsInitialized() const;

    // 禁止拷贝和赋值
    LevelDBService(const LevelDBService&)            = delete;
    LevelDBService& operator=(const LevelDBService&) = delete;

private:
    // 私有构造函数
    LevelDBService() = default;
    // 私有析构函数，通过 Shutdown 管理资源释放
    ~LevelDBService(); // Need definition to handle unique_ptr<DB>

    std::unique_ptr<leveldb::DB>           db_;
    std::string                            db_path_;
    bool                                   initialized_ = false;
    static LevelDBService* instance_;
    static std::once_flag                  once_flag_; // For thread-safe singleton initialization
};
} // namespace native_ac