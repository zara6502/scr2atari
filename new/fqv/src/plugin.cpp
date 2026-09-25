#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using string = std::wstring;

#include "plugin.hpp"
#include "qoi.h"

static PluginStartupInfo g_far{};
static FarStandardFunctions g_fsf{};

static const GUID PluginGuid =
{
    0x6a6d1e35,
    0x7c7f,
    0x4b7b,
    { 0x9f, 0x4b, 0x2e, 0x0d, 0x1d, 0x6f, 0x52, 0x91 }
};

static const GUID MenuGuid =
{
    0x2b2c3d44,
    0x5e6f,
    0x47a1,
    { 0x91, 0x72, 0x3a, 0x8c, 0x4d, 0x51, 0x9e, 0x20 }
};

static const wchar_t* const kClassName = L"FarQoiViewerWindow";
enum class BackgroundMode
{
    Checkers,
    Black,
    White
};
struct ViewerState
{
    QoiImage image;
    std::wstring filename;

    HWND hwnd = nullptr;

    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;

    bool fitToWindow = true;

    /*
       QOI RGBA -> Windows BGRA.
       Prepared once after loading.
    */
    std::vector<uint8_t> bgra;
};

static ViewerState* g_state = nullptr;


static bool has_qoi_extension(const wchar_t* name)
{
    if (!name)
        return false;

    const wchar_t* dot = wcsrchr(name, L'.');

    if (!dot)
        return false;

    return _wcsicmp(dot, L".qoi") == 0;
}


static bool load_file(const wchar_t* name, QoiImage& image)
{
    HANDLE h = CreateFileW(
        name,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER li{};

    if (!GetFileSizeEx(h, &li) ||
        li.QuadPart <= 0 ||
        static_cast<unsigned long long>(li.QuadPart) > SIZE_MAX)
    {
        CloseHandle(h);
        return false;
    }

    const size_t size = static_cast<size_t>(li.QuadPart);

    std::vector<uint8_t> data(size);

    size_t done = 0;

    while (done < size)
    {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(size - done, 1u << 20));

        DWORD got = 0;

        if (!ReadFile(
                h,
                data.data() + done,
                chunk,
                &got,
                nullptr) ||
            got == 0)
        {
            CloseHandle(h);
            return false;
        }

        done += got;
    }

    CloseHandle(h);

    return qoi_decode(data.data(), data.size(), image);
}


static bool prepare_bgra(ViewerState& s)
{
    if (s.image.rgba.empty())
        return false;

    s.bgra.resize(s.image.rgba.size());

    for (size_t i = 0; i < s.bgra.size(); i += 4)
    {
        const uint8_t r = s.image.rgba[i + 0];
        const uint8_t g = s.image.rgba[i + 1];
        const uint8_t b = s.image.rgba[i + 2];
        const uint8_t a = s.image.rgba[i + 3];

        if (a == 255)
        {
            s.bgra[i + 0] = b;
            s.bgra[i + 1] = g;
            s.bgra[i + 2] = r;
            s.bgra[i + 3] = 255;
        }
        else
        {
            const size_t pixel = i / 4;

            const int px =
                static_cast<int>(pixel % s.image.width);

            const int py =
                static_cast<int>(pixel / s.image.width);

            const int cell =
                ((px / 12 + py / 12) & 1)
                    ? 220
                    : 245;

            s.bgra[i + 0] = static_cast<uint8_t>(
                (b * a + cell * (255 - a)) / 255);

            s.bgra[i + 1] = static_cast<uint8_t>(
                (g * a + cell * (255 - a)) / 255);

            s.bgra[i + 2] = static_cast<uint8_t>(
                (r * a + cell * (255 - a)) / 255);

            s.bgra[i + 3] = 255;
        }
    }

    return true;
}


static void checker(
    HDC dc,
    const RECT& r)
{
    constexpr int cellSize = 12;

    for (int y = r.top; y < r.bottom; y += cellSize)
    {
        for (int x = r.left; x < r.right; x += cellSize)
        {
            RECT q
            {
                x,
                y,
                std::min(x + cellSize, static_cast<int>(r.right)),
                std::min(y + cellSize, static_cast<int>(r.bottom))
            };

            const COLORREF color =
                ((x / cellSize + y / cellSize) & 1)
                    ? RGB(220, 220, 220)
                    : RGB(245, 245, 245);

            HBRUSH brush = CreateSolidBrush(color);

            FillRect(dc, &q, brush);

            DeleteObject(brush);
        }
    }
}


/*
   Calculate the exact rectangle occupied by the image.
*/
static bool get_image_rect(
    HWND hwnd,
    const ViewerState& s,
    RECT& out)
{
    RECT rc{};

    GetClientRect(hwnd, &rc);

    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;

    if (cw <= 0 ||
        ch <= 0 ||
        s.image.width == 0 ||
        s.image.height == 0)
    {
        SetRectEmpty(&out);
        return false;
    }

    const double fit = std::min(
        static_cast<double>(cw) /
            static_cast<double>(s.image.width),
        static_cast<double>(ch) /
            static_cast<double>(s.image.height));

    if (fit <= 0.0)
    {
        SetRectEmpty(&out);
        return false;
    }

    const double scale =
        s.fitToWindow
            ? fit
            : s.zoom;

    const int dw = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(s.image.width) * scale)));

    const int dh = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(s.image.height) * scale)));

    const int x = static_cast<int>(
        std::lround((cw - dw) * 0.5 + s.panX));

    const int y = static_cast<int>(
        std::lround((ch - dh) * 0.5 + s.panY));

    out.left = x;
    out.top = y;
    out.right = x + dw;
    out.bottom = y + dh;

    return true;
}


/*
   Invalidate the complete area that can have changed.

   IMPORTANT:

   RGN_XOR is NOT correct for scaling because the
   overlapping area contains different pixels after
   resampling.

   We therefore invalidate the UNION:

       old image + new image

   No background erase is requested. The old image
   remains visible until the new image is actually
   painted over it.
*/
static void invalidate_image_change(
    HWND hwnd,
    const RECT& oldRect,
    const RECT& newRect)
{
    if (EqualRect(&oldRect, &newRect))
        return;

    RECT changed{};

    if (IsRectEmpty(&oldRect))
    {
        changed = newRect;
    }
    else if (IsRectEmpty(&newRect))
    {
        changed = oldRect;
    }
    else
    {
        changed.left =
            std::min(oldRect.left, newRect.left);

        changed.top =
            std::min(oldRect.top, newRect.top);

        changed.right =
            std::max(oldRect.right, newRect.right);

        changed.bottom =
            std::max(oldRect.bottom, newRect.bottom);
    }

    InvalidateRect(
        hwnd,
        &changed,
        FALSE);
}


/*
   Draw only the current paint region.

   BeginPaint clips the DC to ps.rcPaint, so there is
   no need to redraw the whole client area.
*/
static void draw_image(
    HWND hwnd,
    HDC dc,
    ViewerState& s,
    const RECT& paintRect)
{
    RECT imageRect{};

    if (!get_image_rect(hwnd, s, imageRect))
    {
        checker(dc, paintRect);
        return;
    }

    HRGN paintRegion =
        CreateRectRgnIndirect(&paintRect);

    HRGN imageRegion =
        CreateRectRgnIndirect(&imageRect);

    HRGN backgroundRegion =
        CreateRectRgn(0, 0, 0, 0);

    HRGN imagePaintRegion =
        CreateRectRgn(0, 0, 0, 0);

    if (!paintRegion ||
        !imageRegion ||
        !backgroundRegion ||
        !imagePaintRegion)
    {
        if (paintRegion)
            DeleteObject(paintRegion);

        if (imageRegion)
            DeleteObject(imageRegion);

        if (backgroundRegion)
            DeleteObject(backgroundRegion);

        if (imagePaintRegion)
            DeleteObject(imagePaintRegion);

        return;
    }

    /*
       Background:
           paintRegion - imageRegion
    */
    CombineRgn(
        backgroundRegion,
        paintRegion,
        imageRegion,
        RGN_DIFF);

    /*
       Image:
           paintRegion ∩ imageRegion
    */
    CombineRgn(
        imagePaintRegion,
        paintRegion,
        imageRegion,
        RGN_AND);

    /*
       1. Draw checkerboard ONLY where the current
          image does not exist.
    */
    SelectClipRgn(dc, backgroundRegion);

    checker(dc, paintRect);

    /*
       2. Draw the new image ONLY where the current
          image intersects the invalidated region.

       The old image is NEVER cleared first.
    */
    SelectClipRgn(dc, imagePaintRegion);

    BITMAPINFO bmi{};

    bmi.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);

    bmi.bmiHeader.biWidth =
        static_cast<LONG>(s.image.width);

    bmi.bmiHeader.biHeight =
        -static_cast<LONG>(s.image.height);

    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);

    StretchDIBits(
        dc,
        imageRect.left,
        imageRect.top,
        imageRect.right - imageRect.left,
        imageRect.bottom - imageRect.top,
        0,
        0,
        static_cast<int>(s.image.width),
        static_cast<int>(s.image.height),
        s.bgra.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);

    SelectClipRgn(dc, nullptr);

    DeleteObject(paintRegion);
    DeleteObject(imageRegion);
    DeleteObject(backgroundRegion);
    DeleteObject(imagePaintRegion);
}

static void update_title(
    HWND hwnd,
    const ViewerState& s)
{
    const wchar_t* mode =
        s.fitToWindow
            ? L"Fit"
            : L"100%";

    wchar_t title[512]{};

    swprintf_s(
        title,
        L"%s - %ux%u  %s  |  %s  |  "
        L"1 100%%  0 fit  +/- zoom  Space/LMB switch  Esc close",
        s.filename.c_str(),
        s.image.width,
        s.image.height,
        s.image.channels == 4 ? L"RGBA" : L"RGB",
        mode);

    SetWindowTextW(hwnd, title);
}


static void change_zoom(
    HWND hwnd,
    ViewerState& s,
    double newZoom)
{
    RECT oldRect{};
    get_image_rect(hwnd, s, oldRect);

    s.fitToWindow = false;
    s.zoom = std::clamp(newZoom, 0.05, 32.0);

    RECT newRect{};
    get_image_rect(hwnd, s, newRect);

    invalidate_image_change(
        hwnd,
        oldRect,
        newRect);

    update_title(hwnd, s);
}


static void set_fit_mode(
    HWND hwnd,
    ViewerState& s)
{
    RECT oldRect{};
    get_image_rect(hwnd, s, oldRect);

    s.fitToWindow = true;
    s.panX = 0;
    s.panY = 0;

    RECT newRect{};
    get_image_rect(hwnd, s, newRect);

    invalidate_image_change(
        hwnd,
        oldRect,
        newRect);

    update_title(hwnd, s);
}


static void set_100_percent(
    HWND hwnd,
    ViewerState& s)
{
    RECT oldRect{};
    get_image_rect(hwnd, s, oldRect);

    s.fitToWindow = false;
    s.zoom = 1.0;
    s.panX = 0;
    s.panY = 0;

    RECT newRect{};
    get_image_rect(hwnd, s, newRect);

    invalidate_image_change(
        hwnd,
        oldRect,
        newRect);

    update_title(hwnd, s);
}


static void toggle_view_mode(
    HWND hwnd,
    ViewerState& s)
{
    RECT oldRect{};
    get_image_rect(hwnd, s, oldRect);

    s.fitToWindow = !s.fitToWindow;

    s.panX = 0;
    s.panY = 0;

    RECT newRect{};
    get_image_rect(hwnd, s, newRect);

    invalidate_image_change(
        hwnd,
        oldRect,
        newRect);

    update_title(hwnd, s);
}


static LRESULT CALLBACK wndproc(
    HWND hwnd,
    UINT msg,
    WPARAM wp,
    LPARAM lp)
{
    ViewerState* s =
        reinterpret_cast<ViewerState*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_CREATE:
        {
            auto* cs =
                reinterpret_cast<CREATESTRUCTW*>(lp);

            s = reinterpret_cast<ViewerState*>(
                cs->lpCreateParams);

            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(s));

            s->hwnd = hwnd;

            update_title(hwnd, *s);

            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};

            HDC dc = BeginPaint(hwnd, &ps);

            if (s)
                draw_image(
                    hwnd,
                    dc,
                    *s,
                    ps.rcPaint);

            EndPaint(hwnd, &ps);

            return 0;
        }

        case WM_ERASEBKGND:
            /*
               Never erase the background.
               This is what prevents the visible
               checkerboard -> image flicker.
            */
            return 1;

        case WM_SIZE:
            /*
               Resize changes the fit geometry.
               Let Windows repaint the affected area,
               but never erase it first.
            */
            InvalidateRect(
                hwnd,
                nullptr,
                FALSE);

            return 0;

        case WM_MOUSEWHEEL:
        {
            if (!s)
                return 0;

            const int delta =
                GET_WHEEL_DELTA_WPARAM(wp);

            const double newZoom =
                delta > 0
                    ? s->zoom * 1.20
                    : s->zoom / 1.20;

            change_zoom(
                hwnd,
                *s,
                newZoom);

            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            if (!s)
                return 0;

            /*
               No mouse dragging.
               Left click toggles Fit / explicit zoom.
            */
            toggle_view_mode(hwnd, *s);

            return 0;
        }

        case WM_KEYDOWN:
        {
            if (!s)
                break;

            switch (wp)
            {
                case VK_ESCAPE:
                    DestroyWindow(hwnd);
                    return 0;

                case VK_SPACE:
                    toggle_view_mode(hwnd, *s);
                    return 0;

                case VK_HOME:
                {
                    RECT oldRect{};
                    get_image_rect(hwnd, *s, oldRect);

                    s->panX = 0;
                    s->panY = 0;

                    RECT newRect{};
                    get_image_rect(hwnd, *s, newRect);

                    invalidate_image_change(
                        hwnd,
                        oldRect,
                        newRect);

                    return 0;
                }

                case '1':
                    set_100_percent(hwnd, *s);
                    return 0;

                case '0':
                    set_fit_mode(hwnd, *s);
                    return 0;

                case VK_ADD:
                case VK_OEM_PLUS:
                    change_zoom(
                        hwnd,
                        *s,
                        s->zoom * 1.20);
                    return 0;

                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    change_zoom(
                        hwnd,
                        *s,
                        s->zoom / 1.20);
                    return 0;

                case VK_LEFT:
                {
                    RECT oldRect{};
                    get_image_rect(hwnd, *s, oldRect);

                    s->panX += 32;

                    RECT newRect{};
                    get_image_rect(hwnd, *s, newRect);

                    invalidate_image_change(
                        hwnd,
                        oldRect,
                        newRect);

                    return 0;
                }

                case VK_RIGHT:
                {
                    RECT oldRect{};
                    get_image_rect(hwnd, *s, oldRect);

                    s->panX -= 32;

                    RECT newRect{};
                    get_image_rect(hwnd, *s, newRect);

                    invalidate_image_change(
                        hwnd,
                        oldRect,
                        newRect);

                    return 0;
                }

                case VK_UP:
                {
                    RECT oldRect{};
                    get_image_rect(hwnd, *s, oldRect);

                    s->panY += 32;

                    RECT newRect{};
                    get_image_rect(hwnd, *s, newRect);

                    invalidate_image_change(
                        hwnd,
                        oldRect,
                        newRect);

                    return 0;
                }

                case VK_DOWN:
                {
                    RECT oldRect{};
                    get_image_rect(hwnd, *s, oldRect);

                    s->panY -= 32;

                    RECT newRect{};
                    get_image_rect(hwnd, *s, newRect);

                    invalidate_image_change(
                        hwnd,
                        oldRect,
                        newRect);

                    return 0;
                }
            }

            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}


static DWORD WINAPI viewer_thread(LPVOID param)
{
    std::unique_ptr<ViewerState> state(
        reinterpret_cast<ViewerState*>(param));

    g_state = state.get();

    const HINSTANCE inst =
        GetModuleHandleW(nullptr);

    WNDCLASSW wc{};

    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassW(&wc);

    RECT wa{};

    SystemParametersInfoW(
        SPI_GETWORKAREA,
        0,
        &wa,
        0);

    const int workWidth =
        wa.right - wa.left;

    const int workHeight =
        wa.bottom - wa.top;

    const int ww =
        std::min(workWidth, 1200);

    const int wh =
        std::min(workHeight, 900);

    const int x =
        wa.left + (workWidth - ww) / 2;

    const int y =
        wa.top + (workHeight - wh) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"QOI",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x,
        y,
        ww,
        wh,
        nullptr,
        nullptr,
        inst,
        state.get());

    if (!hwnd)
    {
        g_state = nullptr;
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};

    while (GetMessageW(
               &msg,
               nullptr,
               0,
               0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_state = nullptr;

    return 0;
}


/*
   Get the currently selected file from the active FAR panel.

   Current FAR API:
       FCTL_GETPANELINFO
       FCTL_GETCURRENTPANELITEM
       FCTL_GETPANELDIRECTORY

   FarPanelDirectory uses pointers to strings inside the
   caller-provided buffer. Param1 is the buffer size.
*/

static bool get_current_filename(std::wstring& path)
{
    if (!g_far.PanelControl)
        return false;

    PanelInfo pi{};

    pi.StructSize = sizeof(pi);

    if (!g_far.PanelControl(
            PANEL_ACTIVE,
            FCTL_GETPANELINFO,
            0,
            &pi))
    {
        return false;
    }

    if (pi.PanelType != PTYPE_FILEPANEL)
        return false;

    FarGetPluginPanelItem item{};

    item.StructSize = sizeof(item);

    const intptr_t required =
        g_far.PanelControl(
            PANEL_ACTIVE,
            FCTL_GETCURRENTPANELITEM,
            0,
            &item);

    if (required <= 0)
        return false;

    std::vector<unsigned char> itemBuffer(
        static_cast<size_t>(required));

    item.StructSize = sizeof(item);
    item.Size = itemBuffer.size();
    item.Item =
        reinterpret_cast<PluginPanelItem*>(
            itemBuffer.data());

    if (!g_far.PanelControl(
            PANEL_ACTIVE,
            FCTL_GETCURRENTPANELITEM,
            0,
            &item))
    {
        return false;
    }

    if (!item.Item ||
        !item.Item->FileName ||
        !item.Item->FileName[0])
    {
        return false;
    }

    const std::wstring filename =
        item.Item->FileName;

    /*
       First ask FAR for the required directory
       buffer size.
    */

    const intptr_t dirSize =
        g_far.PanelControl(
            PANEL_ACTIVE,
            FCTL_GETPANELDIRECTORY,
            0,
            nullptr);

    if (dirSize <= 0)
        return false;

    std::vector<unsigned char> dirBuffer(
        static_cast<size_t>(dirSize));

    auto* dir =
        reinterpret_cast<FarPanelDirectory*>(
            dirBuffer.data());

    dir->StructSize = sizeof(FarPanelDirectory);

    if (!g_far.PanelControl(
            PANEL_ACTIVE,
            FCTL_GETPANELDIRECTORY,
            dirSize,
            dir))
    {
        return false;
    }

    if (!dir->Name || !dir->Name[0])
        return false;

    path = dir->Name;

    if (!path.empty() &&
        path.back() != L'\\')
    {
        path.push_back(L'\\');
    }

    path += filename;

    return true;
}


static void show_error(const wchar_t* text)
{
    if (!g_far.Message)
        return;

    const wchar_t* items[] =
    {
        L"FarQoiViewer",
        L"",
        text,
        L"",
        L"OK"
    };

    g_far.Message(
        &PluginGuid,
        nullptr,
        FMSG_ERRORTYPE | FMSG_MB_OK,
        nullptr,
        items,
        5,
        1);
}


static bool show_qoi_file(const std::wstring& path)
{
    if (!has_qoi_extension(path.c_str()))
        return false;

    QoiImage image;

    if (!load_file(path.c_str(), image))
    {
        show_error(
            L"Invalid or unsupported QOI file.");

        return true;
    }

    /*
       Only one viewer window at a time.
    */

    if (g_state && g_state->hwnd)
    {
        SetForegroundWindow(g_state->hwnd);
        return true;
    }

    auto* state = new ViewerState;

    state->image = std::move(image);

    if (!prepare_bgra(*state))
    {
        delete state;

        show_error(
            L"Unable to prepare image.");

        return true;
    }

    state->filename = path;

    HANDLE thread = CreateThread(
        nullptr,
        0,
        viewer_thread,
        state,
        0,
        nullptr);

    if (!thread)
    {
        delete state;
        return true;
    }

    CloseHandle(thread);

    return true;
}


static bool show_qoi_for_current_file()
{
    std::wstring path;

    if (!get_current_filename(path))
        return false;

    return show_qoi_file(path);
}


/*
   FAR plugin initialization.
*/

extern "C" __declspec(dllexport)
void WINAPI GetGlobalInfoW(GlobalInfo* info)
{
    if (!info)
        return;

    info->StructSize = sizeof(*info);

    info->MinFarVersion = MAKEFARVERSION(
        FARMANAGERVERSION_MAJOR,
        FARMANAGERVERSION_MINOR,
        FARMANAGERVERSION_REVISION,
        FARMANAGERVERSION_BUILD,
        FARMANAGERVERSION_STAGE);

    info->Version =
        MAKEFARVERSION(
            1,
            0,
            0,
            1,
            VS_RELEASE);

    info->Guid = PluginGuid;

    info->Title =
        L"QOI Viewer";

    info->Description =
        L"Fast QOI image viewer for Far Manager";

    info->Author =
        L"QOI Viewer";

    info->Instance = nullptr;
}


extern "C" __declspec(dllexport)
void WINAPI SetStartupInfoW(
    const PluginStartupInfo* info)
{
    if (!info)
        return;

    g_far = *info;

    if (info->FSF)
        g_fsf = *info->FSF;

    g_far.FSF = &g_fsf;
}


/*
   F11 -> Plugins -> QOI Viewer
*/

extern "C" __declspec(dllexport)
void WINAPI GetPluginInfoW(
    PluginInfo* info)
{
    if (!info)
        return;

    static const wchar_t* const menuStrings[] =
    {
        L"QOI Viewer"
    };

    static const UUID menuGuids[] =
    {
        MenuGuid
    };

    info->StructSize = sizeof(*info);

    info->Flags = PF_PRELOAD;

    info->DiskMenu.Guids = nullptr;
    info->DiskMenu.Strings = nullptr;
    info->DiskMenu.Count = 0;

    info->PluginMenu.Guids = menuGuids;
    info->PluginMenu.Strings = menuStrings;
    info->PluginMenu.Count = 1;

    info->PluginConfig.Guids = nullptr;
    info->PluginConfig.Strings = nullptr;
    info->PluginConfig.Count = 0;

    info->CommandPrefix = L"qoi";

    info->Instance = nullptr;
}


extern "C" __declspec(dllexport)
HANDLE WINAPI OpenW(const OpenInfo* info)
{
    if (!info)
        return nullptr;

    if (!info->Guid ||
        *info->Guid != MenuGuid)
    {
        return nullptr;
    }

    show_qoi_for_current_file();

    return nullptr;
}


extern "C" __declspec(dllexport)
intptr_t WINAPI ProcessConsoleInputW(
    ProcessConsoleInputInfo* info)
{
    if (!info)
        return 0;

    if (info->StructSize <
        sizeof(ProcessConsoleInputInfo))
    {
        return 0;
    }

    if (info->Rec.EventType != KEY_EVENT)
        return 0;

    const KEY_EVENT_RECORD& key =
        info->Rec.Event.KeyEvent;

    if (!key.bKeyDown)
        return 0;

    if (key.wVirtualKeyCode != VK_F3)
        return 0;

    if (key.dwControlKeyState &
        (LEFT_ALT_PRESSED |
         RIGHT_ALT_PRESSED |
         LEFT_CTRL_PRESSED |
         RIGHT_CTRL_PRESSED |
         SHIFT_PRESSED))
    {
        return 0;
    }

    std::wstring path;

    if (!get_current_filename(path))
        return 0;

    if (!has_qoi_extension(path.c_str()))
        return 0;

    show_qoi_file(path);

    return 1;
}


extern "C" __declspec(dllexport)
void WINAPI ExitFARW(
    const ExitInfo* /*info*/)
{
    /*
       The viewer owns its own thread/window.
       If FAR exits, Windows will terminate the
       plugin process anyway.
    */
}