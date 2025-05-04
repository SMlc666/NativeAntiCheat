# LevelDB 存储方案设计

## 1. 概述

本文档定义了 NativeAntiCheat 模块使用 LevelDB 进行数据持久化的设计方案。目标是提供一个高效、可扩展且易于维护的存储机制，支持 Ban、Mute 及未来其他模块的数据存储需求。核心设计包括统一的 Key 结构、标准化的 Value 格式、过期数据处理机制以及一个共享的 LevelDB 访问封装层。

## 2. 核心设计原则

*   **共享数据库实例:** 所有模块通过一个共享的 `LevelDBService` 访问同一个 LevelDB 数据库实例，以提高资源效率和简化管理。
*   **逻辑隔离:** 使用唯一的 Key 前缀来区分不同模块和数据类型，确保数据在逻辑上隔离。
*   **开发阶段优化:** 优先考虑开发和调试阶段的便利性，采用 JSON 作为 Value 格式。
*   **可扩展性:** 设计应易于添加新的数据类型和模块。

## 3. Key 结构设计

使用结构化的 Key 来优化查找和排序。

### 3.1. 主数据 Key (Primary Data Key)

*   **用途:** 通过唯一标识符（IP、玩家名等）快速查找对应的数据详情。
*   **格式:** `<type_prefix>:<subtype_prefix>:<identifier>`
*   **前缀定义:**
    *   `b`: Ban
    *   `m`: Mute
    *   `i`: IP Address
    *   `n`: Player Name
    *   *(未来可添加其他模块前缀，如 `s:` for stats)*
*   **示例:**
    *   IP Ban: `b:i:192.168.1.100`
    *   Name Ban: `b:n:steve` (假设 identifier 已规范化为小写)
    *   Mute: `m:n:alex`
*   **规范化:** 存入数据库前，`<identifier>` 必须进行规范化处理（例如，IP 地址统一格式，玩家名称统一小写）。

### 3.2. 过期索引 Key (Expiration Index Key)

*   **用途:** 高效扫描和查找已过期的记录，利用 LevelDB 按 Key 排序的特性。
*   **格式:** `e:<type_prefix>:<subtype_prefix>:<formatted_expiration_time>:<identifier>`
*   **前缀定义:**
    *   `e`: Expiry Index
    *   其他同上 (`b`, `m`, `i`, `n`)
*   **`<formatted_expiration_time>`:**
    *   将 `std::chrono::system_clock::time_point` 转换为 `uint64_t` 类型的 **毫秒** 时间戳。
    *   将此 `uint64_t` 时间戳转换为 **8 字节、大端序 (Big-Endian)** 的二进制表示。这确保了 LevelDB 按 Key 字典序排序时，时间戳早的 Key 排在前面。
*   **示例** (假设时间戳 1714728000000ms 对应 8 字节大端序 `\x00\x00\x01\x8F\x1B\xA8\x1E\x00`):
    *   IP Ban Expiry: `e:b:i:\x00\x00\x01\x8F\x1B\xA8\x1E\x00:192.168.1.100`
    *   Name Ban Expiry: `e:b:n:\x00\x00\x01\x8F\x1B\xA8\x1E\x00:steve`
    *   Mute Expiry: `e:m:n:\x00\x00\x01\x8F\x1B\xA8\x1E\x00:alex`
*   **Value:** 此 Key 对应的 Value 应为空字符串 `""`，以节省空间。

## 4. Value 格式设计 (用于主数据 Key)

*   **用途:** 存储与主数据 Key 关联的具体信息（如过期时间、原因等）。
*   **推荐格式:** **JSON 字符串**
    *   **示例:** `{"e": 1714728000000, "r": "Griefing", "admin": "AdminUser"}` (字段名尽量简短)
    *   **字段:**
        *   `e`: expiration\_time (uint64_t 毫秒时间戳)
        *   `r`: reason (string)
        *   *(可根据需要灵活添加其他字段)*
    *   **理由:** 在开发和 DEBUG 阶段，JSON 提供极佳的可读性、灵活性和调试便利性。易于扩展添加新字段。性能和空间开销在当前阶段通常可接受。
*   **备选格式:** 自定义二进制格式（性能/空间更优，但扩展性差，不推荐在开发阶段使用）。

## 5. 操作流程

### 5.1. 添加记录 (例如 Ban)

1.  获取 `identifier`, `reason`, `expiration_time` 及其他信息。
2.  规范化 `identifier`。
3.  构造 JSON Value 对象。
4.  序列化 JSON 对象为字符串 `value_str`。
5.  通过 `LevelDBService` 生成 `primary_key` 和 `expiry_key`。
6.  使用 `LevelDBService` 的批量写入功能 (或单独 Put)：
    *   `Put(primary_key, value_str)`
    *   `Put(expiry_key, "")`

### 5.2. 查询记录 (例如查询 Name Ban)

1.  获取 `player_name`。
2.  规范化 `player_name`。
3.  通过 `LevelDBService` 生成 `primary_key = "b:n:" + normalized_name`。
4.  调用 `LevelDBService::get(primary_key)`。
5.  如果返回 `std::optional<nlohmann::json>` 包含值：
    *   解析 JSON 获取 `expiration_time` 和 `reason`。
    *   检查 `expiration_time` 是否已过期。如果过期，应视为无效并可触发删除。

### 5.3. 删除记录

1.  需要知道 `identifier` 和原始的 `expiration_time` (用于构造正确的 `expiry_key`)。
2.  规范化 `identifier`。
3.  通过 `LevelDBService` 生成 `primary_key` 和 `expiry_key`。
4.  使用 `LevelDBService` 的批量写入功能 (或单独 Delete)：
    *   `Delete(primary_key)`
    *   `Delete(expiry_key)`

### 5.4. 清理过期记录 (后台任务)

1.  应由一个后台任务（例如通过 `TickScheduler` 定时触发）调用 `LevelDBService::cleanupExpired`。
2.  `cleanupExpired` 内部逻辑：
    a.  获取当前时间 `current_time`，格式化为 `<formatted_current_time>`。
    b.  对需要清理的每种类型 (`b:i`, `b:n`, `m:n` 等) 执行范围扫描：
        i.  起始 Key: `e:<type>:<subtype>:\x00...`
        ii. 结束 Key: `e:<type>:<subtype>:<formatted_current_time>`
    c.  使用 LevelDB 迭代器遍历此范围内的 `expiry_key`。
    d.  对于每个找到的 `expiry_key`：
        i.  解析出 `identifier`。
        ii. 构造对应的 `primary_key`。
        iii. 将 `expiry_key` 和 `primary_key` 添加到 `leveldb::WriteBatch` 中准备删除。
    e.  执行 `WriteBatch` 批量删除。

## 6. 共享 LevelDB 封装层 (`LevelDBService`)

为了简化模块开发、确保一致性并管理数据库实例，建议实现一个共享的封装层。

### 6.1. 职责

*   管理唯一的 LevelDB 数据库实例（打开、关闭、配置）。
*   提供单例访问或通过依赖注入提供服务。
*   封装 Key 生成逻辑。
*   封装 Value 的 JSON 序列化/反序列化。
*   提供简洁的 CRUD (Create, Read, Update, Delete) 和批量操作接口。
*   提供过期记录清理的辅助功能。
*   统一错误处理（例如返回 `bool`+日志 或 `std::optional`）。

### 6.2. 概念性接口 (示例)

```c++
// Conceptual Header: src/mod/Storage/LevelDBService.hpp
namespace native_ac::storage {
class LevelDBService {
public:
    static LevelDBService& getInstance();
    bool initialize(const std::string& path);
    void shutdown();

    std::string createPrimaryKey(const std::string& type, const std::string& subtype, const std::string& identifier);
    std::string createExpiryKey(const std::string& type, const std::string& subtype, const std::string& identifier, const std::chrono::system_clock::time_point& expiration);

    bool put(const std::string& key, const nlohmann::json& value);
    std::optional<nlohmann::json> get(const std::string& key);
    bool remove(const std::string& key);

    // Allows direct batch creation for complex transactions
    leveldb::WriteBatch createWriteBatch(); 
    bool applyBatch(const leveldb::WriteBatch& batch);

    // Callback might be simplified if batch deletion is preferred
    bool cleanupExpired(const std::string& type, const std::string& subtype, leveldb::WriteBatch& batchToDelete); 
};
}
```

### 6.3. 放置位置

建议放在新的共享目录，如 `src/mod/Storage/`。

## 7. 未来考虑

*   **性能优化:** 如果在项目后期性能分析显示 JSON 处理成为瓶颈，且数据结构已稳定，可以考虑切换到二进制 Value 格式。
*   **错误处理细化:** 根据项目需求细化错误处理策略。
*   **封装层完善:** 逐步完善 `LevelDBService` 的功能和接口。