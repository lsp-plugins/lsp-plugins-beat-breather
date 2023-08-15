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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/beat_breather.h>

#define LSP_PLUGINS_BEAT_BREATHER_VERSION_MAJOR       1
#define LSP_PLUGINS_BEAT_BREATHER_VERSION_MINOR       0
#define LSP_PLUGINS_BEAT_BREATHER_VERSION_MICRO       0

#define LSP_PLUGINS_BEAT_BREATHER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_BEAT_BREATHER_VERSION_MAJOR, \
        LSP_PLUGINS_BEAT_BREATHER_VERSION_MINOR, \
        LSP_PLUGINS_BEAT_BREATHER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Plugin metadata
        static const port_item_t bb_tabs[] =
        {
            { "Crossover",      "beat_breather.tabs.crossover"          },
            { "Punch Filter",   "beat_breather.tabs.punch_filter"       },
            { "Beat Processor", "beat_breather.tabs.beat_processor"     },
            { NULL, NULL }
        };

        static const port_item_t bb_listen[] =
        {
            { "Crossover",      "beat_breather.listen.crossover"        },
            { "RMS",            "beat_breather.listen.RMS"              },
            { "Punch Filter",   "beat_breather.listen.punch_filter"     },
            { "Beat Processor", "beat_breather.listen.beat_processor"   },
            { NULL, NULL }
        };

        #define BB_COMMON \
            BYPASS, \
            IN_GAIN, \
            AMP_GAIN("g_dry", "Dry gain", 0.0f, 10.0f), \
            AMP_GAIN("g_wet", "Wet gain", 1.0f, 10.0f), \
            OUT_GAIN, \
            COMBO("ts", "Tab selector", 0, bb_tabs), \
            LOG_CONTROL("react", "FFT reactivity", U_MSEC, beat_breather::FFT_REACT_TIME), \
            CONTROL("shift", "FFT shift gain", U_DB, beat_breather::FFT_SHIFT), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, beat_breather::ZOOM)

        #define BB_COMMON_STEREO \
            SWITCH("ssplit", "Stereo split", 0.0f)

        #define BB_CHANNEL_METERS(id, label) \
            METER_GAIN("ilm" id, "Input level meter" label, GAIN_AMP_P_24_DB), \
            METER_GAIN("olm" id, "Output level meter" label, GAIN_AMP_P_24_DB), \
            SWITCH("ife" id, "Input FFT graph enable" label, 1.0f), \
            SWITCH("ofe" id, "Output FFT graph enable" label, 1.0f), \
            MESH("ifg" id, "Input FFT graph" label, 2, beat_breather::FFT_MESH_POINTS + 2), \
            MESH("ofg" id, "Output FFT graph" label, 2, beat_breather::FFT_MESH_POINTS), \
            MESH("ag" id, "Output filter graph" label, 2, beat_breather::FFT_MESH_POINTS)

        #define BB_SPLIT(id, label, on, freq) \
            SWITCH("se" id, "Frequency split enable" label, on), \
            LOG_CONTROL_DFL("sf" id, "Split frequency" label, U_HZ, beat_breather::FREQ, freq)

        #define BB_BAND(id, label) \
            SWITCH("bs" id, "Solo band" label, 0.0f), \
            SWITCH("bm" id, "Mute band" label, 0.0f), \
            COMBO("bls" id, "Band listen stage" label, beat_breather::LISTEN_DFL, bb_listen), \
            CONTROL("lps" id, "Lo-pass slope" label, U_DB, beat_breather::SLOPE), \
            CONTROL("hps" id, "Hi-pass slope" label, U_DB, beat_breather::SLOPE), \
            CONTROL("flat" id, "Filter cap flatten" label, U_DB, beat_breather::FLATTEN), \
            LOG_CONTROL("bg" id, "Band output gain" label, U_GAIN_AMP, beat_breather::BAND_GAIN), \
            METER("fre" id, "Frequency range end" label, U_HZ,  beat_breather::OUT_FREQ), \
            MESH("bfg" id, "Band filter graph" label, 2, beat_breather::FFT_MESH_POINTS + 2), \
            CONTROL("pflt" id, "Punch filter long-time RMS estimation" label, U_GAIN_AMP, beat_breather::LONG_RMS), \
            CONTROL("pfst" id, "Punch filter short-time RMS estimation" label, U_GAIN_AMP, beat_breather::SHORT_RMS), \
            CONTROL("pflk" id, "Punch filter lookahead" label, U_MSEC, beat_breather::PF_LOOKAHEAD), \
            LOG_CONTROL("pfat" id, "Punch filter attack time" label, U_MSEC, beat_breather::PF_ATTACK), \
            LOG_CONTROL("pfrt" id, "Punch filter release time" label, U_MSEC, beat_breather::PF_RELEASE), \
            CONTROL("pfth" id, "Punch filter threshold" label, U_DB, beat_breather::PF_THRESHOLD), \
            CONTROL("pfrl" id, "Punch filter reduction level" label, U_DB, beat_breather::PF_REDUCTION), \
            CONTROL("pfrz" id, "Punch filter reduction zone" label, U_DB, beat_breather::PF_ZONE), \
            MESH("pfg" id, "Punch filter curve graph" label, 2, beat_breather::CURVE_MESH_POINTS), \
            LOG_CONTROL("bpat" id, "Beat processor attack time" label, U_DB, beat_breather::BP_ATTACK), \
            LOG_CONTROL("bprt" id, "Beat processor release time" label, U_DB, beat_breather::BP_RELEASE), \
            CONTROL("bpts" id, "Beat processor time shift" label, U_MSEC, beat_breather::BP_TIME_SHIFT), \
            CONTROL("bpth" id, "Beat processor threshold" label, U_DB, beat_breather::BP_THRESHOLD), \
            CONTROL("bper" id, "Beat processor expand ratio" label, U_NONE, beat_breather::BP_RATIO), \
            CONTROL("bpmg" id, "Beat processor maximum expand gain" label, U_DB, beat_breather::BP_MAX_GAIN), \
            MESH("bpg" id, "Beat processor curve graph" label, 2, beat_breather::CURVE_MESH_POINTS)

        #define BB_BAND_METERS(id, label) \
            METER_OUT_GAIN("ilm" id, "Band input level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("olm" id, "Band output level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("pfem" id, "Punch filter envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("pfcm" id, "Punch filter curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("pfgm" id, "Punch filter gain level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpem" id, "Beat processor envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpcm" id, "Beat processor curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpgm" id, "Beat processor gain level meter" label, GAIN_AMP_P_36_DB)

        static const port_t beat_breather_mono_ports[] =
        {
            // Input and output audio ports
            PORTS_MONO_PLUGIN,
            BB_COMMON,
            BB_CHANNEL_METERS("", ""),

            BB_SPLIT("_1", " 1", 0.0f, 40.0f),
            BB_SPLIT("_2", " 2", 1.0f, 100.0f),
            BB_SPLIT("_3", " 3", 0.0f, 252.0f),
            BB_SPLIT("_4", " 4", 1.0f, 632.0f),
            BB_SPLIT("_5", " 5", 0.0f, 1587.0f),
            BB_SPLIT("_6", " 6", 1.0f, 3984.0f),
            BB_SPLIT("_7", " 7", 0.0f, 10000.0f),

            BB_BAND("_1", " 1"),
            BB_BAND("_2", " 2"),
            BB_BAND("_3", " 3"),
            BB_BAND("_4", " 4"),
            BB_BAND("_5", " 5"),
            BB_BAND("_6", " 6"),
            BB_BAND("_7", " 7"),
            BB_BAND("_8", " 8"),

            BB_BAND_METERS("_1", " 1"),
            BB_BAND_METERS("_2", " 2"),
            BB_BAND_METERS("_3", " 3"),
            BB_BAND_METERS("_4", " 4"),
            BB_BAND_METERS("_5", " 5"),
            BB_BAND_METERS("_6", " 6"),
            BB_BAND_METERS("_7", " 7"),
            BB_BAND_METERS("_8", " 8"),

            PORTS_END
        };

        static const port_t beat_breather_stereo_ports[] =
        {
            // Input and output audio ports
            PORTS_STEREO_PLUGIN,
            BB_COMMON,
            BB_COMMON_STEREO,
            BB_CHANNEL_METERS("_l", " Left"),
            BB_CHANNEL_METERS("_r", " Right"),

            BB_SPLIT("_1", " 1", 0.0f, 40.0f),
            BB_SPLIT("_2", " 2", 1.0f, 100.0f),
            BB_SPLIT("_3", " 3", 0.0f, 252.0f),
            BB_SPLIT("_4", " 4", 1.0f, 632.0f),
            BB_SPLIT("_5", " 5", 0.0f, 1587.0f),
            BB_SPLIT("_6", " 6", 1.0f, 3984.0f),
            BB_SPLIT("_7", " 7", 0.0f, 10000.0f),

            BB_BAND("_1", " 1"),
            BB_BAND("_2", " 2"),
            BB_BAND("_3", " 3"),
            BB_BAND("_4", " 4"),
            BB_BAND("_5", " 5"),
            BB_BAND("_6", " 6"),
            BB_BAND("_7", " 7"),
            BB_BAND("_8", " 8"),

            BB_BAND_METERS("_1l", " 1 Left"),
            BB_BAND_METERS("_2l", " 2 Left"),
            BB_BAND_METERS("_3l", " 3 Left"),
            BB_BAND_METERS("_4l", " 4 Left"),
            BB_BAND_METERS("_5l", " 5 Left"),
            BB_BAND_METERS("_6l", " 6 Left"),
            BB_BAND_METERS("_7l", " 7 Left"),
            BB_BAND_METERS("_8l", " 8 Left"),

            BB_BAND_METERS("_1r", " 1 Right"),
            BB_BAND_METERS("_2r", " 2 Right"),
            BB_BAND_METERS("_3r", " 3 Right"),
            BB_BAND_METERS("_4r", " 4 Right"),
            BB_BAND_METERS("_5r", " 5 Right"),
            BB_BAND_METERS("_6r", " 6 Right"),
            BB_BAND_METERS("_7r", " 7 Right"),
            BB_BAND_METERS("_8r", " 8 Right"),

            PORTS_END
        };

        static const int plugin_classes[]       = { C_DYNAMICS, -1 };
        static const int clap_features_mono[]   = { CF_AUDIO_EFFECT, CF_UTILITY, CF_MONO, -1 };
        static const int clap_features_stereo[] = { CF_AUDIO_EFFECT, CF_UTILITY, CF_STEREO, -1 };

        const meta::bundle_t beat_breather_bundle =
        {
            "beat_breather",
            "Beat Breather",
            B_DYNAMICS,
            "", // TODO: provide ID of the video on YouTube
            "This plugin allows to drive much more dynamics into punchy sounds like drums and make them breathe again."
        };

        const plugin_t beat_breather_mono =
        {
            "Beat Breather Mono",
            "Beat Breather Mono",
            "BB1M",
            &developers::v_sadovnikov,
            "beat_breather_mono",
            LSP_LV2_URI("beat_breather_mono"),
            LSP_LV2UI_URI("beat_breather_mono"),
            "bb1m",
            LSP_LADSPA_BIT_BREATHER_BASE + 0,
            LSP_LADSPA_URI("beat_breather_mono"),
            LSP_CLAP_URI("beat_breather_mono"),
            LSP_PLUGINS_BEAT_BREATHER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            beat_breather_mono_ports,
            "dynamics/beat_breather/mono.xml",
            NULL,
            mono_plugin_port_groups,
            &beat_breather_bundle
        };

        const plugin_t beat_breather_stereo =
        {
            "Beat Breather Stereo",
            "Beat Breather Stereo",
            "BB1S",
            &developers::v_sadovnikov,
            "beat_breather_stereo",
            LSP_LV2_URI("beat_breather_stereo"),
            LSP_LV2UI_URI("beat_breather_stereo"),
            "bb1s",
            LSP_LADSPA_BIT_BREATHER_BASE + 1,
            LSP_LADSPA_URI("beat_breather_stereo"),
            LSP_CLAP_URI("beat_breather_stereo"),
            LSP_PLUGINS_BEAT_BREATHER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            beat_breather_stereo_ports,
            "dynamics/beat_breather/stereo.xml",
            NULL,
            stereo_plugin_port_groups,
            &beat_breather_bundle
        };
    } /* namespace meta */
} /* namespace lsp */



