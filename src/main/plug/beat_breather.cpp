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
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>

#include <private/plugins/beat_breather.h>

/* The size of temporary buffer for audio processing */
#define BUFFER_SIZE         0x1000U

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
            size_t szof_channels    = align_size(sizeof(channel_t) * nChannels, DEFAULT_ALIGN);
            size_t to_alloc         = szof_channels;

            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;

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
                    b->sBpDelay.construct();

                    b->nAnIn                = an_ch++;
                    b->nAnOut               = an_ch++;

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

                    b->pPfListen            = NULL;
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

                        b->pPfListen            = sb->pPfListen;
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

                        b->pPfListen            = TRACE_PORT(ports[port_id++]);
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

        void beat_breather::update_sample_rate(long sr)
        {
        }

        void beat_breather::update_settings()
        {
        }

        void beat_breather::process(size_t samples)
        {
        }

        void beat_breather::dump(dspu::IStateDumper *v) const
        {
        }

    } /* namespace plugins */
} /* namespace lsp */


