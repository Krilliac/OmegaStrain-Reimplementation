#include "omega/runtime/job_service.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace omega::runtime
{
struct JobService::Impl
{
    explicit Impl(const JobServiceConfig validated_config) noexcept
        : config(validated_config), configured_worker_count(validated_config.worker_count)
    {
    }

    ~Impl()
    {
        Shutdown();
    }

    // Deterministic shutdown: refuse new work, wake every worker, and let each worker
    // drain the remaining queue before its loop exits. std::jthread joins on destruction,
    // so clearing the vector blocks until every worker has finished its final job.
    // Idempotent, and callable through a still-valid Impl pointer so drain-time Submit()
    // calls observe a stable object and receive the documented rejection.
    void Shutdown()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            stopping = true;
        }
        work_available.notify_all();
        workers.clear();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void RunWorker();

    JobServiceConfig config;
    const std::size_t configured_worker_count;
    std::mutex mutex;
    std::condition_variable work_available;
    std::condition_variable pool_idle;
    std::deque<std::move_only_function<void()>> pending;
    std::size_t active_jobs = 0;
    bool accepting = true;
    bool stopping = false;
    std::uint64_t executed_jobs = 0;
    std::uint64_t failed_jobs = 0;
    std::vector<std::jthread> workers;
};

void JobService::Impl::RunWorker()
{
    for (;;)
    {
        std::move_only_function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            work_available.wait(lock, [this] { return stopping || !pending.empty(); });
            if (pending.empty())
                return; // stopping and fully drained
            job = std::move(pending.front());
            pending.pop_front();
            ++active_jobs;
        }
        bool escaped_exception = false;
        try
        {
            job();
        }
        catch (...)
        {
            // Contract violation contained deterministically; counted, never rethrown.
            escaped_exception = true;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex);
            --active_jobs;
            if (escaped_exception)
                ++failed_jobs;
            else
                ++executed_jobs;
            if (pending.empty() && active_jobs == 0)
                pool_idle.notify_all();
        }
    }
}

std::size_t DefaultJobWorkerCount() noexcept
{
    const unsigned int reported = std::thread::hardware_concurrency();
    const std::size_t resolved =
        reported == 0U ? JobService::kMinWorkerCount : static_cast<std::size_t>(reported);
    return std::clamp(resolved, JobService::kMinWorkerCount, JobService::kMaxWorkerCount);
}

std::expected<JobService, std::string> JobService::Create(const JobServiceConfig config)
{
    if (config.worker_count < kMinWorkerCount || config.worker_count > kMaxWorkerCount)
        return std::unexpected(std::format(
            "job service worker_count {} is outside [{}, {}]",
            config.worker_count, kMinWorkerCount, kMaxWorkerCount));
    if (config.max_pending_jobs < 1U || config.max_pending_jobs > kMaxPendingJobsLimit)
        return std::unexpected(std::format(
            "job service max_pending_jobs {} is outside [1, {}]",
            config.max_pending_jobs, kMaxPendingJobsLimit));

    auto impl = std::make_unique<Impl>(config);
    impl->workers.reserve(config.worker_count);
    try
    {
        for (std::size_t index = 0; index < config.worker_count; ++index)
        {
            Impl& state = *impl;
            impl->workers.emplace_back([&state] { state.RunWorker(); });
        }
    }
    catch (const std::exception& error)
    {
        // ~Impl joins the workers that did start; no thread leaks on partial failure.
        return std::unexpected(
            std::string("job service failed to start a worker thread: ") + error.what());
    }
    return JobService(std::move(impl));
}

JobService::JobService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

JobService::~JobService() = default;

JobService::JobService(JobService&& other) noexcept = default;

JobService& JobService::operator=(JobService&& other) noexcept
{
    if (this != &other)
    {
        // Shut down the destination pool through the still-valid pointer before touching
        // impl_. unique_ptr::reset() would null impl_ before ~Impl drains the pool, so a
        // destination job calling Submit() during the drain would race on impl_ and
        // dereference null instead of receiving the documented shutdown rejection. After
        // Shutdown() returns every worker has joined, so no destination job can still be
        // executing when impl_ is finally rebound.
        if (impl_)
            impl_->Shutdown();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<void, std::string> JobService::Submit(std::move_only_function<void()> job)
{
    if (!impl_)
        return std::unexpected("job service is moved-from and owns no workers");
    if (!job)
        return std::unexpected("job service rejects an empty job");
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->accepting)
            return std::unexpected("job service is shutting down and rejects new jobs");
        if (impl_->pending.size() >= impl_->config.max_pending_jobs)
            return std::unexpected(std::format(
                "job service pending budget of {} jobs is exhausted",
                impl_->config.max_pending_jobs));
        impl_->pending.push_back(std::move(job));
    }
    impl_->work_available.notify_one();
    return {};
}

void JobService::WaitForIdle()
{
    if (!impl_)
        return;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->pool_idle.wait(
        lock, [this] { return impl_->pending.empty() && impl_->active_jobs == 0; });
}

std::uint64_t JobService::executed_job_count() const
{
    if (!impl_)
        return 0;
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->executed_jobs;
}

std::uint64_t JobService::failed_job_count() const
{
    if (!impl_)
        return 0;
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->failed_jobs;
}

std::size_t JobService::pending_job_count() const
{
    if (!impl_)
        return 0;
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pending.size();
}

std::size_t JobService::worker_count() const noexcept
{
    return impl_ ? impl_->configured_worker_count : 0U;
}
} // namespace omega::runtime
