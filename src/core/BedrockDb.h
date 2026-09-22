#pragma once

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace justnbt {
class BedrockDb {
public:
    static std::shared_ptr<BedrockDb> shared(const QString& dbDir);
    static bool isOpenInThisProcess(const QString& dbDir);

    explicit BedrockDb(const QString& dbDir);
    ~BedrockDb();

    BedrockDb(const BedrockDb&) = delete;
    BedrockDb& operator=(const BedrockDb&) = delete;

    std::optional<std::string> get(std::string_view key) const;
    void put(std::string_view key, std::string_view value);

    using Change = std::pair<std::string, std::optional<std::string>>;
    void apply(const std::vector<Change>& changes);

    void compactAll();

    void forEachWithPrefix(std::string_view prefix,
                           const std::function<bool(std::string_view key, std::string_view value)>& fn) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
