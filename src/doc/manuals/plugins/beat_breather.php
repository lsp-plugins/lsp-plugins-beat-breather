<?php
	plugin_header();
	$m      =   ($PAGE == 'beat_breather_mono') ? 'm' : 's';
	$sm     =   ($m != 'm') ? ' L, R' : '';
?>

<p>This plugin allows to drive much more dynamics into punchy sounds like drums and make them breathe again.</p>
<p>The plugin detects punchy sounds and adds more dynamics to them. This is acieved by applying multi-stage processing:</p>
<ul>
	<li><b>Frequency split</b> - at this stage the whole frequency range becomes split into individual bands. To make sound
	much cleaner, the linear phase crossover is applied which adds some latency but provides several benefits:
	</li>
	<ul>
		<li>Unlike classic crossovers which use IIR (Infinite Impulse Response) filters to split signal into multiple bands and shift the phase
		of the audio signal at band split points, the <b>Linear Phase</b> allows to use FIR (Finite Impulse Response) filters which are deprived of this.
		<li>Unlike most IIR filters which are designed using bilinear transform, linear phase filters allow simulate their tranfer function
		to look like the transfer function of analog filters, without deforming it's magnitude envelope near the nyquist frequency.</li>
		<li>Unlike design of classic Linkwitz-Riley filters, the design of IIR filters provides shorter transition zone of the filter.</li>
		<li>Unlike classic IIR filters the slope of the FIR filter is not restricted to be the multiple of 6 dB.</li>
	</ul>
	<li><b>Punch detection</b> - detection of peaks above the average level of the audio. For that case two type of RMS values are computed:</li>
	<ul>
		<li><b>Long time</b> - the long-time RMS value (by default, the esimation period is 400 ms) to estimate the average level of the audio signal.</li> 
		<li><b>Short time</b> - the short-time RMS value (by default, the estimation period is less than 20 ms and depends on the frequency range) to estimate the envelope of the changing in time signal.</li>
	</ul>
	<li><b>Punch filtering</b> - additional stage to eliminate some noise from the signal not associated with punches.</li>
	<li><b>Beat processing</b> - short-time dynamic amplification of the frequency band accoding to the signal passed from the punch filter.</li>
</ul>

<p>The simlified schema of the device is shown on the figure below.</p>
<?php out_image('graph/beat-breather-scheme', 'Simplified scheme of the audio processing') ?>

<p><b>Controls:</b></p>
<ul>
	<li>
		<b>Bypass</b> - bypass switch, when turned on (led indicator is shining), the output signal is similar to input signal but delayed by the latency introduced by the plugin.
	</li>
	<?php if ($m == 's') { ?>
	<li><b>Stereo Split</b> - enables independent compression of left and right channels.</li>
	<?php } ?>
	<li><b>Filters<?= $sm ?></b> - enables drawing tranfer function of each sidechain filter on the spectrum graph.</li>
	<li><b>Zoom</b> - zoom fader, allows to adjust zoom on the frequency chart.</li>
</ul>
<p><b>'Analysis' section:</b></p>
<ul>
	<li><b>FFT<?= $sm ?> In</b> - enables FFT curve graph of input signal on the spectrum graph.</li>
	<li><b>FFT<?= $sm ?> Out</b> - enables FFT curve graph of output signal on the spectrum graph.</li>
	<li><b>Reactivity</b> - the reactivity (smoothness) of the spectral analysis.</li>
	<li><b>Shift</b> - allows to adjust the overall gain of the analysis.</li>
</ul>
<p><b>'Signal' section:</b></p>
<ul>
	<li><b>Input</b> - the amount of gain applied to the input signal before processing.</li>
	<li><b>Output</b> - the amount of gain applied to the output signal before processing.</li>
	<li><b>Dry</b> - the amount of dry (unprocessed) signal passed to the output.</li>
	<li><b>Wet</b> - the amount of wet (processed) signal passed to the output.</li>
	<li><b>Dry/Wet</b> - the balance between the mixed signal (see Dry and Wet knobs) and unprocessed (Dry) signal.</li>
	<li><b>In</b> - the input signal meter.</li>
	<li><b>Out</b> - the output signal meter.</li>
</ul>
<p><b>Common tab controls:</b>
<ul>
	<li><b>Band</b> - the button that enables the corresponding band. Band #1 is always enabled and can not be disabled.</li>
	<li><b>S</b> - turn on the soloing mode of the corresponding band.</li>
	<li><b>M</b> - mute the corresponding band.</li>
	<li><b>Chain output</b> - output the signal from the selected chain:</li>
	<ul>
		<li><b>BF</b> - output the signal directly from Band Filter.</li>
		<li><b>PD</b> - output the signal directly from Punch Detector.</li>
		<li><b>PF</b> - output the signal directly from Punch Filter.</li>
		<li><b>BP</b> - output the signal directly from Beat Processor (default).</li>
	</ul>
</ul>
<p><b>'Band filter' tab:</b></p>
<ul>
	<li><b>Range</b> - shows the frequency range of the corresponding band and allows to tune the split frequency.</li>
	<li><b>Input</b> - the level meter of the input signal for the corresponding band.</li>
	<li><b>Output</b> - the level meter of the output signal from the corresponding band.</li>
	<li><b>HPF</b> - the slope of the hi-pass filter for the corresponding band.</li>
	<li><b>LPF</b> - the slope of the lo-pass filter for the corresponding band.</li>
	<li><b>Flatten</b> - the additional flattening of the band by cutting the high cap of the band.</li>
	<li><b>Gain</b> - the output gain of the band.</li>
</ul>
<p><b>'Punch Detector' tab:</b></p>
<ul>
	<li><b>Punch Graph</b> - the graph which shows detected punches for the corresponding frequency band.</li>
	<li><b>Long RMS</b> - the time frame for estimating long-time RMS value.</li>
	<li><b>Short RMS</b> - the time frame for estimating short-time RMS value.</li>
	<li><b>RMS Bias</b> - additional amplification of short RMS value for effective noise removal.</li>
	<li><b>Makeup</b> - the makeup gain of the signal passed from the Punch Detector to the Punch Filter.</li>
</ul>
<p><b>'Punch Filter' tab:</b></p>
<ul>
	<li><b>Reduction Graph</b> - the graph that demonstrates the work of the Punch Filter for the corresponding band.</li>
	<li><b>Lookahead</b> - additional lookahead for the Punch Filter.</li>
	<li><b>Attack</b> - the attack time of the Punch Filter.</li>
	<li><b>Release</b> - the release time of the Punch Filter.</li>
	<li><b>Threshold</b> - the threshold below which the input signal becomes gain-reduced.</li>
	<li><b>Zone</b> - the size of the transition zone between fully gain-reduced signal and non-reduced signal.</li>
	<li><b>Reduction</b> - the maximum gain reduction of the signal.</li>
</ul>
<p><b>'Beat Processor' tab:</b></p>
<ul>
	<li><b>Gain Graph</b> - the graph that demonstrates the work of the Beat Processor for the corresponding band.</li>
	<li><b>Time Shift</b> - the time shift of the gain amplification impulse relatively to the original signal.
	  If positive, the amplification happens some time after the punch, some time before the punch if negative.</li>
	<li><b>Attack</b> - the attack time of the Beat Processor.</li>
	<li><b>Release</b> - the release time of the Beat Processor.</li>
	<li><b>Threshold</b> - the threshold above which the signal becomes amplified.</li>
	<li><b>Ratio</b> - the amplification ratio of the original signal.</li>
	<li><b>Max Gain</b> - the maximum amplification gain that can be applied to the original signal.</li>
</ul>



