/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-beat-breather
 * Created on: 14 авг 2023 г.
 *
 * lsp-plugins-beat-breather is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-beat-breather is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-beat-breather. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_BEAT_BREATHER_H_
#define PRIVATE_META_BEAT_BREATHER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>
#include <lsp-plug.in/dsp-units/misc/windows.h>

namespace lsp
{
    //-------------------------------------------------------------------------
    // Plugin metadata
    namespace meta
    {
        typedef struct beat_breather
        {
            static constexpr float  FREQ_MIN                = 10.0f;
            static constexpr float  FREQ_MAX                = 20000.0f;
            static constexpr float  FREQ_DFL                = 1000.0f;
            static constexpr float  FREQ_STEP               = 0.002f;

            static constexpr float  SLOPE_MIN               = 0.0f;
            static constexpr float  SLOPE_MAX               = 72.0f;
            static constexpr float  SLOPE_DFL               = 36.0f;
            static constexpr float  SLOPE_STEP              = 0.1f;

            static constexpr float  FLATTEN_MIN             = 0.0f;
            static constexpr float  FLATTEN_MAX             = 6.0f;
            static constexpr float  FLATTEN_DFL             = 0.0f;
            static constexpr float  FLATTEN_STEP            = 0.01f;

            static constexpr float  BAND_GAIN_MIN           = GAIN_AMP_M_INF_DB;
            static constexpr float  BAND_GAIN_MAX           = GAIN_AMP_P_12_DB;
            static constexpr float  BAND_GAIN_DFL           = GAIN_AMP_0_DB;
            static constexpr float  BAND_GAIN_STEP          = 0.05f;

            static constexpr float  PD_LONG_RMS_MIN         = 100.0f;
            static constexpr float  PD_LONG_RMS_MAX         = 1000.0f;
            static constexpr float  PD_LONG_RMS_DFL         = 400.0f;
            static constexpr float  PD_LONG_RMS_STEP        = 0.5f;

            static constexpr float  PD_SHORT_RMS_MIN        = 0.1f;
            static constexpr float  PD_SHORT_RMS_MAX        = 20.0f;
            static constexpr float  PD_SHORT_RMS_DFL        = 10.0f;
            static constexpr float  PD_SHORT_RMS_STEP       = 0.01f;

            static constexpr float  PD_MAKEUP_MIN           = -12.0f;
            static constexpr float  PD_MAKEUP_MAX           = 12.0f;
            static constexpr float  PD_MAKEUP_DFL           = 0.0f;
            static constexpr float  PD_MAKEUP_STEP          = 0.05f;
            static constexpr float  PD_MAKEUP_SHIFT         = -12.0f;

            static constexpr float  PF_LOOKAHEAD_MIN        = 0.0f;
            static constexpr float  PF_LOOKAHEAD_MAX        = 5.0f;
            static constexpr float  PF_LOOKAHEAD_DFL        = 0.0f;
            static constexpr float  PF_LOOKAHEAD_STEP       = 0.0025f;

            static constexpr float  PF_ATTACK_MIN           = 0.0f;
            static constexpr float  PF_ATTACK_MAX           = 10.0f;
            static constexpr float  PF_ATTACK_DFL           = 1.0f;
            static constexpr float  PF_ATTACK_STEP          = 0.005f;

            static constexpr float  PF_RELEASE_MIN          = 0.0f;
            static constexpr float  PF_RELEASE_MAX          = 100.0f;
            static constexpr float  PF_RELEASE_DFL          = 5.0f;
            static constexpr float  PF_RELEASE_STEP         = 0.005f;

            static constexpr float  PF_THRESHOLD_MIN        = GAIN_AMP_M_24_DB;
            static constexpr float  PF_THRESHOLD_MAX        = GAIN_AMP_P_24_DB;
            static constexpr float  PF_THRESHOLD_DFL        = GAIN_AMP_M_9_DB;
            static constexpr float  PF_THRESHOLD_STEP       = 0.01f;

            static constexpr float  PF_REDUCTION_MIN        = GAIN_AMP_M_48_DB;
            static constexpr float  PF_REDUCTION_MAX        = GAIN_AMP_0_DB;
            static constexpr float  PF_REDUCTION_DFL        = GAIN_AMP_M_12_DB;
            static constexpr float  PF_REDUCTION_STEP       = 0.01f;

            static constexpr float  PF_ZONE_MIN             = GAIN_AMP_M_24_DB;
            static constexpr float  PF_ZONE_MAX             = GAIN_AMP_0_DB;
            static constexpr float  PF_ZONE_DFL             = GAIN_AMP_M_3_DB;
            static constexpr float  PF_ZONE_STEP            = 0.01f;

            static constexpr float  BP_ATTACK_MIN           = 0.0f;
            static constexpr float  BP_ATTACK_MAX           = 100.0f;
            static constexpr float  BP_ATTACK_DFL           = 10.0f;
            static constexpr float  BP_ATTACK_STEP          = 0.001f;

            static constexpr float  BP_RELEASE_MIN          = 0.0f;
            static constexpr float  BP_RELEASE_MAX          = 200.0f;
            static constexpr float  BP_RELEASE_DFL          = 20.0f;
            static constexpr float  BP_RELEASE_STEP         = 0.001f;

            static constexpr float  BP_TIME_SHIFT_MIN       = -5.0f;
            static constexpr float  BP_TIME_SHIFT_MAX       = 5.0f;
            static constexpr float  BP_TIME_SHIFT_DFL       = 0.0f;
            static constexpr float  BP_TIME_SHIFT_STEP      = 0.01f;

            static constexpr float  BP_THRESHOLD_MIN        = -72.0f;
            static constexpr float  BP_THRESHOLD_MAX        = 0.0f;
            static constexpr float  BP_THRESHOLD_DFL        = -24.0f;
            static constexpr float  BP_THRESHOLD_STEP       = 0.1f;

            static constexpr float  BP_RATIO_MIN            = 1.0f;
            static constexpr float  BP_RATIO_MAX            = 10.0f;
            static constexpr float  BP_RATIO_DFL            = 2.0f;
            static constexpr float  BP_RATIO_STEP           = 0.001f;

            static constexpr float  BP_MAX_GAIN_MIN         = 0.0f;
            static constexpr float  BP_MAX_GAIN_MAX         = 24.0f;
            static constexpr float  BP_MAX_GAIN_DFL         = 6.0f;
            static constexpr float  BP_MAX_GAIN_STEP        = 0.1f;

            static constexpr float  FFT_REACT_TIME_MIN      = 0.000f;
            static constexpr float  FFT_REACT_TIME_MAX      = 1.000f;
            static constexpr float  FFT_REACT_TIME_DFL      = 0.200f;
            static constexpr float  FFT_REACT_TIME_STEP     = 0.001f;

            static constexpr float  FFT_SHIFT_MIN           = -40.0f;
            static constexpr float  FFT_SHIFT_MAX           = 60.0f;
            static constexpr float  FFT_SHIFT_DFL           = 0.0f;
            static constexpr float  FFT_SHIFT_STEP          = 0.1f;

            static constexpr float  ZOOM_MIN                = GAIN_AMP_M_18_DB;
            static constexpr float  ZOOM_MAX                = GAIN_AMP_0_DB;
            static constexpr float  ZOOM_DFL                = GAIN_AMP_0_DB;
            static constexpr float  ZOOM_STEP               = 0.0125f;

            static constexpr float  OUT_FREQ_MIN            = 0.0f;
            static constexpr float  OUT_FREQ_MAX            = MAX_SAMPLE_RATE;
            static constexpr float  OUT_FREQ_DFL            = 20000.0f;
            static constexpr float  OUT_FREQ_STEP           = 0.002f;

            static constexpr size_t FFT_MESH_POINTS         = 640;
            static constexpr size_t CURVE_MESH_POINTS       = 256;
            static constexpr size_t BANDS_MAX               = 8;
            static constexpr size_t FFT_XOVER_RANK_MIN      = 12;
            static constexpr size_t FFT_XOVER_FREQ_MIN      = 44100;
            static constexpr size_t FFT_ANALYZER_RANK       = 13;
            static constexpr size_t FFT_ANALYZER_ITEMS      = 1 << FFT_ANALYZER_RANK;
            static constexpr size_t FFT_ANALYZER_WINDOW     = dspu::windows::HANN;
            static constexpr size_t FFT_ANALYZER_RATE       = 20;
            static constexpr float  PF_CURVE_MIN            = -36.0f;
            static constexpr float  PF_CURVE_MAX            = 24.0f;

            static constexpr size_t TIME_MESH_POINTS        = 320;
            static constexpr float  TIME_HISTORY_MAX        = 2.0f;     // Time history of punch detector

            enum listen_t
            {
                LISTEN_CROSSOVER,
                LISTEN_RMS,
                LISTEN_PUNCH,
                LISTEN_BEAT,

                LISTEN_DFL = LISTEN_BEAT
            };

        } beat_breather;

        // Plugin type metadata
        extern const plugin_t beat_breather_mono;
        extern const plugin_t beat_breather_stereo;

    } /* namespace meta */
} /* namespace lsp */

#endif /* PRIVATE_META_BEAT_BREATHER_H_ */
