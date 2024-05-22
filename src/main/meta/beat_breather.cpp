/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
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
#define LSP_PLUGINS_BEAT_BREATHER_VERSION_MICRO       9

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
            { "Band Filter",    "beat_breather.tabs.band_filter"        },
            { "Punch Detector", "beat_breather.tabs.punch_detector"     },
            { "Punch Filter",   "beat_breather.tabs.punch_filter"       },
            { "Beat Processor", "beat_breather.tabs.beat_processor"     },
            { NULL, NULL }
        };

        #define BB_COMMON \
            BYPASS, \
            IN_GAIN, \
            AMP_GAIN("g_dry", "Dry gain", 0.0f, 10.0f), \
            AMP_GAIN("g_wet", "Wet gain", 1.0f, 10.0f), \
            DRYWET(100.0f), \
            OUT_GAIN, \
            COMBO("ts", "Tab selector", 0, bb_tabs), \
            LOG_CONTROL("react", "FFT reactivity", U_MSEC, beat_breather::FFT_REACT_TIME), \
            CONTROL("shift", "FFT shift gain", U_DB, beat_breather::FFT_SHIFT), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, beat_breather::ZOOM), \
            SWITCH("flt", "Show filters", 1.0f)

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

        #define BB_BAND(id, label, short_rms, pf_attack, pf_release, bp_attack, bp_release) \
            SWITCH("bs" id, "Solo band" label, 0.0f), \
            SWITCH("bm" id, "Mute band" label, 0.0f), \
            COMBO("bls" id, "Band listen stage" label, beat_breather::LISTEN_DFL, bb_tabs), \
            CONTROL("lps" id, "Lo-pass slope" label, U_DB, beat_breather::SLOPE), \
            CONTROL("hps" id, "Hi-pass slope" label, U_DB, beat_breather::SLOPE), \
            CONTROL("flat" id, "Filter cap flatten" label, U_DB, beat_breather::FLATTEN), \
            LOG_CONTROL("bg" id, "Band output gain" label, U_GAIN_AMP, beat_breather::BAND_GAIN), \
            METER("fre" id, "Frequency range end" label, U_HZ,  beat_breather::OUT_FREQ), \
            MESH("bfg" id, "Band filter graph" label, 2, beat_breather::FFT_MESH_POINTS + 2), \
            \
            CONTROL("pdlt" id, "Punch detector long-time RMS estimation" label, U_MSEC, beat_breather::PD_LONG_RMS), \
            CONTROL_DFL("pdst" id, "Punch detector short-time RMS estimation" label, U_MSEC, beat_breather::PD_SHORT_RMS, short_rms), \
            CONTROL("pdbs" id, "Punch detector short RMS bias" label, U_DB, beat_breather::PD_BIAS), \
            CONTROL("pdmk" id, "Punch detector makeup" label, U_DB, beat_breather::PD_MAKEUP), \
            \
            CONTROL("pflk" id, "Punch filter lookahead" label, U_MSEC, beat_breather::PF_LOOKAHEAD), \
            LOG_CONTROL_DFL("pfat" id, "Punch filter attack time" label, U_MSEC, beat_breather::PF_ATTACK, pf_attack), \
            LOG_CONTROL_DFL("pfrt" id, "Punch filter release time" label, U_MSEC, beat_breather::PF_RELEASE, pf_release), \
            LOG_CONTROL("pfth" id, "Punch filter threshold" label, U_GAIN_AMP, beat_breather::PF_THRESHOLD), \
            LOG_CONTROL("pfrl" id, "Punch filter reduction level" label, U_GAIN_AMP, beat_breather::PF_REDUCTION), \
            LOG_CONTROL("pfrz" id, "Punch filter reduction zone" label, U_GAIN_AMP, beat_breather::PF_ZONE), \
            MESH("pfg" id, "Punch filter curve graph" label, 2, beat_breather::CURVE_MESH_POINTS), \
            \
            LOG_CONTROL_DFL("bpat" id, "Beat processor attack time" label, U_DB, beat_breather::BP_ATTACK, bp_attack), \
            LOG_CONTROL_DFL("bprt" id, "Beat processor release time" label, U_DB, beat_breather::BP_RELEASE, bp_release), \
            CONTROL("bpts" id, "Beat processor time shift" label, U_MSEC, beat_breather::BP_TIME_SHIFT), \
            LOG_CONTROL("bpth" id, "Beat processor threshold" label, U_GAIN_AMP, beat_breather::BP_THRESHOLD), \
            CONTROL("bper" id, "Beat processor expand ratio" label, U_NONE, beat_breather::BP_RATIO), \
            LOG_CONTROL("bpmg" id, "Beat processor maximum gain" label, U_GAIN_AMP, beat_breather::BP_MAX_GAIN), \
            MESH("bpg" id, "Beat processor curve graph" label, 2, beat_breather::CURVE_MESH_POINTS)

        #define BB_BAND_METERS(id, label) \
            METER_OUT_GAIN("ilm" id, "Band input level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("olm" id, "Band output level meter" label, GAIN_AMP_P_36_DB), \
            MESH("pdgr" id, "Punch detector time graph" label, 2, beat_breather::TIME_MESH_POINTS), \
            METER_OUT_GAIN("pfem" id, "Punch filter envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("pfcm" id, "Punch filter curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("pfgm" id, "Punch filter gain level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpem" id, "Beat processor envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpcm" id, "Beat processor curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("bpgm" id, "Beat processor gain level meter" label, GAIN_AMP_P_36_DB)

//        3,36088234621628
//        2,31728846573695
//        1,72150391982616
//        1,27824369542404
//        0,949659863530924
//        0,705429701753126
//        0,52404469880833
//        0,392135970622646

//        8,18002455854445
//        6,72583340973318
//        5,75168858655962
//        4,91731019240018
//        4,20524030571878
//        3,5959846503035
//        3,07510586688397
//        2,63974897070376

//        10,0110258487273
//        6,38627283138876
//        4,45852618974032
//        3,11075948889754
//        2,17191256646293
//        1,51612531560858
//        1,05843170856035
//        0,745424520970968

//        61,0830843258433
//        35,8843449279926
//        23,4554958100336
//        15,3202627114696
//        10,0148334700193
//        6,5451992182947
//        4,27802581207648
//        2,82537397766205

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

            BB_BAND("_1", " 1", 19.84f, 3.36f, 8.18, 10.01, 61.08),
            BB_BAND("_2", " 2", 14.79f, 2.32f, 6.72, 6.39, 35.88),
            BB_BAND("_3", " 3", 11.69f, 1.72f, 5.75, 4.46, 23.46),
            BB_BAND("_4", " 4", 9.24f, 1.27f, 4.91, 3.11, 15.32),
            BB_BAND("_5", " 5", 7.31f, 0.95f, 4.21, 2.17, 10.01),
            BB_BAND("_6", " 6", 5.78f, 0.71f, 3.60, 1.52, 6.55),
            BB_BAND("_7", " 7", 4.57f, 0.52f, 3.08, 1.06, 4.27),
            BB_BAND("_8", " 8", 3.63f, 0.32f, 2.64, 0.75, 2.83),

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

            BB_BAND("_1", " 1", 19.84f, 3.36f, 8.18, 10.01, 61.08),
            BB_BAND("_2", " 2", 14.79f, 2.32f, 6.72, 6.39, 35.88),
            BB_BAND("_3", " 3", 11.69f, 1.72f, 5.75, 4.46, 23.46),
            BB_BAND("_4", " 4", 9.24f, 1.27f, 4.91, 3.11, 15.32),
            BB_BAND("_5", " 5", 7.31f, 0.95f, 4.21, 2.17, 10.01),
            BB_BAND("_6", " 6", 5.78f, 0.71f, 3.60, 1.52, 6.55),
            BB_BAND("_7", " 7", 4.57f, 0.52f, 3.08, 1.06, 4.27),
            BB_BAND("_8", " 8", 3.63f, 0.32f, 2.64, 0.75, 2.83),

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
            "-mJ0fQqwAy4",
            "This plugin allows to drive much more dynamics into punchy sounds like drums and make them breathe again."
        };

        const plugin_t beat_breather_mono =
        {
            "Beat Breather Mono",
            "Beat Breather Mono",
            "Beat Breather Mono",
            "BB1M",
            &developers::v_sadovnikov,
            "beat_breather_mono",
            LSP_LV2_URI("beat_breather_mono"),
            LSP_LV2UI_URI("beat_breather_mono"),
            "bb1m",
            LSP_VST3_UID("bb1m    bb1m"),
            LSP_VST3UI_UID("bb1m    bb1m"),
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
            "Beat Breather Stereo",
            "BB1S",
            &developers::v_sadovnikov,
            "beat_breather_stereo",
            LSP_LV2_URI("beat_breather_stereo"),
            LSP_LV2UI_URI("beat_breather_stereo"),
            "bb1s",
            LSP_VST3_UID("bb1s    bb1s"),
            LSP_VST3UI_UID("bb1s    bb1s"),
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



