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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/bits.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>

#include <private/plugins/beat_breather.h>

/* The size of temporary buffer for audio processing */
#define BUFFER_SIZE         0x400U

namespace lsp
{
    static plug::IPort *TRACE_PORT(plug::IPort *p)
    {
        lsp_trace("  port id=%s", (p)->metadata()->id);
        return p;
    }

    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::beat_breather_mono,
            &meta::beat_breather_stereo
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new beat_breather(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 2);

        //---------------------------------------------------------------------
        // Implementation
        beat_breather::beat_breather(const meta::plugin_t *meta):
            Module(meta)
        {
            nChannels       = 1;
            if (!strcmp(meta->uid, meta::beat_breather_stereo.uid))
                nChannels       = 2;

            vChannels       = NULL;
            bStereoSplit    = false;
            fInGain         = GAIN_AMP_0_DB;
            fDryGain        = GAIN_AMP_M_INF_DB;
            fWetGain        = GAIN_AMP_0_DB;

            vAnalyze[0]     = NULL;
            vAnalyze[1]     = NULL;
            vAnalyze[2]     = NULL;
            vAnalyze[3]     = NULL;

            for (size_t i=0; i<meta::beat_breather::BANDS_MAX-1; ++i)
            {
                split_t *s      = &vSplits[i];

                s->nBandId      = i + 1;
                s->fFrequency   = 0.0f;
                s->bEnabled     = false;

                s->pEnable      = NULL;
                s->pFrequency   = NULL;
            }

            vBuffer         = NULL;
            vFftFreqs       = NULL;
            vFftIndexes     = NULL;
            vPdMesh         = NULL;
            vPfMesh         = NULL;
            vBpMesh         = NULL;

            pBypass         = NULL;
            pInGain         = NULL;
            pDryGain        = NULL;
            pWetGain        = NULL;
            pOutGain        = NULL;
            pStereoSplit    = NULL;
            pFFTReactivity  = NULL;
            pFFTShift       = NULL;

            pData           = NULL;
        }

        beat_breather::~beat_breather()
        {
            destroy();
        }

        void beat_breather::destroy()
        {
            Module::destroy();

            // Destroy channels
            if (vChannels != NULL)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    c->sBypass.destroy();
                    c->sCrossover.destroy();
                    c->sDelay.destroy();
                    c->sDryDelay.destroy();

                    for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                    {
                        band_t *b               = &c->vBands[j];

                        b->sDelay.destroy();
                        b->sPdLong.destroy();
                        b->sPdShort.destroy();
                        b->sPdDelay.destroy();
                        b->sPdMeter.destroy();
                        b->sPf.destroy();
                        b->sPfDelay.destroy();
                        b->sBp.destroy();
                        b->sBpScDelay.destroy();
                        b->sBpDelay.destroy();
                    }
                }
                vChannels   = NULL;
            }

            // Free previously allocated data chunk
            if (pData != NULL)
            {
                free_aligned(pData);
                pData       = NULL;
            }
        }

        void beat_breather::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Call parent class for initialization
            Module::init(wrapper, ports);

            // Initialize analyzer
            size_t an_cid           = 0;
            if (!sAnalyzer.init(2*nChannels, meta::beat_breather::FFT_ANALYZER_RANK,
                MAX_SAMPLE_RATE, meta::beat_breather::FFT_ANALYZER_RATE))
                return;

            sAnalyzer.set_rank(meta::beat_breather::FFT_ANALYZER_RANK);
            sAnalyzer.set_activity(false);
            sAnalyzer.set_envelope(dspu::envelope::WHITE_NOISE);
            sAnalyzer.set_window(meta::beat_breather::FFT_ANALYZER_WINDOW);
            sAnalyzer.set_rate(meta::beat_breather::FFT_ANALYZER_RATE);

            // Allocate data
            const size_t szof_channels  = align_size(sizeof(channel_t) * nChannels, DEFAULT_ALIGN);
            const size_t szof_buffer    = align_size(sizeof(float) * BUFFER_SIZE, DEFAULT_ALIGN);
            const size_t szof_fft       = align_size(sizeof(float) * meta::beat_breather::FFT_MESH_POINTS, DEFAULT_ALIGN);
            const size_t szof_ffti      = align_size(sizeof(uint32_t) * meta::beat_breather::FFT_MESH_POINTS, DEFAULT_ALIGN);
            const size_t szof_curve     = align_size(sizeof(float) * meta::beat_breather::CURVE_MESH_POINTS, DEFAULT_ALIGN);
            const size_t szof_time      = align_size(sizeof(float) * meta::beat_breather::TIME_MESH_POINTS, DEFAULT_ALIGN);
            const size_t to_alloc       =
                szof_channels +             // vChannels
                szof_buffer +               // vBuffer
                szof_fft +                  // vFftFreqs
                szof_ffti +                 // vFftIndexes
                szof_time +                 // vPdMesh
                szof_curve +                // vPfMesh
                szof_curve +                // vBpMesh
                nChannels * (
                    szof_buffer +       // channel_t::vInData
                    szof_buffer +       // channel_t::vOutData
                    szof_fft +          // channel_t::vFreqChart
                    meta::beat_breather::BANDS_MAX * (
                        szof_buffer +       // band_t::vInData
                        szof_buffer +       // band_t::vPdData
                        szof_buffer +       // band_t::vPfData
                        szof_buffer         // band_t::vBpData
                    )
                ) +
                meta::beat_breather::BANDS_MAX * (
                    szof_fft +          // band_t::vFreqChart (only for left channel)
                    szof_curve +        // band_t::vPfMesh (only for left channel)
                    szof_curve          // band_t::vBpMesh (only for left channel)
                );

            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;
            lsp_guard_assert(uint8_t *ptr_check = &ptr[to_alloc]);

            vChannels               = reinterpret_cast<channel_t *>(ptr);
            ptr                    += szof_channels;
            vBuffer                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_buffer;
            vFftFreqs               = reinterpret_cast<float *>(ptr);
            ptr                    += szof_fft;
            vFftIndexes             = reinterpret_cast<uint32_t *>(ptr);
            ptr                    += szof_ffti;
            vPdMesh                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_time;
            vPfMesh                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_curve;
            vBpMesh                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_curve;

            // Initialize channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->sBypass.construct();
                c->sCrossover.construct();
                c->sDelay.construct();
                c->sDryDelay.construct();

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    b->sDelay.construct();
                    b->sPdLong.construct();
                    if (!b->sPdLong.init(1, meta::beat_breather::PD_LONG_RMS_MAX))
                        return;
                    b->sPdShort.construct();
                    if (!b->sPdShort.init(1, meta::beat_breather::PD_SHORT_RMS_MAX))
                        return;
                    b->sPdDelay.construct();
                    b->sPdMeter.construct();
                    b->sPf.construct();
                    b->sPfDelay.construct();
                    b->sBp.construct();
                    b->sBpScDelay.construct();
                    b->sBpDelay.construct();

                    b->nOldMode             = BAND_OFF;
                    b->nMode                = BAND_OFF;
                    b->fGain                = GAIN_AMP_0_DB;
                    b->fInLevel             = GAIN_AMP_M_INF_DB;
                    b->fOutLevel            = GAIN_AMP_M_INF_DB;
                    b->fReduction           = GAIN_AMP_0_DB;
                    b->nSync                = SYNC_ALL;
                    b->fPdMakeup            = GAIN_AMP_0_DB;
                    b->fPdLevel             = GAIN_AMP_M_INF_DB;
                    b->fPfInGain            = GAIN_AMP_M_INF_DB;
                    b->fPfOutGain           = GAIN_AMP_M_INF_DB;
                    b->fPfReduction         = GAIN_AMP_M_INF_DB;
                    b->fBpMakeup            = GAIN_AMP_0_DB;
                    b->fBpInGain            = GAIN_AMP_M_INF_DB;
                    b->fBpOutGain           = GAIN_AMP_M_INF_DB;
                    b->fBpReduction         = GAIN_AMP_M_INF_DB;

                    b->vInData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vPdData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vPfData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vBpData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    if (i == 0)
                    {
                        b->vFreqChart           = reinterpret_cast<float *>(ptr);
                        ptr                    += szof_fft;
                        b->vPfMesh              = reinterpret_cast<float *>(ptr);
                        ptr                    += szof_curve;
                        b->vBpMesh              = reinterpret_cast<float *>(ptr);
                        ptr                    += szof_curve;
                    }
                    else
                    {
                        b->vFreqChart           = NULL;
                        b->vPfMesh              = NULL;
                        b->vBpMesh              = NULL;
                    }

                    b->pSolo                = NULL;
                    b->pMute                = NULL;
                    b->pListen              = NULL;
                    b->pLpfSlope            = NULL;
                    b->pHpfSlope            = NULL;
                    b->pFlatten             = NULL;
                    b->pOutGain             = NULL;
                    b->pFreqEnd             = NULL;
                    b->pFreqMesh            = NULL;

                    b->pInLevel             = NULL;
                    b->pOutLevel            = NULL;

                    b->pPdLongTime          = NULL;
                    b->pPdShortTime         = NULL;
                    b->pPdMakeup            = NULL;
                    b->pPdMesh              = NULL;

                    b->pPfLookahead         = NULL;
                    b->pPfAttack            = NULL;
                    b->pPfRelease           = NULL;
                    b->pPfThreshold         = NULL;
                    b->pPfReduction         = NULL;
                    b->pPfZone              = NULL;
                    b->pPfMesh              = NULL;
                    b->pPfEnvLevel          = NULL;
                    b->pPfCurveLevel        = NULL;
                    b->pPfGainLevel         = NULL;

                    b->pBpAttack            = NULL;
                    b->pBpRelease           = NULL;
                    b->pBpTimeShift         = NULL;
                    b->pBpThreshold         = NULL;
                    b->pBpRatio             = NULL;
                    b->pBpMaxGain           = NULL;
                    b->pBpMesh              = NULL;
                    b->pBpEnvLevel          = NULL;
                    b->pBpCurveLevel        = NULL;
                    b->pBpGainLevel         = NULL;
                }

                c->nAnIn                = an_cid++;
                c->nAnOut               = an_cid++;
                c->fInLevel             = GAIN_AMP_M_INF_DB;
                c->fOutLevel            = GAIN_AMP_M_INF_DB;

                c->vIn                  = NULL;
                c->vOut                 = NULL;
                c->vInData              = reinterpret_cast<float *>(ptr);
                ptr                    += szof_buffer;
                c->vOutData             = reinterpret_cast<float *>(ptr);
                ptr                    += szof_buffer;
                c->vFreqChart           = reinterpret_cast<float *>(ptr);
                ptr                    += szof_fft;

                vAnalyze[c->nAnIn]      = c->vInData;
                vAnalyze[c->nAnOut]     = c->vOutData;

                c->pIn                  = NULL;
                c->pOut                 = NULL;

                c->pInLevel             = NULL;
                c->pOutLevel            = NULL;
                c->pInFft               = NULL;
                c->pOutFft              = NULL;
                c->pInMesh              = NULL;
                c->pOutMesh             = NULL;
                c->pFreqMesh            = NULL;
            }

            // Check bounds
            lsp_assert(ptr <= ptr_check);

            // Bind ports
            size_t port_id = 0;

            // Input ports
            lsp_trace("Binding input ports");
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].pIn        = TRACE_PORT(ports[port_id++]);

            // Output ports
            lsp_trace("Binding output ports");
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].pOut       = TRACE_PORT(ports[port_id++]);

            // Common ports
            lsp_trace("Binding common ports");
            pBypass                 = TRACE_PORT(ports[port_id++]);
            pInGain                 = TRACE_PORT(ports[port_id++]);
            pDryGain                = TRACE_PORT(ports[port_id++]);
            pWetGain                = TRACE_PORT(ports[port_id++]);
            pOutGain                = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // skip tab selector
            pFFTReactivity          = TRACE_PORT(ports[port_id++]);
            pFFTShift               = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // skip zoom
            TRACE_PORT(ports[port_id++]); // skip show filters
            if (nChannels > 1)
                pStereoSplit            = TRACE_PORT(ports[port_id++]);

            // Channel meters
            lsp_trace("Binding channel meters");
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->pInLevel             = TRACE_PORT(ports[port_id++]);
                c->pOutLevel            = TRACE_PORT(ports[port_id++]);
                c->pInFft               = TRACE_PORT(ports[port_id++]);
                c->pOutFft              = TRACE_PORT(ports[port_id++]);
                c->pInMesh              = TRACE_PORT(ports[port_id++]);
                c->pOutMesh             = TRACE_PORT(ports[port_id++]);
                c->pFreqMesh            = TRACE_PORT(ports[port_id++]);
            }

            // Splits
            lsp_trace("Binding split ports");
            for (size_t i=0; i<meta::beat_breather::BANDS_MAX-1; ++i)
            {
                split_t *s              = &vSplits[i];

                s->pEnable              = TRACE_PORT(ports[port_id++]);
                s->pFrequency           = TRACE_PORT(ports[port_id++]);
            }

            // Band controls
            lsp_trace("Binding band ports");
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    if (i > 0)
                    {
                        band_t *sb              = &vChannels[0].vBands[j];

                        b->pSolo                = sb->pSolo;
                        b->pMute                = sb->pMute;
                        b->pListen              = sb->pListen;
                        b->pLpfSlope            = sb->pLpfSlope;
                        b->pHpfSlope            = sb->pHpfSlope;
                        b->pFlatten             = sb->pFlatten;
                        b->pOutGain             = sb->pOutGain;
                        b->pFreqEnd             = sb->pFreqEnd;
                        b->pFreqMesh            = NULL;

                        b->pPdLongTime          = sb->pPdLongTime;
                        b->pPdShortTime         = sb->pPdShortTime;
                        b->pPdMakeup            = sb->pPdMakeup;

                        b->pPfLookahead         = sb->pPfLookahead;
                        b->pPfAttack            = sb->pPfAttack;
                        b->pPfRelease           = sb->pPfRelease;
                        b->pPfThreshold         = sb->pPfThreshold;
                        b->pPfReduction         = sb->pPfReduction;
                        b->pPfZone              = sb->pPfZone;
                        b->pPfMesh              = sb->pPfMesh;

                        b->pBpAttack            = sb->pBpAttack;
                        b->pBpRelease           = sb->pBpRelease;
                        b->pBpTimeShift         = sb->pBpTimeShift;
                        b->pBpThreshold         = sb->pBpThreshold;
                        b->pBpRatio             = sb->pBpRatio;
                        b->pBpMaxGain           = sb->pBpMaxGain;
                        b->pBpMesh              = sb->pBpMesh;
                    }
                    else
                    {
                        b->pSolo                = TRACE_PORT(ports[port_id++]);
                        b->pMute                = TRACE_PORT(ports[port_id++]);
                        b->pListen              = TRACE_PORT(ports[port_id++]);
                        b->pLpfSlope            = TRACE_PORT(ports[port_id++]);
                        b->pHpfSlope            = TRACE_PORT(ports[port_id++]);
                        b->pFlatten             = TRACE_PORT(ports[port_id++]);
                        b->pOutGain             = TRACE_PORT(ports[port_id++]);
                        b->pFreqEnd             = TRACE_PORT(ports[port_id++]);
                        b->pFreqMesh            = TRACE_PORT(ports[port_id++]);

                        b->pPdLongTime          = TRACE_PORT(ports[port_id++]);
                        b->pPdShortTime         = TRACE_PORT(ports[port_id++]);
                        b->pPdMakeup            = TRACE_PORT(ports[port_id++]);

                        b->pPfLookahead         = TRACE_PORT(ports[port_id++]);
                        b->pPfAttack            = TRACE_PORT(ports[port_id++]);
                        b->pPfRelease           = TRACE_PORT(ports[port_id++]);
                        b->pPfThreshold         = TRACE_PORT(ports[port_id++]);
                        b->pPfReduction         = TRACE_PORT(ports[port_id++]);
                        b->pPfZone              = TRACE_PORT(ports[port_id++]);
                        b->pPfMesh              = TRACE_PORT(ports[port_id++]);

                        b->pBpAttack            = TRACE_PORT(ports[port_id++]);
                        b->pBpRelease           = TRACE_PORT(ports[port_id++]);
                        b->pBpTimeShift         = TRACE_PORT(ports[port_id++]);
                        b->pBpThreshold         = TRACE_PORT(ports[port_id++]);
                        b->pBpRatio             = TRACE_PORT(ports[port_id++]);
                        b->pBpMaxGain           = TRACE_PORT(ports[port_id++]);
                        b->pBpMesh              = TRACE_PORT(ports[port_id++]);
                    }
                }
            }

            // Band meters
            lsp_trace("Binding band meters");
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    b->pInLevel             = TRACE_PORT(ports[port_id++]);
                    b->pOutLevel            = TRACE_PORT(ports[port_id++]);

                    b->pPdMesh              = TRACE_PORT(ports[port_id++]);

                    b->pPfEnvLevel          = TRACE_PORT(ports[port_id++]);
                    b->pPfCurveLevel        = TRACE_PORT(ports[port_id++]);
                    b->pPfGainLevel         = TRACE_PORT(ports[port_id++]);

                    b->pBpEnvLevel          = TRACE_PORT(ports[port_id++]);
                    b->pBpCurveLevel        = TRACE_PORT(ports[port_id++]);
                    b->pBpGainLevel         = TRACE_PORT(ports[port_id++]);
                }
            }

            float delta;

            // Initialize coordinates for Peak Detector mesh
            delta = meta::beat_breather::TIME_HISTORY_MAX / (meta::beat_breather::TIME_MESH_POINTS-1);
            for (size_t i=0; i<meta::beat_breather::TIME_MESH_POINTS; ++i)
                vPdMesh[i]  = meta::beat_breather::TIME_HISTORY_MAX - delta * i;

            // Initialize coordinates for Peak Filter mesh
            delta = (meta::beat_breather::PF_CURVE_MAX - meta::beat_breather::PF_CURVE_MIN) / (meta::beat_breather::CURVE_MESH_POINTS-1);
            for (size_t i=0; i<meta::beat_breather::CURVE_MESH_POINTS; ++i)
                vPfMesh[i]  = dspu::db_to_gain(meta::beat_breather::PF_CURVE_MIN + delta * i);

            // Initialize coordinates for Beat Processor mesh
            delta = (meta::beat_breather::BP_CURVE_MAX - meta::beat_breather::BP_CURVE_MIN) / (meta::beat_breather::CURVE_MESH_POINTS-1);
            for (size_t i=0; i<meta::beat_breather::CURVE_MESH_POINTS; ++i)
                vBpMesh[i]  = dspu::db_to_gain(meta::beat_breather::BP_CURVE_MIN + delta * i);
        }

        size_t beat_breather::select_fft_rank(size_t sample_rate)
        {
            const size_t k = (sample_rate + meta::beat_breather::FFT_XOVER_FREQ_MIN/2) / meta::beat_breather::FFT_XOVER_FREQ_MIN;
            const size_t n = int_log2(k);
            return meta::beat_breather::FFT_XOVER_RANK_MIN << n;
        }

        void beat_breather::update_sample_rate(long sr)
        {
            const size_t fft_rank       = select_fft_rank(sr);
            const size_t max_delay_pd   = dspu::millis_to_samples(sr,
                (lsp_max(meta::beat_breather::PD_SHORT_RMS_MAX, meta::beat_breather::PD_LONG_RMS_MAX) + 1)/2);
            const size_t max_delay_pf   = dspu::millis_to_samples(sr, meta::beat_breather::PF_LOOKAHEAD_MAX);
            const size_t max_delay_bp   = dspu::millis_to_samples(sr, meta::beat_breather::BP_TIME_SHIFT_MAX);
            const size_t max_delay_fft  = (1 << fft_rank);
            const size_t samples_per_dot= dspu::seconds_to_samples(sr, meta::beat_breather::TIME_HISTORY_MAX / meta::beat_breather::TIME_MESH_POINTS);

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->sBypass.init(sr);
                c->sDelay.init(max_delay_fft + max_delay_pd + max_delay_pf + max_delay_bp + BUFFER_SIZE);
                c->sDryDelay.init(max_delay_fft + max_delay_pd + max_delay_pf + max_delay_bp + BUFFER_SIZE);

                if (fft_rank != c->sCrossover.rank())
                {
                    c->sCrossover.init(fft_rank, meta::beat_breather::BANDS_MAX);
                    for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                        c->sCrossover.set_handler(j, process_band, this, c);
                    c->sCrossover.set_rank(fft_rank);
                    c->sCrossover.set_phase(float(i) / float(nChannels));
                }
                c->sCrossover.set_sample_rate(sr);

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    b->sDelay.init(max_delay_pd + max_delay_pf + max_delay_bp);
                    b->sPdLong.set_sample_rate(sr);
                    b->sPdShort.set_sample_rate(sr);
                    b->sPdDelay.init(max_delay_pd);
                    b->sPdMeter.init(meta::beat_breather::TIME_MESH_POINTS, samples_per_dot);
                    b->sPf.set_sample_rate(sr);
                    b->sPfDelay.init(max_delay_pf);
                    b->sBp.set_sample_rate(sr);
                    b->sBpScDelay.init(max_delay_bp);
                    b->sBpDelay.init(max_delay_pd + max_delay_pf + max_delay_bp);
                }
            }

            // Commit sample rate to analyzer
            sAnalyzer.set_sample_rate(sr);
        }

        int beat_breather::compare_splits(const void *a1, const void *a2, void *data)
        {
            const split_t * const * s1 = reinterpret_cast<const split_t * const *>(a1);
            const split_t * const * s2 = reinterpret_cast<const split_t * const *>(a2);
            if ((*s1)->fFrequency < (*s2)->fFrequency)
                return -1;
            else if ((*s1)->fFrequency > (*s2)->fFrequency)
                return 1;
            return 0;
        }

        beat_breather::band_mode_t beat_breather::decode_band_mode(size_t mode)
        {
            switch (mode)
            {
                case meta::beat_breather::LISTEN_CROSSOVER:
                    return BAND_BF;
                case meta::beat_breather::LISTEN_RMS:
                    return BAND_PD;
                case meta::beat_breather::LISTEN_PUNCH:
                    return BAND_PF;
                case meta::beat_breather::LISTEN_BEAT:
                    return BAND_BP;
                default:
                    break;
            }
            return BAND_OFF;
        }

        void beat_breather::update_settings()
        {
            // Configure global parameters
            float out_gain      = pOutGain->value();
            bStereoSplit        = ((nChannels > 1) && (pStereoSplit != NULL)) ? pStereoSplit->value() >= 0.5f : false;
            fInGain             = pInGain->value();
            fDryGain            = out_gain * pDryGain->value();
            fWetGain            = out_gain * pWetGain->value();
            size_t an_channels  = 0;
            float bypass        = pBypass->value() >= 0.5f;
            bool sync           = false;

            // Update analyzer settings
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update analyzer settings
                sAnalyzer.enable_channel(c->nAnIn, c->pInFft->value() >= 0.5f);
                sAnalyzer.enable_channel(c->nAnOut, c->pOutFft->value() >= 0.5f);

                if (sAnalyzer.channel_active(c->nAnIn))
                    an_channels ++;
                if (sAnalyzer.channel_active(c->nAnOut))
                    an_channels ++;
            }

            // Update analyzer parameters
            sAnalyzer.set_reactivity(pFFTReactivity->value());
            if (pFFTShift != NULL)
                sAnalyzer.set_shift(dspu::db_to_gain(pFFTShift->value()) * 100.0f);
            sAnalyzer.set_activity(an_channels > 0);

            if (sAnalyzer.needs_reconfiguration())
            {
                sAnalyzer.reconfigure();
                sAnalyzer.get_frequencies(
                    vFftFreqs,
                    vFftIndexes,
                    SPEC_FREQ_MIN,
                    SPEC_FREQ_MAX,
                    meta::beat_breather::FFT_MESH_POINTS);
                sync                = true;
            }

            // Configure splits and their order
            size_t nsplits  = 0;
            split_t *vsplits[meta::beat_breather::BANDS_MAX];

            for (size_t i=0; i<meta::beat_breather::BANDS_MAX-1; ++i)
            {
                split_t *sp         = &vSplits[i];
                sp->nBandId         = i + 1;
                sp->bEnabled        = sp->pEnable->value() >= 0.5f;
                sp->fFrequency      = sp->pFrequency->value();
                if (sp->bEnabled)
                    vsplits[nsplits++]  = sp;
            }
            if (nsplits > 1)
                lsp::qsort_r(vsplits, nsplits, sizeof(split_t *), compare_splits, NULL);

            // Configure channels
            size_t max_band_latency     = 0;
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                bool has_solo       = false;

                c->sBypass.set_bypass(bypass);

                // Form the list of bands
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    b->nOldMode         = b->nMode;
                    b->nMode            = BAND_OFF;
                }

                // Configure active frequency bands
                for (size_t j=0; j<=nsplits; ++j)
                {
                    size_t band_id  = (j > 0) ? vsplits[j-1]->nBandId : 0;
                    band_t *b       = &c->vBands[band_id];
                    b->nMode        = decode_band_mode(b->pListen->value());

                    // Configure hi-pass filter
                    if (j > 0)
                    {
                        c->sCrossover.enable_hpf(band_id, true);
                        c->sCrossover.set_hpf_frequency(band_id, vsplits[j-1]->fFrequency);
                        c->sCrossover.set_hpf_slope(band_id, - b->pHpfSlope->value());
                    }
                    else
                        c->sCrossover.disable_hpf(band_id);

                    // Configure lo-pass filter
                    if (j < nsplits)
                    {
                        c->sCrossover.enable_lpf(band_id, true);
                        c->sCrossover.set_lpf_frequency(band_id, vsplits[j]->fFrequency);
                        c->sCrossover.set_lpf_slope(band_id, - b->pLpfSlope->value());
                        b->pFreqEnd->set_value(vsplits[j]->fFrequency);
                    }
                    else
                    {
                        c->sCrossover.disable_lpf(band_id);
                        b->pFreqEnd->set_value(fSampleRate * 0.5f);
                    }

                    c->sCrossover.set_flatten(band_id, dspu::db_to_gain(-b->pFlatten->value()));

                    // Check solo option
                    if (b->pSolo->value() >= 0.5f)
                        has_solo        = true;
                }

                // Configure bands
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    bool solo               = b->pSolo->value() >= 0.5f;
                    bool mute               = ((has_solo) && (!solo)) ? true : b->pMute->value() >= 0.5f;
                    if ((mute) && (b->nMode != BAND_OFF))
                        b->nMode                = BAND_MUTE;

                    b->fGain                = b->pOutGain->value();

                    // Do we have hi-pass filter?
                    c->sCrossover.enable_band(j, b->nMode != BAND_OFF);

                    // Update Peak detector configuration
                    float pd_long           = b->pPdLongTime->value();
                    float pd_short          = b->pPdShortTime->value();
                    size_t pd_short_latency = dspu::millis_to_samples(fSampleRate, pd_long - pd_short) / 2;
                    size_t pd_latency       = dspu::millis_to_samples(fSampleRate, pd_long) / 2;
                    b->fPdMakeup            = dspu::db_to_gain(b->pPdMakeup->value() + meta::beat_breather::PD_MAKEUP_SHIFT);

                    b->sPdLong.set_mode(dspu::SCM_RMS);
                    b->sPdLong.set_source(dspu::SCS_MIDDLE);
                    b->sPdLong.set_reactivity(pd_long);
                    b->sPdLong.set_gain(GAIN_AMP_0_DB);

                    b->sPdShort.set_mode(dspu::SCM_RMS);
                    b->sPdShort.set_source(dspu::SCS_MIDDLE);
                    b->sPdShort.set_reactivity(pd_short);
                    b->sPdShort.set_gain(GAIN_AMP_0_DB);

                    b->sPdDelay.set_delay(pd_short_latency);

                    b->sPdMeter.set_method(dspu::MM_MAXIMUM);
                    if ((b->nOldMode != BAND_OFF) && (b->nMode == BAND_OFF))
                        b->sPdMeter.fill(0.0f);

                    // Update peak filter configuration
                    float pf_thresh         = b->pPfThreshold->value();
                    float pf_zone           = b->pPfZone->value();
                    size_t pf_latency       = dspu::millis_to_samples(fSampleRate, b->pPfLookahead->value());

                    b->sPf.set_attack(b->pPfAttack->value());
                    b->sPf.set_release(b->pPfRelease->value());
                    b->sPf.set_threshold(pf_thresh, pf_thresh);
                    b->sPf.set_zone(pf_zone, pf_zone);
                    b->sPf.set_reduction(b->pPfReduction->value());
                    if (b->sPf.modified())
                    {
                        b->sPf.update_settings();
                        if (i == 0)
                            b->sPf.curve(b->vPfMesh, vPfMesh, meta::beat_breather::CURVE_MESH_POINTS, false);
                        b->nSync               |= SYNC_PEAK_FILTER;
                    }

                    b->sPfDelay.set_delay(pf_latency);

                    // Update beat processor configuration
                    float bp_ratio          = b->pBpRatio->value() - 1.0f;
                    float bp_shift          = b->pBpTimeShift->value();
                    size_t bp_sc_latency    = dspu::millis_to_samples(fSampleRate, lsp_max(bp_shift, 0.0f));
                    size_t bp_latency       = dspu::millis_to_samples(fSampleRate, lsp_max(-bp_shift, 0.0f));
                    if (bp_ratio >= 1e-3f)
                    {
                        float bp_max_gain       = b->pBpMaxGain->value();
                        float bp_user_th        = b->pBpThreshold->value();
                        float bp_log_th         = logf(bp_user_th);
                        float bp_log_gate_th    = logf(bp_max_gain) / bp_ratio + bp_log_th;
                        float bp_th             = expf(bp_log_gate_th);
                        float bp_zone           = bp_user_th / bp_th;

                        b->sBp.set_threshold(bp_th, bp_th);
                        b->sBp.set_reduction(1.0f / bp_max_gain);
                        b->sBp.set_zone(bp_zone, bp_zone);
                        b->fBpMakeup            = bp_max_gain;
                    }
                    else
                    {
                        float bp_th             = b->pBpThreshold->value();
                        b->sBp.set_threshold(bp_th, bp_th);
                        b->sBp.set_reduction(GAIN_AMP_0_DB);
                        b->sBp.set_zone(GAIN_AMP_0_DB, GAIN_AMP_0_DB);
                        b->fBpMakeup            = GAIN_AMP_0_DB;
                    }


                    b->sBp.set_attack(b->pBpAttack->value());
                    b->sBp.set_release(b->pBpRelease->value());

                    if (b->sBp.modified())
                    {
                        b->sBp.update_settings();
                        if (i == 0)
                            b->sBp.curve(b->vBpMesh, vBpMesh, meta::beat_breather::CURVE_MESH_POINTS, false);
                        b->nSync               |= SYNC_BEAT_PROC;
                    }

                    b->sBpScDelay.set_delay(bp_sc_latency);
                    b->sBpDelay.set_delay(pd_latency + pf_latency + bp_latency);

                    // Compute the overall latency of the band
                    if (b->nMode != BAND_OFF)
                        max_band_latency        = lsp_max(max_band_latency, b->sBpDelay.delay());
                }

                // Reconfigure the crossover
                bool csync   = (sync) || (c->sCrossover.needs_update());
                c->sCrossover.update_settings();

                if ((csync) && (i == 0))
                {
                    // Output band parameters and update sync curve flag
                    for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                    {
                        band_t *b               = &c->vBands[j];

                        // Get frequency response for band
                        c->sCrossover.freq_chart(j, b->vFreqChart, vFftFreqs, meta::beat_breather::FFT_MESH_POINTS);
                        b->nSync               |= SYNC_BAND_FILTER;
                    }
                }
            }

            // Apply latency compensations and report latency
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];
                    if (b->nMode == BAND_OFF)
                        continue;
                    b->sDelay.set_delay(max_band_latency - b->sBpDelay.delay());

                    lsp_trace("Band #%d: latency=%d, compensation=%d, overall=%d",
                        int(i),
                        int(b->sBpDelay.delay()),
                        int(b->sDelay.delay()),
                        int(max_band_latency));
                }

                size_t overall_latency  = max_band_latency + c->sCrossover.latency();
                c->sDelay.set_delay(overall_latency);
                c->sDryDelay.set_delay(overall_latency);

                if (i == 0)
                    set_latency(overall_latency);
            }
        }

        void beat_breather::process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count)
        {
            channel_t *c            = static_cast<channel_t *>(subject);
            band_t *b               = &c->vBands[band];

            // Apply delay compensation and store to band's data buffer.
            b->sDelay.process(&b->vInData[sample], data, count);
            // Measure the input level
            b->fInLevel             = lsp_max(dsp::abs_max(&b->vInData[sample], count), b->fInLevel);
        }

        void beat_breather::process(size_t samples)
        {
            bind_inputs();

            for (size_t offset = 0; offset < samples; )
            {
                size_t to_do        = lsp_min(offset - samples, BUFFER_SIZE);

                // Stores band data to band_t::vIn
                split_signal(to_do);
                // Stores normalized RMS difference to band_t::vPdData
                apply_peak_detector(to_do);
                // Stores processed data to band_t::vPfData
                apply_punch_filter(to_do);
                // Stores the processed band data to band_t::vBpData
                apply_beat_processor(to_do);

//                // Stores the processed band data to channel_t::vOutData
//                mix_bands(to_do);
//
//                post_process_block(to_do);

                offset             += to_do;
            }

//            output_meters();
        }

        void beat_breather::bind_inputs()
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();

                c->fInLevel         = GAIN_AMP_M_INF_DB;
                c->fOutLevel        = GAIN_AMP_M_INF_DB;

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];

                    b->fInLevel         = GAIN_AMP_M_INF_DB;
                    b->fOutLevel        = GAIN_AMP_M_INF_DB;

                    b->fPdLevel         = GAIN_AMP_M_INF_DB;

                    b->fPfInGain        = GAIN_AMP_M_INF_DB;
                    b->fPfOutGain       = GAIN_AMP_M_INF_DB;
                    b->fPfReduction     = GAIN_AMP_0_DB;

                    b->fBpInGain        = GAIN_AMP_M_INF_DB;
                    b->fBpOutGain       = GAIN_AMP_M_INF_DB;
                    b->fBpReduction     = GAIN_AMP_M_INF_DB;
                }
            }
        }

        void beat_breather::split_signal(size_t samples)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Apply gain to input signal
                dsp::mul_k3(c->vInData, c->vIn, fInGain, samples);
                // Pass the input signal to crossover
                c->sCrossover.process(c->vInData, samples);
            }
        }

        void beat_breather::normalize_rms(float *dst, const float *lrms, const float *srms, float gain, size_t samples)
        {
            for (size_t i=0; i<samples; ++i)
            {
                const float l       = lrms[i];
                const float s       = srms[i];
                if ((s > l) && (l >= GAIN_AMP_M_140_DB))
                    dst[i]              = (s * gain) / l;
                else
                    dst[i]              = gain;
            }
        }

        void beat_breather::apply_peak_detector(size_t samples)
        {
            // Esimate RMS for all bands
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    if (b->nMode == BAND_OFF)
                        continue;

                    // Estimate long-time RMS
                    b->sPdLong.process(b->vPdData, const_cast<const float **>(&b->vInData), samples);
                    // Estimate short-time RMS
                    b->sPdShort.process(b->vPfData, const_cast<const float **>(&b->vInData), samples);
                    // Apply delay compensation to short-time RMS estimation
                    b->sPdDelay.process(b->vPfData, b->vPfData, samples);
                }
            }

            // Mix sidechain if 'Stereo Split' is not enabled
            if ((nChannels > 1) && (!bStereoSplit))
            {
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *left    = &vChannels[0].vBands[j];
                    band_t *right   = &vChannels[1].vBands[j];
                    if ((left->nMode == BAND_OFF) || (right->nMode == BAND_OFF))
                        continue;

                    // Convert stereo long-time RMS to mono
                    dsp::lr_to_mid(left->vPdData, left->vPdData, right->vPdData, samples);
                    // Duplicate long-time RMS to second channel
                    dsp::copy(right->vPdData, left->vPdData, samples);
                    // Convert stereo short-time RMS to mono
                    dsp::lr_to_mid(left->vPfData, left->vPfData, right->vPfData, samples);
                    // Duplicate short-time RMS to second channel
                    dsp::copy(right->vPfData, left->vPfData, samples);
                }
            }

            // Do post-processing and normalization
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    if (b->nMode == BAND_OFF)
                        continue;

                    // Produce normalized Peak/RMS signal
                    normalize_rms(b->vPdData, b->vPdData, b->vPfData, b->fPdMakeup, samples);
                    b->sPdMeter.process(b->vPdData, samples);
                    b->fPdLevel     = lsp_max(b->fPdLevel, dsp::abs_max(b->vPdData, samples));
                }
            }
        }

        void beat_breather::apply_punch_filter(size_t samples)
        {
            // Do post-processing and normalization
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    if (b->nMode == BAND_OFF)
                        continue;

                    // Process sidechain signal and produce VCA
                    b->sPf.process(b->vPfData, vBuffer, b->vPdData, samples);
                    // Update gating dot meter
                    size_t idx = dsp::max_index(b->vPfData, samples);
                    if (vBuffer[idx] > b->fPfInGain)
                    {
                        b->fPfInGain    = vBuffer[idx];
                        b->fPfOutGain   = b->vPfData[idx] * b->fPfInGain;
                    }
                    b->fPfReduction = lsp_min(dsp::abs_min(b->vPfData, samples), b->fPfReduction);
                    // Apply lookahead delay to Peak/RMS signal
                    b->sPfDelay.process(vBuffer, b->vPdData, samples);
                    // Apply VCA to peak signal
                    dsp::mul2(b->vPfData, vBuffer, samples);
                }
            }
        }

        void beat_breather::apply_beat_processor(size_t samples)
        {
            // Process the data stored in band_t::vPfData and band_t::vIn and store result to band_t::vData
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    if (b->nMode == BAND_OFF)
                        continue;

                    // Apply time shift delay to the sidechain
                    b->sBpScDelay.process(b->vBpData, b->vPfData, samples);
                    // Process sidechain signal and produce VCA
                    b->sBp.process(b->vBpData, vBuffer, b->vBpData, samples);
                    // Update gating dot meter
                    size_t idx = dsp::max_index(b->vBpData, samples);
                    if (vBuffer[idx] > b->fBpInGain)
                    {
                        b->fBpInGain    = vBuffer[idx];
                        b->fBpOutGain   = b->vBpData[idx] * b->fBpInGain * b->fBpMakeup;
                    }
                    b->fBpReduction = lsp_max(dsp::abs_max(b->vBpData, samples) * b->fBpMakeup, b->fBpReduction);
                    // Apply time shift delay + latency compensation to the signal of the band
                    b->sBpDelay.process(b->vInData, b->vInData, samples);
                    // Apply VCA to original signal
                    dsp::fmmul_k3(b->vBpData, b->vInData, b->fBpMakeup, samples);
                }
            }
        }

        void beat_breather::mix_bands(size_t samples)
        {
            // Mix bands depending on the band listen mode
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                dsp::fill_zero(c->vOutData, samples);

                // Compute the averaging value for all peak-detected and peak-filtered tracks
                // Because peak-detected tracks have constant bias, they should be mixed in different way
                ssize_t num_pd      = 0;
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    switch (c->vBands[j].nMode)
                    {
                        case BAND_PD:
                        case BAND_PF:
                            ++num_pd;
                            break;
                        default:
                            break;
                    }
                }
                float pd_makeup     = (num_pd > 0) ? 1.0f / num_pd : 1.0f;

                // Mix the tracks
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    switch (b->nMode)
                    {
                        case BAND_BF:
                            b->fOutLevel            = lsp_max(dsp::abs_max(b->vInData, samples) * b->fGain, b->fOutLevel);
                            b->fReduction           = b->fGain;
                            dsp::fmadd_k3(c->vOutData, b->vInData, b->fGain, samples);
                            break;
                        case BAND_PD:
                            b->fOutLevel            = lsp_max(dsp::abs_max(b->vPdData, samples) * b->fGain * pd_makeup, b->fOutLevel);
                            b->fReduction           = b->fPdLevel * b->fGain;
                            dsp::fmadd_k3(c->vOutData, b->vPdData, b->fGain * pd_makeup, samples);
                            break;
                        case BAND_PF:
                            b->fOutLevel            = lsp_max(dsp::abs_max(b->vPfData, samples) * b->fGain * pd_makeup, b->fOutLevel);
                            b->fReduction           = b->fPfReduction * b->fGain;
                            dsp::fmadd_k3(c->vOutData, b->vPfData, b->fGain * pd_makeup, samples);
                            break;
                        case BAND_BP:
                            b->fOutLevel            = lsp_max(dsp::abs_max(b->vBpData, samples) * b->fGain, b->fOutLevel);
                            b->fReduction           = b->fBpReduction * b->fGain;
                            dsp::fmadd_k3(c->vOutData, b->vBpData, b->fGain, samples);
                            break;

                        case BAND_MUTE:
                        case BAND_OFF:
                        default:
                            break;
                    }
                }
            }
        }

        void beat_breather::post_process_block(size_t samples)
        {
            // Apply delay compensation to input data and measure level
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Delay the channel's input data
                c->sDelay.process(c->vInData, c->vInData, samples);
                // Measure the input level
                c->fInLevel         = lsp_max(dsp::abs_max(c->vInData, samples), c->fInLevel);

                // Mix dry/wet into channel_t::vOutData
                dsp::mix2(c->vOutData, c->vInData, fWetGain, fDryGain, samples);
                // Measure the output level
                c->fOutLevel        = lsp_max(dsp::abs_max(c->vOutData, samples), c->fOutLevel);
            }

            // Measure levels
            sAnalyzer.process(vAnalyze, samples);

            // Apply bypass switch
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Delay the dry signal
                c->sDryDelay.process(vBuffer, c->vIn, samples);
                // Apply bypass and output data
                c->sBypass.process(c->vOut, vBuffer, c->vOutData, samples);
            }
        }

        void beat_breather::update_pointers(size_t samples)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                c->vIn             += samples;
                c->vOut            += samples;
            }
        }

        void beat_breather::output_meters()
        {
            // Output meshes
            plug::mesh_t *mesh;
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Compute transfer curve
                dsp::fill_zero(c->vFreqChart, meta::beat_breather::FFT_MESH_POINTS);
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b       = &c->vBands[j];
                    if ((b->nMode != BAND_OFF) && (b->nMode != BAND_MUTE))
                        dsp::fmadd_k3(
                            c->vFreqChart,
                            vChannels[0].vBands[j].vFreqChart,
                            b->fReduction,
                            meta::beat_breather::FFT_MESH_POINTS);
                }

                // Output input and output level meters
                c->pInLevel->set_value(c->fInLevel);
                c->pOutLevel->set_value(c->fOutLevel);

                // Output transfer function of the channel
                mesh        = (c->pFreqMesh != NULL) ? c->pFreqMesh->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    dsp::copy(mesh->pvData[0], vFftFreqs, meta::beat_breather::FFT_MESH_POINTS);
                    dsp::copy(mesh->pvData[1], c->vFreqChart, meta::beat_breather::FFT_MESH_POINTS);
                    mesh->data(2, meta::beat_breather::FFT_MESH_POINTS);
                }

                // Sync filter characteristics for each band
                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b       = &c->vBands[j];

                    // Output meters
                    b->pInLevel->set_value(b->fInLevel);
                    b->pOutLevel->set_value(b->fOutLevel);
                    b->pPfEnvLevel->set_value(b->fPfInGain);
                    b->pPfCurveLevel->set_value(b->fPfOutGain);
                    b->pPfGainLevel->set_value(b->fPfReduction);
                    b->pBpEnvLevel->set_value(b->fBpInGain);
                    b->pBpCurveLevel->set_value(b->fBpOutGain);
                    b->pBpGainLevel->set_value(b->fBpReduction);

                    // Synchronize band transfer function mesh
                    if ((i == 0) && (b->nSync & SYNC_BAND_FILTER))
                    {
                        mesh        = (b->pFreqMesh != NULL) ? b->pFreqMesh->buffer<plug::mesh_t>() : NULL;
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                            mesh->pvData[0][meta::beat_breather::FFT_MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                            mesh->pvData[1][0] = 0.0f;
                            mesh->pvData[1][meta::beat_breather::FFT_MESH_POINTS+1] = 0.0f;

                            dsp::copy(&mesh->pvData[0][1], vFftFreqs, meta::beat_breather::FFT_MESH_POINTS);
                            dsp::copy(&mesh->pvData[1][1], b->vFreqChart, meta::beat_breather::FFT_MESH_POINTS);
                            mesh->data(2, meta::beat_breather::FFT_MESH_POINTS + 2);

                            b->nSync        &= ~SYNC_BAND_FILTER;
                        }
                    }

                    // Synchronize peak detector mesh
                    mesh            = b->pPdMesh->buffer<plug::mesh_t>();
                    if ((mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Fill mesh with new values
                        dsp::copy(mesh->pvData[0], vPdMesh, meta::beat_breather::TIME_MESH_POINTS);
                        dsp::copy(mesh->pvData[1], b->sPdMeter.data(), meta::beat_breather::TIME_MESH_POINTS);
                        mesh->data(2, meta::beat_breather::TIME_MESH_POINTS);
                    }

                    // Synchronize peak filter function mesh
                    if ((i == 0) && (b->nSync & SYNC_PEAK_FILTER))
                    {
                        mesh        = (b->pPfMesh != NULL) ? b->pPfMesh->buffer<plug::mesh_t>() : NULL;
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            dsp::copy(mesh->pvData[0], vPfMesh, meta::beat_breather::CURVE_MESH_POINTS);
                            dsp::copy(mesh->pvData[1], b->vPfMesh, meta::beat_breather::CURVE_MESH_POINTS);
                            mesh->data(2, meta::beat_breather::CURVE_MESH_POINTS);

                            b->nSync        &= ~SYNC_PEAK_FILTER;
                        }
                    }

                    // Synchronize beat processor function mesh
                    if ((i == 0) && (b->nSync & SYNC_BEAT_PROC))
                    {
                        mesh        = (b->pBpMesh != NULL) ? b->pBpMesh->buffer<plug::mesh_t>() : NULL;
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            dsp::copy(mesh->pvData[0], vBpMesh, meta::beat_breather::CURVE_MESH_POINTS);
                            dsp::mul_k3(mesh->pvData[1], b->vBpMesh, b->fBpMakeup, meta::beat_breather::CURVE_MESH_POINTS);
                            mesh->data(2, meta::beat_breather::CURVE_MESH_POINTS);

                            b->nSync        &= ~SYNC_BEAT_PROC;
                        }
                    }
                }

                // Output spectrum analysis for input channel
                mesh        = ((sAnalyzer.channel_active(c->nAnIn)) && (c->pInMesh != NULL)) ? c->pInMesh->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Add extra points
                    mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                    mesh->pvData[0][meta::beat_breather::FFT_MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                    mesh->pvData[1][0] = 0.0f;
                    mesh->pvData[1][meta::beat_breather::FFT_MESH_POINTS+1] = 0.0f;

                    dsp::copy(&mesh->pvData[0][1], vFftFreqs, meta::beat_breather::FFT_MESH_POINTS);
                    sAnalyzer.get_spectrum(c->nAnIn, &mesh->pvData[1][1], vFftIndexes, meta::beat_breather::FFT_MESH_POINTS);

                    // Mark mesh containing data
                    mesh->data(2, meta::beat_breather::FFT_MESH_POINTS + 2);
                }

                // Output spectrum analysis for output channel
                mesh        = ((sAnalyzer.channel_active(c->nAnOut)) && (c->pOutMesh != NULL)) ? c->pOutMesh->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    dsp::copy(mesh->pvData[0], vFftFreqs, meta::beat_breather::FFT_MESH_POINTS);
                    sAnalyzer.get_spectrum(c->nAnOut, mesh->pvData[1], vFftIndexes, meta::beat_breather::FFT_MESH_POINTS);

                    // Mark mesh containing data
                    mesh->data(2, meta::beat_breather::FFT_MESH_POINTS);
                }
            }
        }

        void beat_breather::ui_activated()
        {
            // Determine number of channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t i=0; i<meta::beat_breather::BANDS_MAX; ++i)
                {
                    band_t *b           = &c->vBands[i];
                    b->nSync            = SYNC_ALL;
                }
            }
        }

        bool beat_breather::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            return false;
        }

        void beat_breather::dump(dspu::IStateDumper *v) const
        {
        }

    } /* namespace plugins */
} /* namespace lsp */


