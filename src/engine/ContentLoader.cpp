#include "ContentLoader.h"

#include <ArduinoJson.h>
#include <SD.h>

namespace {
bool isJsonFile(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".json");
}

bool isDirectory(File& file) {
    return file && file.isDirectory();
}
}

bool ContentLoader::begin(Board& board) {
    board_ = &board;
    scan();
    return sdReady();
}

bool ContentLoader::sdReady() const {
    return board_ != nullptr && board_->sdReady();
}

void ContentLoader::scan() {
    (void)sdReady();
}

String ContentLoader::absoluteEntryPath(const String& directory, const String& entryName) const {
    if (entryName.startsWith("/")) {
        return entryName;
    }
    return directory + "/" + entryName;
}

bool ContentLoader::loadMemoryConfig(MemoryConfig& config) const {
    config = MemoryConfig{};
    if (!sdReady()) {
        return false;
    }

    String path = "/games/memory/default.json";
    if (!SD.exists(path)) {
        File dir = SD.open("/games/memory");
        if (!isDirectory(dir)) {
            if (dir) {
                dir.close();
            }
            return false;
        }
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) {
                break;
            }
            if (!entry.isDirectory()) {
                const String candidate = absoluteEntryPath("/games/memory", entry.name());
                if (isJsonFile(candidate)) {
                    path = candidate;
                    entry.close();
                    break;
                }
            }
            entry.close();
        }
        dir.close();
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        return false;
    }

    config.rows = constrain(static_cast<int>(doc["rows"] | 4), 2, MAX_MEMORY_ROWS);
    config.cols = constrain(static_cast<int>(doc["cols"] | 6), 2, MAX_MEMORY_COLS);
    if ((config.rows * config.cols) % 2 != 0) {
        config.rows = 4;
        config.cols = 6;
    }
    if (config.rows == 4 && config.cols == 4) {
        config.cols = 6;
    }
    config.pairCount = min<uint8_t>(MAX_MEMORY_PAIRS, (config.rows * config.cols) / 2);

    JsonArray symbols = doc["symbols"].as<JsonArray>();
    uint8_t index = 0;
    for (const char* symbol : symbols) {
        if (index >= config.pairCount) {
            break;
        }
        config.symbols[index++] = symbol;
    }
    return true;
}

bool ContentLoader::loadCountingConfig(CountingConfig& config) const {
    config = CountingConfig{};
    if (!sdReady() || !SD.exists("/games/counting/default.json")) {
        return false;
    }
    File file = SD.open("/games/counting/default.json", FILE_READ);
    if (!file) {
        return false;
    }
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        return false;
    }
    config.minCount = constrain(static_cast<int>(doc["min"] | 1), 1, 20);
    config.maxCount = constrain(static_cast<int>(doc["max"] | 10), config.minCount, 20);
    return true;
}
