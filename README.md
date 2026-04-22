# Terminal Metronome

A high-precision terminal-based metronome with visual display and interactive controls, built with C++17 and FTXUI.

## Features

- **Sample-Accurate Timing**: Audio buffer-based timing eliminates jitter and process overhead
- **Visual Beat Display**: Real-time visual feedback with filled (●) and empty (○) circles
- **Time Signature Support**: Customize beats per measure (1-12 beats in X/4 time)
- **Dynamic Tempo Control**: Adjust BPM from 30-300 with instant updates
- **Beat Sound Customization**: Assign normal or accent sounds to individual beats
- **Pause/Resume**: Space bar to pause and resume playback
- **Low CPU Usage**: Direct audio buffer writing with no external process spawning
- **Cross-Platform Ready**: Built on PortAudio for macOS, Linux, and Windows support

## Building

Requires:
- C++17 compatible compiler (clang++ or g++)
- FTXUI library (install via Homebrew: `brew install ftxui`)
- PortAudio library (install via Homebrew: `brew install portaudio`)

Build with make:
```bash
make
```

Or manually:
```bash
clang++ -std=c++17 -Wall -Wextra -O2 \
  -I$(brew --prefix ftxui)/include \
  -I$(brew --prefix portaudio)/include \
  -L$(brew --prefix ftxui)/lib \
  -L$(brew --prefix portaudio)/lib \
  -lftxui-screen -lftxui-dom -lftxui-component -lportaudio \
  -o metronome metronome.cpp
```

## Running

```bash
./metronome
```

Or with make:
```bash
make run
```

## Controls

| Key | Action |
|-----|--------|
| `+` | Increase tempo by 1 BPM |
| `-` | Decrease tempo by 1 BPM |
| `]` | Increase tempo by 5 BPM |
| `[` | Decrease tempo by 5 BPM |
| `}` | Increase beats per measure |
| `{` | Decrease beats per measure |
| `1-9` | Cycle sound type for beat N (Normal → Accent → Normal...) |
| `SPACE` | Pause/Resume metronome |
| `Q` or `q` | Quit application |

## Sounds

The metronome uses two beat sounds located in `./sounds/`:
- **Normal** (`1k.wav`) - Standard beat click
- **Accent** (`accent.wav`) - Emphasized beat for downbeats

By default, beat 1 is accented and all other beats use the normal sound. Press number keys (1-9) to cycle through sound types for each beat:
- Press `1` to cycle beat 1's sound: Normal → Accent → Normal...
- Press `2` to cycle beat 2's sound
- And so on for beats 3-9 (if your time signature has that many beats)

## Example Usage

1. Start the metronome - defaults to 120 BPM in 4/4 time with beat 1 accented
2. Press `SPACE` to start playback
3. Press `]` multiple times to quickly increase tempo by 5 BPM increments
4. Press `}` to change to 5/4 time signature
5. Press `2` to make beat 2 accented as well
6. Press `[` to decrease tempo by 5 BPM
7. Press `SPACE` to pause

## Technical Details

### Architecture
- **UI Framework**: FTXUI for terminal-based interactive UI
- **Audio Engine**: PortAudio for low-latency, cross-platform audio output
- **Threading Model**: 
  - Main thread handles UI rendering and user input
  - Audio thread (managed by PortAudio) runs in real-time with high priority
  - Communication via atomic variables and callbacks for thread-safe operation
- **Audio Format**: 32-bit float stereo output at 48kHz sample rate

### Sample-Accurate Timing
- Beats are written directly to audio buffer at exact sample positions
- No external process spawning or system calls for audio playback
- Beat timing formula: `beat_sample = (beat_number * 60.0 * sample_rate) / bpm`
- All beat positions calculated relative to start sample, preventing drift
- Dynamic BPM changes recalculate future beat positions instantly

### Audio Callback Design
- Real-time audio callback mixes beat sounds into continuous output buffer
- Loads WAV files into memory at startup for zero-latency playback
- Supports simultaneous sound mixing with proper clipping prevention
- Buffer size: 256 frames for low latency (~5ms at 48kHz)
- Sample-accurate beat placement guarantees exact timing

### Performance Benefits
- **No process overhead**: Eliminated spawning of `afplay` processes for each beat
- **Zero jitter**: Sample-accurate timing removes system scheduling variability
- **Predictable latency**: PortAudio manages buffering with known, consistent latency
- **Lower CPU usage**: Direct buffer writing is more efficient than process creation
- **Better control**: Foundation for future features like volume, panning, effects

### State Management
- Thread-safe state updates using atomics for high-frequency access
- Mutex protection for complex state (beat sounds array)
- Clean shutdown protocol ensures proper PortAudio stream termination
- UI updates triggered by audio callback via event posting
