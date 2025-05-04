#include "LevelDBService.hpp"
#include <filesystem> // 用于创建目录，虽然 leveldb::Options::create_if_missing 通常足够
#include <iostream>   // 使用 spdlog 替换

namespace native_ac {
// 定义静态成员变量
LevelDBService* LevelDBService::instance_ = nullptr;
std::once_flag                  LevelDBService::once_flag_;

LevelDBService& LevelDBService::GetInstance() {
    std::call_once(once_flag_, []() {
        // 使用 make_unique 和私有构造函数
        instance_ = new LevelDBService();
    });
    return *instance_;
}

LevelDBService::~LevelDBService() {
    Shutdown(); // 确保在对象销毁时关闭数据库
}

leveldb::Status LevelDBService::Init(const std::string& db_path) {
    if (initialized_) {
        // 可以选择返回错误或记录警告，这里简单返回 OK 表示已初始化
        return leveldb::Status::OK();
    }

    db_path_ = db_path;
    // 主动创建数据库目录
    try {
        if (std::filesystem::create_directories(db_path_)) {
            std::cout << "[LevelDBService] 创建数据库目录成功: " << db_path_ << std::endl;
        } else {
            std::cout << "[LevelDBService] 数据库目录已存在: " << db_path_ << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[LevelDBService] 创建数据库目录失败: " << db_path_ << "，异常: " << e.what() << std::endl;
    }
    leveldb::Options options;
    options.create_if_missing = true; // 如果数据库不存在则创建

    // 尝试打开数据库
    leveldb::DB*    db_ptr = nullptr;
    leveldb::Status status = leveldb::DB::Open(options, db_path_, &db_ptr);

    if (status.ok()) {
        db_.reset(db_ptr); // 将原始指针交给 unique_ptr 管理
        initialized_ = true;
        // std::cout << "LevelDB initialized successfully at: " << db_path_ << std::endl; // 替换为日志
    } else {
        // std::cerr << "Failed to initialize LevelDB: " << status.ToString() << std::endl; // 替换为日志
        db_.reset(); // 确保失败时指针为空
        initialized_ = false;
    }

    return status;
}

void LevelDBService::Shutdown() {
    if (initialized_) {
        db_.reset(); // unique_ptr 会自动删除管理的 DB 对象
        initialized_ = false;
        // std::cout << "LevelDB shutdown." << std::endl; // 替换为日志
    }
}

leveldb::Status LevelDBService::Get(const leveldb::Slice& key, std::string* value) {
    if (!initialized_) {
        return leveldb::Status::IOError("LevelDBService not initialized");
    }
    leveldb::ReadOptions options;
    return db_->Get(options, key, value);
}

leveldb::Status LevelDBService::Put(const leveldb::Slice& key, const leveldb::Slice& value) {
    if (!initialized_) {
        return leveldb::Status::IOError("LevelDBService not initialized");
    }
    leveldb::WriteOptions options;
    // options.sync = true; // 可选：确保写入磁盘，但会影响性能
    return db_->Put(options, key, value);
}

leveldb::Status LevelDBService::Delete(const leveldb::Slice& key) {
    if (!initialized_) {
        return leveldb::Status::IOError("LevelDBService not initialized");
    }
    leveldb::WriteOptions options;
    // options.sync = true; // 可选
    return db_->Delete(options, key);
}

leveldb::Status LevelDBService::Write(const leveldb::WriteOptions& options, leveldb::WriteBatch* updates) {
    if (!initialized_) {
        return leveldb::Status::IOError("LevelDBService not initialized");
    }
    return db_->Write(options, updates);
}

bool LevelDBService::IsInitialized() const { return initialized_; }
} // namespace native_ac