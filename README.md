# Davidian Ashgrove

Circuit-modelled guitar and bass amplifier plugins (LV2, VST3, CLAP and Audio Unit): 11 valve amps from a classic 1980s British amp range, solved at circuit level from the service-manual schematics, with a choice of output valves and speaker cabinets. They come as five plugins, one per amp family, each with a model selector.

**A close model, not an exact copy.** It is a simulation of the circuits from their component values, not sampled or measured amps, and some details are reasoned estimates (listed under [Estimates and sources](#estimates-and-sources)). It has not been compared side by side with built units, so expect it to sound close to the hardware rather than identical.

![The GUI (Classic Lead)](docs/panel_classic_lead.png)

Mono amps (mono or stereo tracks work too). The same GUI in every format on Windows, macOS and Linux. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |
| VST3 | yes | yes | yes |
| CLAP | yes | yes | yes |
| Audio Unit (AUv2) | | yes | |

| Plugin | Models | Output valves | Default cabinet |
|---|---|---|---|
| Classic Lead | CL100 (100 W head), CL50 (50 W head) | EL34, KT77, 6550 | 4x12 Vintage |
| Cascade Lead | CH100, CC100 (100 W head, combo), CH50, CC50 (50 W head, combo) | EL34, KT77, 6550 | 4x12 Vintage |
| Low End | LE50 (50 W), LE100 (100 W) bass heads | EL34, KT77, 6550 | 4x10 Bass |
| Spring Combo | SC212 (2x12), SC410 (4x10) | KT77, 6550 | 2x12 Open |
| Dual Rail | DR250 (250 W head) | 6550, KT77 | 4x12 Modern |

The output valves offered are the ones each spec sheet lists. The manual draws CC100/CH100, CC50/CH50 and SC212/SC410 the same, so each pair sounds the same here.

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-ashgrove/releases). The same files are in the `prebuilt/` folder of the repository.

| Download | Contains | For |
|---|---|---|
| `ashgrove-windows-x64.zip` | LV2, VST3, CLAP | Windows 10/11, 64-bit hosts |
| `ashgrove-macos-universal.zip` | LV2, VST3, CLAP, Audio Unit | macOS 10.13 or later, Apple silicon and Intel |
| `ashgrove-linux-x64.zip` | LV2, VST3, CLAP | 64-bit Intel/AMD Linux (X11 GUI) |

Unzip, copy the plugin folder for your host's format into its plugin folder, and restart your host (or rescan plugins). Each zip has one folder per format; use only the format your host prefers:

| Format | Copy | Into |
|---|---|---|
| VST3 (Windows) | `Davidian Ashgrove.vst3` | `C:\Program Files\Common Files\VST3` |
| VST3 (macOS) | `Davidian Ashgrove.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| VST3 (Linux) | `Davidian Ashgrove.vst3` | `~/.vst3` |
| CLAP (Windows) | `Davidian Ashgrove.clap` | `C:\Program Files\Common Files\CLAP` |
| CLAP (macOS) | `Davidian Ashgrove.clap` | `~/Library/Audio/Plug-Ins/CLAP` |
| CLAP (Linux) | `Davidian Ashgrove.clap` | `~/.clap` |
| Audio Unit (macOS) | `Davidian Ashgrove.component` | `~/Library/Audio/Plug-Ins/Components` |
| LV2 (Windows) | `ashgrove.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) |
| LV2 (macOS) | `ashgrove.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `ashgrove.lv2` | `~/.lv2` |

All five plugins are in each file. In the host they are listed under **Davidian** (VST3, CLAP, Audio Unit) or as **Davidian Ashgrove ...** (LV2). The VST3, AU and CLAP versions are the same plugin, so a project saved in one host opens with the same settings in another that uses the same format. Replace any older copy instead of keeping two: when two copies have the same ID, the host uses one and ignores the other.

The macOS build isn't notarized, so macOS may block it after download. If the host can't load it, run this once in Terminal after copying (and the same for the other formats you installed):

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Davidian Ashgrove.component"
```

Logic Pro and GarageBand only show an Audio Unit once macOS has registered it. If it doesn't appear, log out and back in, or restart the Mac.

## Controls

Every panel has these:

| Panel | Control | Circuit part |
|---|---|---|
| Model | Model | The family's models, default the first. Rebuilds the circuit behind a short fade |
| Output Valves | Output Valves | The spec sheet's list, default the first. Changes the power-valve curves and bias |
| Input | Input / Channel | The family's jacks or channels, default the first: high and low jacks, or channels |
| Oversampling | 2X / 4X | Not on the amp: default 2x. 4x costs about twice the CPU |
| Cabinet | Cabinet | Not on the amp: Off and five cabinets, default per plugin (see [Cabinets](#cabinets)). Switches behind a short fade |
| Levels | Input | Input trim, -36 to +36 dB, default 0 dB: 0 dBFS at the input counts as 1 V peak at the amp's jack |
| Levels | Output | Not on the amp: output level, -60 to +12 dB, default -6 dB. With the cabinet off, 0 dBFS before this control is rated power into the rated load |

The amp controls on each panel:

| Plugin | Amp controls |
|---|---|
| Classic Lead | Volume I, Volume II, Treble, Middle, Bass, Presence |
| Cascade Lead | Preamp, Master, Treble, Middle, Bass, Presence |
| Low End | Volume, Treble, Middle, Mid Sweep, Bass, Slope |
| Spring Combo | Ch I Volume, Bright, Boost, Treble, Middle, Bass; Reverb Depth; Ch II Volume, Treble, Middle, Bass; Master |
| Dual Rail | Ch I Gain, Treble, Middle, Bass, Presence; Ch II Gain, Treble, Middle, Mid Sweep, Bass, Volume |

Amp knobs run 0 to 10, as on the panels, and default to 5. Slope defaults to 0 and reverb depth to 3. Model, valve, input, oversampling and cabinet changes switch behind a short fade, so don't automate them.

Every panel control is a host parameter in VST3, AU and CLAP. Its ID is the control's LV2 port number, so all formats share one numbering. Selectors show their choices by name, and the levels show dB. Knobs and switches can be automated; selectors aren't offered for automation, because each change rebuilds the circuit. The plugin's settings are saved with the project. Latency is 16 samples and is reported to the host. The cabinet port comes after each plugin's knobs, so sessions saved with version 1.0 keep their settings and open with the plugin's default cabinet.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. Click a selector row to choose it; the lit amber lamp is the current choice. Toggles are up for on. The readout at the top right shows the control you last touched and its value.

### Classic Lead

![Classic Lead panel](docs/panel_classic_lead.png)

Two-channel lead head with no master volume. Volume I and Volume II set each channel's level, and the four inputs pick the channel and the high or low jack.

### Cascade Lead

![Cascade Lead panel](docs/panel_cascade_lead.png)

Master-volume lead amp. Preamp sets the gain into the cascaded stage, and Master sets the level into the power amp.

### Low End

![Low End panel](docs/panel_low_end.png)

Bass head with a swept middle. Mid Sweep moves the middle control between about 400 Hz and 1 kHz, and Slope tilts the response.

### Spring Combo

![Spring Combo panel](docs/panel_spring_combo.png)

Two-channel combo with spring reverb. Channel I has Bright and Boost switches. Both channels share the reverb and the master volume.

### Dual Rail

![Dual Rail panel](docs/panel_dual_rail.png)

Relay-switched two-channel 250 W head. Channel II adds a mid sweep and its own volume. The Channel selector stands in for the relay footswitch.

### Cabinets

Each cabinet is a short chain of filters shaped like a close-miked cabinet of that kind: the speaker's low-end resonance, the box's midrange character, the cone's presence peaks and the steep roll-off above them. They are level matched over 100 Hz to 5 kHz, so switching changes the tone more than the loudness, though peaks can rise by a few dB. Set the cabinet to **Off** to use your own speaker impulse response after the plugin.

| Cabinet | Character |
|---|---|
| Off | The amp's speaker-terminal signal |
| 4x12 Vintage | Closed back with vintage-style 25 W speakers: firm low thump, soft upper-mid hump, early top roll-off |
| 4x12 Modern | Closed back with modern high-power speakers: deeper, scooped mids, more top end |
| 2x12 Open | Open-back combo: lighter low end, airy and forward mids |
| 4x10 Bass | Closed bass cabinet: tight lows, punchy upper range |
| 1x15 Bass | Single 15 inch bass cabinet: deep and round, little top end |

Response at 48 kHz, in dB, from `cab_check`:

| Cabinet | 40 Hz | 80 Hz | 120 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 3 kHz | 5 kHz | 8 kHz | 12 kHz |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 4x12 Vintage | -10.6 | 2.7 | 3.0 | -1.8 | -4.1 | -1.6 | 1.7 | 2.7 | -5.5 | -23.0 | -42.3 |
| 4x12 Modern | -11.3 | 1.9 | 3.4 | -0.9 | -4.0 | -3.7 | 1.0 | 2.1 | -2.4 | -19.5 | -38.9 |
| 2x12 Open | -16.4 | -5.1 | -1.4 | -0.1 | -0.9 | 0.4 | 0.8 | 2.0 | -3.2 | -18.5 | -37.7 |
| 4x10 Bass | -3.3 | 2.7 | 1.7 | -0.9 | -1.8 | -0.2 | 1.6 | 0.5 | -3.1 | -18.8 | -37.9 |
| 1x15 Bass | 3.4 | 4.6 | 2.4 | 0.5 | -1.0 | 0.7 | 0.5 | -3.8 | -20.5 | -38.9 | -58.0 |

## How it is modelled

- **Circuit solver** (`src/circuit.h`): each amp is entered as its schematic: resistors, capacitors, pots, valves and the output transformer. A nonlinear state-space (DK-method) solver runs it every sample, using trapezoidal companion models for the capacitors and damped Newton iteration on the valves, with a line search and a fallback for hard overdrive.
- **Valves** (`src/tubes.h`): ECC83/12AX7 triodes and EL34, KT77 and 6550 pentodes use Koren's equations, built into lookup tables. Grid current is modelled, so stages block and clip as they do in the real amps.
- **Sections** (`src/amp.cpp`): the amp is solved in sections (preamp, tone stack, phase splitter, power amp), with supply rails and currents passed between them, so the power supply sags under load.
- **Power amp:** push-pull pairs into an output transformer, modelled as an ideal centre tap with its inductance and load, including the presence and feedback networks.
- **Spring reverb** (`src/spring.h`): the Spring Combo's tank is a separate DSP model, set to the level and decay the spec sheet gives.
- **Oversampling** (`src/oversampler.h`): the whole circuit runs at 2x or 4x behind windowed-sinc filters.
- **Cabinet** (`src/cabinet.h`): after the amp, at the host's sample rate, a chain of up to seven filters (low cut, resonance and presence peaks, top-end roll-off) shapes the speaker-terminal signal. See [Cabinets](#cabinets).

### Checked against the manual

`spec_test` measures every model against its spec sheet, at 48 kHz with 2x oversampling. Where a sheet states the condition (all controls full, mid down, sweep low or high) the test uses it; otherwise tone figures are shown for the setting named in each row.

Close to the manual:

| Check | Model | Manual |
|---|---|---|
| DC voltages, 17 test points in 4 configurations | Within about 5%; see below for points 11 and 14 | Table |
| Power, CL100/CH100 EL34 / KT77 / 6550 | 126 / 129 / 142 W at 4% THD | 115 W |
| Power, CL50/CH50 EL34 / KT77 / 6550 | 53 / 47 / 45 W at 3% THD | Over 50 W |
| Power, LE50 EL34 | 50 W at 3% THD | Over 50 W |
| Power, LE100 EL34 / 6550 | 121 / 146 W at 3% THD | 135 W at clipping |
| Power, SC212 KT77 / 6550 | 126 / 148 W at 4% THD | 120 W |
| Power, DR250 6550 at the marked -75 V bias | 244 W at 5% THD | 250 W |
| Sensitivity, CL50 Ch I / Ch II | 0.92 / 1.16 mV | 1 / 1.5 mV |
| Sensitivity, LE50 | 5.1 mV | 6 mV |
| Sensitivity, DR250 Ch II | 0.36 mV | 0.4 mV |
| SC212 boost / bright | +10.4 dB / +17.4 dB | +11 dB / +16 dB |
| Low input | -6.0 dB | -6 dB |
| SC212 reverb level / decay (calibrated to the sheet) | +17.0 dB / 2.9 s | +17 dB / 3 s |
| Lead bass, others down | 15.3 dB | 15 dB |
| Lead treble, CH50 | 31.5 dB | 32 dB |
| Bass amps: treble / bass / slope | 46.5 / 34.7 / -18.2 dB | 48 / 34 / -20 dB |
| Bass amps: mid sweep range | Deepest cut moves from 1 kHz to 400 Hz | 400 Hz-1 kHz |
| SC212 mid, Ch I / Ch II | 15.4 / 17.3 dB | 15 / 17 dB |
| DR250 Ch I presence | 15 dB | 13 dB |
| DR250 Ch II treble, mid down, sweep low | 38.5 dB | 40 dB |

DC points 11 and 14 (splitter grids) read 33-38 V against 23-25 V in the table. They sit behind 1M resistors, so a meter of that era would load them down; the points beside them match. This is an explanation, not something the manual says.

Misses:

| Check | Model | Manual |
|---|---|---|
| Lead treble (others down), CL100/CH100 / CL50 | 31 dB | 35 / 26 dB |
| Lead mid | 5.4-7.0 dB | 9.5 dB |
| Presence, lead amps | 8.5-9.1 dB | 6 dB |
| Sensitivity, CL100 / CH100 / CH50 | 1.44 / 0.22 / 0.16 mV | 1 / 0.15 / 0.1 mV |
| Sensitivity, LE100 / DR250 Ch I | 18 / 1.4 mV | 12 / 0.7 mV |
| Sensitivity, SC212 Ch I / Ch II | 12.8 / 41.5 mV | 3.5 / 30 mV |
| Bass amp mid | 13.7-16.6 dB | 18 dB |
| SC212 treble and bass, mid down | 41.9-43.1 / 36.6-38.9 dB | 37-40 / 34 dB |
| DR250 Ch I treble / bass, mid down | 31.5 / 29 dB | 28 / 24 dB |
| DR250 Ch II bass, mid down, sweep high | 31.8 dB | 25 dB |
| DR250 mid | 8.4-9.5 dB | 12 dB |
| DR250 with KT77 | 76 W at 5% THD (crossover at a cold bias), 298 W at 10% | No KT77 figure |
| Power at 10% THD, CL100 | 140 W into 16 ohms | 170 W into 4 ohms |
| Power at 10% THD, CL50/CH50 | 61 W into 16 ohms | 90 W into 16 ohms |

Notes on the misses:

- The CL100 and CL50 sheets give different treble figures for one shared drawing (the 1987 sheet says Channel I has a rising treble and Channel II is flat, so they may have measured different channels), so the model can't match both. The same applies to CH100 and CH50.
- The lead amps are all about 3 dB short on sensitivity and about 3 dB over on presence. Presence works by removing negative feedback at high frequencies, so both point to a feedback loop with about 3 dB more gain than the real amps. The circuit around the loop matches the drawings, so the likely cause is a remaining estimate: the power valves' transconductance into a resistive load instead of a speaker.
- SC212 Channel I is 11 dB short where Channel II is 3 dB short. Its dry signal reaches the mixing stage through the 6M8 resistor (R19), and the third stage's 2K7 cathode resistor is unbypassed; both read clearly on the drawing and were rechecked, so the model follows the drawing rather than the sheet here.

### Estimates and sources

Values the manual doesn't give, and where each one comes from:

- **Output valves** (`src/tubes.h`, checked with `tools/fit_valves.cpp`):
  - EL34: Koren's published parameters. At the data sheet's typical point (250 V, -13.5 V) the model draws 97 mA against 100 mA, with 12.2 mA/V against 11.
  - 6550: Koren's later fitted set (2001), which gives 145 mA and 10.9 mA/V against the data sheet's 140 mA and 11 mA/V. The earlier table values used before ran 15% hot.
  - KT77: no published Koren fit, so fitted to the KT77 data sheet's plate curves with Koren's usual exponent: 1.7% rms over ten points, 94 mA and 9.8 mA/V against 100 mA and 10.5 mA/V.
  - Screen current: fitted for each valve to its data sheet's typical screen current.
- **ECC83:** Koren's fit, trimmed to the manual's own DC table. Koren's later fitted 12AX7 set was also tried and matched the table less well (up to 8% off against about 4%).
- **Output transformers:** 50 W 3.2k and 100 W 1.7k plate to plate, the published impedances of the replacements sold for these chassis. The 100 W bass amp and the combo share one transformer (C2668) and use the same figure. Only the 250 W head's (C2728) is still estimated from its rated power. Inductances are estimates.
- **Choke** (1471-263): 5 H at 120 mA with about 115 ohms, from its replacements' ratings. The 250 W head's unmarked choke is assumed to be the same.
- **Bias:** the marked voltages are used, EL34 on the lead and bass amps and 6550 on the 250 W head (its bulletin's -75 V, which idles at about 41 W, inside the 42 W rating of current 6550s). The LE50 uses -29 V instead of "approx -26 V" to stay under the EL34's 25 W rating. Where no bias is marked, KT77 is set to the EL34's idle current and 6550 to 21 W.
- **HT voltages:** the SC212 uses 468 V like the 100 W bass amp, which has the same mains transformer. The DR250's 640 V is estimated from its 479 V winding and bridge rectifier.
- **Supply:** the mains transformers' source resistance and the rectifier drop are estimates.
- **Spring tank:** delay times and tone. Only its type, level and decay come from the manual.
- **Cabinets:** every filter frequency, Q and gain in `src/cabinet.h`. They follow the general shape of each kind of cabinet, not measurements of a particular product.

Readings rechecked on the drawings and corrected in this version:

- **DR250 Channel I:** its second stage drives the third through a 680K/100K divider with no coupling cap. That is DC coupling by design, and the third stage's 3K3 cathode resistor is sized for it, so the cap assumed before is gone.
- **DR250 middle pots:** in both channels they are wired as variable resistors with the cap to the top, not to the wiper as on the lead amps.
- **DR250 Channel II volume:** it is 1K LOG, as drawn. Both channels end in a low-impedance network for the relay and the 10K effects loop.
- **DR250 filters:** the reservoir and screen filters are 50 uF (two 100 uF banks in series), not 25 uF.
- **SC212 grid resistors:** 180K with 6550 (the USA build), not 150K.
- **SC212 reverb drive:** taken through R13 (1M) and C11 (500p) onto the driver's 1M grid resistor, and the send now comes from the driver's grid.

`spec_test` now also measures each sheet's Channel II sensitivity, and uses the 250 W sheet's sweep settings: sweep low (400 Hz) for treble, sweep high (1 kHz) for bass, with mid down.

### Simplifications

- Output valves are matched pairs into a resistive load, with no speaker impedance curve.
- The cabinets are fixed filter shapes: no microphone choice, room or speaker break-up. For more detail, set Cabinet to Off and use an impulse response.
- Effects loops, preamp out, the DR250's DI output and the footswitches aren't modelled. Boost, Reverb and the channel choice are controls instead.
- One more model in the manual isn't included yet.

## Build

Plain C++17 and C11 with no dependencies apart from X11 for the Linux GUI and Cocoa for the macOS GUI. Denormals are flushed on x86 and ARM so quiet tails don't slow the CPU down. No LV2 SDK is needed. The VST3, CLAP and AU builds come from one CLAP implementation (`src/clap`) through [clap-wrapper](https://github.com/free-audio/clap-wrapper): configuring with `AG_PLUGIN_FORMATS=ON` (the CMake default) downloads clap-wrapper and the CLAP, VST3 and AudioUnit SDKs from GitHub at pinned versions. Set it to `OFF` to build LV2 only, with no downloads.

| Source | What it is |
|---|---|
| `src/circuit.h`, `src/tubes.h`, `src/amp.h`, `amp.cpp`, `src/spring.h`, `src/oversampler.h`, `src/cabinet.h` | The amp model: circuit solver, valves, amp circuits, spring reverb, oversampling, cabinets |
| `src/engine.h` | The engine every format runs: amp, cabinet, levels and the fade around selector changes |
| `src/ports.h` | Port tables shared by the plugin, the Turtle generator and the tests |
| `src/plugin.cpp`, `src/ui/ui_lv2.c` | LV2 plugin and GUI |
| `src/clap/ashgrove_clap.cpp`, `src/clap/entry.cpp` | The CLAP plugins (parameters, state, latency, GUI) and the exported entry point; clap-wrapper builds VST3 and AU from them |
| `src/ui/layouts.h`, `src/ui/panel.h`, `src/ui/ui_window.c` | The five panel layouts, the panel drawing and mouse handling, and the Win32, X11 and Cocoa window layers shared by the LV2 and CLAP GUIs |
| `src/ui/style.h`, `canvas.h`, `font.h` | Davidian GUI house style: palette, software renderer and stroke font |
| `src/lv2_abi.h` | The LV2 C interface, so no SDK is needed |
| `tools/gen_ttl.cpp` | Writes `manifest.ttl` and `ashgrove.ttl` for the build's system from the port tables |
| `tools/fit_valves.cpp` | Checks the output-valve models against their data sheets and fits the KT77; run by hand when changing `src/tubes.h` |
| `prebuilt/` | Ready-built plugins for each system and format, the same files as the latest release |

The build uses CMake 3.21 or later and a C++17 compiler. The LV2 bundle lands in `<build folder>/lv2/ashgrove.lv2`, and the VST3, CLAP and AU plugins in `<build folder>/plugins`.

### Windows (Visual Studio)

Needs Visual Studio 18 with its C++ tools and CMake; edit `VS` at the top of `build.sh` if your install is somewhere else. From Git Bash:

```bash
sh build.sh
```

It builds LV2 only unless you run it as `AG_PLUGIN_FORMATS=ON sh build.sh`.

### macOS, Linux

On Linux, install CMake, a C++ compiler, Ninja and the X11 headers (`libx11-dev` on Debian/Ubuntu), then:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
```

On macOS, install the Xcode command-line tools and CMake, then build for both Apple silicon and Intel:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" && cmake --build build --parallel
```

## Tests

Windows, from Git Bash (rebuilds, then runs every check and writes the results to `build/`):

```bash
sh run_checks.sh
```

- `spec_test` measures every model against its spec sheet (power, sensitivity, tone ranges, boost, bright, reverb) and the manual's DC voltage table.
- `idle_check` runs each model with no input, at half and full controls, and reports any oscillation.
- `cab_check` checks every cabinet at 44.1 to 192 kHz: stable, level-matched, and with the expected low cut and top-end roll-off. It prints each cabinet's response.
- `plugin_check <library>` loads the built LV2 library the way a host does and runs every plugin with every cabinet, on any system.
- `clap_check <.clap binary>` loads the built CLAP the way a host does and checks every plugin: parameters against the port tables, processing with every cabinet, parameter changes, saving and loading state, on Windows opening the GUI in a host window, and on macOS creating the Cocoa GUI. The VST3 and AU versions run the same code.
- `ui_ports_check` checks that every panel control sits on the right port with the plugin's default and range.
- `ui_render <folder>` draws every panel to a BMP file without opening a window.
- `host_check` (Windows) validates the bundle with the LV2 parser from an Ardour 9 install and runs every model, valve and input combination.
- `ui_check` (Windows) loads the LV2 GUI the way a Windows host does and drives each panel with mouse messages.

CI also runs Steinberg's VST3 validator on all three systems, Apple's `auval` on each Audio Unit, and lilv's `lv2info` on the LV2 bundle on Linux, and checks that the Linux LV2 plugin doesn't need a newer C++ runtime than the host has.

GitHub Actions builds and tests every format on Windows, Linux and macOS for every push (see `.github/workflows/build.yml`). Pushing a version tag such as `v1.2.0` publishes a release with a zip for each system.

## Use of AI

Made with generative AI (Claude Code, Anthropic's Claude Opus 5.5), directed and reviewed by Davidian. The code, tests, LV2 metadata, build scripts and this README were written by the AI.

- **The author** supplied the service manual, the GUI house style, the names and the licensing choices. They also decided which amps to build and which valve options to offer.
- **The AI**:
  - read component values and topology from the schematics and spec sheets, and chose the estimates listed under [Estimates and sources](#estimates-and-sources)
  - researched valve data sheets and replacement-part ratings to replace estimates, and rechecked the drawings against the code
  - wrote the circuit solver, valve models, amp circuits and spring reverb
  - chose the cabinet types and designed their filter shapes
  - adapted the house-style GUI to the five panels, and wrote the Linux window layer from the house X11 code
  - made the build portable and wrote the GitHub Actions workflow
  - wrote the CLAP implementation and set up clap-wrapper to build the VST3 and AU versions from it
  - wrote the test programs and documentation
- **Checking:** the results are checked by the automated tests described above:
  - power, sensitivity, tone ranges and DC voltages against the manual
  - stability with no input
  - every cabinet's stability, level and shape
  - every plugin and cabinet run from the built library on Windows, Linux and macOS
  - the bundle loaded through lilv on Windows and Linux
  - the CLAP loaded and checked on all three systems, the VST3 through Steinberg's validator, and each Audio Unit through Apple's `auval`
  - the Windows GUI loaded and driven by simulated mouse input, and opened in a host window through CLAP
  - the macOS GUI loaded as a Cocoa LV2 UI, its drawing compared with the panel renderer, and driven by simulated mouse events
- **Not checked:** the sound hasn't been compared with real amplifiers or cabinets. The valve, transformer and choke values are fitted to data sheets and published ratings, but the values still listed as estimates under [Estimates and sources](#estimates-and-sources) should be tuned by ear. The Linux and macOS GUIs, the VST3, CLAP and AU versions and the macOS builds haven't been tried in a host.

## References

The model re-implements equations from these published works. No code was copied from them.

- N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996. The triode and pentode equations and the starting valve parameters.
- D. T. Yeh, "Digital implementation of musical distortion circuits by analysis and simulation", PhD thesis, Stanford University, 2009. The nonlinear state-space (DK) method used by the circuit solver.
- R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the Audio EQ Cookbook). The low-pass, high-pass and peaking filters in the cabinets.
- N. Koren, "Finding SPICE tube model parameters" (normankoren.com, 2001). The fitted 6550 parameters.
- EL34, KT77 and 6550 manufacturers' data sheets: typical operating points, and the KT77's plate curves used for its fit.
- Published ratings of the replacement output transformers and chokes sold for these chassis: the output transformer impedances and the choke's inductance and resistance.

## Licence

Copyright (C) 2026 Davidian. GPL-3.0-or-later: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text. Every source file starts with an SPDX licence line saying which licence applies to it.

### Third-party licences

| Component | Licence | How it's used |
|---|---|---|
| LV2 C interface (`lv2/core/lv2.h`, `lv2/ui/ui.h`) | ISC | Its declarations are re-stated in `src/lv2_abi.h`, the UI ABI block of `src/ui/ui_lv2.c` and two test programs, so no LV2 SDK is needed. Copyright and permission notice in [LICENSE-LV2](LICENSE-LV2). |
| LV2 vocabularies (`lv2core`, `ui`) | ISC | The `.ttl` files refer to them by URI. Nothing is copied. |
| lilv | ISC | `tests/host_check.cpp` re-states the signatures of the functions it calls and loads the copy that ships with Ardour 9 at run time. Not included in this repository. Notice in [LICENSE-LV2](LICENSE-LV2). |
| Ardour 9 | GPL-2.0-or-later | Used only as a test host and as the source of lilv for `host_check`. No Ardour code is included. |
| Microsoft C runtime | Visual Studio licence | The Windows build links it statically (`/MT`) into the DLLs. It counts as a system library under the GPL, so this is allowed. |
| GCC runtime (`libstdc++`, `libgcc`) | GPL-3.0 with the GCC Runtime Library Exception | The Linux LV2 build links it statically, so it runs on hosts with an older C++ runtime; the Linux VST3 and CLAP builds use the system's. The exception allows this. |
| CLAP SDK | MIT | Its headers define the CLAP interface the plugins implement. Built into the CLAP, VST3 and AU versions. |
| clap-wrapper | MIT | Builds the VST3 and AU versions from the CLAP implementation. Its wrapper code is built into those versions. |
| VST3 SDK (Steinberg) | MIT | Used by clap-wrapper for the VST3 version; parts of it are built into the VST3 plugin. |
| AudioUnitSDK (Apple) | Apache-2.0 | Used by clap-wrapper for the AU version; parts of it are built into the Audio Units. Apache-2.0 is compatible with GPL-3.0. |
| GitHub Actions (`actions/checkout`, `upload-artifact`, `download-artifact`) | MIT | Used by the build workflow only. Nothing is included in the plugins. |

The SDKs above are downloaded when the build is configured and are not stored in this repository. Each release zip includes their licence texts in its `licenses` folder.

This project is not affiliated with or endorsed by the maker of the original amplifiers. Valve type numbers (EL34, KT77, 6550, ECC83, 12AX7) are generic industry designations. VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of Apple Inc.
