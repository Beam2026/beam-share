/**
 * @file src/platform/windows/beam_audio.h
 * @brief Beam: audio captured in fewer channels than the stream carries, widened to fit (S3).
 */
#pragma once

// standard includes
#include <cstdint>

namespace platf::audio::beam {
  /**
   * @brief Widen captured frames to the stream's channel count.
   *
   * Writes `frames` frames of `out_channels` samples to `out`, from frames of `in_channels` samples
   * in `in`: the captured channels first -- front left and right, in every layout Sunshine streams
   * -- and the rest silent. All of it silent when the capture said so.
   */
  inline void widen_frames(const float *in, int in_channels, float *out, int out_channels, std::uint32_t frames, bool silent) {
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      for (int channel = 0; channel < out_channels; ++channel) {
        out[frame * out_channels + channel] = (!silent && channel < in_channels) ? in[frame * in_channels + channel] : 0.0f;
      }
    }
  }
}  // namespace platf::audio::beam
