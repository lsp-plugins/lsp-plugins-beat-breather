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

#ifndef PRIVATE_PLUGINS_BEAT_BREATHER_H_
#define PRIVATE_PLUGINS_BEAT_BREATHER_H_

#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/dynamics/Gate.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/FFTCrossover.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>
#include <lsp-plug.in/plug-fw/plug.h>
#include <private/meta/beat_breather.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Base class for the latency compensation delay
         */
        class beat_breather: public plug::Module
        {
            private:
                beat_breather & operator = (const beat_breather &);
                beat_breather (const beat_breather &);

            protected:
                typedef struct split_t
                {
                    size_t              nBandId;        // Associated band identifier
                    float               fFrequency;     // Frequency
                    bool                bEnabled;       // Enable flag

                    plug::IPort        *pEnable;        // Enable flag
                    plug::IPort        *pFrequency;     // Split frequency
                } split_t;

                typedef struct band_t
                {
                    dspu::Delay         sDelay;         // Delay compensation for the whole band
                    dspu::Sidechain     sLongSc;        // Long RMS estimation
                    dspu::Sidechain     sShortSc;       // Short RMS estimation
                    dspu::Delay         sShortDelay;    // Delay for short RMS estimation
                    dspu::Gate          sPf;            // Punch filter
                    dspu::Delay         sPfDelay;       // Delay for lookahead of punch filter
                    dspu::Gate          sBp;            // Beat processor
                    dspu::Gate          sBpDelay;       // Beat processor delay

                    size_t              nAnIn;          // Analyzer input channel identifier
                    size_t              nAnOut;         // Analyzer output channel identifier

                    plug::IPort        *pSolo;          // Solo band
                    plug::IPort        *pMute;          // Mute band
                    plug::IPort        *pListen;        // Listen band
                    plug::IPort        *pLpfSlope;      // Lo-pass slope
                    plug::IPort        *pHpfSlope;      // Hi-pass slope
                    plug::IPort        *pFlatten;       // Band flatten
                    plug::IPort        *pOutGain;       // Output gain
                    plug::IPort        *pFreqEnd;       // Frequency of the lo-pass filter

                    plug::IPort        *pInLevel;       // Input level meter
                    plug::IPort        *pOutLevel;      // Output level meter

                    plug::IPort        *pPfListen;      // Listen punch filter
                    plug::IPort        *pPfLongTime;    // Long-time RMS estimation
                    plug::IPort        *pPfShortTime;   // Short-time RMS estimation
                    plug::IPort        *pPfLookahead;   // Punch filer lookahead
                    plug::IPort        *pPfAttack;      // Punch filer attack time
                    plug::IPort        *pPfRelease;     // Punch filer release time
                    plug::IPort        *pPfThreshold;   // Punch filer threshold
                    plug::IPort        *pPfReduction;   // Punch filer reduction
                    plug::IPort        *pPfZone;        // Punch filer zone
                    plug::IPort        *pPfMesh;        // Punch filer curve mesh
                    plug::IPort        *pPfEnvLevel;    // Punch filer envelope meter
                    plug::IPort        *pPfCurveLevel;  // Punch filer curve meter
                    plug::IPort        *pPfGainLevel;   // Punch filer gain meter

                    plug::IPort        *pBpAttack;      // Beat processor attack time
                    plug::IPort        *pBpRelease;     // Beat processor release time
                    plug::IPort        *pBpTimeShift;   // Beat processor time shift
                    plug::IPort        *pBpThreshold;   // Beat processor threshold
                    plug::IPort        *pBpRatio;       // Beat processor ratio
                    plug::IPort        *pBpMaxGain;     // Beat processor max gain
                    plug::IPort        *pBpMesh;        // Beat processor curve mesh
                    plug::IPort        *pBpEnvLevel;    // Beat processor envelope meter
                    plug::IPort        *pBpCurveLevel;  // Beat processor curve meter
                    plug::IPort        *pBpGainLevel;   // Beat processor gain meter
                } band_t;

                typedef struct channel_t
                {
                    dspu::Bypass        sBypass;        // Bypass
                    dspu::FFTCrossover  sCrossover;     // FFT crossover
                    dspu::Delay         sDelay;         // Delay for Dry signal

                    band_t              vBands[meta::beat_breather::BANDS_MAX];     // Bands

                    plug::IPort        *pIn;            // Input
                    plug::IPort        *pOut;           // Output

                    plug::IPort        *pInLevel;       // Input level meter
                    plug::IPort        *pOutLevel;      // Output level meter
                    plug::IPort        *pInFft;         // Input FFT enable
                    plug::IPort        *pOutFft;        // Output FFT enable
                    plug::IPort        *pInMesh;        // Input FFT mesh
                    plug::IPort        *pOutMesh;       // Output FFT mesh
                } channel_t;

            protected:
                size_t              nChannels;          // Number of channels
                channel_t          *vChannels;          // Delay channels

                dspu::Analyzer      sAnalyzer;          // Analyzer
                split_t             vSplits[meta::beat_breather::BANDS_MAX-1];

                plug::IPort        *pBypass;            // Bypass
                plug::IPort        *pInGain;            // Input gain
                plug::IPort        *pDryGain;           // Dry gain
                plug::IPort        *pWetGain;           // Wet gain
                plug::IPort        *pOutGain;           // Output gain
                plug::IPort        *pStereoSplit;       // Stereo split
                plug::IPort        *pFFTReactivity;     // FFT reactivity
                plug::IPort        *pFFTShift;          // FFT shift

                uint8_t            *pData;              // Allocated data

            public:
                explicit beat_breather(const meta::plugin_t *meta);
                virtual ~beat_breather() override;

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

            public:
                virtual void        update_sample_rate(long sr) override;
                virtual void        update_settings() override;
                virtual void        process(size_t samples) override;
                virtual void        dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */


#endif /* PRIVATE_PLUGINS_BEAT_BREATHER_H_ */

