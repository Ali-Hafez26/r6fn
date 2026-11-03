#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "math.hpp"

namespace ext {

struct DrawItem {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float health = 0.0f;
    float max_health = 100.0f;
    float distance = 0.0f;
    float aim_distance = 0.0f;
    bool has_health = true;
    bool visible = true;
    bool targeted = false;
    bool box = true;
    bool show_distance = true;
    std::string name;
};

struct DrawFrame {
    std::vector<DrawItem> items;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float fov_radius = 0.0f;
    bool draw_fov = false;
};

// GDI overlay: a layered, click-through, no-activate tool window with a black
// color key. The game's Present is never touched; this is a separate top-level
// window painted on top of the client.
class Overlay {
public:
    Overlay() = default;
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    bool start(int x, int y, int width, int height);
    void stop();
    void set_frame(const DrawFrame& frame);

    bool running() const noexcept { return running_.load(); }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void thread_main();
    void paint(HDC target);
    void draw_item(HDC dc, const DrawItem& item);
    void draw_text(HDC dc, int x, int y, COLORREF color, const std::string& text) const;

    std::thread thread_;
    std::mutex mutex_;
    DrawFrame frame_{};

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};

    HWND hwnd_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    HBRUSH health_back_ = nullptr;
    HBRUSH health_front_ = nullptr;
    HBRUSH health_low_ = nullptr;
    HPEN box_pen_ = nullptr;
    HPEN target_pen_ = nullptr;
    HPEN fov_pen_ = nullptr;

    int x_ = 0;
    int y_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace ext
