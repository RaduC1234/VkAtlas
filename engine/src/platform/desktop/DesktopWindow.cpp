#include "DesktopWindow.hpp"

#include "core/Keyboard.hpp"

#ifdef ATLAS_PLATFORM_DESKTOP

#include <cassert>
#include <stdexcept>

#include "stb_image.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windowsx.h>

#include "core/Mouse.hpp"

#pragma comment(lib, "dwmapi.lib")

namespace Atlas {
    namespace {
        constexpr wchar_t WINDOW_POINTER_PROPERTY[] = L"AtlasDesktopWindow";
        constexpr wchar_t ORIGINAL_WNDPROC_PROPERTY[] = L"AtlasOriginalWndProc";
        constexpr wchar_t CAPTION_BAR_CLASS[] = L"AtlasCaptionBar";

        constexpr int TITLEBAR_HEIGHT = 32;
        constexpr int CAPTION_BTN_W = 46;
        constexpr int CAPTION_BUTTON_AREA_WIDTH = CAPTION_BTN_W * 3; // 138
        constexpr int TITLEBAR_INTERACTIVE_AREA_WIDTH = 350;

        // Win11 Fluent colour palette, matched to the ImGui titlebar.
        constexpr COLORREF COL_NORMAL = RGB(38, 38, 38);
        constexpr COLORREF COL_HOVER = RGB(45, 45, 48);
        constexpr COLORREF COL_PRESSED = RGB(52, 52, 56);
        constexpr COLORREF COL_CLOSE_HOV = RGB(196, 43, 28);
        constexpr COLORREF COL_CLOSE_PRS = RGB(139, 10, 20);
        constexpr COLORREF COL_ICON = RGB(255, 255, 255);
        constexpr COLORREF COL_ICON_DIM = RGB(160, 160, 160);
    }

    // =========================================================================
    //  GDI caption button painter — flat Win11 Fluent style
    // =========================================================================
    static void paintCaptionButton(HDC hdc, int index, RECT rc, bool hovered, bool pressed, bool maximised) {
        // Background
        COLORREF bg = COL_NORMAL;
        if (hovered) {
            if (index == 2) bg = pressed ? COL_CLOSE_PRS : COL_CLOSE_HOV;
            else bg = pressed ? COL_PRESSED : COL_HOVER;
        }
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(hdc, &rc, br);
        DeleteObject(br);

        // Icon
        const COLORREF iconCol = (hovered && index == 2) ? COL_ICON_DIM : COL_ICON;
        HPEN pen = CreatePen(PS_SOLID, 1, iconCol);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));

        const int cx = rc.left + (rc.right - rc.left) / 2;
        const int cy = rc.top + (rc.bottom - rc.top) / 2;

        if (index == 0) {
            // Minimize — horizontal bar
            MoveToEx(hdc, cx - 5, cy + 1, nullptr);
            LineTo(hdc, cx + 5, cy + 1);
        } else if (index == 1) {
            if (maximised) {
                // Restore — two overlapping squares
                POINT back[] = {
                    {cx - 2, cy - 5},
                    {cx + 5, cy - 5},
                    {cx + 5, cy + 2},
                };
                Polyline(hdc, back, 3);
                Rectangle(hdc, cx - 5, cy - 2, cx + 3, cy + 5);
            } else {
                // Maximize — single square
                Rectangle(hdc, cx - 5, cy - 4, cx + 5, cy + 5);
            }
        } else {
            // Close — X with 1.5px visual weight via two offset lines
            MoveToEx(hdc, cx - 5, cy - 4, nullptr);
            LineTo(hdc, cx + 6, cy + 6);
            MoveToEx(hdc, cx - 4, cy - 4, nullptr);
            LineTo(hdc, cx + 6, cy + 5);
            MoveToEx(hdc, cx + 5, cy - 4, nullptr);
            LineTo(hdc, cx - 6, cy + 6);
            MoveToEx(hdc, cx + 4, cy - 4, nullptr);
            LineTo(hdc, cx - 6, cy + 5);
        }

        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // =========================================================================
    //  CaptionBar WndProc
    // =========================================================================
    static int captionHitTest(HWND hwnd, int x, int y) {
        if (y < 0 || y >= TITLEBAR_HEIGHT) return -1;
        if (x >= 0 && x < CAPTION_BTN_W) return 0;
        if (x >= CAPTION_BTN_W && x < CAPTION_BTN_W * 2) return 1;
        if (x >= CAPTION_BTN_W * 2 && x < CAPTION_BTN_W * 3) return 2;
        return -1;
    }

    static LRESULT CALLBACK captionBarWndProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp) {
        auto *cb = reinterpret_cast<CaptionBar *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!cb) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                HWND owner = GetWindow(hwnd, GW_OWNER);
                bool max = !!IsZoomed(owner);
                for (int i = 0; i < 3; ++i) {
                    RECT rc{
                        i * CAPTION_BTN_W, 0,
                        (i + 1) * CAPTION_BTN_W, TITLEBAR_HEIGHT
                    };
                    paintCaptionButton(hdc, i, rc,
                                       cb->hovered == i, cb->pressed == i, max);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_MOUSEMOVE: {
                int now = captionHitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                if (now != cb->hovered) {
                    cb->hovered = now;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                    TrackMouseEvent(&tme);
                }
                return 0;
            }

            case WM_MOUSELEAVE:
                cb->hovered = -1;
                cb->pressed = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;

            case WM_LBUTTONDOWN:
                cb->pressed = captionHitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;

            case WM_LBUTTONUP: {
                int btn = captionHitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                cb->pressed = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                HWND owner = GetWindow(hwnd, GW_OWNER);
                if (btn == 2)
                    PostMessage(owner, WM_CLOSE, 0, 0);
                if (btn == 1) ShowWindow(owner, IsZoomed(owner) ? SW_RESTORE : SW_MAXIMIZE);
                if (btn == 0) ShowWindow(owner, SW_MINIMIZE);
                return 0;
            }

            case WM_LBUTTONDBLCLK: {
                if (captionHitTest(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) == 1) {
                    HWND owner = GetWindow(hwnd, GW_OWNER);
                    ShowWindow(owner, IsZoomed(owner) ? SW_RESTORE : SW_MAXIMIZE);
                }
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // =========================================================================
    //  CaptionBar public methods
    // =========================================================================
    void CaptionBar::create(void *ownerHwnd) {
        HWND owner = static_cast<HWND>(ownerHwnd);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = captionBarWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = CAPTION_BAR_CLASS;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        RegisterClassExW(&wc);

        RECT wr{};
        GetWindowRect(owner, &wr);

        hwnd = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            CAPTION_BAR_CLASS, nullptr,
            WS_POPUP | WS_VISIBLE,
            wr.right - CAPTION_BUTTON_AREA_WIDTH, wr.top,
            CAPTION_BUTTON_AREA_WIDTH, TITLEBAR_HEIGHT,
            owner, nullptr,
            GetModuleHandleW(nullptr), nullptr);

        SetLayeredWindowAttributes(static_cast<HWND>(hwnd), 0, 255, LWA_ALPHA);
        SetWindowLongPtrW(static_cast<HWND>(hwnd), GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));
    }

    void CaptionBar::destroy() {
        if (hwnd) {
            DestroyWindow(static_cast<HWND>(hwnd));
            hwnd = nullptr;
        }
    }

    void CaptionBar::reposition(void *ownerHwnd) const {
        if (!hwnd) return;
        HWND owner = static_cast<HWND>(ownerHwnd);
        RECT wr{};
        GetWindowRect(owner, &wr);

        int x = wr.right - CAPTION_BUTTON_AREA_WIDTH;
        int y = wr.top;

        if (IsZoomed(owner)) {
            const UINT dpi = GetDpiForWindow(owner);
            const int border = GetSystemMetricsForDpi(SM_CXFRAME, dpi)
                               + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            x = wr.right - CAPTION_BUTTON_AREA_WIDTH - border;
            y = wr.top + border;
        }

        SetWindowPos(static_cast<HWND>(hwnd), HWND_TOP,
                     x, y, CAPTION_BUTTON_AREA_WIDTH, TITLEBAR_HEIGHT,
                     SWP_NOACTIVATE);
    }

    void CaptionBar::invalidate() const {
        if (hwnd) InvalidateRect(static_cast<HWND>(hwnd), nullptr, TRUE);
    }

    // =========================================================================
    //  Main window helpers
    // =========================================================================
    namespace {
        int getResizeBorderThickness(HWND hwnd) {
            const UINT dpi = GetDpiForWindow(hwnd);
            return GetSystemMetricsForDpi(SM_CXFRAME, dpi)
                   + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        }

        LRESULT hitTestCustomFrame(HWND hwnd, LPARAM lParam) {
            RECT wr{};
            GetWindowRect(hwnd, &wr);
            const POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int border = getResizeBorderThickness(hwnd);
            const bool resizable = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;

            if (resizable) {
                const bool L = cursor.x >= wr.left && cursor.x < wr.left + border;
                const bool R = cursor.x < wr.right && cursor.x >= wr.right - border;
                const bool T = cursor.y >= wr.top && cursor.y < wr.top + border;
                const bool B = cursor.y < wr.bottom && cursor.y >= wr.bottom - border;
                if (T && L) return HTTOPLEFT;
                if (T && R) return HTTOPRIGHT;
                if (B && L) return HTBOTTOMLEFT;
                if (B && R) return HTBOTTOMRIGHT;
                if (L) return HTLEFT;
                if (R) return HTRIGHT;
                if (T) return HTTOP;
                if (B) return HTBOTTOM;
            }

            const int localY = cursor.y - wr.top;
            const int localX = cursor.x - wr.left;
            const int width = wr.right - wr.left;

            if (localY >= 0 && localY < TITLEBAR_HEIGHT) {
                if (localX < TITLEBAR_INTERACTIVE_AREA_WIDTH ||
                    localX >= width - CAPTION_BUTTON_AREA_WIDTH) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        LRESULT CALLBACK atlasWindowProc(HWND hwnd, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
            switch (message) {
                case WM_NCCALCSIZE:
                    if (wParam == TRUE) {
                        // When maximized Windows pushes the window outside the
                        // screen by the resize border thickness on all sides so
                        // the invisible border is hidden. We must inset the
                        // client rect by that amount or content overflows the
                        // monitor edge.
                        if (IsZoomed(hwnd)) {
                            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);
                            const int border = getResizeBorderThickness(hwnd);
                            params->rgrc[0].left += border;
                            params->rgrc[0].top += border;
                            params->rgrc[0].right -= border;
                            params->rgrc[0].bottom -= border;
                        }
                        return 0;
                    }
                    break;

                case WM_NCHITTEST:
                    return hitTestCustomFrame(hwnd, lParam);

                case WM_SIZE:
                case WM_MOVE: {
                    auto *win = static_cast<DesktopWindow *>(
                        GetPropW(hwnd, WINDOW_POINTER_PROPERTY));
                    if (win) {
                        win->captionBar.reposition(hwnd);
                        win->captionBar.invalidate();
                    }
                    break;
                }

                case WM_ACTIVATE: {
                    auto *win = static_cast<DesktopWindow *>(
                        GetPropW(hwnd, WINDOW_POINTER_PROPERTY));
                    if (win && win->captionBar.hwnd)
                        SetWindowPos(static_cast<HWND>(win->captionBar.hwnd),
                                     HWND_TOP, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    break;
                }

                case WM_DESTROY:
                    RemovePropW(hwnd, WINDOW_POINTER_PROPERTY);
                    break;

                default:
                    break;
            }

            auto *window = static_cast<DesktopWindow *>(
                GetPropW(hwnd, WINDOW_POINTER_PROPERTY));
            auto orig = reinterpret_cast<WNDPROC>(
                GetPropW(hwnd, ORIGINAL_WNDPROC_PROPERTY));

            if (!window || !orig)
                return DefWindowProcW(hwnd, message, wParam, lParam);
            return CallWindowProcW(orig, hwnd, message, wParam, lParam);
        }
    }

    // =========================================================================
    //  DesktopWindow
    // =========================================================================
    DesktopWindow::DesktopWindow(const CreateInfo &properties) {
        this->width = properties.width;
        this->height = properties.height;
        this->customTitleBar =
                (properties.properties & CustomTitlebar) != 0;

        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        assert(!(properties.properties & Decorated &&
                properties.properties & Undecorated) &&
            "A window cannot be decorated and undecorated at the same time");
        assert(!(properties.properties & Resizeable &&
                properties.properties & NonResizeable) &&
            "A window cannot be resizable and non-resizable at the same time");

        if (customTitleBar)
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        const bool wantDecorated =
                (properties.properties & Undecorated) ? false : true;
        glfwWindowHint(GLFW_DECORATED, wantDecorated ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE,
                       (properties.properties & NonResizeable)
                           ? GLFW_FALSE
                           : GLFW_TRUE);

        glfwWindow = glfwCreateWindow(
            properties.width, properties.height,
            properties.title.c_str(), nullptr, nullptr);

        glfwSetWindowUserPointer(glfwWindow, this);
        glfwSetFramebufferSizeCallback(glfwWindow, framebufferResizeCallback);
        glfwSetCursorPosCallback(glfwWindow, mouseCursorPositionCallback);
        glfwSetMouseButtonCallback(glfwWindow, mouseButtonCallback);
        glfwSetKeyCallback(glfwWindow, keyboardKeyCallback);
        glfwSetCharCallback(glfwWindow, keyboardTextCallback);

        if (!properties.iconPath.empty())
            DesktopWindow::setWindowIcon(properties.iconPath);

        if (customTitleBar) {
            installCustomTitleBar();
            glfwShowWindow(glfwWindow);
        }
    }

    DesktopWindow::~DesktopWindow() { removeCustomTitleBar(); }

    bool DesktopWindow::shouldClose() {
        return glfwWindowShouldClose(this->glfwWindow);
    }

    void DesktopWindow::createWindowSurface(VkInstance instance,
                                            VkSurfaceKHR *surface) const {
        if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create window surface");
    }

    void DesktopWindow::pollEvents() { glfwPollEvents(); }
    void DesktopWindow::waitEvents() { glfwWaitEvents(); }

    std::vector<const char *> DesktopWindow::getRequiredExtensions() {
        uint32_t count = 0;
        const auto ext = glfwGetRequiredInstanceExtensions(&count);
        return {ext, ext + count};
    }

    void DesktopWindow::setCursorMode(const CursorMode cursorMode) {
        switch (cursorMode) {
            case CursorMode::Disabled:
                glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                break;
            case CursorMode::Normal:
                glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
            case CursorMode::Hidden:
                glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                break;
        }
    }

    void DesktopWindow::setWindowIcon(const std::string &iconPath) {
        if (iconPath.empty()) return;
        int w = 0, h = 0, ch = 0;
        unsigned char *pixels = stbi_load(iconPath.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            const char *r = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to load window icon '") + iconPath +
                "': " + (r ? r : "unknown"));
        }
        GLFWimage img{w, h, pixels};
        glfwSetWindowIcon(glfwWindow, 1, &img);
        stbi_image_free(pixels);
    }

    void DesktopWindow::setTheme(Theme theme) {
        this->theme = theme;

        HWND hwnd = glfwGetWin32Window(glfwWindow);
        if (!hwnd) return;
        BOOL dark = (theme == Theme::Dark) ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        captionBar.invalidate();
    }

    void DesktopWindow::installCustomTitleBar() {
        HWND hwnd = glfwGetWin32Window(glfwWindow);
        if (!hwnd) return;

        SetPropW(hwnd, WINDOW_POINTER_PROPERTY, this);
        originalWindowProc = reinterpret_cast<void *>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(atlasWindowProc)));
        SetPropW(hwnd, ORIGINAL_WNDPROC_PROPERTY, originalWindowProc);

        // Rounded corners (Windows 11 only — no-op on Win10)
        // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
        const DWORD corner = 2;
        DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOOWNERZORDER);

        captionBar.create(hwnd);
    }

    void DesktopWindow::removeCustomTitleBar() {
        captionBar.destroy();
        HWND hwnd = glfwGetWin32Window(glfwWindow);
        if (!hwnd || !originalWindowProc) return;
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProc));
        RemovePropW(hwnd, WINDOW_POINTER_PROPERTY);
        RemovePropW(hwnd, ORIGINAL_WNDPROC_PROPERTY);
        originalWindowProc = nullptr;
    }

    void *DesktopWindow::getNativeHandle() const { return glfwWindow; }

    void DesktopWindow::framebufferResizeCallback(GLFWwindow *w, int width, int height) {
        auto *win = static_cast<DesktopWindow *>(glfwGetWindowUserPointer(w));
        win->width = width;
        win->height = height;
        win->framebufferResized = true;
    }

    void DesktopWindow::mouseCursorPositionCallback(GLFWwindow *, double x, double y) {
        Mouse::xPos = x;
        Mouse::yPos = y;
        Mouse::dragging = Mouse::buttonPressed[0] || Mouse::buttonPressed[1] || Mouse::buttonPressed[2];
    }

    void DesktopWindow::mouseButtonCallback(GLFWwindow *, int button, int action, int) {
        if (button >= static_cast<int>(Mouse::buttonPressed.size())) return;
        if (action == GLFW_PRESS) Mouse::buttonPressed[button] = true;
        if (action == GLFW_RELEASE) Mouse::buttonPressed[button] = false;
    }

    void DesktopWindow::keyboardKeyCallback(GLFWwindow *glfwWindow, int key, int scancode, int action, int mods) {
        if (key > Keyboard::keyPressed.size()) {
            return;
        }

        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            Keyboard::keyPressed[key] = true;
        } else if (action == GLFW_RELEASE) {
            Keyboard::keyPressed[key] = false;
        }
    }

    void DesktopWindow::keyboardTextCallback(GLFWwindow *, unsigned int) {
    }
}

#endif
