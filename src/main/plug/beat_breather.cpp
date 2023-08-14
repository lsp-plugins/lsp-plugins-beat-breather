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

            pBypass         = NULL;
            pInGain         = NULL;
            pDryGain        = NULL;
            pWetGain        = NULL;
            pOutGain        = NULL;
            pStereoSplit    = NULL;
            pFFTReactivity  = NULL;
            pFFTShift       = NULL;

            for (size_t i=0; i<meta::beat_breather::BANDS_MAX-1; ++i)
            {
                split_t *s      = &vSplits[i];

                s->nBandId      = i + 1;
                s->fFrequency   = 0.0f;
                s->bEnabled     = false;

                s->pEnable      = NULL;
                s->pFrequency   = NULL;
            }

            vScBuffer       = NULL;
            vBuffer         = NULL;

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

                    for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                    {
                        band_t *b               = &c->vBands[j];

                        b->sDelay.destroy();
                        b->sLongSc.destroy();
                        b->sShortSc.destroy();
                        b->sShortDelay.destroy();
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

            // Allocate data
            const size_t szof_channels  = align_size(sizeof(channel_t) * nChannels, DEFAULT_ALIGN);
            const size_t szof_buffer    = sizeof(float) * BUFFER_SIZE;
            const size_t to_alloc       =
                szof_channels +
                szof_buffer +               // vScBuffer
                szof_buffer +               // vBuffer
                nChannels * (
                    meta::beat_breather::BANDS_MAX * (
                        szof_buffer +       // band_t::vIn
                        szof_buffer +       // band_t::vRMS
                        szof_buffer +       // band_t::vPfData
                        szof_buffer +       // band_t::vBpData
                        szof_buffer         // band_t::vData
                    ));

            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;

            vChannels               = reinterpret_cast<channel_t *>(ptr);
            ptr                    += meta::beat_breather::BANDS_MAX;
            vScBuffer               = reinterpret_cast<float *>(ptr);
            ptr                    += szof_buffer;
            vBuffer                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_buffer;

            // Initialize channels
            size_t an_ch            = 0;
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->sBypass.construct();
                c->sCrossover.construct();
                c->sDelay.construct();

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    b->sDelay.construct();
                    b->sLongSc.construct();
                    b->sShortSc.construct();
                    b->sShortDelay.construct();
                    b->sPf.construct();
                    b->sPfDelay.construct();
                    b->sBp.construct();
                    b->sBpScDelay.construct();
                    b->sBpDelay.construct();

                    b->nAnIn                = an_ch++;
                    b->nAnOut               = an_ch++;
                    b->nMode                = BAND_OFF;
                    b->fGain                = GAIN_AMP_0_DB;

                    b->vIn                  = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vRMS                 = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vPfData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;
                    b->vBpData              = reinterpret_cast<float *>(ptr);
                    ptr                    += szof_buffer;

                    b->pInLevel             = NULL;
                    b->pOutLevel            = NULL;

                    b->pSolo                = NULL;
                    b->pMute                = NULL;
                    b->pListen              = NULL;
                    b->pLpfSlope            = NULL;
                    b->pHpfSlope            = NULL;
                    b->pFlatten             = NULL;
                    b->pOutGain             = NULL;
                    b->pFreqEnd             = NULL;

                    b->pPfLongTime          = NULL;
                    b->pPfShortTime         = NULL;
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

                c->vIn                  = NULL;
                c->vOut                 = NULL;

                c->pIn                  = NULL;
                c->pOut                 = NULL;

                c->pInLevel             = NULL;
                c->pOutLevel            = NULL;
                c->pInFft               = NULL;
                c->pOutFft              = NULL;
                c->pInMesh              = NULL;
                c->pOutMesh             = NULL;
            }

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
            pStereoSplit            = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // skip tab selector
            pFFTReactivity          = TRACE_PORT(ports[port_id++]);
            pFFTShift               = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // skip zoom

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

                        b->pPfLongTime          = sb->pPfLongTime;
                        b->pPfShortTime         = sb->pPfShortTime;
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

                        b->pListen              = TRACE_PORT(ports[port_id++]);

                        b->pPfLongTime          = TRACE_PORT(ports[port_id++]);
                        b->pPfShortTime         = TRACE_PORT(ports[port_id++]);
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

                    b->pPfEnvLevel          = TRACE_PORT(ports[port_id++]);
                    b->pPfCurveLevel        = TRACE_PORT(ports[port_id++]);
                    b->pPfGainLevel         = TRACE_PORT(ports[port_id++]);

                    b->pBpEnvLevel          = TRACE_PORT(ports[port_id++]);
                    b->pBpCurveLevel        = TRACE_PORT(ports[port_id++]);
                    b->pBpGainLevel         = TRACE_PORT(ports[port_id++]);
                }
            }
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
            const size_t max_delay_rms  = dspu::millis_to_samples(sr,
                (lsp_max(meta::beat_breather::SHORT_RMS_MAX, meta::beat_breather::LONG_RMS_MAX) + 1)/2);
            const size_t max_delay_pflk = dspu::millis_to_samples(sr, meta::beat_breather::PF_LOOKAHEAD_MAX);
            const size_t max_delay_bpts = dspu::millis_to_samples(sr, meta::beat_breather::BP_TIME_SHIFT_MAX);
            const size_t max_delay_fft  = (1 << fft_rank);

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->sBypass.init(sr);
                c->sDelay.init(max_delay_fft + max_delay_rms + max_delay_pflk + max_delay_bpts + BUFFER_SIZE);
                c->sCrossover.init(fft_rank, meta::beat_breather::BANDS_MAX);
                c->sCrossover.set_sample_rate(sr);
                if (fft_rank != c->sCrossover.rank())
                {
                    c->sCrossover.init(fft_rank, meta::beat_breather::BANDS_MAX);
                    for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                        c->sCrossover.set_handler(j, process_band, this, c);
                    c->sCrossover.set_rank(fft_rank);
                    c->sCrossover.set_phase(float(i) / float(nChannels));
                }

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b               = &c->vBands[j];

                    b->sDelay.init(max_delay_rms + max_delay_pflk + max_delay_bpts);
                    b->sLongSc.set_sample_rate(sr);
                    b->sShortSc.set_sample_rate(sr);
                    b->sShortDelay.init(max_delay_pflk);
                    b->sPfDelay.init(max_delay_pflk);
                    b->sBpScDelay.init(max_delay_bpts);
                    b->sBpDelay.init(max_delay_rms + max_delay_pflk + max_delay_bpts);
                }
            }
        }

        void beat_breather::update_settings()
        {
        }

        void beat_breather::process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count)
        {
            channel_t *c            = static_cast<channel_t *>(subject);
            band_t *b               = &c->vBands[band];

            // Apply delay compensation and store to band's data buffer.
            b->sDelay.process(&b->vIn[sample], data, count);
        }

        void beat_breather::process(size_t samples)
        {
            bind_inputs();

            for (size_t offset = 0; offset < samples; )
            {
                size_t to_do        = lsp_min(offset - samples, BUFFER_SIZE);

                // Stores band data to band_t::vIn
                split_signal(to_do);
                // Stores normalized RMS to band_t::vRMS and post-processed RMS to band_t::vPfData
                apply_peak_filter(to_do);
                // Stores the processed band data to band_t::vBpData
                apply_beat_processor(to_do);
                // Stores the processed band data to channel_t::vData
                mix_bands(to_do);

                post_process(to_do);
            }
        }

        void beat_breather::bind_inputs()
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();
            }
        }

        void beat_breather::split_signal(size_t samples)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                dsp::mul_k3(vBuffer, c->vIn, fInGain, samples);
                c->sCrossover.process(vBuffer, samples);
            }
        }

        void beat_breather::normalize_rms(float *dst, const float *lrms, const float *srms, size_t samples)
        {
            for (size_t i=0; i<samples; ++i)
            {
                const float s       = srms[i];
                const float l       = lrms[i];
                if ((s > l) && (l >= GAIN_AMP_M_140_DB))
                    dst[i]              = (s * meta::beat_breather::LEVEL_NORMING) / l;
                else
                    dst[i]              = meta::beat_breather::LEVEL_NORMING;
            }
        }

        void beat_breather::apply_peak_filter(size_t samples)
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
                    b->sLongSc.process(b->vRMS, const_cast<const float **>(&b->vIn), samples);
                    // Estimate short-time RMS
                    b->sShortSc.process(b->vPfData, const_cast<const float **>(&b->vIn), samples);
                    // Apply delay compensation to short-time RMS estimation
                    b->sShortDelay.process(b->vPfData, b->vPfData, samples);
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
                    dsp::lr_to_mid(left->vRMS, left->vRMS, right->vRMS, samples);
                    // Duplicate long-time RMS to second channel
                    dsp::copy(right->vRMS, left->vRMS, samples);
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
                    normalize_rms(b->vRMS, b->vRMS, b->vPfData, samples);
                    // Apply lookahead delay to Peak/RMS signal
                    b->sPfDelay.process(b->vPfData, b->vRMS, samples);
                    // Process sidechain signal and produce VCA
                    b->sBp.process(b->vPfData, vBuffer, b->vRMS, samples);
                    // Apply VCA to peak signal
                    dsp::mul2(b->vPfData, b->vPfData, samples);
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

                    // Apply time shift delay
                    b->sBpScDelay.process(vBuffer, b->vPfData, samples);
                    // Process sidechain signal and produce VCA
                    b->sBp.process(b->vPfData, vBuffer, vBuffer, samples);
                    // Apply time shift delay + latency compensation to band signal
                    b->sBpDelay.process(vBuffer, b->vIn, samples);
                    // Apply VCA to original signal
                    dsp::mul2(b->vPfData, vBuffer, samples);
                }
            }
        }

        void beat_breather::mix_bands(size_t samples)
        {
            // Mix bands depending on the band listen mode
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                dsp::fill_zero(c->vData, samples);

                for (size_t j=0; j<meta::beat_breather::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    switch (b->nMode)
                    {
                        case BAND_LISTEN_XOVER:
                            dsp::fmadd_k3(c->vData, b->vIn, b->fGain, samples);
                            break;
                        case BAND_LISTEN_RMS:
                            dsp::fmadd_k3(c->vData, b->vRMS, b->fGain, samples);
                            break;
                        case BAND_LISTEN_PF:
                            dsp::fmadd_k3(c->vData, b->vPfData, b->fGain, samples);
                            break;
                        case BAND_ON:
                            dsp::fmadd_k3(c->vData, b->vBpData, b->fGain, samples);
                            break;

                        case BAND_MUTE:
                        case BAND_OFF:
                        default:
                            break;
                    }
                }
            }
        }

        void beat_breather::post_process(size_t samples)
        {
            // Mix bands depending on the band listen mode
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Delay the dry signal
                c->sDelay.process(vBuffer, c->vIn, samples);
                // Mix dry/wet into channel_t::vData
                dsp::mix2(c->vData, vBuffer, fWetGain, fDryGain, samples);
                // Apply bypass
                c->sBypass.process(c->vOut, vBuffer, c->vData, samples);
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

        void beat_breather::dump(dspu::IStateDumper *v) const
        {
        }

    } /* namespace plugins */
} /* namespace lsp */


