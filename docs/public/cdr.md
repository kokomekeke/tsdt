

## Interference signal generators included in Scenario Generator software

Below is a catalogue of the available *Signal* types with their associated parameters.
The [bracket] notation specifies units of measurement, value representation, and/or parameter domain.
Common parameters (frequency and amplitude) are specified by the *Event*, rather than the *Signal*.

Each *Signal* and *Output* will be identified by a name, that is also specified as a parameter.
*Events* reference these names to connect a signal to an output.

### Tone
Simple sine wave.

_Parameters_: -
_Algorithm_:
Because tuning happens after generation, each sample is actually set to 1.0 at this point in the process.


### Noise
White noise with a given frequency span.

_Parameters_:
- span: [Hz, double]

_Algorithm_:
A random value sampled from a normal distribution is added to each signal sample I/Q component independently.
The random number generator is initialized using a predefined constant seed for reproducibility.

y_i = real_i + j * imag_i
real_i ~ N(0, s^2)
imag_i ~ N(0, s^2)
s^2 = (psd * f_s / 2)

where:
y_i: output signal
s^2: variance
f_s: sample rate
psd: power spectral density

psd is set to an arbitrary constant, 10^(-6) in this case.
This corresponds to -60db/Hz.
The I/Q values are clamped to the [-1.0, 1.0] range individually.
The constant ensures that the samples dont regularly exceed the  range.
To change the amplitude, one should adjust the "amplitude" parameter of the _Event_.

### Chirp
Sine wave constantly shifting in frequency in a "saw" pattern.

_Parameters_:
- span: Span of the frequencies that the signal will sweep through. [Hz, double]
- rate: Frequency that the full span sweep repeats with. [Hz, double]

_Algorithm_:
A real valued saw wave is generated, then FM modulated.

x_i = span * (frac(i * rate / f_s) - 0.5)
y_i = cos(p_i) + j * sin(p_i)
p_0 = 0; p_{i+1} = p_i + (x_i * 2 * pi / f_s)

where:
x_i: saw wave
y_i: output chirp signal
p_i: phase
frac: fractional part function
i: sample index
f_s: sample rate
j: imaginary constant

### Pulse
Sine wave that is turns periodically.

_Parameters_:
- rate: Frequency of the on/off cycle.
- dutycycle: Ratio of time that the signal is on. [double, [0.0, 1.0]]

_Algorithm_:

y_i = 1, if frac(p_i) < dutycycle
y_i = 0.5, if frac(p_i) = dutycycle
y_i = 0.0, if frac(p_i) > dutycycle
p_0 = 0; p_{i+1} = p_i + (rate / f_s)

where:
y_i: output samples
p_i: phase
frac: fractional part function


### IQFile
IQ samples from a prerecorded file.
Available formats: SigMF, WAV

_Parameters_:
- file: Path to file [string]

### AudioFile
Real valued samples from a prerecorded file with optional analog modulation.
Available modulation types are: FM, AM, PM, IQ. The "IQ" option means no modulation.
Available formats: WAV

_Parameters_:
- file: Path to the file containing audio [string]
- modulation: Analog modulation type [string]

_Algorithm_:

By modulation:
FM:
y_i = cos(p_i) + j * sin(p_i)
p_{i+1} = p_i + dev * x_i / f_s

AM:
y_i = 0.5 + dev * x_i

PM:
y_i = cos(x_i * dev) + j * sin(x_i * dev)

where:
y_i: output samples
x_i: input samples
dev: "deviation" constant. For FM, this is set to 50kHz, for AM it is 0.3, and for PM it is pi

Note, that the output will be tuned based on the actual target frequency.


### Resampling and tuning

After the baseband signal is generated, it will be tuned by the appropriate frequency shift, to hit the desired absolute frequency when streaming on the selected output device.
After this, it will be resampled to the output device's sample rate.
Resampling happens via a chain of multirate FIR-s.



## Other parameters

### *Outputs*
Each *Output* represents an output radio device.
In streaming/playback mode, these are actual devices transmitting signals.
In recording mode, the signal is generated, and saved to a file identified with the device name.

Parameters are device type dependent, but should include:
- Transmission frequency
- Transmission sample rate
- Gain
- Device serial

### *Events*
An occurrence of a *Signal* on an *Output* is called an *Event*.
Multiple *Events* can refer to the same *Signal* with different *Event* parameters.
Also, multiple *Events* can refer to the same *Output* with different occurrence times and durations.
*Event* parameters are independent of the *Signal*/*Output* type.
- signal: Name of the signal [string]
- output: Name of the output [string]
- start: *Signal* occurrence starting time relative to scenario start [s, double]
- end: *Signal* occurrence ending time relative to scenario start [s, double]
- frequency: Center frequency of the signal occurrence. It has to be in the frequency range covered by the output device. [Hz, double]
- amplitude: Linear gain for the generated samples. [double]

The scenario descriptor contains an additional parameter:
- duration: [s, double]


## Timing

*Event* "start"/"end" parameters are given in seconds relative to the start of the scenario.
Therefore, these have to be in the range [0.0, duration] where duration is the scenario parameter.
Any number of *Events* may overlap in time, even on the same *Output*.
Occurrences of a *Signal* are independent, a *Signal* may appear in multiple *Events*, even if those overlap.


## Internal design

The digitally generated signal is repersented by complex, double precision floating point values, (2x64bit) with both I and Q components in the range [-1.0, 1.0].
(The values may exceed this range internally, but they will be clamped before streaming.)
The actual radio signal amplitude will be determined by the output device and antenna gain.

The *Signal* type, with its parameters specifies the generation process, but *Events* are generated independently.
This design is to ensure that occurrences of a *Signal* can overlap without interference, and they can also be arbitrarily far apart without an unbounded buffer.

*Event* overlaps will be handled by adding the concurrent signal data together before streaming to the *Output*.


## Scenario descriptor example

```json
{
    "duration": 20,
    "outputs": [
        {
            "name": "r1",
            "type": "example_sdr",
            "serial": "ABC123",
            "samplerate": 300000,
            "frequency": 434500000.0,
            "gain": 40.0
        }
    ],
    "signals": [
        {
            "name": "tone",
            "type": "Tone"
        },
        {
            "name": "noise",
            "type": "Noise",
            "span": 1000000
        },
        {
            "name": "slow_chirp",
            "type": "Chirp",
            "span": 1000000,
            "rate": 3.0
        },
        {
            "name": "fast_chirp",
            "type": "Chirp",
            "span": 1000000,
            "rate": 20.0
        },
        {
            "name": "sample_file",
            "type": "IQFile",
            "file": "C:/recordings/sample_file.sigmf-data"
        },
    ],
    "events": [
        {
            "signal": "slow_chirp",
            "output": "r1",
            "frequency": 434500000.0,
            "amplitude": 0.4,
            "start": 5,
            "end": 10
        },
        {
            "signal": "fast_chirp",
            "output": "r1",
            "frequency": 434600000.0,
            "amplitude": 0.5,
            "start": 7,
            "end": 19
        }
    ]
}
```

### Explanation of the example

The example specifies a single output device, which is an "example_srd" type radio.
The output device covers the frequency range 434.5MHz +/- 0.3MHz, with 40.0dB gain.
There are 5 defined *Signal*-s, but only 2 of them actually occur in the scenario.
The "slow_chirp" *Signal* is present in the interval [t+5s, t+10s], where t is the starting time of the scenario.


