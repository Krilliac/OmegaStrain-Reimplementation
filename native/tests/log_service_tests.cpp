#include "omega/runtime/log_service.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

using omega::runtime::LogRecord;
using omega::runtime::LogService;
using omega::runtime::LogServiceConfig;
using omega::runtime::LogSeverity;
using omega::runtime::LogSink;
using omega::runtime::RingLogSink;
using omega::runtime::StderrLogSink;

// Consume() is serialized by the owning service, so no local lock is needed while writers
// run; validation reads happen only after every writer has joined.
struct CaptureSink final : LogSink
{
    std::vector<LogRecord> records;

    void Consume(const LogRecord& record) override
    {
        records.push_back(record);
    }
};

[[nodiscard]] LogServiceConfig ConfigWith(LogSink* const sink)
{
    LogServiceConfig config;
    config.minimum_severity = LogSeverity::Trace;
    if (sink != nullptr)
        config.sinks.push_back(sink);
    return config;
}
} // namespace

int LogServiceFailureCount()
{
    // --- Configuration validation -------------------------------------------------------
    {
        CaptureSink sink;
        Check(LogService::Create(ConfigWith(&sink)).has_value(),
            "a bounded single-sink configuration is accepted");
        Check(LogService::Create(LogServiceConfig{}).has_value(),
            "a sinkless service is valid and still counts records");

        LogServiceConfig config = ConfigWith(&sink);
        config.max_category_bytes = LogService::kMinCategoryBytes - 1U;
        Check(!LogService::Create(config), "a category budget below the minimum is rejected");
        config = ConfigWith(&sink);
        config.max_category_bytes = LogService::kMaxCategoryBytes + 1U;
        Check(!LogService::Create(config), "a category budget above the maximum is rejected");
        config = ConfigWith(&sink);
        config.max_message_bytes = LogService::kMinMessageBytes - 1U;
        Check(!LogService::Create(config), "a message budget below the minimum is rejected");
        config = ConfigWith(&sink);
        config.max_message_bytes = LogService::kMaxMessageBytes + 1U;
        Check(!LogService::Create(config), "a message budget above the maximum is rejected");
        config = ConfigWith(&sink);
        config.minimum_severity = static_cast<LogSeverity>(200);
        Check(!LogService::Create(config), "an undefined severity floor is rejected");
        config = ConfigWith(&sink);
        config.sinks.push_back(nullptr);
        Check(!LogService::Create(config), "a null sink registration is rejected");
        config = ConfigWith(&sink);
        config.sinks.push_back(&sink);
        Check(!LogService::Create(config), "a duplicate sink registration is rejected");

        config = ConfigWith(nullptr);
        std::vector<CaptureSink> too_many(LogService::kMaxSinks + 1U);
        for (CaptureSink& extra : too_many)
            config.sinks.push_back(&extra);
        Check(!LogService::Create(config), "registering more than kMaxSinks is rejected");
        config.sinks.pop_back();
        Check(LogService::Create(config).has_value(), "exactly kMaxSinks sinks are accepted");
    }

    Check(!RingLogSink::Create(0), "a zero-capacity ring sink is rejected");
    Check(!RingLogSink::Create(RingLogSink::kMaxCapacity + 1U),
        "an over-budget ring capacity is rejected");
    Check(RingLogSink::Create(RingLogSink::kMinCapacity).has_value(),
        "the minimum ring capacity is accepted");

    // --- Severity floor filtering -------------------------------------------------------
    {
        CaptureSink sink;
        LogServiceConfig config = ConfigWith(&sink);
        config.minimum_severity = LogSeverity::Warning;
        auto service = LogService::Create(std::move(config));
        Check(service.has_value(), "a Warning-floor service is created");
        if (service)
        {
            service->Trace("floor", "dropped");
            service->Debug("floor", "dropped");
            service->Info("floor", "dropped");
            service->Write(static_cast<LogSeverity>(200), "floor", "undefined severity");
            service->Warning("floor", "kept");
            service->Error("floor", "kept");
            Check(sink.records.size() == 2U, "records below the floor never reach a sink");
            Check(service->written_count() == 2U && service->dropped_count() == 4U,
                "written and dropped counters split at the severity floor");
            Check(sink.records.size() == 2U && sink.records[0].sequence == 0U &&
                      sink.records[1].sequence == 1U,
                "dropped records consume no sequence numbers");
            Check(sink.records.size() == 2U &&
                      sink.records[0].severity == LogSeverity::Warning &&
                      sink.records[1].severity == LogSeverity::Error,
                "convenience overloads carry their named severities");
            Check(service->minimum_severity() == LogSeverity::Warning &&
                      service->sink_count() == 1U,
                "immutable accessors report the validated configuration");
        }
    }

    // --- Truncation budgets at the exact boundary ----------------------------------------
    {
        CaptureSink sink;
        LogServiceConfig config = ConfigWith(&sink);
        config.max_category_bytes = 4;
        config.max_message_bytes = 8;
        auto service = LogService::Create(std::move(config));
        Check(service.has_value(), "minimum truncation budgets are accepted");
        if (service)
        {
            service->Info("ABCD", "12345678");
            service->Info("ABCDE", "123456789");
            service->Info("", "");
            Check(sink.records.size() == 3U, "all truncation probes are emitted");
            if (sink.records.size() == 3U)
            {
                Check(sink.records[0].category == "ABCD" &&
                          sink.records[0].message == "12345678",
                    "fields exactly at the budget are stored unmodified");
                Check(sink.records[1].category == "A..." &&
                          sink.records[1].category.size() == 4U,
                    "an over-budget category truncates to the budget with the marker");
                Check(sink.records[1].message == "12345..." &&
                          sink.records[1].message.size() == 8U,
                    "an over-budget message truncates to the budget with the marker");
                Check(sink.records[2].category.empty() && sink.records[2].message.empty(),
                    "empty fields pass through the budgets untouched");
            }
        }
    }

    // --- Ring overwrite semantics ---------------------------------------------------------
    {
        auto ring = RingLogSink::Create(4);
        Check(ring.has_value(), "a four-record ring sink is created");
        if (ring)
        {
            LogServiceConfig config = ConfigWith(ring->get());
            auto service = LogService::Create(std::move(config));
            Check(service.has_value(), "the ring-backed service is created");
            if (service)
            {
                service->Info("ring", "zero");
                const auto partial = (*ring)->Snapshot();
                for (int index = 1; index < 6; ++index)
                    service->Info("ring", std::to_string(index));
                const auto snapshot = (*ring)->Snapshot();
                Check(snapshot.size() == 4U, "the retained window is capped at capacity");
                bool oldest_first = snapshot.size() == 4U;
                for (std::size_t index = 0; oldest_first && index < snapshot.size(); ++index)
                    oldest_first = snapshot[index].sequence == 2U + index;
                Check(oldest_first, "overwrite drops exactly the oldest records, oldest first");
                Check(snapshot.size() == 4U && snapshot.front().message == "2" &&
                          snapshot.back().message == "5",
                    "retained payloads match the surviving writes");
                Check(partial.size() == 1U && partial[0].message == "zero",
                    "snapshots are owned copies unaffected by later overwrites");
                Check((*ring)->consumed_count() == 6U && (*ring)->capacity() == 4U,
                    "the ring counts every consumed record beyond its capacity");
            }
        }
    }

    // --- Sequence monotonicity under a multi-threaded burst ------------------------------
    {
        constexpr std::size_t kWriters = 8;
        constexpr std::size_t kWritesPerThread = 250;
        constexpr std::size_t kTotalWrites = kWriters * kWritesPerThread;
        constexpr std::size_t kRingCapacity = 512;

        auto ring = RingLogSink::Create(kRingCapacity);
        CaptureSink capture;
        Check(ring.has_value(), "the burst ring sink is created");
        if (ring)
        {
            LogServiceConfig config = ConfigWith(ring->get());
            config.sinks.push_back(&capture);
            auto service = LogService::Create(std::move(config));
            Check(service.has_value(), "the two-sink burst service is created");
            if (service)
            {
                {
                    std::vector<std::jthread> writers;
                    writers.reserve(kWriters);
                    for (std::size_t writer = 0; writer < kWriters; ++writer)
                    {
                        writers.emplace_back([&service, writer]
                        {
                            for (std::size_t write = 0; write < kWritesPerThread; ++write)
                                service->Info("burst",
                                    std::to_string(writer) + ":" + std::to_string(write));
                        });
                    }
                }

                Check(service->written_count() == kTotalWrites &&
                          service->dropped_count() == 0U,
                    "the burst emits every record and drops none");
                Check(capture.records.size() == kTotalWrites,
                    "an unbounded capture sink receives every burst record");

                std::vector<std::uint64_t> sequences;
                sequences.reserve(capture.records.size());
                for (const LogRecord& record : capture.records)
                    sequences.push_back(record.sequence);
                Check(std::is_sorted(sequences.begin(), sequences.end()),
                    "sinks observe records in ascending sequence order");
                std::sort(sequences.begin(), sequences.end());
                bool dense = sequences.size() == kTotalWrites;
                for (std::size_t index = 0; dense && index < sequences.size(); ++index)
                    dense = sequences[index] == index;
                Check(dense, "no sequence number is lost or duplicated across the burst");

                bool ticks_monotone = true;
                for (std::size_t index = 1; index < capture.records.size(); ++index)
                {
                    ticks_monotone = ticks_monotone &&
                        capture.records[index - 1U].ticks_since_start <=
                            capture.records[index].ticks_since_start;
                }
                Check(ticks_monotone, "ticks never decrease as sequence numbers advance");

                const auto window = (*ring)->Snapshot();
                bool window_dense = window.size() == kRingCapacity;
                for (std::size_t index = 0; window_dense && index < window.size(); ++index)
                    window_dense = window[index].sequence ==
                        kTotalWrites - kRingCapacity + index;
                Check(window_dense,
                    "the retained ring window is exactly the newest contiguous sequences");
                Check((*ring)->consumed_count() == kTotalWrites,
                    "the ring consumed the full burst before overwriting");
            }
        }
    }

    // --- Deterministic stderr line format -------------------------------------------------
    {
        LogRecord record;
        record.sequence = 7;
        record.ticks_since_start = std::chrono::nanoseconds{1234};
        record.severity = LogSeverity::Warning;
        record.category = "NET";
        record.message = "hello";
        Check(StderrLogSink::FormatLine(record) == "[7] +1234ns WARNING NET: hello\n",
            "the stderr line format is pinned byte for byte");
        Check(StderrLogSink::FormatLine(record) == StderrLogSink::FormatLine(record),
            "formatting the same record twice is deterministic");

        record.message = "a\nb\tc\x7f""d";
        const std::string line = StderrLogSink::FormatLine(record);
        Check(line == "[7] +1234ns WARNING NET: a?b?c?d\n",
            "control bytes are replaced so a record cannot split the line");
        Check(std::count(line.begin(), line.end(), '\n') == 1 && line.back() == '\n',
            "each record renders as exactly one terminated line");

        using omega::runtime::LogSeverityName;
        Check(LogSeverityName(LogSeverity::Trace) == "TRACE" &&
                  LogSeverityName(LogSeverity::Debug) == "DEBUG" &&
                  LogSeverityName(LogSeverity::Info) == "INFO" &&
                  LogSeverityName(LogSeverity::Warning) == "WARNING" &&
                  LogSeverityName(LogSeverity::Error) == "ERROR",
            "every defined severity has a fixed uppercase name");
        Check(LogSeverityName(static_cast<LogSeverity>(200)) == "INVALID",
            "undefined severity values name themselves as invalid");
    }

    // Moved-from services follow the documented lifecycle: uncounted no-op writes and
    // zero/default accessors instead of crashing.
    {
        auto ring = RingLogSink::Create(4);
        Check(ring.has_value(), "the moved-from test ring is created");
        if (ring)
        {
            LogServiceConfig config;
            config.sinks = {ring->get()};
            auto source = LogService::Create(std::move(config));
            Check(source.has_value(), "the moved-from test service is created");
            if (source)
            {
                LogService moved = std::move(*source);
                source->Write(LogSeverity::Error, "core", "after move");
                source->Error("core", "after move via overload");
                Check((*ring)->consumed_count() == 0,
                    "a moved-from service delivers nothing to the old sinks");
                Check(source->written_count() == 0 && source->dropped_count() == 0,
                    "a moved-from service reports zero written and dropped records");
                Check(source->sink_count() == 0 && source->max_category_bytes() == 0 &&
                          source->max_message_bytes() == 0 &&
                          source->minimum_severity() == LogSeverity::Info,
                    "a moved-from service reports empty budgets and the default floor");
                moved.Write(LogSeverity::Error, "core", "after move target");
                Check((*ring)->consumed_count() == 1 && moved.written_count() == 1,
                    "the move target owns the sinks and the counters");
            }
        }
    }

    return failures;
}
