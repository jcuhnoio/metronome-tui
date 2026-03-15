# Terminal Metronome

A high-precision terminal-based metronome with visual display and interactive controls, built with C++17 and FTXUI.

## Features

- **Visual Beat Display**: Real-time visual feedback with filled (●) and empty (○) circles
- **Time Signature Support**: Customize beats per measure (1-12 beats in X/4 time)
- **Dynamic Tempo Control**: Adjust BPM from 30-300 with instant updates
- **Beat Sound Customization**: Assign normal or accent sounds to individual beats
- **Pause/Resume**: Space bar to pause and resume playback
- **Efficient Threading**: Condition variable-based design with minimal CPU usage
- **Drift-Free Timing**: High-precision timing using `std::chrono::steady_clock`

## Building

Requires:
- C++17 compatible compiler (clang++ or g++)
- FTXUI library (install via Homebrew: `brew install ftxui`)
- macOS (for audio playback via `afplay`)

Build with make:
```bash
make
```

Or manually:
```bash
clang++ -std=c++17 -Wall -Wextra -O2 \
  -I/opt/homebrew/opt/ftxui/include \
  -L/opt/homebrew/opt/ftxui/lib \
  -lftxui-screen -lftxui-dom -lftxui-component \
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
- **Threading Model**: 
  - Main thread handles UI rendering and user input
  - Separate metronome thread manages timing and audio playback
  - Communication via `std::condition_variable` and atomic flags
- **Audio Playback**: macOS `afplay` command spawned as background processes

### Timing Precision
- Uses `std::chrono::steady_clock` for monotonic, high-precision timing
- Implements drift-free scheduling with `sleep_until()` instead of `sleep_for()`
- Each beat is scheduled relative to the original start time, preventing cumulative timing errors
- Dynamic BPM changes recalculate timing on the next beat without introducing discontinuities

### Thread Efficiency
- Thread blocks on condition variable when paused (minimal CPU usage)
- Wakes immediately on state changes (unpause, BPM change, quit)
- UI updates synchronized with beat events only (no unnecessary redraws)
- Sound playback happens before UI update to ensure visual/audio synchronization

### State Management
- Thread-safe state updates using `std::mutex` and lock guards
- Atomic flags for high-frequency checks (`running`, `paused`, `bpm_changed`)
- Clean shutdown protocol with condition variable notification ensures proper thread termination
