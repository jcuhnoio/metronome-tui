#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <portaudio.h>

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

// Audio file paths for different sounds
const std::string SOUND_NORMAL = "./sounds/1k.wav";
const std::string SOUND_ACCENT = "./sounds/accent.wav";

// WAV file loader
class WavFile {
public:
        std::vector<float> samples;
        int sample_rate = 0;
        int channels = 0;

        bool load(const std::string& filename) {
                std::ifstream file(filename, std::ios::binary);
                if (!file.is_open()) {
                        std::cerr << "Failed to open WAV file: " << filename << std::endl;
                        return false;
                }

                // Read RIFF header
                char riff[4];
                file.read(riff, 4);
                if (std::strncmp(riff, "RIFF", 4) != 0) {
                        std::cerr << "Not a valid WAV file (missing RIFF): " << filename
                                  << std::endl;
                        return false;
                }

                uint32_t file_size;
                file.read(reinterpret_cast<char*>(&file_size), 4);

                char wave[4];
                file.read(wave, 4);
                if (std::strncmp(wave, "WAVE", 4) != 0) {
                        std::cerr << "Not a valid WAV file (missing WAVE): " << filename
                                  << std::endl;
                        return false;
                }

                // Find fmt chunk
                char chunk_id[4];
                uint32_t chunk_size;
                bool fmt_found = false;

                while (file.read(chunk_id, 4)) {
                        file.read(reinterpret_cast<char*>(&chunk_size), 4);

                        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
                                fmt_found = true;
                                uint16_t audio_format;
                                file.read(reinterpret_cast<char*>(&audio_format), 2);
                                file.read(reinterpret_cast<char*>(&channels), 2);
                                file.read(reinterpret_cast<char*>(&sample_rate), 4);

                                uint32_t byte_rate;
                                file.read(reinterpret_cast<char*>(&byte_rate), 4);

                                uint16_t block_align;
                                file.read(reinterpret_cast<char*>(&block_align), 2);

                                uint16_t bits_per_sample;
                                file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

                                // Skip any extra format bytes
                                if (chunk_size > 16) {
                                        file.seekg(chunk_size - 16, std::ios::cur);
                                }

                                // Only support PCM format
                                if (audio_format != 1) {
                                        std::cerr << "Only PCM WAV files supported: " << filename
                                                  << std::endl;
                                        return false;
                                }

                                // Only support 16-bit
                                if (bits_per_sample != 16) {
                                        std::cerr << "Only 16-bit WAV files supported: " << filename
                                                  << std::endl;
                                        return false;
                                }

                                break;
                        } else {
                                // Skip this chunk
                                file.seekg(chunk_size, std::ios::cur);
                        }
                }

                if (!fmt_found) {
                        std::cerr << "No fmt chunk found in WAV file: " << filename << std::endl;
                        return false;
                }

                // Find data chunk
                file.clear();
                file.seekg(12, std::ios::beg); // Skip to after WAVE

                while (file.read(chunk_id, 4)) {
                        file.read(reinterpret_cast<char*>(&chunk_size), 4);

                        if (std::strncmp(chunk_id, "data", 4) == 0) {
                                // Read audio data
                                int num_samples = chunk_size / (2 * channels); // 2 bytes per sample
                                samples.resize(num_samples * channels);

                                for (size_t i = 0; i < samples.size(); i++) {
                                        int16_t sample_16;
                                        file.read(reinterpret_cast<char*>(&sample_16), 2);
                                        samples[i] =
                                            sample_16 / 32768.0f; // Convert to float [-1, 1]
                                }

                                return true;
                        } else {
                                // Skip this chunk
                                file.seekg(chunk_size, std::ios::cur);
                        }
                }

                std::cerr << "No data chunk found in WAV file: " << filename << std::endl;
                return false;
        }
};

// Audio engine using PortAudio
class AudioEngine {
private:
        PaStream* stream = nullptr;
        int sample_rate = 48000;
        std::atomic<long long> current_sample{0};
        std::atomic<long long> next_beat_sample{0};
        std::atomic<double> bpm{120.0};
        std::atomic<int> beats_per_measure{4};
        std::atomic<int> current_beat{0};
        std::atomic<bool> active{false};
        std::vector<int> beat_sounds;
        mutable std::mutex beat_sounds_mutex;

        WavFile normal_sound;
        WavFile accent_sound;

        std::atomic<int> playback_position_normal{-1};
        std::atomic<int> playback_position_accent{-1};

        std::function<void()> on_beat_callback;

        static int audio_callback_static(const void* /*input*/, void* output,
                                         unsigned long frame_count,
                                         const PaStreamCallbackTimeInfo* /*time_info*/,
                                         PaStreamCallbackFlags /*status_flags*/, void* user_data) {
                return static_cast<AudioEngine*>(user_data)->audio_callback(output, frame_count);
        }

        int audio_callback(void* output, unsigned long frame_count) {
                float* out = static_cast<float*>(output);
                std::memset(out, 0, frame_count * 2 * sizeof(float)); // Stereo silence

                if (!active.load()) {
                        return paContinue;
                }

                for (unsigned long i = 0; i < frame_count; i++) {
                        long long sample_pos = current_sample.load() + i;

                        // Check if we need to trigger a beat
                        if (sample_pos == next_beat_sample.load()) {
                                // Start playing the appropriate sound
                                int beat = current_beat.load();
                                int sound_type;
                                {
                                        std::lock_guard<std::mutex> lock(beat_sounds_mutex);
                                        if (beat < static_cast<int>(beat_sounds.size())) {
                                                sound_type = beat_sounds[beat];
                                        } else {
                                                sound_type = 0;
                                        }
                                }

                                if (sound_type == 1) {
                                        playback_position_accent.store(0);
                                } else {
                                        playback_position_normal.store(0);
                                }

                                // Calculate next beat
                                int next_beat = (beat + 1) % beats_per_measure.load();
                                current_beat.store(next_beat);

                                double current_bpm = bpm.load();
                                long long samples_per_beat =
                                    static_cast<long long>((60.0 * sample_rate) / current_bpm);
                                next_beat_sample.store(sample_pos + samples_per_beat);

                                // Notify UI
                                if (on_beat_callback) {
                                        on_beat_callback();
                                }
                        }

                        // Mix in normal sound if playing
                        int pos_normal = playback_position_normal.load();
                        if (pos_normal >= 0 &&
                            pos_normal < static_cast<int>(normal_sound.samples.size())) {
                                if (normal_sound.channels == 1) {
                                        out[i * 2] += normal_sound.samples[pos_normal];
                                        out[i * 2 + 1] += normal_sound.samples[pos_normal];
                                } else {
                                        out[i * 2] += normal_sound.samples[pos_normal * 2];
                                        out[i * 2 + 1] += normal_sound.samples[pos_normal * 2 + 1];
                                }
                                playback_position_normal.store(pos_normal + 1);

                                if (pos_normal + 1 >=
                                    static_cast<int>(normal_sound.samples.size()) /
                                        normal_sound.channels) {
                                        playback_position_normal.store(-1);
                                }
                        }

                        // Mix in accent sound if playing
                        int pos_accent = playback_position_accent.load();
                        if (pos_accent >= 0 &&
                            pos_accent < static_cast<int>(accent_sound.samples.size())) {
                                if (accent_sound.channels == 1) {
                                        out[i * 2] += accent_sound.samples[pos_accent];
                                        out[i * 2 + 1] += accent_sound.samples[pos_accent];
                                } else {
                                        out[i * 2] += accent_sound.samples[pos_accent * 2];
                                        out[i * 2 + 1] += accent_sound.samples[pos_accent * 2 + 1];
                                }
                                playback_position_accent.store(pos_accent + 1);

                                if (pos_accent + 1 >=
                                    static_cast<int>(accent_sound.samples.size()) /
                                        accent_sound.channels) {
                                        playback_position_accent.store(-1);
                                }
                        }

                        // Clamp output to [-1, 1]
                        if (out[i * 2] > 1.0f)
                                out[i * 2] = 1.0f;
                        if (out[i * 2] < -1.0f)
                                out[i * 2] = -1.0f;
                        if (out[i * 2 + 1] > 1.0f)
                                out[i * 2 + 1] = 1.0f;
                        if (out[i * 2 + 1] < -1.0f)
                                out[i * 2 + 1] = -1.0f;
                }

                current_sample.fetch_add(frame_count);
                return paContinue;
        }

public:
        bool initialize() {
                PaError err = Pa_Initialize();
                if (err != paNoError) {
                        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << std::endl;
                        return false;
                }

                // Load sounds
                if (!normal_sound.load(SOUND_NORMAL)) {
                        std::cerr << "Failed to load normal sound" << std::endl;
                        return false;
                }
                if (!accent_sound.load(SOUND_ACCENT)) {
                        std::cerr << "Failed to load accent sound" << std::endl;
                        return false;
                }

                // Open stream
                err = Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, sample_rate, 256,
                                           audio_callback_static, this);
                if (err != paNoError) {
                        std::cerr << "PortAudio stream error: " << Pa_GetErrorText(err)
                                  << std::endl;
                        return false;
                }

                err = Pa_StartStream(stream);
                if (err != paNoError) {
                        std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
                        return false;
                }

                return true;
        }

        void shutdown() {
                if (stream) {
                        Pa_StopStream(stream);
                        Pa_CloseStream(stream);
                        stream = nullptr;
                }
                Pa_Terminate();
        }

        void start() {
                current_sample.store(0);
                current_beat.store(0);
                next_beat_sample.store(0);
                active.store(true);
        }

        void stop() {
                active.store(false);
        }

        void set_bpm(double new_bpm) {
                bpm.store(new_bpm);
        }

        void set_beats_per_measure(int beats) {
                beats_per_measure.store(beats);
                std::lock_guard<std::mutex> lock(beat_sounds_mutex);
                beat_sounds.resize(beats, 0);
                if (beats > 0) {
                        beat_sounds[0] = 1; // First beat is accented
                }
        }

        void set_beat_sound(int beat, int sound_type) {
                std::lock_guard<std::mutex> lock(beat_sounds_mutex);
                if (beat >= 0 && beat < static_cast<int>(beat_sounds.size())) {
                        beat_sounds[beat] = sound_type;
                }
        }

        int get_current_beat() const {
                return current_beat.load();
        }

        bool is_active() const {
                return active.load();
        }

        void set_on_beat_callback(std::function<void()> callback) {
                on_beat_callback = callback;
        }

        std::vector<int> get_beat_sounds() const {
                std::lock_guard<std::mutex> lock(beat_sounds_mutex);
                return beat_sounds;
        }
};

class MetronomeApp {
private:
        // State
        double bpm = 120.0;
        int beats_per_measure = 4;
        std::atomic<bool> running{true};
        std::atomic<bool> paused{true};
        std::mutex state_mutex;
        std::string status_message;

        ScreenInteractive screen = ScreenInteractive::Fullscreen();
        AudioEngine audio_engine;

        std::string get_sound_name(int sound_type) {
                switch (sound_type) {
                case 0:
                        return "Normal";
                case 1:
                        return "Accent";
                default:
                        return "Unknown";
                }
        }

        Element render_ui() {
                std::lock_guard<std::mutex> lock(state_mutex);

                int current_beat = audio_engine.get_current_beat();
                bool is_playing = audio_engine.is_active();
                auto beat_sounds = audio_engine.get_beat_sounds();

                // Title
                auto title = text("TERMINAL METRONOME") | bold | center;

                // Tempo and time signature
                std::stringstream ss;
                ss << std::fixed << std::setprecision(0) << bpm;
                auto tempo_display =
                    hbox({text("Tempo: ") | bold, text(ss.str() + " BPM"), text("  |  "),
                          text("Time: ") | bold, text(std::to_string(beats_per_measure) + "/4")});

                // Beat indicators (visual metronome)
                Elements beat_circles;
                for (int i = 0; i < beats_per_measure; i++) {
                        if (i == current_beat && is_playing) {
                                beat_circles.push_back(text("●") | color(Color::Green) | bold);
                        } else {
                                beat_circles.push_back(text("○") | dim);
                        }
                        if (i < beats_per_measure - 1) {
                                beat_circles.push_back(text(" "));
                        }
                }
                auto beat_indicator = hbox(std::move(beat_circles)) | center;

                // Sound assignments for each beat
                Elements sound_labels;
                for (int i = 0; i < beats_per_measure; i++) {
                        int sound_type =
                            (i < static_cast<int>(beat_sounds.size())) ? beat_sounds[i] : 0;
                        std::string label =
                            std::to_string(i + 1) + ":" + get_sound_name(sound_type);
                        sound_labels.push_back(text(label));
                        if (i < beats_per_measure - 1) {
                                sound_labels.push_back(text(" | "));
                        }
                }
                auto sound_display = hbox(std::move(sound_labels)) | center;

                // Current count
                auto count_display = text("Count: " + std::to_string(current_beat + 1) + "/" +
                                          std::to_string(beats_per_measure)) |
                                     center;

                // Status
                std::string status_text = paused ? "⏸  PAUSED" : "▶  RUNNING";
                Color status_color = paused ? Color::Yellow : Color::Green;
                auto status_display = text(status_text) | bold | color(status_color) | center;

                // Controls guide
                auto controls = vbox({
                    text("Controls:") | bold | underlined,
                    text(""),
                    text("  +/-      Change tempo by 1 BPM"),
                    text("  ]/[      Change tempo by 5 BPM"),
                    text("  }/{      Change time signature"),
                    text("  1-9      Cycle sound for beat N"),
                    text("  SPACE    Pause/Resume"),
                    text("  Q        Quit"),
                    text(""),
                    text("Sound Cycle:") | bold,
                    text("  Normal → Accent  "),
                    text(""),
                    text("Audio: Sample-accurate buffer timing") | dim,
                });

                // Status message
                Element status_msg = text("");
                if (!status_message.empty()) {
                        status_msg = text(status_message) | color(Color::Cyan) | center;
                }

                // Main layout
                auto content =
                    vbox({separator(), title, separator(), text(""), tempo_display | center,
                          text(""), beat_indicator, text(""), sound_display, text(""),
                          count_display, text(""), status_display, text(""), separator(),
                          controls | center, separator(), status_msg});

                return content | border | center;
        }

        Component create_component() {
                return Renderer([&] { return render_ui(); }) | CatchEvent([&](Event event) {
                               std::lock_guard<std::mutex> lock(state_mutex);

                               if (event == Event::Character('+')) {
                                       bpm = std::min(300.0, bpm + 1.0);
                                       audio_engine.set_bpm(bpm);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       return true;
                               }
                               if (event == Event::Character('-')) {
                                       bpm = std::max(30.0, bpm - 1.0);
                                       audio_engine.set_bpm(bpm);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       return true;
                               }
                               if (event == Event::Character(']')) {
                                       bpm = std::min(300.0, bpm + 5.0);
                                       audio_engine.set_bpm(bpm);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       return true;
                               }
                               if (event == Event::Character('[')) {
                                       bpm = std::max(30.0, bpm - 5.0);
                                       audio_engine.set_bpm(bpm);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       return true;
                               }
                               if (event == Event::Character('}')) {
                                       beats_per_measure = std::min(12, beats_per_measure + 1);
                                       audio_engine.set_beats_per_measure(beats_per_measure);
                                       status_message =
                                           "Time: " + std::to_string(beats_per_measure) + "/4";
                                       return true;
                               }
                               if (event == Event::Character('{')) {
                                       beats_per_measure = std::max(1, beats_per_measure - 1);
                                       audio_engine.set_beats_per_measure(beats_per_measure);
                                       status_message =
                                           "Time: " + std::to_string(beats_per_measure) + "/4";
                                       return true;
                               }
                               if (event == Event::Character(' ')) {
                                       bool was_paused = paused.load();
                                       paused = !was_paused;
                                       if (paused) {
                                               audio_engine.stop();
                                               status_message = "Paused";
                                       } else {
                                               audio_engine.start();
                                               status_message = "Resumed";
                                       }
                                       return true;
                               }
                               if (event == Event::Character('q') ||
                                   event == Event::Character('Q')) {
                                       running = false;
                                       screen.ExitLoopClosure()();
                                       return true;
                               }

                               // Number keys to change beat sounds
                               if (event.is_character()) {
                                       char ch = event.character()[0];
                                       if (ch >= '1' && ch <= '9') {
                                               int beat_num = ch - '1';
                                               if (beat_num < beats_per_measure) {
                                                       auto beat_sounds =
                                                           audio_engine.get_beat_sounds();
                                                       int current_sound =
                                                           (beat_num <
                                                            static_cast<int>(beat_sounds.size()))
                                                               ? beat_sounds[beat_num]
                                                               : 0;
                                                       int new_sound = (current_sound + 1) % 2;
                                                       audio_engine.set_beat_sound(beat_num,
                                                                                   new_sound);
                                                       status_message =
                                                           "Beat " + std::to_string(beat_num + 1) +
                                                           ": " + get_sound_name(new_sound);
                                               }
                                               return true;
                                       }
                               }

                               return false;
                       });
        }

public:
        MetronomeApp() {
                audio_engine.set_bpm(bpm);
                audio_engine.set_beats_per_measure(beats_per_measure);
        }

        bool initialize() {
                if (!audio_engine.initialize()) {
                        std::cerr << "Failed to initialize audio engine" << std::endl;
                        return false;
                }

                // Set callback to update UI on beat
                audio_engine.set_on_beat_callback([&]() { screen.Post(Event::Custom); });

                return true;
        }

        void run() {
                auto component = create_component();
                screen.Loop(component);

                // Clean up
                running = false;
                audio_engine.shutdown();
        }
};

int main() {
        try {
                MetronomeApp app;
                if (!app.initialize()) {
                        return 1;
                }
                app.run();
        } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
        }

        return 0;
}
