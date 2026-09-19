#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>
#include "plugin.hpp"
#include "qoi.h"

static PluginStartupInfo g_far{};
static FarStandardFunctions g_fsf{};

static const GUID PluginGuid =
{ 0x6a6d1e35, 0x7c7f, 0x4b7b, { 0x9f, 0x4b, 0x2e, 0x0d, 0x1d, 0x6f, 0x52, 0x91 } };

static const wchar_t* const kClassName = L"FarQoiViewerWindow";

struct ViewerState
{
    QoiImage image;
    std::wstring filename;
    HWND hwnd = nullptr;
    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    bool dragging = false;
    POINT dragStart{};
    double dragPanX = 0;
    double dragPanY = 0;
};

static ViewerState* g_state = nullptr;

static bool has_qoi_extension(const wchar_t* name)
{
    if (!name) return false;
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    return _wcsicmp(dot, L".qoi") == 0;
}

static bool load_file(const wchar_t* name, QoiImage& image)
{
    HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                           FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 ||
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
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - done, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(h, data.data() + done, chunk, &got, nullptr) || got == 0)
        {
            CloseHandle(h);
            return false;
        }
        done += got;
    }

    CloseHandle(h);
    return qoi_decode(data.data(), data.size(), image);
}

static void checker(HDC dc, const RECT& r)
{
    const int s = 12;
    for (int y = r.top; y < r.bottom; y += s)
    {
        for (int x = r.left; x < r.right; x += s)
        {
RECT q{
    x,
    y,
    static_cast<int>(std::min<LONG>(x + s, r.right)),
    static_cast<int>(std::min<LONG>(y + s, r.bottom))
};
            HBRUSH b = CreateSolidBrush(((x / s + y / s) & 1) ? RGB(220,220,220) : RGB(245,245,245));
            FillRect(dc, &q, b);
            DeleteObject(b);
        }
    }
}

static void draw_image(HWND hwnd, HDC dc, ViewerState& s)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    checker(dc, rc);

    if (s.image.width == 0 || s.image.height == 0)
        return;

    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;

    double fit = std::min(
        double(cw) / double(s.image.width),
        double(ch) / double(s.image.height));

    if (fit <= 0) return;

    double scale = fit * s.zoom;
    if (s.zoom == 0) scale = fit;

    const int dw = std::max(1, int(std::lround(s.image.width * scale)));
    const int dh = std::max(1, int(std::lround(s.image.height * scale)));

    const int x = int(std::lround((cw - dw) * 0.5 + s.panX));
    const int y = int(std::lround((ch - dh) * 0.5 + s.panY));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(s.image.width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(s.image.height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // QOI is RGB(A), while DIB expects BGR(A).
    std::vector<uint8_t> bgra(s.image.rgba.size());
    for (size_t i = 0; i < bgra.size(); i += 4)
    {
        const uint8_t r = s.image.rgba[i + 0];
        const uint8_t g = s.image.rgba[i + 1];
        const uint8_t b = s.image.rgba[i + 2];
        const uint8_t a = s.image.rgba[i + 3];

        if (a == 255)
        {
            bgra[i + 0] = b;
            bgra[i + 1] = g;
            bgra[i + 2] = r;
            bgra[i + 3] = 255;
        }
        else
        {
            // Composite against the same checkerboard in image space.
            const int px = int(i / 4) % int(s.image.width);
            const int py = int(i / 4) / int(s.image.width);
            const int cell = ((px / 12 + py / 12) & 1) ? 220 : 245;

            bgra[i + 0] = uint8_t((b * a + cell * (255 - a)) / 255);
            bgra[i + 1] = uint8_t((g * a + cell * (255 - a)) / 255);
            bgra[i + 2] = uint8_t((r * a + cell * (255 - a)) / 255);
            bgra[i + 3] = 255;
        }
    }

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);

    StretchDIBits(dc,
        x, y, dw, dh,
        0, 0, static_cast<int>(s.image.width), static_cast<int>(s.image.height),
        bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static void update_title(HWND hwnd, const ViewerState& s)
{
    wchar_t title[512];
    swprintf_s(title, L"%s  —  %ux%u  %s  |  1 fit  0 100%%  +/- zoom  Esc close",
               s.filename.c_str(),
               s.image.width, s.image.height,
               s.image.channels == 4 ? L"RGBA" : L"RGB");
    SetWindowTextW(hwnd, title);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ViewerState* s = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_CREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            s = reinterpret_cast<ViewerState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            s->hwnd = hwnd;
            update_title(hwnd, *s);
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            if (s) draw_image(hwnd, dc, *s);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_MOUSEWHEEL:
            if (s)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                s->zoom *= delta > 0 ? 1.20 : 1.0 / 1.20;
                s->zoom = std::clamp(s->zoom, 0.05, 32.0);
                update_title(hwnd, *s);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (s)
            {
                s->dragging = true;
                s->dragStart.x = GET_X_LPARAM(lp);
                s->dragStart.y = GET_Y_LPARAM(lp);
                s->dragPanX = s->panX;
                s->dragPanY = s->panY;
                SetCapture(hwnd);
            }
            return 0;

        case WM_LBUTTONUP:
            if (s)
            {
                s->dragging = false;
                ReleaseCapture();
            }
            return 0;

        case WM_MOUSEMOVE:
            if (s && s->dragging)
            {
                s->panX = s->dragPanX + GET_X_LPARAM(lp) - s->dragStart.x;
                s->panY = s->dragPanY + GET_Y_LPARAM(lp) - s->dragStart.y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
            if (!s) break;

            switch (wp)
            {
                case VK_ESCAPE:
                    DestroyWindow(hwnd);
                    return 0;

                case VK_HOME:
                    s->panX = s->panY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;

                case '1':
                    s->zoom = 1.0;
                    s->panX = s->panY = 0;
                    update_title(hwnd, *s);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;

                case '0':
                    s->zoom = 1.0;
                    s->panX = s->panY = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;

                case VK_ADD:
                case VK_OEM_PLUS:
                    s->zoom = std::min(32.0, s->zoom * 1.20);
                    update_title(hwnd, *s);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;

                case VK_SUBTRACT:
                case VK_OEM_MINUS:
                    s->zoom = std::max(0.05, s->zoom / 1.20);
                    update_title(hwnd, *s);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;

                case VK_LEFT:  s->panX += 32; InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_RIGHT: s->panX -= 32; InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_UP:    s->panY += 32; InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_DOWN:  s->panY -= 32; InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            break;

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
    std::unique_ptr<ViewerState> state(reinterpret_cast<ViewerState*>(param));
    g_state = state.get();

    HINSTANCE inst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassW(&wc);

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

const int ww = static_cast<int>(std::min<LONG>(wa.right - wa.left, 1200));
const int wh = static_cast<int>(std::min<LONG>(wa.bottom - wa.top, 900));
    const int x = wa.left + ((wa.right - wa.left) - ww) / 2;
    const int y = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"QOI",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, ww, wh,
        nullptr, nullptr, inst, state.get());

    if (!hwnd)
    {
        g_state = nullptr;
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_state = nullptr;
    return 0;
}

static bool get_current_filename(std::wstring& path)
{
    PanelInfo pi{};
    pi.StructSize = sizeof(pi);

    if (!g_far.Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, &pi))
        return false;

    if (pi.PanelType != PTYPE_FILEPANEL || pi.CurrentItem < 0)
        return false;

    FarGetPluginPanelItem item{};
    item.StructSize = sizeof(item);

    const size_t size = static_cast<size_t>(
        g_far.Control(PANEL_ACTIVE, FCTL_GETPANELITEM, pi.CurrentItem, &item));

    if (!size)
        return false;

    std::vector<unsigned char> buffer(size);
    item.Item = reinterpret_cast<PluginPanelItem*>(buffer.data());

    if (!g_far.Control(PANEL_ACTIVE, FCTL_GETPANELITEM, pi.CurrentItem, &item))
        return false;

    if (!item.Item || !item.Item->FileName || !item.Item->FileName[0])
        return false;

    std::wstring name(item.Item->FileName);

    // Current panel directory.
    FarPanelDirectory dir{};
    dir.StructSize = sizeof(dir);

    // Ask Far for the required buffer size.
    if (!g_far.Control(PANEL_ACTIVE, FCTL_GETPANELDIR, 0, &dir))
        return false;

    if (!dir.NameSize)
        return false;

    std::vector<wchar_t> nameBuffer(dir.NameSize + 1);
    dir.Name = nameBuffer.data();

    if (!g_far.Control(PANEL_ACTIVE, FCTL_GETPANELDIR, 0, &dir))
        return false;

    path = nameBuffer.data();
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path += name;
    return true;
}

static void show_qoi_for_current_file()
{
    std::wstring path;
    if (!get_current_filename(path) || !has_qoi_extension(path.c_str()))
        return;

    QoiImage image;
    if (!load_file(path.c_str(), image))
    {
        const wchar_t* items[] =
        {
            L"FarQoiViewer",
            L"",
            L"Invalid or unsupported QOI file.",
            L"",
            L"OK"
        };

        g_far.Message(&PluginGuid, nullptr, FMSG_ERRORTYPE | FMSG_MB_OK,
                      nullptr, items, 5, 1);
        return;
    }

    // Only one viewer at a time.
    if (g_state && g_state->hwnd)
    {
        SetForegroundWindow(g_state->hwnd);
        return;
    }

    auto* state = new ViewerState;
    state->image = std::move(image);
    state->filename = path;

    HANDLE thread = CreateThread(nullptr, 0, viewer_thread, state, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        delete state;
}

extern "C" __declspec(dllexport)
void WINAPI GetGlobalInfoW(GlobalInfo* info)
{
    info->MinFarVersion = MAKEFARVERSION(
        FARMANAGERVERSION_MAJOR,
        FARMANAGERVERSION_MINOR,
        FARMANAGERVERSION_REVISION,
        FARMANAGERVERSION_BUILD,
        FARMANAGERVERSION_STAGE);

    info->Version = MAKEFARVERSION(1, 0, 0, 1, VS_RELEASE);
    info->Guid = PluginGuid;
    info->Title = L"QOI Viewer";
    info->Description = L"Fast QOI image viewer for Far Manager";
    info->Author = L"QOI Viewer";
}

extern "C" __declspec(dllexport)
void WINAPI SetStartupInfoW(const PluginStartupInfo* info)
{
    g_far = *info;
    if (info->FSF)
        g_fsf = *info->FSF;
    g_far.FSF = &g_fsf;
}

extern "C" __declspec(dllexport)
void WINAPI GetPluginInfoW(PluginInfo* info)
{
    info->Flags = PF_PRELOAD;
}

extern "C" __declspec(dllexport)
intptr_t WINAPI ProcessConsoleInputW(ProcessConsoleInputInfo* info)
{
    if (!info || info->StructSize < sizeof(ProcessConsoleInputInfo))
        return 0;

    if (info->Rec.EventType != KEY_EVENT)
        return 0;

    const KEY_EVENT_RECORD& k = info->Rec.Event.KeyEvent;
    if (!k.bKeyDown || k.wVirtualKeyCode != VK_F3)
        return 0;

    // Do not steal modified F3.
    if (k.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED |
                               LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED |
                               SHIFT_PRESSED))
        return 0;

    std::wstring path;
    if (!get_current_filename(path) || !has_qoi_extension(path.c_str()))
        return 0;

    show_qoi_for_current_file();
    return 1;
}

extern "C" __declspec(dllexport)
void WINAPI ExitFARW(const ExitInfo*)
{
}
