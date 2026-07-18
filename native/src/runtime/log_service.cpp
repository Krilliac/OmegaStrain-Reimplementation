#include "omega/runtime/log_service.h"

#include <atomic>
#include <format>
#include <iostream>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool IsKnownSeverity(const LogSeverity severity) noexcept
{
    switch (severity)
    {
    case LogSeverity::Trace:
    case LogSeverity::Debug:
    case LogSeverity::Info:
    case LogSeverity::Warning:
    case LogSeverity::Error:
        return true;
    }
    return false;
}

[[nodiscard]] std::string TruncateBounded(
    const std::string_view value, const std::size_t max_bytes)
{
    if (value.size() <= max_bytes)
        return std::string(value);
    std::string bounded;
    bounded.reserve(max_bytes);
    bounded.assign(value.substr(0, max_bytes - LogService::kTruncationMarker.size()));
    bounded.append(LogService::kTruncationMarker);
    return bounded;
}

[[nodiscard]] std::string SanitizeSingleLine(const std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        sanitized.push_back(byte < 0x20U || byte == 0x7FU ? '?' : character);
    }
    return sanitized;
}
} // namespace

std::string_view LogSeverityName(const LogSeverity severity) noexcept
{
    switch (severity)
    {
    case LogSeverity::Trace:
        return "TRACE";
    case LogSeverity::Debug:
        return "DEBUG";
    case LogSeverity::Info:
        return "INFO";
    case LogSeverity::Warning:
        return "WARNING";
    case LogSeverity::Error:
        return "ERROR";
    }
    return "INVALID";
}

std::string StderrLogSink::FormatLine(const LogRecord& record)
{
    return std::format("[{}] +{}ns {} {}: {}\n", record.sequence,
        record.ticks_since_start.count(), LogSeverityName(record.severity),
        SanitizeSingleLine(record.category), SanitizeSingleLine(record.message));
}

void StderrLogSink::Consume(const LogRecord& record)
{
    std::cerr << FormatLine(record);
}

std::expected<std::unique_ptr<RingLogSink>, std::string> RingLogSink::Create(
    const std::size_t capacity)
{
    if (capacity < kMinCapacity || capacity > kMaxCapacity)
        return std::unexpected(std::format(
            "ring sink capacity must be between {} and {} records", kMinCapacity, kMaxCapacity));
    return std::unique_ptr<RingLogSink>(new RingLogSink(capacity));
}

RingLogSink::RingLogSink(const std::size_t capacity)
    : capacity_(capacity)
{
    records_.reserve(capacity_);
}

void RingLogSink::Consume(const LogRecord& record)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (records_.size() < capacity_)
        records_.push_back(record);
    else
        records_[next_slot_] = record;
    next_slot_ = (next_slot_ + 1U) % capacity_;
    ++consumed_;
}

std::vector<LogRecord> RingLogSink::Snapshot() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogRecord> snapshot;
    snapshot.reserve(records_.size());
    if (records_.size() < capacity_)
    {
        snapshot.assign(records_.begin(), records_.end());
        return snapshot;
    }
    for (std::size_t index = 0; index < capacity_; ++index)
        snapshot.push_back(records_[(next_slot_ + index) % capacity_]);
    return snapshot;
}

std::size_t RingLogSink::capacity() const noexcept
{
    return capacity_;
}

std::uint64_t RingLogSink::consumed_count() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return consumed_;
}

struct LogService::Impl
{
    LogSeverity minimum_severity = LogSeverity::Info;
    std::size_t max_category_bytes = 0;
    std::size_t max_message_bytes = 0;
    std::vector<LogSink*> sinks;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::mutex mutex;
    std::uint64_t next_sequence = 0;
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> sink_failures{0};
};

std::expected<LogService, std::string> LogService::Create(LogServiceConfig config)
{
    if (!IsKnownSeverity(config.minimum_severity))
        return std::unexpected("minimum severity must be a defined LogSeverity value");
    if (config.max_category_bytes < kMinCategoryBytes ||
        config.max_category_bytes > kMaxCategoryBytes)
        return std::unexpected(std::format("category budget must be between {} and {} bytes",
            kMinCategoryBytes, kMaxCategoryBytes));
    if (config.max_message_bytes < kMinMessageBytes ||
        config.max_message_bytes > kMaxMessageBytes)
        return std::unexpected(std::format(
            "message budget must be between {} and {} bytes", kMinMessageBytes, kMaxMessageBytes));
    if (config.sinks.size() > kMaxSinks)
        return std::unexpected(
            std::format("at most {} sinks may be registered at construction", kMaxSinks));
    for (std::size_t outer = 0; outer < config.sinks.size(); ++outer)
    {
        if (config.sinks[outer] == nullptr)
            return std::unexpected("sink registrations must not be null");
        for (std::size_t inner = 0; inner < outer; ++inner)
        {
            if (config.sinks[inner] == config.sinks[outer])
                return std::unexpected("sink registrations must not contain duplicates");
        }
    }

    auto impl = std::make_unique<Impl>();
    impl->minimum_severity = config.minimum_severity;
    impl->max_category_bytes = config.max_category_bytes;
    impl->max_message_bytes = config.max_message_bytes;
    impl->sinks = std::move(config.sinks);
    impl->start = std::chrono::steady_clock::now();
    return LogService(std::move(impl));
}

LogService::LogService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LogService::~LogService() = default;
LogService::LogService(LogService&&) noexcept = default;
LogService& LogService::operator=(LogService&&) noexcept = default;

void LogService::Write(const LogSeverity severity, const std::string_view category,
    const std::string_view message)
{
    if (!impl_)
        return; // moved-from: uncounted no-op (the drop counter lives in the moved state)
    Impl& impl = *impl_;
    if (!IsKnownSeverity(severity) ||
        std::to_underlying(severity) < std::to_underlying(impl.minimum_severity))
    {
        impl.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    LogRecord record;
    record.severity = severity;
    record.category = TruncateBounded(category, impl.max_category_bytes);
    record.message = TruncateBounded(message, impl.max_message_bytes);
    {
        const std::lock_guard<std::mutex> lock(impl.mutex);
        record.sequence = impl.next_sequence++;
        record.ticks_since_start = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - impl.start);
        for (LogSink* const sink : impl.sinks)
        {
            try
            {
                sink->Consume(record);
            }
            catch (...)
            {
                // Sink implementations are extension points. A faulty sink cannot unwind through
                // concurrent writers or prevent the remaining sinks from observing this sequence.
                impl.sink_failures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    impl.written.fetch_add(1, std::memory_order_relaxed);
}

void LogService::Trace(const std::string_view category, const std::string_view message)
{
    Write(LogSeverity::Trace, category, message);
}

void LogService::Debug(const std::string_view category, const std::string_view message)
{
    Write(LogSeverity::Debug, category, message);
}

void LogService::Info(const std::string_view category, const std::string_view message)
{
    Write(LogSeverity::Info, category, message);
}

void LogService::Warning(const std::string_view category, const std::string_view message)
{
    Write(LogSeverity::Warning, category, message);
}

void LogService::Error(const std::string_view category, const std::string_view message)
{
    Write(LogSeverity::Error, category, message);
}

LogSeverity LogService::minimum_severity() const noexcept
{
    return impl_ ? impl_->minimum_severity : LogSeverity::Info;
}

std::size_t LogService::max_category_bytes() const noexcept
{
    return impl_ ? impl_->max_category_bytes : 0U;
}

std::size_t LogService::max_message_bytes() const noexcept
{
    return impl_ ? impl_->max_message_bytes : 0U;
}

std::size_t LogService::sink_count() const noexcept
{
    return impl_ ? impl_->sinks.size() : 0U;
}

std::uint64_t LogService::written_count() const noexcept
{
    return impl_ ? impl_->written.load(std::memory_order_relaxed) : 0U;
}

std::uint64_t LogService::dropped_count() const noexcept
{
    return impl_ ? impl_->dropped.load(std::memory_order_relaxed) : 0U;
}

std::uint64_t LogService::sink_failure_count() const noexcept
{
    return impl_ ? impl_->sink_failures.load(std::memory_order_relaxed) : 0U;
}
} // namespace omega::runtime
