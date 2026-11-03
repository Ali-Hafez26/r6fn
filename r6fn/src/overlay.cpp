#include "overlay.hpp"

#include <algorithm>
#include <cstdio>

namespace ext {
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    out.resize(static_cast<std::size_t>(needed - 1));
    return out;
}

constexpr COLORREF kTransparentKey = RGB(0, 0, 0);

} // namespace

Overlay::~Overlay() {
    stop();
}

bool Overlay::start(int x, int y, int width, int height) {
    if (running_.load()) {
        return true;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }

    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
    stop_.store(false);

    thread_ = std::thread(&Overlay::thread_main, this);

    for (int i = 0; i < 200 && !running_.load(); ++i) {
        Sleep(5);
    }
    return running_.load();
}

void Overlay::stop() {
    stop_.store(true);

    if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    running_.store(false);
}

void Overlay::set_frame(const DrawFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = frame;
}

void Overlay::thread_main() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &Overlay::window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    // FIXED: IDC_ARROW cast to wide resource pointer for LoadCursorW
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
    window_class.lpszClassName = L"R6FN_OVERLAY_WINDOW";

    RegisterClassExW(&window_class);

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                WS_EX_NOACTIVATE,
                            window_class.lpszClassName, L"r6fn", WS_POPUP, x_, y_, width_, height_,
                            nullptr, nullptr, window_class.hInstance, nullptr);

    if (hwnd_ == nullptr) {
        stop_.store(true);
        return;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(hwnd_, kTransparentKey, 255, LWA_COLORKEY);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    HDC window_dc = GetDC(hwnd_);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    bitmap_ = CreateDIBSection(window_dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    memory_dc_ = CreateCompatibleDC(window_dc);
    previous_bitmap_ = SelectObject(memory_dc_, bitmap_);

    font_ = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                        FIXED_PITCH | FF_MODERN, L"Consolas");

    background_ = CreateSolidBrush(kTransparentKey);
    health_back_ = CreateSolidBrush(RGB(20, 20, 20));
    health_front_ = CreateSolidBrush(RGB(80, 220, 120));
    health_low_ = CreateSolidBrush(RGB(230, 70, 70));
    box_pen_ = CreatePen(PS_SOLID, 1, RGB(235, 235, 235));
    target_pen_ = CreatePen(PS_SOLID, 1, RGB(255, 90, 200));
    fov_pen_ = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));

    SelectObject(memory_dc_, font_);
    SetBkMode(memory_dc_, TRANSPARENT);

    running_.store(true);

    MSG message{};
    while (!stop_.load()) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        paint(window_dc);
        Sleep(1);
    }

    if (memory_dc_ != nullptr) {
        SelectObject(memory_dc_, previous_bitmap_);
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (memory_dc_ != nullptr) {
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (background_ != nullptr) {
        DeleteObject(background_);
        background_ = nullptr;
    }
    if (health_back_ != nullptr) {
        DeleteObject(health_back_);
        health_back_ = nullptr;
    }
    if (health_front_ != nullptr) {
        DeleteObject(health_front_);
        health_front_ = nullptr;
    }
    if (health_low_ != nullptr) {
        DeleteObject(health_low_);
        health_low_ = nullptr;
    }
    if (box_pen_ != nullptr) {
        DeleteObject(box_pen_);
        box_pen_ = nullptr;
    }
    if (target_pen_ != nullptr) {
        DeleteObject(target_pen_);
        target_pen_ = nullptr;
    }
    if (fov_pen_ != nullptr) {
        DeleteObject(fov_pen_);
        fov_pen_ = nullptr;
    }

    if (window_dc != nullptr) {
        ReleaseDC(hwnd_, window_dc);
    }

    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    running_.store(false);
}

void Overlay::paint(HDC target) {
    if (memory_dc_ == nullptr) {
        return;
    }

    DrawFrame frame;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame = frame_;
    }

    RECT full{0, 0, width_, height_};
    FillRect(memory_dc_, &full, background_);

    if (frame.draw_fov && frame.fov_radius > 0.0f) {
        const int radius = static_cast<int>(frame.fov_radius);
        const int cx = static_cast<int>(frame.center_x);
        const int cy = static_cast<int>(frame.center_y);
        HGDIOBJ old_pen = SelectObject(memory_dc_, fov_pen_);
        HGDIOBJ old_brush = SelectObject(memory_dc_, GetStockObject(NULL_BRUSH));
        Ellipse(memory_dc_, cx - radius, cy - radius, cx + radius, cy + radius);
        SelectObject(memory_dc_, old_brush);
        SelectObject(memory_dc_, old_pen);
    }

    for (const DrawItem& item : frame.items) {
        draw_item(memory_dc_, item);
    }

    BitBlt(target, 0, 0, width_, height_, memory_dc_, 0, 0, SRCCOPY);
}

void Overlay::draw_item(HDC dc, const DrawItem& item) {
    const int x0 = static_cast<int>(item.x0);
    const int y0 = static_cast<int>(item.y0);
    const int x1 = static_cast<int>(item.x1);
    const int y1 = static_cast<int>(item.y1);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    HGDIOBJ old_pen = SelectObject(dc, item.targeted ? target_pen_ : box_pen_);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (item.box) {
        Rectangle(dc, x0, y0, x1, y1);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);

    if (item.has_health && item.max_health > 0.0f) {
        const int bar_width = 3;
        const int bar_height = y1 - y0;
        const float ratio = std::clamp(item.health / item.max_health, 0.0f, 1.0f);
        const int filled = static_cast<int>(static_cast<float>(bar_height) * ratio);

        RECT back{x0 - bar_width - 2, y0, x0 - 2, y1};
        FillRect(dc, &back, health_back_);

        RECT front{x0 - bar_width - 2, y1 - filled, x0 - 2, y1};
        FillRect(dc, &front, ratio > 0.25f ? health_front_ : health_low_);
    }

    if (!item.name.empty()) {
        draw_text(dc, x0, y0 - 17, item.visible ? RGB(240, 240, 240) : RGB(255, 170, 90),
                  item.name);
    }

    char distance[64]{};
    if (item.show_distance) {
        std::snprintf(distance, sizeof(distance), "%.0fm", item.distance);
        draw_text(dc, x0, y1 + 2, RGB(200, 200, 200), distance);
    }
}

void Overlay::draw_text(HDC dc, int x, int y, COLORREF color, const std::string& text) const {
    const std::wstring wide = widen(text);
    if (wide.empty()) {
        return;
    }
    SetTextColor(dc, color);
    TextOutW(dc, x, y, wide.c_str(), static_cast<int>(wide.size()));
}

LRESULT CALLBACK Overlay::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ext
