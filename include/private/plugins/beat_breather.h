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
#include <lsp-plug.in/dsp-units/util/MeterGraph.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>
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
                enum band_mode_t
                {
                    BAND_OFF,       // Band disabled
                    BAND_MUTE,      // Band muted
                    BAND_BF,        // + Band filter
                    BAND_PD,        // + Peak detector
                    BAND_PF,        // + Peak filter
                    BAND_BP         // + Beat processor
                };

                enum sync_t
                {
                    SYNC_BAND_FILTER    = 1 << 0,       // Band curve
                    SYNC_PEAK_FILTER    = 1 << 1,       // Peak filter curve
                    SYNC_BEAT_PROC      = 1 << 2,       // Peak processor curve
                    SYNC_ALL            = SYNC_BAND_FILTER | SYNC_PEAK_FILTER | SYNC_BEAT_PROC
                };

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
                    dspu::Sidechain     sPdLong;        // Peak detector long RMS estimation
                    dspu::Sidechain     sPdShort;       // Peak detector short RMS estimation
                    dspu::Delay         sPdDelay;       // Peak detector delay for short RMS estimation
                    dspu::MeterGraph    sPdMeter;       // Meter graph
                    dspu::Gate          sPf;            // Punch filter
                    dspu::Delay         sPfDelay;       // Delay for lookahead of punch filter
                    dspu::Gate          sBp;            // Beat processor
                    dspu::Delay         sBpScDelay;     // Beat processor sidechain delay
                    dspu::Delay         sBpDelay;       // Beat processor delay

                    band_mode_t         nOldMode;       // Old band mode
                    band_mode_t         nMode;          // Band mode
                    float               fGain;          // Band gain
                    float               fInLevel;       // Input level measured
                    float               fOutLevel;      // Output level measured
                    float               fReduction;     // Band reduction
                    size_t              nSync;          // Synchronize curve flags
                    float               fPdMakeup;      // Peak detector makeup gain
                    float               fPdLevel;       // Peak detector level
                    float               fPfInGain;      // Peak filter input gain
                    float               fPfOutGain;     // Peak filter output gain
                    float               fPfReduction;   // Peak filter reduction value
                    float               fBpMakeup;      // Beat processor makeup gain
                    float               fBpInGain;      // Beat processor input gain
                    float               fBpOutGain;     // Beat processor output gain
                    float               fBpReduction;   // Beat processor reduction value

                    float              *vInData;        // Original band data after crossover
                    float              *vPdData;        // Peak detector data
                    float              *vPfData;        // Output of Peak Filter
                    float              *vBpData;        // Output of Beat Processor
                    float              *vFreqChart;     // Frequency chart
                    float              *vPfMesh;        // Peak filter mesh
                    float              *vBpMesh;        // Beat processor mesh

                    plug::IPort        *pSolo;          // Solo band
                    plug::IPort        *pMute;          // Mute band
                    plug::IPort        *pListen;        // Listen band
                    plug::IPort        *pLpfSlope;      // Lo-pass slope
                    plug::IPort        *pHpfSlope;      // Hi-pass slope
                    plug::IPort        *pFlatten;       // Band flatten
                    plug::IPort        *pOutGain;       // Output gain
                    plug::IPort        *pFreqEnd;       // Frequency of the lo-pass filter
                    plug::IPort        *pFreqMesh;      // Output transfer function mesh

                    plug::IPort        *pInLevel;       // Input level meter
                    plug::IPort        *pOutLevel;      // Output level meter

                    plug::IPort        *pPdLongTime;    // Punch detector Long-time RMS estimation
                    plug::IPort        *pPdShortTime;   // Punch detector Short-time RMS estimation
                    plug::IPort        *pPdBias;        // Punch detector Short-time RMS bias
                    plug::IPort        *pPdMakeup;      // Punch detector Makeup gain
                    plug::IPort        *pPdMesh;        // Punch detector output mesh

                    plug::IPort        *pPfLookahead;   // Punch detector Punch filer lookahead
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
                    dspu::Delay         sDelay;         // Delay for channel signal
                    dspu::Delay         sDryDelay;      // Delay compensation for the dry (unprocessed) signal

                    band_t              vBands[meta::beat_breather::BANDS_MAX];     // Bands

                    size_t              nAnIn;          // Analyzer input channel identifier
                    size_t              nAnOut;         // Analyzer output channel identifier
                    float               fInLevel;       // Input level measured
                    float               fOutLevel;      // Output level measured

                    float              *vIn;            // Input buffer
                    float              *vOut;           // Output buffer
                    float              *vInData;        // Processed input data
                    float              *vOutData;       // Processed channel data
                    float              *vFreqChart;     // Frequency chart

                    plug::IPort        *pIn;            // Input
                    plug::IPort        *pOut;           // Output

                    plug::IPort        *pInLevel;       // Input level meter
                    plug::IPort        *pOutLevel;      // Output level meter
                    plug::IPort        *pInFft;         // Input FFT enable
                    plug::IPort        *pOutFft;        // Output FFT enable
                    plug::IPort        *pInMesh;        // Input FFT mesh
                    plug::IPort        *pOutMesh;       // Output FFT mesh
                    plug::IPort        *pFreqMesh;      // Output transfer function mesh
                } channel_t;

            protected:
                size_t              nChannels;          // Number of channels
                channel_t          *vChannels;          // Delay channels
                bool                bStereoSplit;       // Stereo split
                float               fInGain;            // Input gain
                float               fDryGain;           // Dry gain
                float               fWetGain;           // Wet gain
                float               fZoom;              // Zoom
                float              *vAnalyze[4];        // Buffers for spectrum analyzer

                dspu::Analyzer      sAnalyzer;          // Analyzer
                split_t             vSplits[meta::beat_breather::BANDS_MAX-1];

                float              *vBuffer;            // Temporary buffer for processing
                float              *vFftFreqs;          // List of FFT frequencies
                uint32_t           *vFftIndexes;        // List of analyzer FFT indexes
                float              *vPdMesh;            // Peak detector mesh
                float              *vPfMesh;            // Horizontal coordinates of peak filter curve
                float              *vBpMesh;            // Horizontal coordinates of beat processor curve

                plug::IPort        *pBypass;            // Bypass
                plug::IPort        *pInGain;            // Input gain
                plug::IPort        *pDryGain;           // Dry gain
                plug::IPort        *pWetGain;           // Wet gain
                plug::IPort        *pOutGain;           // Output gain
                plug::IPort        *pStereoSplit;       // Stereo split
                plug::IPort        *pFFTReactivity;     // FFT reactivity
                plug::IPort        *pFFTShift;          // FFT shift
                plug::IPort        *pZoom;              // Zoom

                core::IDBuffer     *pIDisplay;          // Inline display buffer

                uint8_t            *pData;              // Allocated data

            protected:
                static inline size_t        select_fft_rank(size_t sample_rate);
                static void                 process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count);
                static void                 normalize_rms(float *dst, const float *lrms, const float *srms, float gain, size_t samples);
                static int                  compare_splits(const void *a1, const void *a2, void *data);
                static band_mode_t          decode_band_mode(size_t mode);

            protected:
                void                bind_inputs();
                void                split_signal(size_t samples);
                void                apply_peak_detector(size_t samples);
                void                apply_punch_filter(size_t samples);
                void                apply_beat_processor(size_t samples);
                void                mix_bands(size_t samples);
                void                post_process_block(size_t samples);
                void                update_pointers(size_t samples);
                void                output_meters();

            public:
                explicit beat_breather(const meta::plugin_t *meta);
                virtual ~beat_breather() override;

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

            public:
                virtual void        update_sample_rate(long sr) override;
                virtual void        update_settings() override;
                virtual void        process(size_t samples) override;
                virtual void        ui_activated() override;
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height) override;
                virtual void        dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */


#endif /* PRIVATE_PLUGINS_BEAT_BREATHER_H_ */

