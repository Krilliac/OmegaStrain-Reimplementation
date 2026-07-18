#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace omega::runtime
{
// Severity ordering is ascending: Trace is the most verbose, Error the most severe. The
// numeric spacing is a synthetic-shell choice with no retail meaning.
enum class LogSeverity : std::uint8_t
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
};

// [any thread; reentrant] Returns a fixed uppercase ASCII name; "INVALID" for unknown values.
[[nodiscard]] std::string_view LogSeverityName(LogSeverity severity) noexcept;

// One emitted record. Category and message are already bounded by the service budgets: a
// truncated field is exactly the budget size and ends with the explicit "..." marker.
// Sequence numbers are per-service, start at zero, and count only emitted records.
struct LogRecord
{
    std::uint64_t sequence = 0;
    std::chrono::nanoseconds ticks_since_start{0};
    LogSeverity severity = LogSeverity::Info;
    std::string category;
    std::string message;
};

// Sink contract. The owning LogService serializes Consume() calls under its internal write
// lock and delivers records in strictly ascending sequence order.
class LogSink
{
public:
    virtual ~LogSink() = default;

    // [any thread; serialized by the owning service] Must not call back into the owning
    // LogService and must not block indefinitely; every writer waits behind this call. Exceptions
    // are contained by the service, counted as sink failures, and do not prevent later sinks from
    // receiving the same record.
    virtual void Consume(const LogRecord& record) = 0;

protected:
    LogSink() = default;
    LogSink(const LogSink&) = default;
    LogSink& operator=(const LogSink&) = default;
    LogSink(LogSink&&) = default;
    LogSink& operator=(LogSink&&) = default;
};

// Writes one deterministic single line per record to stderr. Control bytes (0x00-0x1F, 0x7F)
// in the category or message are replaced with '?' so a record can never split the line.
class StderrLogSink final : public LogSink
{
public:
    // [any thread; reentrant] Pure formatting with no I/O; exposed so tests can pin the
    // exact line bytes: "[<sequence>] +<ticks>ns <SEVERITY> <category>: <message>\n".
    [[nodiscard]] static std::string FormatLine(const LogRecord& record);

    // [any thread; serialized by the owning service] Streams FormatLine() to stderr.
    void Consume(const LogRecord& record) override;
};

// Bounded in-memory ring for tests and the future debug UI: fixed capacity chosen at
// creation, overwrite-oldest once full, snapshot access with owned copies.
class RingLogSink final : public LogSink
{
public:
    // Synthetic-shell hard budgets for validation, not retail claims.
    static constexpr std::size_t kMinCapacity = 1;
    static constexpr std::size_t kMaxCapacity = 65536;

    // [any thread; reentrant] Rejects capacities outside [kMinCapacity, kMaxCapacity]. The
    // caller owns the sink and must keep it alive for the whole LogService lifetime.
    [[nodiscard]] static std::expected<std::unique_ptr<RingLogSink>, std::string> Create(
        std::size_t capacity);

    // [any thread; serialized by the owning service] Stores an owned copy; once the ring is
    // full the oldest retained record is overwritten.
    void Consume(const LogRecord& record) override;

    // [any thread; thread-safe] Returns owned copies of the retained window, oldest first.
    // The result never aliases ring storage and later writes do not mutate it.
    [[nodiscard]] std::vector<LogRecord> Snapshot() const;

    // [any thread; reentrant] Fixed capacity in records.
    [[nodiscard]] std::size_t capacity() const noexcept;

    // [any thread; thread-safe] Total records ever consumed, including overwritten ones.
    [[nodiscard]] std::uint64_t consumed_count() const;

private:
    explicit RingLogSink(std::size_t capacity);

    mutable std::mutex mutex_;
    std::size_t capacity_ = 0;
    std::size_t next_slot_ = 0;
    std::uint64_t consumed_ = 0;
    std::vector<LogRecord> records_;
};

// Fixed after Create(); the defaults are synthetic-shell choices, not retail behavior claims.
struct LogServiceConfig
{
    // Records below this floor are dropped cheaply: no truncation, no lock, no sink call.
    LogSeverity minimum_severity = LogSeverity::Info;
    // Byte budgets for the two record strings; oversized input is truncated deterministically
    // so the stored field is exactly the budget size and ends with the "..." marker.
    std::size_t max_category_bytes = 32;
    std::size_t max_message_bytes = 512;
    // Non-owning registrations, fixed for the service lifetime so the hot path stays
    // lock-simple. The owner (the future OmegaApp) guarantees every sink outlives the
    // service. Null pointers, duplicates, and more than kMaxSinks entries are rejected.
    std::vector<LogSink*> sinks;
};

// Thread-safe bounded logging service owned by the future OmegaApp via unique_ptr. The
// steady-clock epoch starts at Create(); record timestamps are assigned under the write lock,
// so ticks are non-decreasing in sequence order.
class LogService final
{
public:
    // Synthetic-shell hard budgets for validation, not retail claims.
    static constexpr std::size_t kMinCategoryBytes = 4;
    static constexpr std::size_t kMaxCategoryBytes = 256;
    static constexpr std::size_t kMinMessageBytes = 8;
    static constexpr std::size_t kMaxMessageBytes = 65536;
    static constexpr std::size_t kMaxSinks = 8;
    static constexpr std::string_view kTruncationMarker = "...";

    // [game thread] Validates every configured bound and starts the steady-clock epoch.
    [[nodiscard]] static std::expected<LogService, std::string> Create(LogServiceConfig config);

    // [game thread, after all writers have stopped]
    ~LogService();

    // [game thread] Moving is permitted only while no other thread holds a reference to the
    // source object. The moved-from service keeps no sinks or counters: Write() and the
    // convenience overloads become uncounted no-ops (the drop counter lives in the moved
    // state), and every accessor returns zero / its severity floor default.
    LogService(LogService&&) noexcept;
    LogService& operator=(LogService&&) noexcept;
    LogService(const LogService&) = delete;
    LogService& operator=(const LogService&) = delete;

    // [any thread; thread-safe] Drops records below the severity floor (and unknown severity
    // values) without formatting; otherwise truncates to the byte budgets, assigns the next
    // sequence number and timestamp under the write lock, and delivers to every sink.
    void Write(LogSeverity severity, std::string_view category, std::string_view message);

    // [any thread; thread-safe] Convenience overload set forwarding to Write().
    void Trace(std::string_view category, std::string_view message);
    void Debug(std::string_view category, std::string_view message);
    void Info(std::string_view category, std::string_view message);
    void Warning(std::string_view category, std::string_view message);
    void Error(std::string_view category, std::string_view message);

    // [any thread; immutable after Create()]
    [[nodiscard]] LogSeverity minimum_severity() const noexcept;
    [[nodiscard]] std::size_t max_category_bytes() const noexcept;
    [[nodiscard]] std::size_t max_message_bytes() const noexcept;
    [[nodiscard]] std::size_t sink_count() const noexcept;

    // [any thread; thread-safe] Records dispatched to the configured sink set / dropped below the
    // floor. A record remains written when one sink throws because another sink may already have
    // consumed it; sink_failure_count() reports failed individual deliveries separately.
    [[nodiscard]] std::uint64_t written_count() const noexcept;
    [[nodiscard]] std::uint64_t dropped_count() const noexcept;
    [[nodiscard]] std::uint64_t sink_failure_count() const noexcept;

private:
    struct Impl;
    explicit LogService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::runtime
