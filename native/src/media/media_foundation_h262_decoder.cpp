#include "omega/media/media_foundation_h262_decoder.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#endif

namespace omega::media {
namespace {
using Error = MediaFoundationH262DecoderError;
using ErrorCode = MediaFoundationH262DecoderErrorCode;

[[nodiscard]] Error MakeError(const ErrorCode code,
                              const std::int32_t platform_code,
                              std::string message) {
  return Error{
      .code = code,
      .platform_code = platform_code,
      .message = std::move(message),
  };
}

[[nodiscard]] std::optional<Error>
ValidateConfiguration(const H262SequenceHeaderFacts &sequence,
                      const MediaFoundationH262DecoderLimits &limits) {
  if (sequence.width == 0U || sequence.height == 0U ||
      sequence.frame_rate_numerator == 0U ||
      sequence.frame_rate_denominator == 0U ||
      sequence.sequence_header_count == 0U) {
    return MakeError(ErrorCode::InvalidConfiguration, 0,
                     "H.262 sequence facts are incomplete");
  }
  if ((sequence.width & 1U) != 0U || (sequence.height & 1U) != 0U) {
    return MakeError(ErrorCode::InvalidConfiguration, 0,
                     "NV12 decoding requires even H.262 dimensions");
  }
  if (limits.maximum_input_chunk_bytes == 0U ||
      limits.maximum_total_input_bytes == 0U ||
      limits.maximum_output_bytes_per_call == 0U ||
      limits.maximum_frames_per_call == 0U ||
      limits.maximum_total_frames == 0U || limits.maximum_width == 0U ||
      limits.maximum_height == 0U) {
    return MakeError(ErrorCode::InvalidConfiguration, 0,
                     "Media Foundation H.262 limits must be nonzero");
  }
  if (limits.maximum_input_chunk_bytes >
          std::numeric_limits<std::uint32_t>::max() ||
      limits.maximum_total_input_bytes >
          kMediaFoundationH262MaximumTotalInputBytes ||
      limits.maximum_width > kMediaFoundationH262MaximumWidth ||
      limits.maximum_height > kMediaFoundationH262MaximumHeight ||
      sequence.width > limits.maximum_width ||
      sequence.height > limits.maximum_height) {
    return MakeError(
        ErrorCode::InvalidConfiguration, 0,
        "Media Foundation H.262 limits exceed the supported ceiling");
  }

  const std::uint64_t frame_bytes =
      static_cast<std::uint64_t>(sequence.width) * sequence.height * 3U / 2U;
  if (frame_bytes == 0U ||
      frame_bytes > kMediaFoundationH262MaximumFrameBytes ||
      frame_bytes > limits.maximum_output_bytes_per_call) {
    return MakeError(ErrorCode::InvalidConfiguration, 0,
                     "one NV12 frame exceeds the configured output budget");
  }
  return std::nullopt;
}
} // namespace

#ifndef _WIN32
struct MediaFoundationH262Decoder::Impl {};

MediaFoundationH262Decoder::MediaFoundationH262Decoder(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

MediaFoundationH262Decoder::MediaFoundationH262Decoder(
    MediaFoundationH262Decoder &&) noexcept = default;
MediaFoundationH262Decoder::~MediaFoundationH262Decoder() = default;

std::expected<MediaFoundationH262Decoder, MediaFoundationH262DecoderError>
MediaFoundationH262Decoder::Create(
    const H262SequenceHeaderFacts &sequence,
    const MediaFoundationH262DecoderLimits limits) {
  if (auto error = ValidateConfiguration(sequence, limits))
    return std::unexpected(std::move(*error));
  return std::unexpected(
      MakeError(ErrorCode::UnsupportedPlatform, 0,
                "Media Foundation H.262 decoding requires Windows"));
}

MediaFoundationH262DecodeResult
MediaFoundationH262Decoder::Push(const std::span<const std::byte>,
                                 const std::optional<std::int64_t>) {
  return std::unexpected(
      MakeError(ErrorCode::UnsupportedPlatform, 0,
                "Media Foundation H.262 decoding requires Windows"));
}

MediaFoundationH262DecodeResult MediaFoundationH262Decoder::Drain() {
  return std::unexpected(
      MakeError(ErrorCode::UnsupportedPlatform, 0,
                "Media Foundation H.262 decoding requires Windows"));
}
#else
namespace {
using Microsoft::WRL::ComPtr;

constexpr UINT32 kMaximumDecoderCandidates = 64U;
constexpr DWORD kMaximumOutputTypes = 128U;
constexpr std::uint32_t kMaximumNoProgressIterations = 16U;

[[nodiscard]] Error HresultError(const ErrorCode code, const HRESULT result,
                                 std::string message) {
  return MakeError(code, static_cast<std::int32_t>(result), std::move(message));
}

struct ActivationArray {
  IMFActivate **values = nullptr;
  UINT32 count = 0U;

  ActivationArray() = default;
  ActivationArray(const ActivationArray &) = delete;
  ActivationArray &operator=(const ActivationArray &) = delete;
  ~ActivationArray() {
    for (UINT32 index = 0U; index < count; ++index) {
      if (values[index] != nullptr)
        values[index]->Release();
    }
    CoTaskMemFree(values);
  }
};

[[nodiscard]] bool ResolveStreamIds(IMFTransform &transform,
                                    DWORD &input_stream_id,
                                    DWORD &output_stream_id) noexcept {
  input_stream_id = 0U;
  output_stream_id = 0U;
  const HRESULT result =
      transform.GetStreamIDs(1U, &input_stream_id, 1U, &output_stream_id);
  return SUCCEEDED(result) || result == E_NOTIMPL;
}

[[nodiscard]] HRESULT SendMessage(IMFTransform &transform,
                                  const MFT_MESSAGE_TYPE message,
                                  const ULONG_PTR parameter = 0U) noexcept {
  const HRESULT result = transform.ProcessMessage(message, parameter);
  return result == E_NOTIMPL ? S_OK : result;
}

[[nodiscard]] HRESULT CreateInputType(const H262SequenceHeaderFacts &sequence,
                                      IMFMediaType **const output) {
  ComPtr<IMFMediaType> type;
  HRESULT result = MFCreateMediaType(type.GetAddressOf());
  if (SUCCEEDED(result))
    result = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(result))
    result = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MPEG2);
  if (SUCCEEDED(result))
    result = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, sequence.width,
                                sequence.height);
  if (SUCCEEDED(result)) {
    result = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE,
                                 sequence.frame_rate_numerator,
                                 sequence.frame_rate_denominator);
  }
  if (SUCCEEDED(result)) {
    result = type->SetUINT32(MF_MT_INTERLACE_MODE,
                             MFVideoInterlace_MixedInterlaceOrProgressive);
  }
  if (FAILED(result))
    return result;
  *output = type.Detach();
  return S_OK;
}

struct OutputFormat {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t frame_rate_numerator = 0U;
  std::uint32_t frame_rate_denominator = 1U;
  std::int32_t default_stride = 0;
};

[[nodiscard]] std::expected<OutputFormat, Error>
ReadOutputFormat(IMFMediaType &type, const H262SequenceHeaderFacts &fallback,
                 const MediaFoundationH262DecoderLimits &limits) {
  GUID major_type = GUID_NULL;
  GUID subtype = GUID_NULL;
  HRESULT result = type.GetGUID(MF_MT_MAJOR_TYPE, &major_type);
  if (SUCCEEDED(result))
    result = type.GetGUID(MF_MT_SUBTYPE, &subtype);
  if (FAILED(result) || !IsEqualGUID(major_type, MFMediaType_Video) ||
      !IsEqualGUID(subtype, MFVideoFormat_NV12)) {
    return std::unexpected(HresultError(ErrorCode::OutputTypeUnavailable,
                                        result,
                                        "decoder output is not NV12 video"));
  }

  UINT32 width = fallback.width;
  UINT32 height = fallback.height;
  static_cast<void>(
      MFGetAttributeSize(&type, MF_MT_FRAME_SIZE, &width, &height));
  UINT32 frame_rate_numerator = fallback.frame_rate_numerator;
  UINT32 frame_rate_denominator = fallback.frame_rate_denominator;
  static_cast<void>(MFGetAttributeRatio(
      &type, MF_MT_FRAME_RATE, &frame_rate_numerator, &frame_rate_denominator));

  UINT32 stored_stride = width;
  if (FAILED(type.GetUINT32(MF_MT_DEFAULT_STRIDE, &stored_stride)))
    stored_stride = width;
  const std::int32_t stride = static_cast<std::int32_t>(stored_stride);

  if (width == 0U || height == 0U || (width & 1U) != 0U ||
      (height & 1U) != 0U || frame_rate_numerator == 0U ||
      frame_rate_denominator == 0U || width > limits.maximum_width ||
      height > limits.maximum_height ||
      width > kMediaFoundationH262MaximumWidth ||
      height > kMediaFoundationH262MaximumHeight || stride <= 0 ||
      static_cast<std::uint32_t>(stride) < width) {
    return std::unexpected(
        MakeError(ErrorCode::UnsupportedOutputLayout, 0,
                  "decoder published an unsupported NV12 layout"));
  }
  const std::uint64_t frame_bytes =
      static_cast<std::uint64_t>(width) * height * 3U / 2U;
  if (frame_bytes == 0U ||
      frame_bytes > kMediaFoundationH262MaximumFrameBytes ||
      frame_bytes > limits.maximum_output_bytes_per_call) {
    return std::unexpected(
        MakeError(ErrorCode::OutputLimitExceeded, 0,
                  "decoder NV12 output exceeds the per-call byte limit"));
  }
  return OutputFormat{
      .width = width,
      .height = height,
      .frame_rate_numerator = frame_rate_numerator,
      .frame_rate_denominator = frame_rate_denominator,
      .default_stride = stride,
  };
}

struct OutputSelection {
  ComPtr<IMFMediaType> type;
  OutputFormat format;
};

[[nodiscard]] std::expected<OutputSelection, Error>
SelectNv12OutputType(IMFTransform &transform, const DWORD output_stream_id,
                     const H262SequenceHeaderFacts &sequence,
                     const MediaFoundationH262DecoderLimits &limits) {
  HRESULT last_result = MF_E_INVALIDMEDIATYPE;
  for (DWORD index = 0U; index < kMaximumOutputTypes; ++index) {
    ComPtr<IMFMediaType> type;
    const HRESULT available = transform.GetOutputAvailableType(
        output_stream_id, index, type.GetAddressOf());
    if (available == MF_E_NO_MORE_TYPES)
      break;
    if (FAILED(available)) {
      return std::unexpected(
          HresultError(ErrorCode::OutputTypeUnavailable, available,
                       "decoder output-type enumeration failed"));
    }

    GUID subtype = GUID_NULL;
    if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
        !IsEqualGUID(subtype, MFVideoFormat_NV12)) {
      continue;
    }
    auto format = ReadOutputFormat(*type.Get(), sequence, limits);
    if (!format)
      return std::unexpected(std::move(format.error()));

    HRESULT result = transform.SetOutputType(output_stream_id, type.Get(),
                                             MFT_SET_TYPE_TEST_ONLY);
    if (SUCCEEDED(result))
      result = transform.SetOutputType(output_stream_id, type.Get(), 0U);
    if (SUCCEEDED(result))
      return OutputSelection{.type = std::move(type), .format = *format};
    last_result = result;
  }
  return std::unexpected(
      HresultError(ErrorCode::OutputTypeUnavailable, last_result,
                   "decoder exposes no accepted NV12 output type"));
}

struct DecoderActivation {
  ComPtr<IMFTransform> transform;
  DWORD input_stream_id = 0U;
  DWORD output_stream_id = 0U;
  OutputSelection output;
};

[[nodiscard]] std::expected<DecoderActivation, Error>
ActivateDecoder(const H262SequenceHeaderFacts &sequence,
                const MediaFoundationH262DecoderLimits &limits) {
  ComPtr<IMFMediaType> input_type;
  HRESULT result = CreateInputType(sequence, input_type.GetAddressOf());
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::MediaTypeRejected, result,
                     "failed to create the H.262 input media type"));
  }

  const MFT_REGISTER_TYPE_INFO registration{
      .guidMajorType = MFMediaType_Video,
      .guidSubtype = MFVideoFormat_MPEG2,
  };
  ActivationArray activations;
  result = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                     MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                     &registration, nullptr, &activations.values,
                     &activations.count);
  if (FAILED(result)) {
    return std::unexpected(HresultError(ErrorCode::InitializationFailed, result,
                                        "MPEG-2 decoder enumeration failed"));
  }

  const UINT32 candidate_count =
      std::min(activations.count, kMaximumDecoderCandidates);
  HRESULT last_result = MF_E_TOPO_CODEC_NOT_FOUND;
  for (UINT32 index = 0U; index < candidate_count; ++index) {
    DecoderActivation candidate;
    result = activations.values[index]->ActivateObject(
        IID_PPV_ARGS(candidate.transform.GetAddressOf()));
    if (FAILED(result)) {
      last_result = result;
      continue;
    }

    ComPtr<IMFAttributes> attributes;
    UINT32 asynchronous = FALSE;
    if (SUCCEEDED(
            candidate.transform->GetAttributes(attributes.GetAddressOf())) &&
        SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &asynchronous)) &&
        asynchronous != FALSE) {
      last_result = MF_E_INVALIDREQUEST;
      continue;
    }
    if (!ResolveStreamIds(*candidate.transform.Get(), candidate.input_stream_id,
                          candidate.output_stream_id)) {
      last_result = MF_E_INVALIDSTREAMNUMBER;
      continue;
    }

    result = candidate.transform->SetInputType(
        candidate.input_stream_id, input_type.Get(), MFT_SET_TYPE_TEST_ONLY);
    if (SUCCEEDED(result)) {
      result = candidate.transform->SetInputType(candidate.input_stream_id,
                                                 input_type.Get(), 0U);
    }
    if (FAILED(result)) {
      last_result = result;
      continue;
    }

    auto output =
        SelectNv12OutputType(*candidate.transform.Get(),
                             candidate.output_stream_id, sequence, limits);
    if (!output) {
      last_result = static_cast<HRESULT>(output.error().platform_code);
      continue;
    }
    candidate.output = std::move(*output);
    return candidate;
  }

  return std::unexpected(HresultError(
      candidate_count == 0U ? ErrorCode::DecoderUnavailable
                            : ErrorCode::MediaTypeRejected,
      last_result,
      "no synchronous MPEG-2 decoder accepted the H.262 contract"));
}

class OutputClock {
public:
  OutputClock(const std::uint32_t numerator,
              const std::uint32_t denominator) noexcept {
    Reconfigure(numerator, denominator);
  }

  void Reconfigure(const std::uint32_t numerator,
                   const std::uint32_t denominator) noexcept {
    step_numerator_ = 10'000'000ULL * denominator;
    step_denominator_ = numerator;
    remainder_ = 0U;
  }

  struct Stamp {
    std::int64_t timestamp = 0;
    std::int64_t duration = 0;
  };

  [[nodiscard]] Stamp
  Observe(const std::optional<std::int64_t> observed_timestamp,
          const std::optional<std::int64_t> observed_duration) noexcept {
    const std::uint64_t accumulated = step_numerator_ + remainder_;
    const std::uint64_t cadence_duration = accumulated / step_denominator_;
    remainder_ = accumulated % step_denominator_;
    const std::int64_t duration =
        observed_duration && *observed_duration > 0
            ? *observed_duration
            : static_cast<std::int64_t>(cadence_duration);

    std::int64_t timestamp = 0;
    if (!previous_end_) {
      timestamp = observed_timestamp.value_or(0);
    } else if (observed_timestamp && *observed_timestamp > *previous_end_) {
      timestamp = *observed_timestamp;
    } else {
      timestamp = *previous_end_;
    }
    if (timestamp <= std::numeric_limits<std::int64_t>::max() - duration)
      previous_end_ = timestamp + duration;
    else
      previous_end_ = std::numeric_limits<std::int64_t>::max();
    return Stamp{.timestamp = timestamp, .duration = duration};
  }

private:
  std::uint64_t step_numerator_ = 0U;
  std::uint64_t step_denominator_ = 1U;
  std::uint64_t remainder_ = 0U;
  std::optional<std::int64_t> previous_end_;
};

[[nodiscard]] std::expected<ComPtr<IMFSample>, Error>
CreateInputSample(const std::span<const std::byte> bytes,
                  const std::optional<std::int64_t> timestamp_100ns,
                  const bool discontinuity) {
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT result = MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()),
                                        buffer.GetAddressOf());
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to allocate an H.262 input buffer"));
  }

  BYTE *destination = nullptr;
  result = buffer->Lock(&destination, nullptr, nullptr);
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to lock an H.262 input buffer"));
  }
  std::memcpy(destination, bytes.data(), bytes.size());
  const HRESULT unlock_result = buffer->Unlock();
  if (FAILED(unlock_result)) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, unlock_result,
                     "failed to unlock an H.262 input buffer"));
  }
  result = buffer->SetCurrentLength(static_cast<DWORD>(bytes.size()));

  ComPtr<IMFSample> sample;
  if (SUCCEEDED(result))
    result = MFCreateSample(sample.GetAddressOf());
  if (SUCCEEDED(result))
    result = sample->AddBuffer(buffer.Get());
  if (SUCCEEDED(result) && timestamp_100ns)
    result = sample->SetSampleTime(*timestamp_100ns);
  if (SUCCEEDED(result) && discontinuity)
    result = sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to construct an H.262 input sample"));
  }
  return sample;
}

[[nodiscard]] std::expected<OwnedNv12VideoFrame, Error>
CopyNv12Frame(IMFSample &sample, const OutputFormat &format,
              OutputClock &clock) {
  const std::uint64_t luma_bytes =
      static_cast<std::uint64_t>(format.width) * format.height;
  const std::uint64_t frame_bytes = luma_bytes + luma_bytes / 2U;
  if (frame_bytes == 0U ||
      frame_bytes > kMediaFoundationH262MaximumFrameBytes ||
      frame_bytes > std::numeric_limits<DWORD>::max() ||
      frame_bytes > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected(MakeError(ErrorCode::UnsupportedOutputLayout, 0,
                                     "decoded NV12 frame size is unsupported"));
  }

  std::vector<std::byte> pixels;
  try {
    pixels.resize(static_cast<std::size_t>(frame_bytes));
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(ErrorCode::AllocationFailed, 0,
                                     "decoded NV12 frame allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        MakeError(ErrorCode::OutputLimitExceeded, 0,
                  "decoded NV12 frame exceeds vector capacity"));
  }

  ComPtr<IMFMediaBuffer> buffer;
  HRESULT result = sample.ConvertToContiguousBuffer(buffer.GetAddressOf());
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to access a decoded NV12 sample"));
  }

  ComPtr<IMF2DBuffer> two_dimensional;
  if (SUCCEEDED(buffer.As(&two_dimensional))) {
    DWORD contiguous_length = 0U;
    result = two_dimensional->GetContiguousLength(&contiguous_length);
    if (FAILED(result) || contiguous_length != frame_bytes) {
      return std::unexpected(HresultError(
          ErrorCode::UnsupportedOutputLayout, result,
          "decoded NV12 two-dimensional buffer has an unsupported size"));
    }
    result = two_dimensional->ContiguousCopyTo(
        reinterpret_cast<BYTE *>(pixels.data()), contiguous_length);
    if (FAILED(result)) {
      return std::unexpected(
          HresultError(ErrorCode::TransformFailure, result,
                       "failed to copy a decoded NV12 surface"));
    }
  } else {
    BYTE *source = nullptr;
    DWORD maximum_length = 0U;
    DWORD current_length = 0U;
    result = buffer->Lock(&source, &maximum_length, &current_length);
    if (FAILED(result)) {
      return std::unexpected(
          HresultError(ErrorCode::TransformFailure, result,
                       "failed to lock a decoded NV12 buffer"));
    }

    const std::uint64_t source_stride =
        static_cast<std::uint32_t>(format.default_stride);
    const std::uint64_t required_source_bytes =
        source_stride * format.height + source_stride * (format.height / 2U);
    if (source_stride < format.width ||
        required_source_bytes > current_length ||
        required_source_bytes > maximum_length) {
      static_cast<void>(buffer->Unlock());
      return std::unexpected(
          MakeError(ErrorCode::UnsupportedOutputLayout, 0,
                    "decoded NV12 buffer is truncated or padded unexpectedly"));
    }

    for (std::uint32_t row = 0U; row < format.height; ++row) {
      std::memcpy(pixels.data() + static_cast<std::size_t>(row) * format.width,
                  source + static_cast<std::size_t>(row) * source_stride,
                  format.width);
    }
    const std::size_t source_chroma =
        static_cast<std::size_t>(source_stride) * format.height;
    const std::size_t destination_chroma = static_cast<std::size_t>(luma_bytes);
    for (std::uint32_t row = 0U; row < format.height / 2U; ++row) {
      std::memcpy(pixels.data() + destination_chroma +
                      static_cast<std::size_t>(row) * format.width,
                  source + source_chroma +
                      static_cast<std::size_t>(row) * source_stride,
                  format.width);
    }
    const HRESULT unlock_result = buffer->Unlock();
    if (FAILED(unlock_result)) {
      return std::unexpected(
          HresultError(ErrorCode::TransformFailure, unlock_result,
                       "failed to unlock a decoded NV12 buffer"));
    }
  }

  LONGLONG observed_timestamp = 0;
  std::optional<std::int64_t> timestamp;
  result = sample.GetSampleTime(&observed_timestamp);
  if (SUCCEEDED(result))
    timestamp = observed_timestamp;
  else if (result != MF_E_NO_SAMPLE_TIMESTAMP) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to read a decoded frame timestamp"));
  }
  LONGLONG observed_duration = 0;
  std::optional<std::int64_t> duration;
  result = sample.GetSampleDuration(&observed_duration);
  if (SUCCEEDED(result)) {
    if (observed_duration > 0)
      duration = observed_duration;
  } else if (result != MF_E_NO_SAMPLE_DURATION) {
    return std::unexpected(
        HresultError(ErrorCode::TransformFailure, result,
                     "failed to read a decoded frame duration"));
  }
  const OutputClock::Stamp stamp = clock.Observe(timestamp, duration);
  return OwnedNv12VideoFrame{
      .width = format.width,
      .height = format.height,
      .luma_stride = format.width,
      .chroma_stride = format.width,
      .timestamp_100ns = stamp.timestamp,
      .duration_100ns = stamp.duration,
      .pixels = std::move(pixels),
  };
}
} // namespace

struct MediaFoundationH262Decoder::Impl {
  explicit Impl(const H262SequenceHeaderFacts &sequence_facts,
                const MediaFoundationH262DecoderLimits decoder_limits) noexcept
      : creator_thread_id(GetCurrentThreadId()), sequence(sequence_facts),
        limits(decoder_limits), clock(sequence_facts.frame_rate_numerator,
                                      sequence_facts.frame_rate_denominator) {}

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;

  ~Impl() {
    assert(GetCurrentThreadId() == creator_thread_id);
    if (streaming_started && transform) {
      static_cast<void>(
          SendMessage(*transform.Get(), MFT_MESSAGE_NOTIFY_END_STREAMING));
    }
    transform.Reset();
    output_type.Reset();
    if (mf_started)
      MFShutdown();
    if (com_uninitialize)
      CoUninitialize();
  }

  [[nodiscard]] std::optional<Error> CheckThread() const {
    if (GetCurrentThreadId() == creator_thread_id)
      return std::nullopt;
    return MakeError(
        ErrorCode::WrongThread, 0,
        "Media Foundation H.262 decoder called from the wrong thread");
  }

  [[nodiscard]] Error Poison(Error error) {
    poisoned = true;
    return error;
  }

  [[nodiscard]] std::expected<std::vector<OwnedNv12VideoFrame>, Error>
  PullUntilNeedInput() {
    std::vector<OwnedNv12VideoFrame> frames;
    if (output_stream_ended)
      return frames;
    try {
      frames.reserve(static_cast<std::size_t>(
          std::min<std::uint64_t>(limits.maximum_frames_per_call, 16U)));
    } catch (const std::bad_alloc &) {
      return std::unexpected(
          Poison(MakeError(ErrorCode::AllocationFailed, 0,
                           "decoded frame-list allocation failed")));
    } catch (const std::length_error &) {
      return std::unexpected(Poison(
          MakeError(ErrorCode::OutputLimitExceeded, 0,
                    "decoded frame-list reserve exceeds vector capacity")));
    }

    std::uint64_t output_bytes = 0U;
    auto append_from_sample = [&](IMFSample &sample) -> std::optional<Error> {
      auto frame = CopyNv12Frame(sample, output_format, clock);
      if (!frame)
        return Poison(std::move(frame.error()));
      const std::uint64_t frame_bytes = frame->pixels.size();
      if (frames.size() >= limits.maximum_frames_per_call ||
          decoded_frame_count >= limits.maximum_total_frames) {
        return Poison(
            MakeError(ErrorCode::FrameLimitExceeded, 0,
                      "decoded frame count exceeds a configured limit"));
      }
      if (frame_bytes > limits.maximum_output_bytes_per_call - output_bytes) {
        return Poison(
            MakeError(ErrorCode::OutputLimitExceeded, 0,
                      "decoded frame bytes exceed the per-call output limit"));
      }
      try {
        frames.push_back(std::move(*frame));
      } catch (const std::bad_alloc &) {
        return Poison(MakeError(ErrorCode::AllocationFailed, 0,
                                "decoded frame-list growth failed"));
      } catch (const std::length_error &) {
        return Poison(MakeError(ErrorCode::OutputLimitExceeded, 0,
                                "decoded frame-list exceeds vector capacity"));
      }
      output_bytes += frame_bytes;
      ++decoded_frame_count;
      return std::nullopt;
    };

    std::uint32_t no_progress = 0U;
    for (;;) {
      MFT_OUTPUT_STREAM_INFO stream_info{};
      HRESULT result =
          transform->GetOutputStreamInfo(output_stream_id, &stream_info);
      if (FAILED(result)) {
        return std::unexpected(Poison(HresultError(
            ErrorCode::TransformFailure, result,
            "failed to query MPEG-2 decoder output requirements")));
      }
      if (stream_info.cbSize > kMediaFoundationH262MaximumFrameBytes ||
          stream_info.cbSize > limits.maximum_output_bytes_per_call) {
        return std::unexpected(Poison(
            MakeError(ErrorCode::OutputLimitExceeded, 0,
                      "decoder output buffer exceeds a configured limit")));
      }

      ComPtr<IMFSample> provided_sample;
      if ((stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                  MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) ==
          0U) {
        if (stream_info.cbSize == 0U) {
          return std::unexpected(Poison(MakeError(
              ErrorCode::TransformFailure, 0,
              "decoder requested an empty caller-owned output buffer")));
        }
        ComPtr<IMFMediaBuffer> buffer;
        result = MFCreateAlignedMemoryBuffer(
            stream_info.cbSize, stream_info.cbAlignment, buffer.GetAddressOf());
        if (SUCCEEDED(result))
          result = MFCreateSample(provided_sample.GetAddressOf());
        if (SUCCEEDED(result))
          result = provided_sample->AddBuffer(buffer.Get());
        if (FAILED(result)) {
          return std::unexpected(Poison(
              HresultError(ErrorCode::TransformFailure, result,
                           "failed to allocate a decoder output sample")));
        }
      }

      MFT_OUTPUT_DATA_BUFFER output{};
      output.dwStreamID = output_stream_id;
      output.pSample = provided_sample.Get();
      DWORD status = 0U;
      result = transform->ProcessOutput(0U, 1U, &output, &status);
      if (output.pEvents != nullptr) {
        output.pEvents->Release();
        output.pEvents = nullptr;
      }

      ComPtr<IMFSample> transform_sample;
      if (output.pSample != nullptr && output.pSample != provided_sample.Get())
        transform_sample.Attach(output.pSample);
      IMFSample *const sample =
          transform_sample ? transform_sample.Get() : provided_sample.Get();

      if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return frames;
      if (output.dwStatus == MFT_OUTPUT_DATA_BUFFER_STREAM_END) {
        output_stream_ended = true;
        if (!drained) {
          return std::unexpected(Poison(
              MakeError(ErrorCode::TransformFailure, 0,
                        "MPEG-2 decoder output stream ended before drain")));
        }
        return frames;
      }
      if (result == MF_E_TRANSFORM_STREAM_CHANGE ||
          output.dwStatus == MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE) {
        auto selection = SelectNv12OutputType(
            *transform.Get(), output_stream_id, sequence, limits);
        if (!selection) {
          return std::unexpected(Poison(MakeError(
              ErrorCode::OutputTypeUnavailable, selection.error().platform_code,
              "failed to renegotiate NV12 after a decoder stream change")));
        }
        output_type = std::move(selection->type);
        output_format = selection->format;
        clock.Reconfigure(output_format.frame_rate_numerator,
                          output_format.frame_rate_denominator);
        no_progress = 0U;
        continue;
      }
      if (FAILED(result)) {
        return std::unexpected(
            Poison(HresultError(ErrorCode::TransformFailure, result,
                                "MPEG-2 decoder output failed")));
      }
      if (output.dwStatus == MFT_OUTPUT_DATA_BUFFER_NO_SAMPLE ||
          sample == nullptr) {
        if (++no_progress > kMaximumNoProgressIterations) {
          return std::unexpected(
              Poison(MakeError(ErrorCode::TransformFailure, 0,
                               "MPEG-2 decoder made no output progress")));
        }
        continue;
      }
      if (output.dwStatus != 0U &&
          output.dwStatus != MFT_OUTPUT_DATA_BUFFER_INCOMPLETE) {
        return std::unexpected(Poison(
            MakeError(ErrorCode::TransformFailure, 0,
                      "MPEG-2 decoder returned an unsupported output status")));
      }
      no_progress = 0U;
      if (auto error = append_from_sample(*sample))
        return std::unexpected(std::move(*error));
    }
  }

  DWORD creator_thread_id = 0U;
  H262SequenceHeaderFacts sequence;
  MediaFoundationH262DecoderLimits limits;
  bool com_uninitialize = false;
  bool mf_started = false;
  bool streaming_started = false;
  bool first_input = true;
  bool drained = false;
  bool poisoned = false;
  bool output_stream_ended = false;
  std::uint64_t accepted_input_bytes = 0U;
  std::uint64_t decoded_frame_count = 0U;
  ComPtr<IMFTransform> transform;
  DWORD input_stream_id = 0U;
  DWORD output_stream_id = 0U;
  ComPtr<IMFMediaType> output_type;
  OutputFormat output_format;
  OutputClock clock;
};

MediaFoundationH262Decoder::MediaFoundationH262Decoder(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

MediaFoundationH262Decoder::MediaFoundationH262Decoder(
    MediaFoundationH262Decoder &&) noexcept = default;
MediaFoundationH262Decoder::~MediaFoundationH262Decoder() = default;

std::expected<MediaFoundationH262Decoder, MediaFoundationH262DecoderError>
MediaFoundationH262Decoder::Create(
    const H262SequenceHeaderFacts &sequence,
    const MediaFoundationH262DecoderLimits limits) {
  if (auto error = ValidateConfiguration(sequence, limits))
    return std::unexpected(std::move(*error));

  std::unique_ptr<Impl> impl;
  try {
    impl = std::make_unique<Impl>(sequence, limits);
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        MakeError(ErrorCode::AllocationFailed, 0,
                  "Media Foundation decoder allocation failed"));
  }

  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(result))
    impl->com_uninitialize = true;
  else if (result != RPC_E_CHANGED_MODE) {
    return std::unexpected(HresultError(ErrorCode::InitializationFailed, result,
                                        "COM initialization failed"));
  }

  result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(result)) {
    return std::unexpected(HresultError(ErrorCode::InitializationFailed, result,
                                        "Media Foundation startup failed"));
  }
  impl->mf_started = true;

  auto activation = ActivateDecoder(sequence, limits);
  if (!activation)
    return std::unexpected(std::move(activation.error()));
  impl->transform = std::move(activation->transform);
  impl->input_stream_id = activation->input_stream_id;
  impl->output_stream_id = activation->output_stream_id;
  impl->output_type = std::move(activation->output.type);
  impl->output_format = activation->output.format;
  impl->clock.Reconfigure(impl->output_format.frame_rate_numerator,
                          impl->output_format.frame_rate_denominator);

  result =
      SendMessage(*impl->transform.Get(), MFT_MESSAGE_NOTIFY_BEGIN_STREAMING);
  if (SUCCEEDED(result))
    result =
        SendMessage(*impl->transform.Get(), MFT_MESSAGE_NOTIFY_START_OF_STREAM);
  if (FAILED(result)) {
    return std::unexpected(
        HresultError(ErrorCode::InitializationFailed, result,
                     "MPEG-2 decoder streaming startup failed"));
  }
  impl->streaming_started = true;
  return MediaFoundationH262Decoder(std::move(impl));
}

MediaFoundationH262DecodeResult MediaFoundationH262Decoder::Push(
    const std::span<const std::byte> elementary_bytes,
    const std::optional<std::int64_t> timestamp_100ns) {
  if (!impl_) {
    return std::unexpected(
        MakeError(ErrorCode::DecoderPoisoned, 0,
                  "Media Foundation H.262 decoder is moved-from"));
  }
  if (auto error = impl_->CheckThread())
    return std::unexpected(std::move(*error));
  if (impl_->poisoned) {
    return std::unexpected(
        MakeError(ErrorCode::DecoderPoisoned, 0,
                  "Media Foundation H.262 decoder is poisoned"));
  }
  if (impl_->drained) {
    return std::unexpected(
        MakeError(ErrorCode::AlreadyDrained, 0,
                  "Media Foundation H.262 decoder is already drained"));
  }
  if (elementary_bytes.empty()) {
    return std::unexpected(MakeError(ErrorCode::InvalidConfiguration, 0,
                                     "H.262 input chunks must be nonempty"));
  }
  if (elementary_bytes.size() > impl_->limits.maximum_input_chunk_bytes ||
      elementary_bytes.size() > std::numeric_limits<DWORD>::max()) {
    return std::unexpected(impl_->Poison(
        MakeError(ErrorCode::InputLimitExceeded, 0,
                  "H.262 input chunk exceeds a configured limit")));
  }
  if (elementary_bytes.size() >
      impl_->limits.maximum_total_input_bytes - impl_->accepted_input_bytes) {
    return std::unexpected(impl_->Poison(MakeError(
        ErrorCode::InputLimitExceeded, 0,
        "H.262 input exceeds the configured decoder-lifetime limit")));
  }

  auto input_sample =
      CreateInputSample(elementary_bytes, timestamp_100ns, impl_->first_input);
  if (!input_sample)
    return std::unexpected(impl_->Poison(std::move(input_sample.error())));
  impl_->first_input = false;

  std::vector<OwnedNv12VideoFrame> frames;
  std::uint64_t output_bytes = 0U;
  const auto merge_frames = [&](std::vector<OwnedNv12VideoFrame> &&batch)
      -> std::optional<MediaFoundationH262DecoderError> {
    if (frames.size() > impl_->limits.maximum_frames_per_call ||
        batch.size() > impl_->limits.maximum_frames_per_call - frames.size()) {
      return impl_->Poison(
          MakeError(ErrorCode::FrameLimitExceeded, 0,
                    "decoded frame count exceeds the per-call frame limit"));
    }
    std::uint64_t batch_bytes = 0U;
    for (const OwnedNv12VideoFrame &frame : batch) {
      if (frame.pixels.size() >
          impl_->limits.maximum_output_bytes_per_call - batch_bytes) {
        return impl_->Poison(
            MakeError(ErrorCode::OutputLimitExceeded, 0,
                      "decoded frame bytes exceed the per-call output limit"));
      }
      batch_bytes += frame.pixels.size();
    }
    if (batch_bytes >
        impl_->limits.maximum_output_bytes_per_call - output_bytes) {
      return impl_->Poison(
          MakeError(ErrorCode::OutputLimitExceeded, 0,
                    "decoded frame bytes exceed the per-call output limit"));
    }
    try {
      frames.insert(frames.end(), std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
    } catch (const std::bad_alloc &) {
      return impl_->Poison(MakeError(ErrorCode::AllocationFailed, 0,
                                     "decoded frame-list merge failed"));
    } catch (const std::length_error &) {
      return impl_->Poison(
          MakeError(ErrorCode::OutputLimitExceeded, 0,
                    "decoded frame-list exceeds vector capacity"));
    }
    output_bytes += batch_bytes;
    return std::nullopt;
  };
  std::uint32_t rejected_count = 0U;
  for (;;) {
    const HRESULT result = impl_->transform->ProcessInput(
        impl_->input_stream_id, input_sample->Get(), 0U);
    if (SUCCEEDED(result)) {
      impl_->accepted_input_bytes += elementary_bytes.size();
      auto pulled = impl_->PullUntilNeedInput();
      if (!pulled)
        return std::unexpected(std::move(pulled.error()));
      if (auto error = merge_frames(std::move(*pulled)))
        return std::unexpected(std::move(*error));
      return frames;
    }
    if (result != MF_E_NOTACCEPTING) {
      return std::unexpected(impl_->Poison(HresultError(
          ErrorCode::TransformFailure, result, "MPEG-2 decoder input failed")));
    }
    if (++rejected_count > kMaximumNoProgressIterations) {
      return std::unexpected(impl_->Poison(MakeError(
          ErrorCode::TransformFailure, 0,
          "MPEG-2 decoder repeatedly rejected input without progress")));
    }
    auto pulled = impl_->PullUntilNeedInput();
    if (!pulled)
      return std::unexpected(std::move(pulled.error()));
    if (auto error = merge_frames(std::move(*pulled)))
      return std::unexpected(std::move(*error));
  }
}

MediaFoundationH262DecodeResult MediaFoundationH262Decoder::Drain() {
  if (!impl_) {
    return std::unexpected(
        MakeError(ErrorCode::DecoderPoisoned, 0,
                  "Media Foundation H.262 decoder is moved-from"));
  }
  if (auto error = impl_->CheckThread())
    return std::unexpected(std::move(*error));
  if (impl_->poisoned) {
    return std::unexpected(
        MakeError(ErrorCode::DecoderPoisoned, 0,
                  "Media Foundation H.262 decoder is poisoned"));
  }
  if (impl_->drained) {
    return std::unexpected(
        MakeError(ErrorCode::AlreadyDrained, 0,
                  "Media Foundation H.262 decoder is already drained"));
  }
  impl_->drained = true;

  HRESULT result =
      SendMessage(*impl_->transform.Get(), MFT_MESSAGE_NOTIFY_END_OF_STREAM,
                  static_cast<ULONG_PTR>(impl_->input_stream_id));
  if (SUCCEEDED(result))
    result = SendMessage(*impl_->transform.Get(), MFT_MESSAGE_COMMAND_DRAIN);
  if (FAILED(result)) {
    return std::unexpected(
        impl_->Poison(HresultError(ErrorCode::TransformFailure, result,
                                   "MPEG-2 decoder drain startup failed")));
  }
  return impl_->PullUntilNeedInput();
}
#endif
} // namespace omega::media
