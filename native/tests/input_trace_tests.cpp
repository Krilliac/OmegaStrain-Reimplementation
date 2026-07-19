#include "omega/runtime/input_trace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputSnapshot;
using omega::runtime::InputTrace;
using omega::runtime::InputTraceActionState;
using omega::runtime::InputTraceConfig;
using omega::runtime::InputTraceError;
using omega::runtime::InputTraceErrorCode;
using omega::runtime::InputTraceFrameState;
using omega::runtime::InputTraceRecorder;
using omega::runtime::InputTracker;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
bool CheckError(const std::expected<Value, InputTraceError>& result,
    const InputTraceErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().code == code &&
                         result.error().message ==
                             omega::runtime::InputTraceErrorMessage(code);
    Check(matches, message);
    return matches;
}

[[nodiscard]] InputTracker MakeTracker(
    const std::span<const std::uint32_t> actions, const std::size_t event_budget)
{
    std::vector<InputBinding> bindings;
    bindings.reserve(actions.size());
    for (std::size_t index = 0U; index < actions.size(); ++index)
    {
        bindings.push_back(InputBinding{
            .device = InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(index),
            .action = actions[index],
        });
    }

    auto table = InputBindingTable::FromBindings(bindings);
    if (!table)
    {
        Check(false, "input-trace fixture binding table is valid");
        std::abort();
    }
    auto tracker = InputTracker::Create(std::move(*table), event_budget);
    if (!tracker)
    {
        Check(false, "input-trace fixture tracker is valid");
        std::abort();
    }
    return std::move(*tracker);
}

bool Push(InputTracker& tracker, const std::uint16_t code, const bool pressed)
{
    return tracker
        .PushEvent(InputEvent{
            .device = InputDevice::Keyboard,
            .code = code,
            .pressed = pressed,
        })
        .has_value();
}

[[nodiscard]] InputTrace TakeTrace(
    std::expected<InputTrace, InputTraceError>& finished,
    const std::string_view message)
{
    if (!finished)
    {
        Check(false, message);
        std::abort();
    }
    return std::move(*finished);
}

void CheckContract()
{
    static_assert(std::is_trivially_copyable_v<InputTraceFrameState>);
    static_assert(std::is_standard_layout_v<InputTraceFrameState>);
    static_assert(std::is_trivially_copyable_v<InputTraceActionState>);
    static_assert(std::is_standard_layout_v<InputTraceActionState>);
    static_assert(!std::is_copy_constructible_v<InputTraceRecorder>);
    static_assert(!std::is_copy_assignable_v<InputTraceRecorder>);
    static_assert(std::is_nothrow_move_constructible_v<InputTraceRecorder>);
    static_assert(!std::is_move_assignable_v<InputTraceRecorder>);
    static_assert(!std::is_copy_constructible_v<InputTrace>);
    static_assert(!std::is_copy_assignable_v<InputTrace>);
    static_assert(std::is_nothrow_move_constructible_v<InputTrace>);
    static_assert(!std::is_move_assignable_v<InputTrace>);
    static_assert(noexcept(std::declval<InputTraceRecorder&>().Append(
        std::declval<const InputSnapshot&>())));
    static_assert(noexcept(std::declval<InputTraceRecorder&&>().Finish()));
    static_assert(noexcept(std::declval<const InputTrace&>().FrameAt(0U)));
    static_assert(noexcept(std::declval<const InputTrace&>().ActionAt(0U, 0U)));

    Check(omega::runtime::kMaximumInputTraceFrames == 65'536U,
        "the input-trace frame hard maximum is fixed");
    Check(InputTraceConfig{}.maximum_frames == 0U &&
              InputTraceConfig{}.first_frame_index == 0U,
        "default input-trace configuration is deliberately invalid");
    Check(InputTraceFrameState{} == InputTraceFrameState{} &&
              InputTraceActionState{} == InputTraceActionState{},
        "owned input-trace query states default to zero and false");

    struct ErrorContract
    {
        InputTraceErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array contracts{
        ErrorContract{InputTraceErrorCode::InvalidConfiguration,
            "invalid-configuration", "input trace configuration is invalid"},
        ErrorContract{InputTraceErrorCode::InvalidActionSchema,
            "invalid-action-schema", "input trace action schema is invalid"},
        ErrorContract{InputTraceErrorCode::AllocationFailed,
            "allocation-failed", "input trace allocation failed"},
        ErrorContract{InputTraceErrorCode::InvalidRecorderState,
            "invalid-recorder-state", "input trace recorder is not open"},
        ErrorContract{InputTraceErrorCode::CapacityExceeded,
            "capacity-exceeded", "input trace frame capacity is exceeded"},
        ErrorContract{InputTraceErrorCode::FrameDiscontinuity,
            "frame-discontinuity", "input trace frame index is not contiguous"},
        ErrorContract{InputTraceErrorCode::ActionSchemaMismatch,
            "action-schema-mismatch", "input trace action schema does not match"},
    };
    for (const ErrorContract& contract : contracts)
    {
        Check(omega::runtime::InputTraceErrorCodeName(contract.code) == contract.name,
            "every input-trace error has its fixed code name");
        Check(omega::runtime::InputTraceErrorMessage(contract.code) == contract.message,
            "every input-trace error has its fixed category message");
        const InputTraceError error{
            .code = contract.code,
            .message = omega::runtime::InputTraceErrorMessage(contract.code),
        };
        Check(error.message == contract.message,
            "an input-trace error carries only its fixed category message");
    }

    constexpr auto unknown = static_cast<InputTraceErrorCode>(255);
    Check(omega::runtime::InputTraceErrorCodeName(unknown) == "unknown" &&
              omega::runtime::InputTraceErrorMessage(unknown) ==
                  "input trace error is unknown",
        "unknown input-trace error values fail closed to fixed text");
}

void CheckCreationValidation()
{
    constexpr std::array<std::uint32_t, 1U> one_action{10U};
    const std::span<const std::uint32_t> empty_schema;

    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{.maximum_frames = 0U}, one_action),
        InputTraceErrorCode::InvalidConfiguration,
        "zero frame capacity is rejected before allocation");
    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{
                       .maximum_frames = omega::runtime::kMaximumInputTraceFrames + 1U},
                   one_action),
        InputTraceErrorCode::InvalidConfiguration,
        "frame capacity above the hard maximum is rejected");
    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{
                       .maximum_frames = 2U,
                       .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
                   },
                   one_action),
        InputTraceErrorCode::InvalidConfiguration,
        "the configured contiguous frame range cannot overflow uint64");

    auto terminal_range = InputTraceRecorder::Create(
        InputTraceConfig{
            .maximum_frames = 2U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max() - 1U,
        },
        one_action);
    Check(terminal_range && terminal_range->first_frame_index() ==
                                std::numeric_limits<std::uint64_t>::max() - 1U &&
              terminal_range->maximum_frames() == 2U,
        "the contiguous range ending exactly at uint64 max is valid");
    CheckError(InputTraceRecorder::Create(InputTraceConfig{}, empty_schema),
        InputTraceErrorCode::InvalidConfiguration,
        "configuration validation has priority over action-schema validation");

    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{.maximum_frames = 1U}, empty_schema),
        InputTraceErrorCode::InvalidActionSchema,
        "an empty action schema is rejected");
    constexpr std::array duplicate_schema{10U, 10U};
    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{.maximum_frames = 1U}, duplicate_schema),
        InputTraceErrorCode::InvalidActionSchema,
        "a duplicate action identifier is rejected");
    constexpr std::array descending_schema{20U, 10U};
    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{.maximum_frames = 1U}, descending_schema),
        InputTraceErrorCode::InvalidActionSchema,
        "a descending action schema is rejected");
    std::array<std::uint32_t, InputBindingTable::kMaxActions + 1U> too_many{};
    for (std::size_t index = 0U; index < too_many.size(); ++index)
        too_many[index] = static_cast<std::uint32_t>(index);
    CheckError(InputTraceRecorder::Create(
                   InputTraceConfig{.maximum_frames = 1U}, too_many),
        InputTraceErrorCode::InvalidActionSchema,
        "an action schema above the 64-action mask budget is rejected");

    auto final_index = InputTraceRecorder::Create(
        InputTraceConfig{
            .maximum_frames = 1U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
        },
        one_action);
    Check(final_index && final_index->first_frame_index() ==
                             std::numeric_limits<std::uint64_t>::max() &&
              final_index->maximum_frames() == 1U && final_index->frame_count() == 0U &&
              final_index->actions().size() == 1U && final_index->actions()[0] == 10U,
        "one frame beginning at the final uint64 index is valid");
    if (final_index)
    {
        auto trace = std::move(*final_index).Finish();
        Check(trace && trace->first_frame_index() ==
                           std::numeric_limits<std::uint64_t>::max() &&
                  trace->maximum_frames() == 1U && trace->frame_count() == 0U,
            "an empty final-index trace retains exact validated metadata");
    }

    auto exact_maximum = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = omega::runtime::kMaximumInputTraceFrames},
        one_action);
    Check(exact_maximum &&
              exact_maximum->maximum_frames() == omega::runtime::kMaximumInputTraceFrames,
        "the exact frame hard maximum preallocates successfully");
    if (exact_maximum)
    {
        auto trace = std::move(*exact_maximum).Finish();
        Check(trace && trace->frame_count() == 0U,
            "an exact-maximum empty recorder finishes without another allocation");
    }
}

void CheckSnapshotParityAndPreservation()
{
    constexpr std::array schema{10U, 20U, 30U};
    InputTracker tracker = MakeTracker(schema, 2U);
    Check(Push(tracker, 0U, true) && Push(tracker, 1U, true),
        "two bound presses enter the capture fixture");
    Check(!Push(tracker, 2U, true),
        "the capture fixture records one rejected over-budget event");
    const InputSnapshot snapshot = tracker.EndFrame();

    const InputTraceFrameState expected_frame{
        .frame_index = snapshot.frame_index(),
        .accepted_event_count = snapshot.accepted_event_count(),
        .rejected_event_count = snapshot.rejected_event_count(),
    };
    std::array<InputTraceActionState, schema.size()> expected_actions{};
    for (std::size_t index = 0U; index < schema.size(); ++index)
    {
        expected_actions[index] = InputTraceActionState{
            .held = snapshot.IsHeld(schema[index]),
            .pressed = snapshot.WasPressed(schema[index]),
            .released = snapshot.WasReleased(schema[index]),
        };
    }

    auto created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 2U}, schema);
    Check(created.has_value(), "a parity recorder is created");
    if (!created)
        return;
    InputTraceRecorder recorder = std::move(*created);
    const auto appended = recorder.Append(snapshot);
    Check(appended && recorder.frame_count() == 1U,
        "a const real InputSnapshot is captured once");
    Check(snapshot.frame_index() == expected_frame.frame_index &&
              snapshot.accepted_event_count() == expected_frame.accepted_event_count &&
              snapshot.rejected_event_count() == expected_frame.rejected_event_count &&
              snapshot.actions().size() == schema.size(),
        "capture preserves caller-owned snapshot metadata and schema");
    for (std::size_t index = 0U; index < schema.size(); ++index)
    {
        Check(snapshot.actions()[index] == schema[index] &&
                  snapshot.IsHeld(schema[index]) == expected_actions[index].held &&
                  snapshot.WasPressed(schema[index]) == expected_actions[index].pressed &&
                  snapshot.WasReleased(schema[index]) == expected_actions[index].released,
            "capture preserves every caller-owned snapshot action state");
    }

    auto finished = std::move(recorder).Finish();
    InputTrace trace = TakeTrace(finished, "the parity recorder finishes");
    Check(trace.first_frame_index() == 0U && trace.maximum_frames() == 2U &&
              trace.frame_count() == 1U && trace.actions().size() == schema.size(),
        "the partial trace retains configuration, schema, and active count");
    Check(trace.FrameAt(0U) == expected_frame,
        "owned trace frame metadata matches the real snapshot exactly");
    for (std::size_t index = 0U; index < schema.size(); ++index)
    {
        Check(trace.ActionAt(0U, schema[index]) == expected_actions[index],
            "owned trace action state matches the real snapshot exactly");
    }

    const auto unknown = trace.ActionAt(0U, 999U);
    Check(unknown && *unknown == InputTraceActionState{},
        "an unknown action on a valid frame is engaged and all-false");
    Check(!trace.FrameAt(1U) && !trace.FrameAt(std::numeric_limits<std::size_t>::max()),
        "frame queries reject offsets at and far beyond the active count");
    Check(!trace.ActionAt(1U, schema[0]) && !trace.ActionAt(1U, 999U),
        "action queries reject an invalid frame before action lookup");
}

void CheckFailurePriorityAndAtomicity()
{
    constexpr std::array expected_schema{10U, 20U};
    constexpr std::array wrong_schema{10U, 30U};
    InputTracker expected_tracker = MakeTracker(expected_schema, 8U);
    InputTracker wrong_tracker = MakeTracker(wrong_schema, 8U);
    const InputSnapshot blank;

    auto size_mismatch_created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 1U}, expected_schema);
    Check(size_mismatch_created.has_value(), "a schema-size mismatch recorder is created");
    if (size_mismatch_created)
    {
        CheckError(size_mismatch_created->Append(blank),
            InputTraceErrorCode::ActionSchemaMismatch,
            "an exact frame with a different schema size is rejected");
        InputTracker size_mismatch_tracker = MakeTracker(expected_schema, 1U);
        const InputSnapshot valid_after_size_mismatch = size_mismatch_tracker.EndFrame();
        Check(size_mismatch_created->Append(valid_after_size_mismatch).has_value(),
            "schema-size rejection does not consume the expected frame");
        auto size_mismatch_trace = std::move(*size_mismatch_created).Finish();
        Check(size_mismatch_trace && size_mismatch_trace->frame_count() == 1U,
            "a recorder remains finishable after a schema-size rejection");
    }

    const InputSnapshot wrong_frame_and_schema = wrong_tracker.EndFrame();
    (void)expected_tracker.EndFrame();
    auto created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 2U, .first_frame_index = 1U},
        expected_schema);
    Check(created.has_value(), "an error-priority recorder is created");
    if (!created)
        return;
    InputTraceRecorder recorder = std::move(*created);

    CheckError(recorder.Append(wrong_frame_and_schema),
        InputTraceErrorCode::FrameDiscontinuity,
        "frame discontinuity has priority over action-schema mismatch");
    Check(recorder.frame_count() == 0U,
        "a rejected discontinuous frame leaves the recorder empty");

    Check(Push(expected_tracker, 0U, true),
        "the first valid contiguous fixture press is accepted");
    const InputSnapshot first_valid = expected_tracker.EndFrame();
    Check(recorder.Append(first_valid) && recorder.frame_count() == 1U,
        "the expected frame remains appendable after a failed append");

    (void)wrong_tracker.EndFrame();
    Check(Push(wrong_tracker, 1U, true),
        "the schema-mismatch snapshot carries a nontrivial action state");
    const InputSnapshot wrong_schema_at_expected_frame = wrong_tracker.EndFrame();
    const InputTraceFrameState mismatch_frame_before{
        .frame_index = wrong_schema_at_expected_frame.frame_index(),
        .accepted_event_count = wrong_schema_at_expected_frame.accepted_event_count(),
        .rejected_event_count = wrong_schema_at_expected_frame.rejected_event_count(),
    };
    std::array<std::uint32_t, wrong_schema.size()> mismatch_schema_before{};
    std::array<InputTraceActionState, wrong_schema.size()> mismatch_actions_before{};
    for (std::size_t index = 0U; index < wrong_schema.size(); ++index)
    {
        mismatch_schema_before[index] = wrong_schema_at_expected_frame.actions()[index];
        mismatch_actions_before[index] = InputTraceActionState{
            .held = wrong_schema_at_expected_frame.IsHeld(wrong_schema[index]),
            .pressed = wrong_schema_at_expected_frame.WasPressed(wrong_schema[index]),
            .released = wrong_schema_at_expected_frame.WasReleased(wrong_schema[index]),
        };
    }
    CheckError(recorder.Append(wrong_schema_at_expected_frame),
        InputTraceErrorCode::ActionSchemaMismatch,
        "an exact frame with a different ordered schema is rejected");
    bool mismatch_snapshot_preserved =
        wrong_schema_at_expected_frame.frame_index() == mismatch_frame_before.frame_index &&
        wrong_schema_at_expected_frame.accepted_event_count() ==
            mismatch_frame_before.accepted_event_count &&
        wrong_schema_at_expected_frame.rejected_event_count() ==
            mismatch_frame_before.rejected_event_count &&
        wrong_schema_at_expected_frame.actions().size() == mismatch_schema_before.size();
    for (std::size_t index = 0U;
         mismatch_snapshot_preserved && index < mismatch_schema_before.size(); ++index)
    {
        mismatch_snapshot_preserved =
            wrong_schema_at_expected_frame.actions()[index] ==
                mismatch_schema_before[index] &&
            wrong_schema_at_expected_frame.IsHeld(mismatch_schema_before[index]) ==
                mismatch_actions_before[index].held &&
            wrong_schema_at_expected_frame.WasPressed(mismatch_schema_before[index]) ==
                mismatch_actions_before[index].pressed &&
            wrong_schema_at_expected_frame.WasReleased(mismatch_schema_before[index]) ==
                mismatch_actions_before[index].released;
    }
    Check(mismatch_snapshot_preserved,
        "a failed append preserves the caller snapshot's complete public state");
    Check(recorder.frame_count() == 1U,
        "a schema mismatch leaves the prior frame and next index unchanged");

    Check(Push(expected_tracker, 0U, false),
        "the second valid contiguous fixture release is accepted");
    const InputSnapshot second_valid = expected_tracker.EndFrame();
    Check(recorder.Append(second_valid) && recorder.frame_count() == 2U,
        "the expected frame remains appendable after a schema mismatch");

    CheckError(recorder.Append(blank), InputTraceErrorCode::CapacityExceeded,
        "capacity exhaustion has priority over frame and schema errors");
    Check(recorder.frame_count() == 2U,
        "capacity failure leaves a full recorder unchanged");

    auto finished = std::move(recorder).Finish();
    Check(finished && finished->frame_count() == 2U &&
              finished->FrameAt(0U) ==
                  InputTraceFrameState{
                      .frame_index = 1U,
                      .accepted_event_count = 1U,
                  } &&
              finished->FrameAt(1U) ==
                  InputTraceFrameState{
                      .frame_index = 2U,
                      .accepted_event_count = 1U,
                  },
        "successful frames survive intervening failures in exact contiguous order");
    CheckError(recorder.Append(blank), InputTraceErrorCode::InvalidRecorderState,
        "finished state has priority over all append validation");
    auto finished_again = std::move(recorder).Finish();
    CheckError(finished_again, InputTraceErrorCode::InvalidRecorderState,
        "a recorder cannot finish twice");
    Check(recorder.first_frame_index() == 0U && recorder.maximum_frames() == 0U &&
              recorder.frame_count() == 0U && recorder.actions().empty(),
        "a finished recorder is observably inert");
}

void CheckEmptyAndMoveLifecycle()
{
    constexpr std::array schema{5U, 9U};
    auto empty_created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 3U, .first_frame_index = 7U}, schema);
    Check(empty_created.has_value(), "an empty-finish recorder is created");
    if (!empty_created)
        return;
    InputTraceRecorder empty_recorder = std::move(*empty_created);
    auto empty_finished = std::move(empty_recorder).Finish();
    Check(empty_finished && empty_finished->first_frame_index() == 7U &&
              empty_finished->maximum_frames() == 3U &&
              empty_finished->frame_count() == 0U &&
              empty_finished->actions().size() == schema.size() &&
              !empty_finished->FrameAt(0U) && !empty_finished->ActionAt(0U, schema[0]),
        "an open zero-frame recorder publishes an immutable empty trace");

    InputTracker tracker = MakeTracker(schema, 4U);
    Check(Push(tracker, 1U, true), "the move fixture press is accepted");
    const InputSnapshot snapshot = tracker.EndFrame();
    auto source_created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 2U}, schema);
    Check(source_created && source_created->Append(snapshot),
        "the recorder move fixture captures one frame");
    if (!source_created)
        return;

    InputTraceRecorder source = std::move(*source_created);
    InputTraceRecorder destination = std::move(source);
    Check(source.first_frame_index() == 0U && source.maximum_frames() == 0U &&
              source.frame_count() == 0U && source.actions().empty(),
        "move construction leaves the recorder source inert");
    CheckError(source.Append(snapshot), InputTraceErrorCode::InvalidRecorderState,
        "a moved-from recorder rejects append");
    auto source_finish = std::move(source).Finish();
    CheckError(source_finish, InputTraceErrorCode::InvalidRecorderState,
        "a moved-from recorder rejects finish");
    Check(destination.maximum_frames() == 2U && destination.frame_count() == 1U &&
              destination.actions().size() == schema.size(),
        "recorder move construction transfers complete logical ownership");

    std::optional<InputTraceFrameState> owned_frame;
    std::optional<InputTraceActionState> owned_action;
    {
        auto destination_finish = std::move(destination).Finish();
        InputTrace trace = TakeTrace(destination_finish, "the moved recorder finishes");
        owned_frame = trace.FrameAt(0U);
        owned_action = trace.ActionAt(0U, schema[1]);
        InputTrace moved_trace = std::move(trace);
        Check(trace.first_frame_index() == 0U && trace.maximum_frames() == 0U &&
                  trace.frame_count() == 0U && trace.actions().empty() &&
                  !trace.FrameAt(0U) && !trace.ActionAt(0U, schema[1]),
            "move construction leaves the trace source inert");
        Check(moved_trace.maximum_frames() == 2U && moved_trace.frame_count() == 1U &&
                  moved_trace.FrameAt(0U) == owned_frame &&
                  moved_trace.ActionAt(0U, schema[1]) == owned_action,
            "trace move construction transfers all immutable query state");
    }
    Check(owned_frame == InputTraceFrameState{
                             .frame_index = 0U,
                             .accepted_event_count = 1U,
                         } &&
              owned_action == InputTraceActionState{
                                  .held = true,
                                  .pressed = true,
                                  .released = false,
                              },
        "owned query values survive destruction of the final owning trace");
}

void CheckSixtyFourthAction()
{
    std::array<std::uint32_t, InputBindingTable::kMaxActions> schema{};
    for (std::size_t index = 0U; index < schema.size(); ++index)
        schema[index] = 1'000U + static_cast<std::uint32_t>(index);

    InputTracker tracker = MakeTracker(schema, 2U);
    Check(Push(tracker, 63U, true), "the 64th action fixture press is accepted");
    const InputSnapshot pressed_snapshot = tracker.EndFrame();
    Check(Push(tracker, 63U, false), "the 64th action fixture release is accepted");
    const InputSnapshot released_snapshot = tracker.EndFrame();
    auto created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 2U}, schema);
    Check(created && created->Append(pressed_snapshot) &&
              created->Append(released_snapshot),
        "an exact 64-action schema captures press and release without shifting past its mask");
    if (!created)
        return;
    auto finished = std::move(*created).Finish();
    Check(finished && finished->actions().size() == 64U &&
              finished->actions().front() == schema.front() &&
              finished->actions().back() == schema.back(),
        "the exact 64-action schema is retained in ascending order");
    if (!finished)
        return;

    for (std::size_t index = 0U; index + 1U < schema.size(); ++index)
    {
        const auto state = finished->ActionAt(0U, schema[index]);
        if (!state || *state != InputTraceActionState{})
        {
            Check(false, "actions below bit 63 remain all-false");
            break;
        }
    }
    Check(finished->ActionAt(0U, schema.back()) ==
              InputTraceActionState{.held = true, .pressed = true, .released = false},
        "the action at ordinal 63 round-trips held and pressed bits exactly");
    Check(finished->ActionAt(1U, schema.back()) ==
              InputTraceActionState{.held = false, .pressed = false, .released = true},
        "the action at ordinal 63 round-trips the release bit exactly");
    Check(finished->ActionAt(0U, std::numeric_limits<std::uint32_t>::max()) ==
              InputTraceActionState{},
        "an unknown action above the 64-action schema remains all-false");
}

[[nodiscard]] InputTrace BuildDeterministicTrace()
{
    constexpr std::array schema{10U, 20U, 30U};
    InputTracker tracker = MakeTracker(schema, 4U);
    auto created = InputTraceRecorder::Create(
        InputTraceConfig{.maximum_frames = 3U}, schema);
    if (!created)
        std::abort();
    InputTraceRecorder recorder = std::move(*created);

    if (!Push(tracker, 0U, true) || !recorder.Append(tracker.EndFrame()))
        std::abort();
    if (!Push(tracker, 1U, true) || !Push(tracker, 0U, false) ||
        !recorder.Append(tracker.EndFrame()))
    {
        std::abort();
    }
    if (!Push(tracker, 2U, true) || !Push(tracker, 2U, false) ||
        !Push(tracker, 99U, true) || !recorder.Append(tracker.EndFrame()))
    {
        std::abort();
    }

    auto finished = std::move(recorder).Finish();
    if (!finished)
        std::abort();
    return std::move(*finished);
}

void CheckTwoRunDeterminism()
{
    InputTrace first = BuildDeterministicTrace();
    InputTrace second = BuildDeterministicTrace();
    bool identical = first.first_frame_index() == second.first_frame_index() &&
                     first.maximum_frames() == second.maximum_frames() &&
                     first.frame_count() == second.frame_count() &&
                     first.actions().size() == second.actions().size();
    for (std::size_t index = 0U; identical && index < first.actions().size(); ++index)
        identical = first.actions()[index] == second.actions()[index];
    for (std::size_t frame = 0U; identical && frame < first.frame_count(); ++frame)
    {
        identical = first.FrameAt(frame) == second.FrameAt(frame);
        for (const std::uint32_t action : first.actions())
            identical = identical && first.ActionAt(frame, action) ==
                                         second.ActionAt(frame, action);
    }
    Check(identical,
        "two identical tracker event runs produce identical compact trace queries");
}
} // namespace

int main()
{
    CheckContract();
    CheckCreationValidation();
    CheckSnapshotParityAndPreservation();
    CheckFailurePriorityAndAtomicity();
    CheckEmptyAndMoveLifecycle();
    CheckSixtyFourthAction();
    CheckTwoRunDeterminism();

    if (failures == 0)
        std::cout << "omega_input_trace_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
