/**
 * @file tests/unit/platform/windows/test_beam_audio.cpp
 * @brief Beam: widening captured audio to the stream's channel count (S3).
 */
#include "../../../tests_common.h"

#include <src/platform/windows/beam_audio.h>

#include <vector>

// A stereo capture sent as 7.1: front left and right carry it, the other six are silent.
TEST(BeamAudioTest, StereoBecomesSevenOneWithTheRestSilent) {
  const std::vector<float> stereo = {0.1f, 0.2f, 0.3f, 0.4f};  // two frames
  std::vector<float> out(2 * 8, -1.0f);

  platf::audio::beam::widen_frames(stereo.data(), 2, out.data(), 8, 2, false);

  const std::vector<float> expected = {
    0.1f, 0.2f, 0, 0, 0, 0, 0, 0,
    0.3f, 0.4f, 0, 0, 0, 0, 0, 0,
  };
  EXPECT_EQ(out, expected);
}

TEST(BeamAudioTest, ASilentCaptureStaysSilent) {
  const std::vector<float> stereo = {0.5f, 0.5f};
  std::vector<float> out(6, -1.0f);

  platf::audio::beam::widen_frames(stereo.data(), 2, out.data(), 6, 1, true);

  EXPECT_EQ(out, std::vector<float>(6, 0.0f));
}
