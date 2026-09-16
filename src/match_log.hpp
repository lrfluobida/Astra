#pragma once

#include "session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace astra {

class MatchLogger {
public:
    explicit MatchLogger(std::ostream& output,
                         std::size_t chunk_bytes = 2048,
                         std::size_t maximum_payload_bytes = 256 * 1024);

    void log_round(const Json::Value& request,
                   const Json::Value& response,
                   const SessionDiagnostics& before,
                   const SessionDiagnostics& after,
                   std::chrono::microseconds elapsed);

private:
    struct Fingerprint {
        std::uint64_t first = 0;
        std::uint64_t second = 0;
        std::size_t bytes = 0;
    };

    Fingerprint fingerprint(const std::string& value) const;
    bool same_fingerprint(const Fingerprint& left, const Fingerprint& right) const;
    void emit(Json::Value event);
    void emit_text(std::uint64_t parent_sequence,
                   int ordinal,
                   const std::string& kind,
                   const std::string& content,
                   const Json::Value& metadata = Json::Value(Json::objectValue));

    std::ostream& output_;
    std::size_t chunk_bytes_;
    std::size_t maximum_payload_bytes_;
    std::uint64_t sequence_ = 0;
    bool has_last_request_ = false;
    Fingerprint last_request_;
    std::uint64_t last_generation_ = 0;
    int last_round_ = 0;
    bool has_last_task_ = false;
    Fingerprint last_task_;
    std::uint64_t last_task_generation_ = 0;
};

}  // namespace astra
