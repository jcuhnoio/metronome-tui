#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

// Audio file paths for different sounds
const std::string SOUND_NORMAL = "./sounds/1k.wav";
const std::string SOUND_ACCENT = "./sounds/accent.wav";

class MetronomeApp {
private:
        // State
        double bpm = 120.0;
        int beats_per_measure = 4;
        int current_beat = 0;
        std::atomic<bool> running{true};
        std::atomic<bool> paused{true};
        std::mutex state_mutex;
        std::condition_variable state_cv;
        std::atomic<bool> bpm_changed{false};
        std::vector<int> beat_sounds;
        std::string status_message;

        ScreenInteractive screen = ScreenInteractive::Fullscreen();

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

        void play_sound(int sound_type) {
                std::string sound_path;
                if (sound_type == 0) {
                        sound_path = SOUND_NORMAL;
                } else if (sound_type == 1) {
                        sound_path = SOUND_ACCENT;
                } else {
                        sound_path = SOUND_NORMAL;
                }

                std::string cmd = "afplay " + sound_path + " > /dev/null 2>&1 &";
                std::system(cmd.c_str());
        }

        Element render_ui() {
                std::lock_guard<std::mutex> lock(state_mutex);

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
                        if (i == current_beat && !paused) {
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
                        std::string label =
                            std::to_string(i + 1) + ":" + get_sound_name(beat_sounds[i]);
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
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       bpm_changed = true;
                                       state_cv.notify_one();
                                       return true;
                               }
                               if (event == Event::Character('-')) {
                                       bpm = std::max(30.0, bpm - 1.0);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       bpm_changed = true;
                                       state_cv.notify_one();
                                       return true;
                               }
                               if (event == Event::Character(']')) {
                                       bpm = std::min(300.0, bpm + 5.0);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       bpm_changed = true;
                                       state_cv.notify_one();
                                       return true;
                               }
                               if (event == Event::Character('[')) {
                                       bpm = std::max(30.0, bpm - 5.0);
                                       status_message =
                                           "Tempo: " + std::to_string((int)bpm) + " BPM";
                                       bpm_changed = true;
                                       state_cv.notify_one();
                                       return true;
                               }
                               if (event == Event::Character('}')) {
                                       beats_per_measure = std::min(12, beats_per_measure + 1);
                                       current_beat = 0;
                                       beat_sounds.resize(beats_per_measure, 0);
                                       beat_sounds[0] = 1;
                                       status_message =
                                           "Time: " + std::to_string(beats_per_measure) + "/4";
                                       return true;
                               }
                               if (event == Event::Character('{')) {
                                       beats_per_measure = std::max(1, beats_per_measure - 1);
                                       current_beat = 0;
                                       beat_sounds.resize(beats_per_measure, 0);
                                       if (beats_per_measure > 0)
                                               beat_sounds[0] = 1;
                                       status_message =
                                           "Time: " + std::to_string(beats_per_measure) + "/4";
                                       return true;
                               }
                               if (event == Event::Character(' ')) {
                                       bool was_paused = paused.load();
                                       paused = !was_paused;
                                       current_beat = 0;
                                       status_message = paused ? "Paused" : "Resumed";

                                       // Wake up the metronome thread
                                       state_cv.notify_one();
                                       return true;
                               }
                               if (event == Event::Character('q') ||
                                   event == Event::Character('Q')) {
                                       running = false;
                                       state_cv.notify_one(); // Wake up the thread so it can exit
                                       screen.ExitLoopClosure()();
                                       return true;
                               }

                               // Number keys to change beat sounds
                               if (event.is_character()) {
                                       char ch = event.character()[0];
                                       if (ch >= '1' && ch <= '9') {
                                               int beat_num = ch - '1';
                                               if (beat_num < beats_per_measure) {
                                                       beat_sounds[beat_num] =
                                                           (beat_sounds[beat_num] + 1) % 7;
                                                       status_message =
                                                           "Beat " + std::to_string(beat_num + 1) +
                                                           ": " +
                                                           get_sound_name(beat_sounds[beat_num]);
                                               }
                                               return true;
                                       }
                               }

                               return false;
                       });
        }

        void metronome_thread() {
                while (running) {
                        // Wait while paused
                        std::unique_lock<std::mutex> lock(state_mutex);
                        state_cv.wait(lock, [this] { return !paused || !running; });

                        if (!running) {
                                break;
                        }

                        // Read BPM once at the start of the loop
                        double current_bpm = bpm;
                        lock.unlock();

                        // Calculate beat interval
                        long long interval_ms = static_cast<long long>(60000.0 / current_bpm);
                        auto interval = std::chrono::milliseconds(interval_ms);
                        auto next_tick = std::chrono::steady_clock::now();

                        // Reset to beat 0 when starting
                        {
                                std::lock_guard<std::mutex> beat_lock(state_mutex);
                                current_beat = 0;
                        }
                        screen.Post(Event::Custom);

                        while (!paused && running) {
                                // Schedule next beat
                                next_tick += interval;

                                // Update UI and play sound BEFORE sleeping
                                {
                                        std::lock_guard<std::mutex> beat_lock(state_mutex);
                                        play_sound(beat_sounds[current_beat]);
                                }
                                screen.Post(Event::Custom);

                                // Sleep until next beat
                                auto now = std::chrono::steady_clock::now();
                                if (next_tick > now) {
                                        std::this_thread::sleep_until(next_tick);
                                }

                                // After sleep, advance to next beat
                                {
                                        std::lock_guard<std::mutex> beat_lock(state_mutex);
                                        current_beat = (current_beat + 1) % beats_per_measure;

                                        // Check if BPM changed - if so, recalculate timing
                                        if (bpm_changed) {
                                                bpm_changed = false;
                                                current_bpm = bpm;
                                                interval_ms =
                                                    static_cast<long long>(60000.0 / current_bpm);
                                                interval = std::chrono::milliseconds(interval_ms);
                                                // Reset timing to avoid discontinuity
                                                next_tick = std::chrono::steady_clock::now();
                                        }
                                }
                        }
                }
        }

public:
        MetronomeApp() {
                beat_sounds.resize(beats_per_measure, 0);
                beat_sounds[0] = 1; // First beat is accented
        }

        void run() {
                auto component = create_component();

                // Start metronome thread
                std::thread metro_thread([this]() { metronome_thread(); });

                screen.Loop(component);

                // Clean up
                running = false;
                if (metro_thread.joinable()) {
                        metro_thread.join();
                }
        }
};

int main() {
        try {
                MetronomeApp app;
                app.run();
        } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
        }

        return 0;
}
