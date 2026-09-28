#include "core/sentinel_scanner.h"
#include <utility>

// Store sentinel we're searching for; pending_ starts empty by default
SentinelScanner::SentinelScanner(std::string sentinel)
    : sentinel_(std::move(sentinel)) {}

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    // Search for pending_ + new chunk
    std::string combined = pending_ + std::string(chunk);

    std::size_t pos = combined.find(sentinel_);
    if (pos != std::string::npos) {
        // Sentinel is found, anything after it is discarded
        std::string safe = combined.substr(0, pos);
        pending_.clear();
        return  {
            safe, true
        };
    }

    // Sentinel not found yet, so holdback the last sentinel_.size() - 1 character
    // in case they're the start of the sentinel
    std::size_t hold_back = sentinel_.size() - 1;
    if (combined.size() > hold_back) {
        std::size_t safe_len = combined.size() - hold_back;
        std::string safe = combined.substr(0, safe_len);
        pending_ = combined.substr(safe_len);
        return {
            safe, false
        };
    } else {
        // Combined can still become the start of the sentinel
        pending_ = combined;
        return {
            std::string(), false
        };
    }
}

SentinelScanner::Out SentinelScanner::flush() {
    // Stream is over, so it's all safe to release
    std::string safe = pending_;
    pending_.clear();
    return {
        safe, false
    };
}