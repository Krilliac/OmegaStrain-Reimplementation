#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace omega::runtime
{
// Synthetic-shell worker-pool budgets. These bounds are project infrastructure choices, not
// retail claims: no evidence about the original title's threading model is asserted here.
struct JobServiceConfig
{
    // Exact number of worker threads to spawn. Must lie in
    // [JobService::kMinWorkerCount, JobService::kMaxWorkerCount].
    std::size_t worker_count = 0;
    // Hard budget for jobs accepted but not yet started. Submit() rejects (never blocks)
    // once this many jobs are pending. Must lie in [1, JobService::kMaxPendingJobsLimit].
    std::size_t max_pending_jobs = 0;
};

// [any thread; reentrant] Synthetic-shell default worker count:
// std::thread::hardware_concurrency() clamped into [kMinWorkerCount, kMaxWorkerCount], with an
// unreported concurrency (0) resolving to kMinWorkerCount. Callers still pass the resolved
// value explicitly through JobServiceConfig; Create() never guesses.
[[nodiscard]] std::size_t DefaultJobWorkerCount() noexcept;

// Worker-pool owner for file reads, decompression, parsing, and CPU-side asset preparation.
// OmegaApp is the sole owner (std::unique_ptr member); other services receive non-owning
// references. Implementation is a plain mutex + condition_variable queue; the pending queue is
// strict FIFO, so a single-worker service executes already-queued jobs in submission order
// (with multiple workers only dequeue order is FIFO, completion order is unspecified).
//
// Job contract: jobs must not throw; a throwing job is a programming error. The service still
// contains the fault deterministically: an escaped exception is swallowed at the worker
// boundary and counted in failed_job_count() instead of terminating or unwinding into pool
// internals. No exception ever crosses this public boundary.
class JobService final
{
public:
    static constexpr std::size_t kMinWorkerCount = 1;
    static constexpr std::size_t kMaxWorkerCount = 64;
    static constexpr std::size_t kMaxPendingJobsLimit = 1'048'576;

    // [game thread] Validates the explicit budgets and spawns exactly worker_count threads.
    // Thread-creation failure is reported as an error, never thrown.
    [[nodiscard]] static std::expected<JobService, std::string> Create(JobServiceConfig config);

    // [game thread] Deterministic shutdown policy: stops accepting new submissions, executes
    // every already-queued job, then joins every worker. Jobs that call Submit() during
    // shutdown receive a rejection. Never deadlocks, never abandons a queued job, never leaks
    // a thread.
    ~JobService();

    // [game thread] Moving is permitted only while no other thread holds a reference to the
    // source object. The moved-from service keeps no workers; its Submit() rejects and its
    // waits return immediately. Move-assignment first shuts down the destination pool under
    // the destructor policy above.
    JobService(JobService&& other) noexcept;
    JobService& operator=(JobService&& other) noexcept;
    JobService(const JobService&) = delete;
    JobService& operator=(const JobService&) = delete;

    // [any thread; reentrant, including from inside a running job — self-submission cannot
    // deadlock] Queues one job for execution on some worker thread. Rejects instead of
    // blocking when the max_pending_jobs budget is exhausted, when the job is empty, or when
    // the service is shutting down / moved-from.
    [[nodiscard]] std::expected<void, std::string> Submit(std::move_only_function<void()> job);

    // [game thread] Blocks (condition-variable wait, no spinning) until every accepted job —
    // including jobs submitted by running jobs while the wait is in progress — has finished.
    // Returns immediately on an idle or moved-from service. The service keeps accepting
    // submissions during and after the wait.
    void WaitForIdle();

    // Statistics memory-order contract: all counters are written under the same internal
    // mutex that guards the queue, and each accessor below acquires that mutex, so a read
    // observes every update sequenced before the most recent lock release (plain
    // acquire/release via std::mutex; no relaxed atomics). After WaitForIdle() returns,
    // executed_job_count() + failed_job_count() equals the number of jobs accepted before that
    // return, and both values are stable until the next accepted submission.

    // [any thread] Number of jobs that ran to completion without an escaped exception.
    [[nodiscard]] std::uint64_t executed_job_count() const;

    // [any thread] Number of jobs whose invocation escaped with an exception (contract
    // violations contained at the worker boundary).
    [[nodiscard]] std::uint64_t failed_job_count() const;

    // [any thread; instantaneous snapshot] Jobs accepted but not yet handed to a worker.
    [[nodiscard]] std::size_t pending_job_count() const;

    // [any thread] Number of worker threads spawned at Create(); zero once moved-from.
    [[nodiscard]] std::size_t worker_count() const noexcept;

private:
    struct Impl;
    explicit JobService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::runtime
