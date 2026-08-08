#include "Platform/Window.h"
#include "Renderer/Gl.h"
#include "Core/Log.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <cstring>
#include <unordered_map>

namespace Crux {

using XWindow = ::Window;

struct Window::Impl {
    Display* display = nullptr;
    XWindow win = 0;
    GLXContext glx = nullptr;
    Atom wmDeleteMessage = 0;

    bool keys[256] = {};
    bool justPressed[256] = {};
    bool justReleased[256] = {};
    bool mouse[3] = {};
    bool mouseJustPressed[3] = {};
    bool mouseJustReleased[3] = {};
    Vec2 mousePos;
    Vec2 lastMousePos;
    bool cursorHidden = false;
    bool cursorWarped = false;

    static Key mapKey(KeySym sym);
};

Key Window::Impl::mapKey(KeySym s) {
    switch (s) {
        case XK_a: case XK_A: return Key::A;
        case XK_b: case XK_B: return Key::B;
        case XK_c: case XK_C: return Key::C;
        case XK_d: case XK_D: return Key::D;
        case XK_e: case XK_E: return Key::E;
        case XK_f: case XK_F: return Key::F;
        case XK_g: case XK_G: return Key::G;
        case XK_h: case XK_H: return Key::H;
        case XK_i: case XK_I: return Key::I;
        case XK_j: case XK_J: return Key::J;
        case XK_k: case XK_K: return Key::K;
        case XK_l: case XK_L: return Key::L;
        case XK_m: case XK_M: return Key::M;
        case XK_n: case XK_N: return Key::N;
        case XK_o: case XK_O: return Key::O;
        case XK_p: case XK_P: return Key::P;
        case XK_q: case XK_Q: return Key::Q;
        case XK_r: case XK_R: return Key::R;
        case XK_s: case XK_S: return Key::S;
        case XK_t: case XK_T: return Key::T;
        case XK_u: case XK_U: return Key::U;
        case XK_v: case XK_V: return Key::V;
        case XK_w: case XK_W: return Key::W;
        case XK_x: case XK_X: return Key::X;
        case XK_y: case XK_Y: return Key::Y;
        case XK_z: case XK_Z: return Key::Z;
        case XK_0: return Key::Digit0;
        case XK_1: return Key::Digit1;
        case XK_2: return Key::Digit2;
        case XK_3: return Key::Digit3;
        case XK_4: return Key::Digit4;
        case XK_5: return Key::Digit5;
        case XK_6: return Key::Digit6;
        case XK_7: return Key::Digit7;
        case XK_8: return Key::Digit8;
        case XK_9: return Key::Digit9;
        case XK_Escape: return Key::Escape;
        case XK_Return: return Key::Enter;
        case XK_Tab: return Key::Tab;
        case XK_space: return Key::Space;
        case XK_BackSpace: return Key::Backspace;
        case XK_Shift_L: return Key::LeftShift;
        case XK_Shift_R: return Key::RightShift;
        case XK_Control_L: return Key::LeftControl;
        case XK_Control_R: return Key::RightControl;
        case XK_Alt_L: return Key::LeftAlt;
        case XK_Alt_R: return Key::RightAlt;
        case XK_Up: return Key::ArrowUp;
        case XK_Down: return Key::ArrowDown;
        case XK_Left: return Key::ArrowLeft;
        case XK_Right: return Key::ArrowRight;
        case XK_F1: return Key::F1;
        case XK_F2: return Key::F2;
        case XK_F3: return Key::F3;
        case XK_F4: return Key::F4;
        case XK_F5: return Key::F5;
        case XK_F6: return Key::F6;
        case XK_F7: return Key::F7;
        case XK_F8: return Key::F8;
        case XK_F9: return Key::F9;
        case XK_F10: return Key::F10;
        case XK_F11: return Key::F11;
        case XK_F12: return Key::F12;
        case XK_minus: return Key::Minus;
        case XK_equal: return Key::Equal;
        case XK_bracketleft: return Key::LeftBracket;
        case XK_bracketright: return Key::RightBracket;
        case XK_semicolon: return Key::Semicolon;
        case XK_apostrophe: return Key::Apostrophe;
        case XK_comma: return Key::Comma;
        case XK_period: return Key::Period;
        case XK_slash: return Key::Slash;
        case XK_backslash: return Key::Backslash;
        case XK_grave: return Key::Grave;
        default: return Key::Count;
    }
}

Window::~Window() { shutdown(); }

bool Window::init(const WindowConfig& cfg) {
    impl_ = new Impl();
    Impl& p = *impl_;

    p.display = XOpenDisplay(nullptr);
    if (!p.display) {
        CRUX_LOG_ERROR("Failed to open X display");
        delete impl_; impl_ = nullptr;
        return false;
    }

    int screen = DefaultScreen(p.display);
    XWindow root = RootWindow(p.display, screen);

    int attrs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        None
    };

    GLXFBConfig fbConfig = nullptr;
    int fbCount = 0;
    GLXFBConfig* fbConfigs = glXChooseFBConfig(p.display, screen, attrs, &fbCount);
    if (!fbConfigs || fbCount == 0) {
        CRUX_LOG_ERROR("No suitable GLX framebuffer config");
        XCloseDisplay(p.display); delete impl_; impl_ = nullptr;
        return false;
    }
    fbConfig = fbConfigs[0];

    XVisualInfo* vi = glXGetVisualFromFBConfig(p.display, fbConfig);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = XCreateColormap(p.display, root, vi->visual, AllocNone);
    swa.background_pixmap = None;
    swa.border_pixel = 0;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask;

    u32 w = cfg.width, h = cfg.height;
    p.win = XCreateWindow(p.display, root, 0, 0, w, h, 0, vi->depth,
                          InputOutput, vi->visual,
                          CWColormap | CWEventMask | CWBorderPixel, &swa);

    XStoreName(p.display, p.win, cfg.title);
    p.wmDeleteMessage = XInternAtom(p.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(p.display, p.win, &p.wmDeleteMessage, 1);

    if (cfg.resizable) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = 320; hints->min_height = 240;
        hints->max_width = 8192; hints->max_height = 8192;
        XSetWMNormalHints(p.display, p.win, hints);
        XFree(hints);
    }

    if (cfg.fullscreen) {
        Atom wmState = XInternAtom(p.display, "_NET_WM_STATE", False);
        Atom wmFullscreen = XInternAtom(p.display, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(p.display, p.win, wmState, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)&wmFullscreen, 1);
    }

    p.glx = glXCreateContext(p.display, vi, nullptr, GL_TRUE);
    XFree(vi);
    XFree(fbConfigs);
    if (!p.glx) {
        CRUX_LOG_ERROR("Failed to create GLX context");
        XDestroyWindow(p.display, p.win);
        XCloseDisplay(p.display); delete impl_; impl_ = nullptr;
        return false;
    }

    glXMakeCurrent(p.display, p.win, p.glx);
    if (!Gl::loadFunctions()) {
        CRUX_LOG_ERROR("Failed to load GL functions");
        return false;
    }

    XMapWindow(p.display, p.win);
    XFlush(p.display);

    width_ = w; height_ = h;
    p.mousePos = Vec2((f32)w * 0.5f, (f32)h * 0.5f);
    p.lastMousePos = p.mousePos;

    CRUX_LOG_INFO("Window created: %ux%u (%s)", w, h, cfg.title);
    CRUX_LOG_INFO("OpenGL: %s | GLSL: %s",
        Gl::GetString(GL_VERSION), Gl::GetString(GL_SHADING_LANGUAGE_VERSION));
    return true;
}

void Window::shutdown() {
    if (!impl_) return;
    Impl& p = *impl_;
    if (p.glx) {
        glXMakeCurrent(p.display, None, nullptr);
        glXDestroyContext(p.display, p.glx);
    }
    if (p.win) XDestroyWindow(p.display, p.win);
    if (p.display) XCloseDisplay(p.display);
    delete impl_; impl_ = nullptr;
}

void Window::setCursorVisible(bool visible) {
    if (!impl_) return;
    impl_->cursorHidden = !visible;
    if (visible) {
        XUndefineCursor(impl_->display, impl_->win);
    } else {
        char data[1] = { 0 };
        Pixmap blank = XCreateBitmapFromData(impl_->display, impl_->win, data, 1, 1);
        XColor black = {};
        Cursor cursor = XCreatePixmapCursor(impl_->display, blank, blank, &black, &black, 0, 0);
        XDefineCursor(impl_->display, impl_->win, cursor);
        XFreePixmap(impl_->display, blank);
    }
    XFlush(impl_->display);
}

void Window::setCursorCaptured(bool captured) {
    captured_ = captured;
    setCursorVisible(!captured);
    if (captured_ && impl_) {
        XWarpPointer(impl_->display, None, impl_->win, 0, 0, 0, 0,
                     (i32)width_ / 2, (i32)height_ / 2);
        impl_->mousePos = Vec2((f32)width_ * 0.5f, (f32)height_ * 0.5f);
        impl_->lastMousePos = impl_->mousePos;
    }
}

void Window::pollEvents() {
    if (!impl_) return;
    Impl& p = *impl_;

    for (i32 i = 0; i < 256; i++) {
        p.justPressed[i] = false;
        p.justReleased[i] = false;
    }
    for (i32 i = 0; i < 3; i++) {
        p.mouseJustPressed[i] = false;
        p.mouseJustReleased[i] = false;
    }
    mouseDelta_ = Vec2(0, 0);

    while (XPending(p.display) > 0) {
        XEvent ev;
        XNextEvent(p.display, &ev);
        switch (ev.type) {
            case KeyPress: {
                KeySym sym = XLookupKeysym(&ev.xkey, 0);
                Key k = Impl::mapKey(sym);
                if (k != Key::Count) {
                    i32 idx = (i32)k;
                    if (!p.keys[idx]) p.justPressed[idx] = true;
                    p.keys[idx] = true;
                }
                break;
            }
            case KeyRelease: {
                KeySym sym = XLookupKeysym(&ev.xkey, 0);
                Key k = Impl::mapKey(sym);
                if (k != Key::Count) {
                    i32 idx = (i32)k;
                    p.keys[idx] = false;
                    p.justReleased[idx] = true;
                }
                break;
            }
            case ButtonPress: {
                i32 b = ev.xbutton.button - 1;
                if (b >= 0 && b < 3) {
                    if (!p.mouse[b]) p.mouseJustPressed[b] = true;
                    p.mouse[b] = true;
                }
                break;
            }
            case ButtonRelease: {
                i32 b = ev.xbutton.button - 1;
                if (b >= 0 && b < 3) {
                    p.mouse[b] = false;
                    p.mouseJustReleased[b] = true;
                }
                break;
            }
            case MotionNotify: {
                p.mousePos = Vec2((f32)ev.xmotion.x, (f32)ev.xmotion.y);
                break;
            }
            case FocusIn: focused_ = true; break;
            case FocusOut: focused_ = false; break;
            case ConfigureNotify:
                width_ = (u32)ev.xconfigure.width;
                height_ = (u32)ev.xconfigure.height;
                break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == p.wmDeleteMessage)
                    shouldClose_ = true;
                break;
            default: break;
        }
    }

    mouseDelta_ = p.mousePos - p.lastMousePos;
    p.lastMousePos = p.mousePos;
    mousePos_ = p.mousePos;

    if (captured_) {
        XWarpPointer(p.display, None, p.win, 0, 0, 0, 0,
                     (i32)width_ / 2, (i32)height_ / 2);
        p.mousePos = Vec2((f32)width_ * 0.5f, (f32)height_ * 0.5f);
        p.lastMousePos = p.mousePos;
    }

    glXSwapBuffers(p.display, p.win);
}

bool Window::isKeyPressed(Key key) const {
    if (!impl_ || key == Key::Count) return false;
    return impl_->keys[(i32)key];
}
bool Window::isKeyJustPressed(Key key) const {
    if (!impl_ || key == Key::Count) return false;
    return impl_->justPressed[(i32)key];
}
bool Window::isKeyJustReleased(Key key) const {
    if (!impl_ || key == Key::Count) return false;
    return impl_->justReleased[(i32)key];
}
bool Window::isMousePressed(MouseButton b) const {
    if (!impl_) return false;
    return impl_->mouse[(i32)b];
}
bool Window::isMouseJustPressed(MouseButton b) const {
    if (!impl_) return false;
    return impl_->mouseJustPressed[(i32)b];
}
bool Window::isMouseJustReleased(MouseButton b) const {
    if (!impl_) return false;
    return impl_->mouseJustReleased[(i32)b];
}

}
