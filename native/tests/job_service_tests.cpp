#include "omega/runtime/job_service.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
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

void SubmitChain(omega::runtime::JobService& service, std::atomic<std::uint64_t>& counter,
    const int remaining, std::atomic<bool>& submit_failed)
{
    if (remaining <= 0)
        return;
    auto submitted = service.Submit(
        [&service, &counter, remaining, &submit_failed]
        {
            counter.fetch_add(1U);
            SubmitChain(service, counter, remaining - 1, submit_failed);
        });
    if (!submitted)
        submit_failed.store(true);
}

void SubmitTree(omega::runtime::JobService& service, std::atomic<std::uint64_t>& counter,
    const int depth, std::atomic<bool>& submit_failed)
{
    if (depth <= 0)
        return;
    for (int child = 0; child < 2; ++child)
    {
        auto submitted = service.Submit(
            [&service, &counter, depth, &submit_failed]
            {
                counter.fetch_add(1U);
                SubmitTree(service, counter, depth - 1, submit_failed);
            });
        if (!submitted)
            submit_failed.store(true);
    }
}
} // namespace

int JobServiceFailureCount()
{
    using omega::runtime::JobService;
    using omega::runtime::JobServiceConfig;

    const std::size_t default_workers = omega::runtime::DefaultJobWorkerCount();
    Check(default_workers >= JobService::kMinWorkerCount &&
              default_workers <= JobService::kMaxWorkerCount,
        "the synthetic default worker count is clamped into the documented bounds");

    Check(!JobService::Create({.worker_count = 0, .max_pending_jobs = 16}),
        "a zero worker count is rejected instead of guessed");
    Check(!JobService::Create(
              {.worker_count = JobService::kMaxWorkerCount + 1U, .max_pending_jobs = 16}),
        "a worker count above the hard budget is rejected");
    Check(!JobService::Create({.worker_count = 1, .max_pending_jobs = 0}),
        "a zero pending budget is rejected instead of meaning unlimited");
    Check(!JobService::Create(
              {.worker_count = 1, .max_pending_jobs = JobService::kMaxPendingJobsLimit + 1U}),
        "a pending budget above the hard limit is rejected");
    {
        auto minimal = JobService::Create({.worker_count = 1, .max_pending_jobs = 1});
        Check(minimal.has_value() && minimal->worker_count() == 1U,
            "the minimal boundary configuration is accepted");
        auto maximal = JobService::Create(
            {.worker_count = JobService::kMaxWorkerCount, .max_pending_jobs = 4});
        Check(maximal.has_value() && maximal->worker_count() == JobService::kMaxWorkerCount,
            "the maximal worker boundary spawns exactly the requested workers");
    }

    // All jobs execute exactly once across many concurrent submitters.
    {
        auto service = JobService::Create({.worker_count = 4, .max_pending_jobs = 8192});
        Check(service.has_value(), "the concurrency test pool is created");
        if (service)
        {
            service->WaitForIdle();
            Check(service->executed_job_count() == 0U && service->pending_job_count() == 0U,
                "waiting on an idle pool returns immediately with zeroed statistics");

            constexpr std::size_t kSubmitters = 4;
            constexpr std::size_t kJobsPerSubmitter = 1024;
            std::atomic<std::uint64_t> counter{0};
            std::atomic<std::uint64_t> accepted{0};
            {
                std::vector<std::jthread> submitters;
                submitters.reserve(kSubmitters);
                for (std::size_t submitter = 0; submitter < kSubmitters; ++submitter)
                {
                    submitters.emplace_back(
                        [&service, &counter, &accepted]
                        {
                            for (std::size_t job = 0; job < kJobsPerSubmitter; ++job)
                            {
                                if (service->Submit([&counter] { counter.fetch_add(1U); }))
                                    accepted.fetch_add(1U);
                            }
                        });
                }
            }
            service->WaitForIdle();
            constexpr std::uint64_t kTotal = kSubmitters * kJobsPerSubmitter;
            Check(accepted.load() == kTotal,
                "an in-budget concurrent burst is accepted without spurious rejection");
            Check(counter.load() == kTotal, "every accepted job executes exactly once");
            Check(service->executed_job_count() == kTotal && service->failed_job_count() == 0U &&
                      service->pending_job_count() == 0U,
                "statistics are exact and final after WaitForIdle returns");

            Check(!service->Submit(std::move_only_function<void()>{}),
                "an empty job is rejected at the submission boundary");
            Check(service->executed_job_count() == kTotal,
                "a rejected empty job does not disturb the executed count");
        }
    }

    // Self-submission from inside running jobs never deadlocks and is seen by WaitForIdle.
    {
        auto service = JobService::Create({.worker_count = 2, .max_pending_jobs = 1024});
        Check(service.has_value(), "the recursion test pool is created");
        if (service)
        {
            std::atomic<std::uint64_t> counter{0};
            std::atomic<bool> submit_failed{false};
            constexpr int kChainDepth = 200;
            SubmitChain(*service, counter, kChainDepth, submit_failed);
            service->WaitForIdle();
            Check(!submit_failed.load(), "no link of the self-submitting chain is rejected");
            Check(counter.load() == static_cast<std::uint64_t>(kChainDepth),
                "WaitForIdle covers every job submitted by a job during the wait");

            counter.store(0);
            constexpr int kTreeDepth = 7; // 2^(d+1) - 2 = 254 jobs
            SubmitTree(*service, counter, kTreeDepth, submit_failed);
            service->WaitForIdle();
            Check(!submit_failed.load(), "no branch of the fan-out recursion is rejected");
            Check(counter.load() == 254U,
                "fan-out recursion completes exactly once per spawned job");
            Check(service->executed_job_count() == 200U + 254U,
                "the executed count accumulates across recursive workloads");
        }
    }

    // The pending budget rejects exactly at the boundary and recovers after a drain.
    {
        constexpr std::size_t kBudget = 16;
        auto service = JobService::Create({.worker_count = 1, .max_pending_jobs = kBudget});
        Check(service.has_value(), "the budget test pool is created");
        if (service)
        {
            std::latch blocker_started(1);
            std::latch blocker_release(1);
            std::atomic<std::uint64_t> counter{0};
            auto blocker = service->Submit(
                [&blocker_started, &blocker_release, &counter]
                {
                    blocker_started.count_down();
                    blocker_release.wait();
                    counter.fetch_add(1U);
                });
            Check(blocker.has_value(), "the blocking gate job is accepted");
            blocker_started.wait();

            std::size_t accepted = 0;
            for (std::size_t job = 0; job < kBudget; ++job)
            {
                if (service->Submit([&counter] { counter.fetch_add(1U); }))
                    ++accepted;
            }
            Check(accepted == kBudget,
                "the full pending budget is usable while a job occupies the worker");
            Check(service->pending_job_count() == kBudget,
                "the pending snapshot reports a full queue");
            auto over_budget = service->Submit([&counter] { counter.fetch_add(1U); });
            Check(!over_budget.has_value(),
                "the first submission past the budget is rejected, not blocked");
            Check(!over_budget.has_value() &&
                      over_budget.error().find("exhausted") != std::string::npos,
                "the budget rejection names the exhausted budget");

            blocker_release.count_down();
            service->WaitForIdle();
            Check(counter.load() == kBudget + 1U,
                "a rejected job is never executed and accepted jobs run exactly once");
            Check(service->Submit([] {}).has_value(),
                "the budget frees again once the queue drains");
            service->WaitForIdle();
        }
    }

    // A single worker executes already-queued jobs in strict FIFO submission order.
    {
        auto service = JobService::Create({.worker_count = 1, .max_pending_jobs = 64});
        Check(service.has_value(), "the FIFO test pool is created");
        if (service)
        {
            std::latch blocker_started(1);
            std::latch blocker_release(1);
            auto blocker = service->Submit(
                [&blocker_started, &blocker_release]
                {
                    blocker_started.count_down();
                    blocker_release.wait();
                });
            Check(blocker.has_value(), "the FIFO gate job is accepted");
            blocker_started.wait();

            std::mutex order_mutex;
            std::vector<int> order;
            constexpr int kOrderedJobs = 12;
            bool all_accepted = true;
            for (int index = 0; index < kOrderedJobs; ++index)
            {
                auto submitted = service->Submit(
                    [&order_mutex, &order, index]
                    {
                        const std::lock_guard<std::mutex> lock(order_mutex);
                        order.push_back(index);
                    });
                if (!submitted)
                    all_accepted = false;
            }
            Check(all_accepted, "every ordered job is accepted while the gate holds");
            blocker_release.count_down();
            service->WaitForIdle();
            bool fifo = order.size() == static_cast<std::size_t>(kOrderedJobs);
            for (int index = 0; fifo && index < kOrderedJobs; ++index)
                fifo = order[static_cast<std::size_t>(index)] == index;
            Check(fifo, "a single worker preserves FIFO order of already-queued jobs");
        }
    }

    // A throwing job is a contained contract violation: counted, never propagated.
    {
        auto service = JobService::Create({.worker_count = 1, .max_pending_jobs = 8});
        Check(service.has_value(), "the exception test pool is created");
        if (service)
        {
            Check(service->Submit([] { throw std::runtime_error("contract violation"); })
                      .has_value(),
                "a throwing job is accepted at submission time");
            service->WaitForIdle();
            Check(service->failed_job_count() == 1U,
                "an escaped exception is counted as exactly one failed job");
            Check(service->executed_job_count() == 0U,
                "a failed job is never counted as executed");
            std::atomic<std::uint64_t> counter{0};
            Check(service->Submit([&counter] { counter.fetch_add(1U); }).has_value(),
                "the pool keeps accepting work after a contained failure");
            service->WaitForIdle();
            Check(counter.load() == 1U && service->executed_job_count() == 1U,
                "workers keep executing jobs after a contained failure");
        }
    }

    // Destruction with a full queue executes every already-queued job, then joins.
    {
        constexpr std::size_t kBudget = 32;
        std::atomic<std::uint64_t> counter{0};
        std::latch blocker_started(1);
        std::latch blocker_release(1);
        std::size_t accepted = 0;
        {
            auto service = JobService::Create({.worker_count = 1, .max_pending_jobs = kBudget});
            Check(service.has_value(), "the shutdown test pool is created");
            if (service)
            {
                auto blocker = service->Submit(
                    [&blocker_started, &blocker_release, &counter]
                    {
                        blocker_started.count_down();
                        blocker_release.wait();
                        counter.fetch_add(1U);
                    });
                Check(blocker.has_value(), "the shutdown gate job is accepted");
                blocker_started.wait();
                for (std::size_t job = 0; job < kBudget; ++job)
                {
                    if (service->Submit([&counter] { counter.fetch_add(1U); }))
                        ++accepted;
                }
                blocker_release.count_down();
                // Destroy with the queue still (potentially) full; no WaitForIdle first.
            }
        }
        Check(accepted == kBudget, "the shutdown test fills the entire pending budget");
        Check(counter.load() == kBudget + 1U,
            "the destructor executes every already-queued job before joining");
    }

    // Moved-from and move-assigned services follow the documented lifecycle.
    {
        auto source = JobService::Create({.worker_count = 1, .max_pending_jobs = 8});
        Check(source.has_value(), "the move test pool is created");
        if (source)
        {
            std::atomic<std::uint64_t> counter{0};
            Check(source->Submit([&counter] { counter.fetch_add(1U); }).has_value(),
                "the move source accepts a job before the transfer");
            JobService moved = std::move(*source);
            moved.WaitForIdle();
            Check(counter.load() == 1U && moved.executed_job_count() == 1U,
                "the move target owns the workers and the accumulated statistics");
            Check(!source->Submit([] {}).has_value(),
                "a moved-from service rejects submissions instead of crashing");
            source->WaitForIdle();
            Check(source->executed_job_count() == 0U && source->pending_job_count() == 0U &&
                      source->worker_count() == 0U,
                "a moved-from service reports zero workers and zero statistics");

            auto replacement = JobService::Create({.worker_count = 1, .max_pending_jobs = 8});
            Check(replacement.has_value(), "the move-assignment pool is created");
            if (replacement)
            {
                Check(moved.Submit([&counter] { counter.fetch_add(1U); }).has_value(),
                    "the assignment destination accepts one final job");
                moved = std::move(*replacement);
                Check(counter.load() == 2U,
                    "move-assignment drains the destination pool before replacing it");
                Check(moved.Submit([&counter] { counter.fetch_add(1U); }).has_value(),
                    "the assigned-in pool accepts new work");
                moved.WaitForIdle();
                Check(counter.load() == 3U && moved.executed_job_count() == 1U,
                    "the assigned-in pool executes with its own fresh statistics");
            }
        }
    }

    // Move-assignment must keep the facade pointer stable while the destination pool drains:
    // a destination job that keeps calling Submit() during the drain must receive the
    // documented shutdown rejection, never observe a torn or null service.
    {
        for (int round = 0; round < 32; ++round)
        {
            auto destination = JobService::Create({.worker_count = 2, .max_pending_jobs = 64});
            auto incoming = JobService::Create({.worker_count = 1, .max_pending_jobs = 8});
            Check(destination.has_value() && incoming.has_value(),
                "the drain-race pools are created");
            if (!destination || !incoming)
                break;
            JobService& facade = *destination;
            std::atomic<bool> job_started{false};
            std::atomic<bool> begin_drain_probe{false};
            std::atomic<bool> shutdown_observed{false};
            std::atomic<std::uint64_t> rejections{0};
            std::atomic<std::uint64_t> wrong_worker_counts{0};
            const auto submitted = facade.Submit(
                [&facade, &job_started, &begin_drain_probe, &shutdown_observed, &rejections,
                    &wrong_worker_counts]
                {
                    job_started.store(true);
                    while (!begin_drain_probe.load())
                        std::this_thread::yield();

                    while (!shutdown_observed.load())
                    {
                        auto probe = facade.Submit([] {});
                        if (!probe)
                        {
                            if (probe.error() ==
                                "job service is shutting down and rejects new jobs")
                                shutdown_observed.store(true);
                            rejections.fetch_add(1U);
                            std::this_thread::yield();
                        }
                    }
                    for (int sample = 0; sample < 4'096; ++sample)
                    {
                        if (facade.worker_count() != 2U)
                            wrong_worker_counts.fetch_add(1U);
                    }
                }
            );
            Check(submitted.has_value(), "the drain-race job is accepted");
            while (!job_started.load())
                std::this_thread::yield();
            begin_drain_probe.store(true);
            facade = std::move(*incoming); // drains while the job hammers Submit()
            Check(facade.worker_count() == 1U,
                "the drain-race assignment installs the incoming single-worker pool");
            Check(shutdown_observed.load(),
                "the drain-race job observes the documented shutdown rejection");
            Check(wrong_worker_counts.load() == 0U,
                "worker_count remains the immutable configured count throughout draining");
            if (round == 0)
                Check(rejections.load() > 0U,
                    "drain-time submissions resolve to accept or reject, never a crash");
        }
    }

    return failures;
}
