#include "keyboard_input.h"
#include "compat/input_keys.h"
#include "global_config.h"

#include "lvgl/lvgl.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(CONFIG_TINYALSA_COMPONENT_ENABLED) && defined(__linux__)
#include <tinyalsa/asoundlib.h>
#endif

#if LV_USE_SDL
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#endif

#if LV_USE_EVDEV
#include <pthread.h>
#endif

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kBoardSize = 4;
constexpr int kTileSize = 34;
constexpr int kTileGap = 3;
constexpr int kBoardX = 8;
constexpr int kBoardY = 10;
constexpr int kPanelX = 172;
constexpr int kSfxSampleRate = 48000;
constexpr const char *kSfxSampleRateText = "48000";
constexpr double kTwoPi = 6.28318530717958647692;

volatile sig_atomic_t g_quit_requested = 0;
lv_obj_t *g_root = nullptr;
lv_indev_t *g_keyboard_indev = nullptr;
lv_group_t *g_group = nullptr;

std::array<std::array<int, kBoardSize>, kBoardSize> g_board{};
int g_score = 0;
bool g_won_seen = false;
bool g_goal_prompt_active = false;
bool g_reset_confirm_active = false;
uint32_t g_escape_first_tick = 0;
uint32_t g_escape_last_tick = 0;
int g_escape_repeat_count = 0;
std::atomic<bool> g_sfx_busy{false};
std::atomic<bool> g_sound_muted{false};

lv_obj_t *g_score_label = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_tiles[kBoardSize][kBoardSize] = {};
lv_obj_t *g_tile_labels[kBoardSize][kBoardSize] = {};

const char *getenv_default(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value ? value : fallback;
}

bool env_enabled(const char *name, bool fallback)
{
    const char *text = std::getenv(name);
    if (!text || !text[0]) return fallback;
    return std::strcmp(text, "0") != 0 &&
           std::strcmp(text, "false") != 0 &&
           std::strcmp(text, "False") != 0 &&
           std::strcmp(text, "off") != 0 &&
           std::strcmp(text, "OFF") != 0;
}

bool read_env_uint(const char *name, unsigned int *value)
{
    const char *text = std::getenv(name);
    if (!text || !text[0]) return false;
    char *end = nullptr;
    unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') return false;
    *value = static_cast<unsigned int>(parsed);
    return true;
}

bool sound_enabled()
{
    return !g_sound_muted.load() && env_enabled("M5_2048_SFX", true);
}

void audio_log(const char *fmt, ...)
{
    if (!env_enabled("M5_2048_AUDIO_LOG", true)) return;
    FILE *fp = std::fopen("/tmp/2048-audio.log", "a");
    if (!fp) return;
    std::time_t now = std::time(nullptr);
    std::fprintf(fp, "%ld ", static_cast<long>(now));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(fp, fmt, args);
    va_end(args);
    std::fprintf(fp, "\n");
    std::fclose(fp);
}

void request_quit()
{
    g_quit_requested = 1;
}

void handle_signal(int)
{
    request_quit();
}

int get_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (!dev_path || buf_size == 0) return -1;
    FILE *fp = std::fopen("/proc/fb", "r");
    if (!fp) return -1;
    char line[256];
    int fb_num = -1;
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, "fb_st7789v") && std::sscanf(line, "%d", &fb_num) == 1) break;
    }
    std::fclose(fp);
    if (fb_num < 0) return -1;
    std::snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}

uint32_t tile_color(int value)
{
    switch (value) {
        case 0: return 0xCDC1B4;
        case 2: return 0xEEE4DA;
        case 4: return 0xEDE0C8;
        case 8: return 0xF2B179;
        case 16: return 0xF59563;
        case 32: return 0xF67C5F;
        case 64: return 0xF65E3B;
        case 128: return 0xEDCF72;
        case 256: return 0xEDCC61;
        case 512: return 0xEDC850;
        case 1024: return 0xEDC53F;
        case 2048: return 0xEDC22E;
        default: return 0x3C3A32;
    }
}

uint32_t text_color(int value)
{
    return value <= 4 ? 0x776E65 : 0xF9F6F2;
}

double clamp_double(double value, double lo, double hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void append_stereo_sample(std::vector<int16_t> &samples, double value)
{
    value = clamp_double(value, -1.0, 1.0);
    int16_t pcm = static_cast<int16_t>(value * 29000.0);
    samples.push_back(pcm);
    samples.push_back(pcm);
}

double envelope(int index, int total)
{
    if (total <= 1) return 0.0;
    double attack = std::min(1.0, static_cast<double>(index) / 48.0);
    double release = std::min(1.0, static_cast<double>(total - index) / 96.0);
    return std::min(attack, release);
}

void append_tone(std::vector<int16_t> &samples, double frequency, int duration_ms, double volume)
{
    int count = std::max(1, kSfxSampleRate * duration_ms / 1000);
    double phase = 0.0;
    double step = kTwoPi * frequency / static_cast<double>(kSfxSampleRate);
    for (int i = 0; i < count; ++i) {
        append_stereo_sample(samples, std::sin(phase) * volume * envelope(i, count));
        phase += step;
        if (phase > kTwoPi) phase -= kTwoPi;
    }
}

void append_sweep(std::vector<int16_t> &samples, double start_hz, double end_hz,
                  int duration_ms, double volume)
{
    int count = std::max(1, kSfxSampleRate * duration_ms / 1000);
    double phase = 0.0;
    for (int i = 0; i < count; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(std::max(1, count - 1));
        double freq = start_hz + (end_hz - start_hz) * t;
        phase += kTwoPi * freq / static_cast<double>(kSfxSampleRate);
        append_stereo_sample(samples, std::sin(phase) * volume * envelope(i, count));
    }
}

void append_chord(std::vector<int16_t> &samples, const std::vector<double> &freqs,
                  int duration_ms, double volume)
{
    int count = std::max(1, kSfxSampleRate * duration_ms / 1000);
    std::vector<double> phases(freqs.size(), 0.0);
    for (int i = 0; i < count; ++i) {
        double mixed = 0.0;
        for (size_t n = 0; n < freqs.size(); ++n) {
            phases[n] += kTwoPi * freqs[n] / static_cast<double>(kSfxSampleRate);
            if (phases[n] > kTwoPi) phases[n] -= kTwoPi;
            mixed += std::sin(phases[n]);
        }
        mixed /= static_cast<double>(std::max<size_t>(1, freqs.size()));
        append_stereo_sample(samples, mixed * volume * envelope(i, count));
    }
}

#if defined(CONFIG_TINYALSA_COMPONENT_ENABLED) && defined(__linux__)
bool play_samples_tinyalsa(const std::vector<int16_t> &samples)
{
    unsigned int card = 1;
    unsigned int device = 0;
    read_env_uint("M5_2048_AUDIO_CARD", &card);
    read_env_uint("M5_2048_AUDIO_PCM_DEVICE", &device);

    pcm_config config {};
    config.channels = 2;
    config.rate = kSfxSampleRate;
    config.period_size = 512;
    config.period_count = 4;
    config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;
    config.silence_size = 0;

    pcm *pcm_out = pcm_open(card, device, PCM_OUT, &config);
    if (!pcm_is_ready(pcm_out)) {
        const char *error = pcm_out ? pcm_get_error(pcm_out) : "pcm_open returned null";
        audio_log("tinyalsa open failed card=%u device=%u error=%s", card, device, error);
        if (pcm_out) pcm_close(pcm_out);
        return false;
    }

    unsigned int frames = static_cast<unsigned int>(samples.size() / config.channels);
    int rc = pcm_writei(pcm_out, samples.data(), frames);
    if (rc < 0) {
        audio_log("tinyalsa write failed card=%u device=%u frames=%u error=%s",
                  card, device, frames, pcm_get_error(pcm_out));
        pcm_close(pcm_out);
        return false;
    }

    audio_log("tinyalsa write ok card=%u device=%u frames=%u", card, device, frames);
    pcm_close(pcm_out);
    return true;
}
#endif

bool play_samples_aplay(const std::vector<int16_t> &samples)
{
    const char *device = getenv_default("M5_2048_AUDIO_DEVICE", "plughw:CARD=ES8388Audio,DEV=0");
    int fds[2];
    if (pipe(fds) != 0) {
        audio_log("aplay pipe failed errno=%d", errno);
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[0], STDIN_FILENO);
        close(fds[0]);
        close(fds[1]);
        execlp("aplay", "aplay", "-q", "-D", device, "-f", "S16_LE",
               "-r", kSfxSampleRateText, "-c", "2", "-t", "raw", static_cast<char *>(nullptr));
        _exit(127);
    }

    close(fds[0]);
    bool ok = false;
    if (pid > 0) {
        const char *bytes = reinterpret_cast<const char *>(samples.data());
        size_t left = samples.size() * sizeof(int16_t);
        while (left > 0) {
            ssize_t written = write(fds[1], bytes, left);
            if (written < 0) {
                if (errno == EINTR) continue;
                audio_log("aplay pipe write failed errno=%d", errno);
                break;
            }
            bytes += written;
            left -= static_cast<size_t>(written);
        }
        close(fds[1]);
        int status = 0;
        waitpid(pid, &status, 0);
        ok = (left == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        audio_log("aplay done device=%s bytes_left=%zu status=%d ok=%d",
                  device, left, status, ok ? 1 : 0);
    } else {
        close(fds[1]);
        audio_log("aplay fork failed errno=%d", errno);
    }
    return ok;
}

void play_samples(std::vector<int16_t> samples)
{
    if (!sound_enabled() || samples.empty()) return;
    if (g_sfx_busy.exchange(true)) return;
    std::thread([samples = std::move(samples)]() {
        bool ok = false;
#if defined(CONFIG_TINYALSA_COMPONENT_ENABLED) && defined(__linux__)
        ok = play_samples_tinyalsa(samples);
#endif
        if (!ok) play_samples_aplay(samples);
        g_sfx_busy.store(false);
    }).detach();
}

int merge_note_hz(int value)
{
    switch (value) {
        case 16: return 262;
        case 32: return 294;
        case 64: return 330;
        case 128: return 349;
        case 256: return 392;
        case 512: return 440;
        case 1024: return 494;
        default:
            if (value < 16) return 220;
            return 523;
    }
}

void play_move_sfx()
{
    std::vector<int16_t> samples;
    append_sweep(samples, 160.0, 330.0, 95, 0.36);
    append_sweep(samples, 330.0, 220.0, 54, 0.24);
    play_samples(std::move(samples));
}

void play_merge_sfx(int value)
{
    std::vector<int16_t> samples;
    int hz = merge_note_hz(value);
    append_sweep(samples, 180.0, static_cast<double>(hz), 54, 0.30);
    append_tone(samples, static_cast<double>(hz), 118, 0.46);
    append_tone(samples, static_cast<double>(hz) * 1.5, 96, 0.34);
    play_samples(std::move(samples));
}

void play_jackpot_sfx()
{
    std::vector<int16_t> samples;
    append_tone(samples, 392.0, 86, 0.36);
    append_tone(samples, 523.25, 86, 0.38);
    append_tone(samples, 659.25, 104, 0.40);
    append_chord(samples, {523.25, 659.25, 783.99}, 190, 0.44);
    append_sweep(samples, 987.77, 1567.98, 190, 0.34);
    append_chord(samples, {659.25, 783.99, 1046.50}, 260, 0.40);
    play_samples(std::move(samples));
}

bool has_empty()
{
    for (const auto &row : g_board) {
        for (int value : row) {
            if (value == 0) return true;
        }
    }
    return false;
}

void add_random_tile()
{
    std::array<std::pair<int, int>, kBoardSize * kBoardSize> empty{};
    int count = 0;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            if (g_board[y][x] == 0) empty[count++] = {y, x};
        }
    }
    if (count == 0) return;
    std::pair<int, int> cell = empty[std::rand() % count];
    g_board[cell.first][cell.second] = (std::rand() % 10 == 0) ? 4 : 2;
}

bool merge_line(std::array<int, kBoardSize> &line, int &max_merged)
{
    std::array<int, kBoardSize> before = line;
    std::array<int, kBoardSize> compact{};
    int pos = 0;
    for (int value : line) {
        if (value != 0) compact[pos++] = value;
    }
    for (int i = 0; i + 1 < kBoardSize; ++i) {
        if (compact[i] != 0 && compact[i] == compact[i + 1]) {
            compact[i] *= 2;
            g_score += compact[i];
            max_merged = std::max(max_merged, compact[i]);
            compact[i + 1] = 0;
        }
    }
    line.fill(0);
    pos = 0;
    for (int value : compact) {
        if (value != 0) line[pos++] = value;
    }
    return line != before;
}

bool move_board(int direction, int &max_merged)
{
    bool moved = false;
    for (int i = 0; i < kBoardSize; ++i) {
        std::array<int, kBoardSize> line{};
        for (int j = 0; j < kBoardSize; ++j) {
            if (direction == 0) line[j] = g_board[j][i];
            else if (direction == 1) line[j] = g_board[i][kBoardSize - 1 - j];
            else if (direction == 2) line[j] = g_board[kBoardSize - 1 - j][i];
            else line[j] = g_board[i][j];
        }
        bool line_moved = merge_line(line, max_merged);
        moved = moved || line_moved;
        for (int j = 0; j < kBoardSize; ++j) {
            if (direction == 0) g_board[j][i] = line[j];
            else if (direction == 1) g_board[i][kBoardSize - 1 - j] = line[j];
            else if (direction == 2) g_board[kBoardSize - 1 - j][i] = line[j];
            else g_board[i][j] = line[j];
        }
    }
    return moved;
}

bool has_won()
{
    for (const auto &row : g_board) {
        for (int value : row) {
            if (value >= 2048) return true;
        }
    }
    return false;
}

bool can_move()
{
    if (has_empty()) return true;
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            if (x + 1 < kBoardSize && g_board[y][x] == g_board[y][x + 1]) return true;
            if (y + 1 < kBoardSize && g_board[y][x] == g_board[y + 1][x]) return true;
        }
    }
    return false;
}

void set_status(const std::string &value)
{
    lv_label_set_text(g_status_label, value.c_str());
}

void show_goal_prompt()
{
    g_won_seen = true;
    g_goal_prompt_active = true;
    set_status("2048 reached!\nEnter keep\nR restart");
}

void continue_after_goal()
{
    g_goal_prompt_active = false;
    set_status("Keep going\nmake 4096");
}

void request_reset_confirmation()
{
    g_reset_confirm_active = true;
    set_status("R again reset\nAny key cancel");
}

void cancel_reset_confirmation()
{
    if (!g_reset_confirm_active) return;
    g_reset_confirm_active = false;
    if (g_goal_prompt_active) {
        set_status("2048 reached!\nEnter keep\nR restart");
    } else if (!can_move()) {
        set_status("Game over\nR restart");
    } else {
        set_status("WASD/FZXC\nmove tiles");
    }
}

void toggle_sound_mute()
{
    g_reset_confirm_active = false;
    bool muted = !g_sound_muted.load();
    g_sound_muted.store(muted);
    set_status(muted ? "Sound off\nM unmute" : "Sound on\nM mute");
    if (!muted) {
        play_merge_sfx(64);
    }
}

void update_ui()
{
    char score_text[32];
    std::snprintf(score_text, sizeof(score_text), "Score %d", g_score);
    lv_label_set_text(g_score_label, score_text);

    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            int value = g_board[y][x];
            char text[8] = {};
            if (value != 0) std::snprintf(text, sizeof(text), "%d", value);
            lv_obj_set_style_bg_color(g_tiles[y][x], lv_color_hex(tile_color(value)), 0);
            lv_obj_set_style_text_color(g_tile_labels[y][x], lv_color_hex(text_color(value)), 0);
            lv_obj_set_style_text_font(
                g_tile_labels[y][x],
                value >= 1024 ? &lv_font_montserrat_10 : &lv_font_montserrat_14,
                0);
            lv_label_set_text(g_tile_labels[y][x], text);
        }
    }

    if (has_won() && !g_won_seen) {
        show_goal_prompt();
    } else if (!can_move()) {
        set_status("Game over\nR restart");
    }
}

void reset_game()
{
    for (auto &row : g_board) row.fill(0);
    g_score = 0;
    g_won_seen = false;
    g_goal_prompt_active = false;
    g_reset_confirm_active = false;
    add_random_tile();
    add_random_tile();
    set_status("WASD/FZXC\nmove tiles");
    update_ui();
}

void set_demo_board(const int values[kBoardSize][kBoardSize], int score, const char *status)
{
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            g_board[y][x] = values[y][x];
        }
    }
    g_score = score;
    g_won_seen = false;
    g_goal_prompt_active = false;
    g_reset_confirm_active = false;
    update_ui();
    set_status(status);
}

void apply_demo_state()
{
    const char *state = std::getenv("M5_2048_DEMO_STATE");
    if (!state || !state[0]) return;

    if (std::strcmp(state, "mid") == 0) {
        const int board[kBoardSize][kBoardSize] = {
            {2, 4, 8, 16},
            {0, 32, 64, 128},
            {2, 0, 256, 512},
            {4, 8, 16, 0},
        };
        set_demo_board(board, 1236, "Plan merge\nkeep space");
    } else if (std::strcmp(state, "win") == 0) {
        const int board[kBoardSize][kBoardSize] = {
            {2, 4, 8, 16},
            {32, 64, 128, 256},
            {512, 1024, 2048, 4},
            {0, 2, 4, 8},
        };
        set_demo_board(board, 24576, "2048 reached!\nEnter keep");
    } else if (std::strcmp(state, "gameover") == 0) {
        const int board[kBoardSize][kBoardSize] = {
            {2, 4, 2, 4},
            {4, 2, 4, 2},
            {8, 16, 8, 16},
            {16, 8, 16, 8},
        };
        set_demo_board(board, 8172, "Game over\nR restart");
    }
}

void handle_escape_key()
{
    uint32_t now = lv_tick_get();
    if (g_escape_repeat_count == 0 || lv_tick_elaps(g_escape_last_tick) > 220) {
        g_escape_first_tick = now;
        g_escape_repeat_count = 1;
        set_status("Hold Esc\nto quit");
    } else {
        ++g_escape_repeat_count;
        if (lv_tick_elaps(g_escape_first_tick) >= 650 || g_escape_repeat_count >= 8) {
            request_quit();
        }
    }
    g_escape_last_tick = now;
}

void create_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                  const lv_font_t *font, uint32_t color, lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, mode);
    lv_label_set_text(label, text);
}

void build_ui()
{
    g_root = lv_screen_active();
    lv_obj_set_size(g_root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0xFAF8EF), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    lv_obj_t *board = lv_obj_create(g_root);
    lv_obj_remove_style_all(board);
    lv_obj_set_pos(board, kBoardX, kBoardY);
    lv_obj_set_size(board, kBoardSize * kTileSize + (kBoardSize + 1) * kTileGap,
                    kBoardSize * kTileSize + (kBoardSize + 1) * kTileGap);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(board, 6, 0);

    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            lv_obj_t *tile = lv_obj_create(board);
            lv_obj_remove_style_all(tile);
            lv_obj_set_pos(tile, kTileGap + x * (kTileSize + kTileGap),
                           kTileGap + y * (kTileSize + kTileGap));
            lv_obj_set_size(tile, kTileSize, kTileSize);
            lv_obj_set_style_radius(tile, 4, 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            g_tiles[y][x] = tile;

            lv_obj_t *label = lv_label_create(tile);
            lv_obj_set_size(label, kTileSize, 18);
            lv_obj_center(label);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            g_tile_labels[y][x] = label;
        }
    }

    create_label(g_root, "2048", kPanelX, 10, 140, 18, &lv_font_montserrat_14, 0x776E65);
    g_score_label = lv_label_create(g_root);
    lv_obj_set_pos(g_score_label, kPanelX, 32);
    lv_obj_set_size(g_score_label, 140, 18);
    lv_obj_set_style_text_font(g_score_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_score_label, lv_color_hex(0x776E65), 0);

    g_status_label = lv_label_create(g_root);
    lv_obj_set_pos(g_status_label, kPanelX, 59);
    lv_obj_set_size(g_status_label, 146, 44);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x776E65), 0);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);

    create_label(g_root, "WASD/FZXC\nR reset M mute\nHold Esc quit",
                 kPanelX, 116, 146, 44, &lv_font_montserrat_10, 0x8F7A66, LV_LABEL_LONG_WRAP);

    lv_obj_add_event_cb(g_root, [](lv_event_t *event) {
        auto *key = static_cast<key_item *>(lv_event_get_param(event));
        if (!key || key->key_state == KBD_KEY_RELEASED) return;

        char ch = 0;
        if (key->utf8[0] && !key->utf8[1]) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(key->utf8[0])));
        }

        if (key->key_code == KEY_ESC) {
            if (g_reset_confirm_active) {
                cancel_reset_confirmation();
                return;
            }
            handle_escape_key();
            return;
        }
        g_escape_repeat_count = 0;

        if (ch == 'm') {
            toggle_sound_mute();
            return;
        }

        if (g_reset_confirm_active) {
            if (ch == 'r') {
                reset_game();
                return;
            }
            cancel_reset_confirmation();
            return;
        }

        if (g_goal_prompt_active) {
            if (key->key_code == KEY_ENTER || ch == '\r' || ch == ' ' || ch == 'y') {
                continue_after_goal();
            } else if (ch == 'r' || ch == 'n') {
                request_reset_confirmation();
            } else if (ch == 'q') {
                request_quit();
            }
            return;
        }

        int direction = -1;
        if (key->key_code == KEY_UP || ch == 'w' || ch == 'f') direction = 0;
        else if (key->key_code == KEY_RIGHT || ch == 'd' || ch == 'c') direction = 1;
        else if (key->key_code == KEY_DOWN || ch == 's' || ch == 'x') direction = 2;
        else if (key->key_code == KEY_LEFT || ch == 'a' || ch == 'z') direction = 3;
        else if (ch == 'q') request_quit();
        else if (ch == 'r') request_reset_confirmation();

        if (direction >= 0 && can_move()) {
            bool had_won = has_won();
            int max_merged = 0;
            if (!move_board(direction, max_merged)) return;
            add_random_tile();
            if (max_merged >= 2048 && !had_won) {
                play_jackpot_sfx();
            } else if (max_merged > 0) {
                play_merge_sfx(max_merged);
            } else {
                play_move_sfx();
            }
            update_ui();
            if (!g_goal_prompt_active && can_move()) {
                set_status("WASD/FZXC\nmove tiles");
            }
        }
    }, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);

    g_group = lv_group_create();
    lv_group_add_obj(g_group, g_root);
    lv_group_focus_obj(g_root);
    if (g_keyboard_indev) lv_indev_set_group(g_keyboard_indev, g_group);

    reset_game();
}

#if LV_USE_EVDEV
int evdev_to_lv_key(uint16_t code)
{
    switch (code) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_TAB: return KEY_TAB;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        default: return code;
    }
}

void keypad_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
    pthread_mutex_lock(&keyboard_mutex);
    if (!STAILQ_EMPTY(&keyboard_queue)) {
        key_item *elm = STAILQ_FIRST(&keyboard_queue);
        STAILQ_REMOVE_HEAD(&keyboard_queue, entries);
        if (g_root) {
            lv_obj_send_event(g_root, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), elm);
        }
        data->key = evdev_to_lv_key(elm->key_code);
        data->state = static_cast<lv_indev_state_t>(elm->key_state);
        data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
        std::free(elm);
    }
    pthread_mutex_unlock(&keyboard_mutex);
}

void lv_linux_indev_init()
{
    const char *keyboard_device = getenv_default(
        "LV_LINUX_KEYBOARD_DEVICE",
        "/dev/input/by-path/platform-3f804000.i2c-event");
    pthread_t thread_id;
    pthread_create(&thread_id, nullptr, keyboard_read_thread, const_cast<char *>(keyboard_device));
    pthread_detach(thread_id);
    g_keyboard_indev = lv_indev_create();
    lv_indev_set_type(g_keyboard_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(g_keyboard_indev, keypad_read_cb);
}
#endif

#if LV_USE_LINUX_FBDEV
void lv_linux_disp_init()
{
    char fbdev[64] = {};
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", nullptr);
    if (!device && get_st7789v_fbdev(fbdev, sizeof(fbdev)) == 0) device = fbdev;
    if (!device) device = "/dev/fb0";
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp) lv_linux_fbdev_set_file(disp, device);
}

#if !LV_USE_EVDEV && !LV_USE_LIBINPUT
void lv_linux_indev_init() {}
#endif

#elif LV_USE_SDL
void lv_linux_disp_init()
{
    lv_display_t *disp = lv_sdl_window_create(kScreenWidth, kScreenHeight);
    lv_sdl_window_set_title(disp, "2048");
}

void lv_linux_indev_init()
{
    lv_sdl_mouse_create();
    g_keyboard_indev = lv_sdl_keyboard_create();
}
#else
#error Unsupported display configuration
#endif

}  // namespace

int main()
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    if (env_enabled("M5_2048_AUDIO_SELFTEST", false)) {
        audio_log("selftest start");
        play_jackpot_sfx();
        for (int i = 0; i < 300 && g_sfx_busy.load(); ++i) {
            usleep(10000);
        }
        audio_log("selftest done busy=%d", g_sfx_busy.load() ? 1 : 0);
        return g_sfx_busy.load() ? 1 : 0;
    }

    lv_init();
    lv_linux_disp_init();
    LV_EVENT_KEYBOARD = lv_event_register_id();
    lv_linux_indev_init();
    build_ui();
    apply_demo_state();

    while (!g_quit_requested) {
        lv_timer_handler();
        usleep(1000);
    }

    return 0;
}
