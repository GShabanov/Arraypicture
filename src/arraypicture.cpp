#include <windows.h>
#include <windowsx.h>

#include <tchar.h>
#include <assert.h>

#include "arraypicture.h"

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif


// using colors for MFC-version
#define  CHART_BACKGROUND_COLOR     RGB(0x20, 0x20, 0x20)
#define  CHART_GRID_COLOR           RGB(0x50, 0x50, 0x50)

HINSTANCE ArrayPicture::s_hInstance = nullptr;

const LPCTSTR
ArrayPicture::ARRAYPICTURE_WNDCLASS = _T("ArrayPicture_WinAPI");

void
ArrayPicture::FreeBackBuffer()
{
    if (m_hMemDC)
    {
        if (m_hOldBmp)
        {
            SelectObject(m_hMemDC, m_hOldBmp);
            m_hOldBmp = NULL;
        }

        if (m_hMemBmp)
        {
            DeleteObject(m_hMemBmp);
            m_hMemBmp = NULL;
        }

        DeleteDC(m_hMemDC);

        m_hMemDC = NULL;
    }
}

void
ArrayPicture::CreateBackBuffer()
{
    RECT rc{};
    GetClientRect(m_hWnd, &rc);

    const int w = max(1L, rc.right - rc.left);
    const int h = max(1L, rc.bottom - rc.top);

    HDC hDC = GetDC(m_hWnd);

    m_hMemDC = CreateCompatibleDC(hDC);
    m_hMemBmp = CreateCompatibleBitmap(hDC, w, h);
    m_hOldBmp = (HBITMAP)SelectObject(m_hMemDC, m_hMemBmp);

    ReleaseDC(m_hWnd, hDC);
}

void
ArrayPicture::MarkPosition(int x, int y)
{
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    return;

    /*if (x < 0 || y < 0 || (UINT)x >= m_cx || (UINT)y >= m_cy)
        return;

    auto setblack = [&](int X, int Y)
        {
            if (X >= 0 && Y >= 0 && (UINT)X < m_cx && (UINT)Y < m_cy)
                m_input[(size_t)Y][(size_t)X] = RGB(0x00, 0x00, 0x00);
        };

    setblack(x, y);

    if (m_markerSize > 1)
    {
        setblack(x - 1, y);
        setblack(x + 1, y);
        setblack(x, y - 1);
        setblack(x, y + 1);

        if (m_markerSize > 2)
        {
            setblack(x - 1, y - 1);
            setblack(x + 1, y - 1);
            setblack(x - 1, y + 1);
            setblack(x + 1, y + 1);
        }
    }*/
}

ArrayPicture::~ArrayPicture()
{
    destroy(); 
}

ArrayPicture::ArrayPicture()
{
    m_cx = 64;
    m_cy = 64;
    m_markerSize = 1;
    m_granularity = 4;           // cell = (granularity + 1) px

    m_hDrawCursor = NULL;
    m_hPrevCursor = NULL;
    m_LButtonPressed = FALSE;

    m_notify = NULL;
    m_notifyUser = NULL;

    // double buffering
    m_hMemDC = NULL;
    m_hMemBmp = NULL;
    m_hOldBmp = NULL;

    m_hWnd = NULL;
    m_parent = NULL;

}

void
ArrayPicture::destroy() noexcept
{

    if (m_hWnd && ::IsWindow(m_hWnd))
    {
        // if child window destroy correctly
        ::DestroyWindow(m_hWnd);
    }

    m_hWnd = NULL;
    m_parent = NULL;
    //m_settings = {};
}

bool
ArrayPicture::create(HWND parent, RECT *rect, const ARRAYPICTURE_INIT *s)
{
    if (m_hWnd)
        return true;

    //m_settings = s;
    m_parent = parent;

    if (s)
    {
        this->m_cx = s->cx;
        this->m_cy = s->cy;
        this->m_markerSize = s->markerSize;
        this->m_hDrawCursor = s->drawCursor;
        this->m_notify = s->notify;
        this->m_notifyUser = s->notifyUser;
        this->m_granularity = s->granularity;

        this->m_hSymbolFont = s->symbolFont;
        this->m_symbolColor = s->symbolColor;

    }

    m_input.resize(m_cy);
    for (auto& row : m_input)
    {
        ARRAYPICTURE_CELL  empty;

        row.resize(m_cx, empty);
    }

    RECT winRect;

    if (rect != NULL)
    {
        winRect = *rect;
    }
    else
    {

        winRect.left = 0;
        winRect.top = 0;
        winRect.right = m_cx * (m_granularity + 1) + 2;
        winRect.bottom = m_cy * (m_granularity + 1) + 2;
    }


    m_hWnd = CreateWnd(parent, winRect);

    

    return m_hWnd != nullptr;
}



HWND
ArrayPicture::detach() noexcept
{
    HWND hWindow = m_hWnd;
    if (m_hWnd) {

        setNotify(NULL, NULL);
        m_hWnd = NULL;
    }

    m_parent = NULL;
    //m_settings = {};

    return hWindow;
}

void
ArrayPicture::moveFrom(ArrayPicture&& o) noexcept
{
    m_hWnd = o.m_hWnd;
    o.m_hWnd = NULL;
    m_parent = o.m_parent;
    o.m_parent = NULL;

    m_cx = o.m_cx;
    m_cy = o.m_cy;

    m_markerSize = o.m_markerSize;
    m_granularity = o.m_granularity;

    m_hDrawCursor = o.m_hDrawCursor;
    m_hPrevCursor = o.m_hPrevCursor;
    m_LButtonPressed = o.m_LButtonPressed;

    //m_settings = std::move(o.m_settings);

    // reset context for native
    if (m_hWnd)
    {
        setNotify(o.m_notify, o.m_notifyUser);
    }
}

bool
ArrayPicture::attach(HWND hWnd, const ArrayPicture::ARRAYPICTURE_INIT *s)
{
    if (!hWnd)
        return false;

    m_hWnd = hWnd;

    if (s)
    {
        setNotify(s->notify, s->notifyUser);

        if (s->drawCursor)
            setCursor(s->drawCursor);
    }

    return true;
}


void
ArrayPicture::OnDraw(HDC cdc)
{
    RECT rc;
    
    GetClientRect(m_hWnd, &rc);


    //const int w = rc.right - rc.left;
    //const int h = rc.bottom - rc.top;

    HBRUSH hBack = CreateSolidBrush(CHART_BACKGROUND_COLOR);
    FillRect(cdc, &rc, hBack);
    DeleteObject(hBack);

    HPEN hGrid = CreatePen(PS_SOLID, 1, CHART_GRID_COLOR);
    HGDIOBJ oldPen = SelectObject(cdc, hGrid);

    const int step = m_granularity + 1;
    const int maxx = (int)m_cx * step;
    const int maxy = (int)m_cy * step;

    // grid + cleanup
    for (UINT y = 0; y < m_cy; ++y)
    {

        for (UINT x = 0; x < m_cx; ++x)
        {
            // vertical line
            MoveToEx(cdc, (int)x * step, 0, nullptr);
            LineTo(cdc, (int)x * step, maxy);

            // cell (x,y)
            RECT cell{
                (int)x * step + 1,
                (int)y * step + 1,
                (int)x * step + step + 1,
                (int)y * step + step + 1
            };

            ARRAYPICTURE_CELL inputcell = m_input[y][x];

            HBRUSH hb = CreateSolidBrush(inputcell.color);

            FillRect(cdc, &cell, hb);
            DeleteObject(hb);

            // 2) if symbol present
            if (inputcell.symbol)
            {
                HFONT hOldF = nullptr;

                if (this->m_hSymbolFont)
                    hOldF = (HFONT)SelectObject(cdc, this->m_hSymbolFont);

                SetBkMode(cdc, TRANSPARENT);
                COLORREF old = SetTextColor(cdc, this->m_symbolColor);

                wchar_t wch[2] = { inputcell.symbol, 0 };

                // centering
                //int cxTxt = step, cyTxt = step;
                RECT txtRc = cell;
                DrawTextW(cdc, wch, 1, &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SetTextColor(cdc, old);

                if (hOldF)
                    SelectObject(cdc, hOldF);
            }
        }

        // horizontal line
        MoveToEx(cdc, 0, (int)y * step, nullptr);
        LineTo(cdc, maxx, (int)y * step);

    }

    SelectObject(cdc, oldPen);
    DeleteObject(hGrid);
}

LRESULT
ArrayPicture::WinPosChanging(LPARAM lParam)
{
    // min dimension
    WINDOWPOS* wp = (WINDOWPOS*)lParam;
    const int minW = (int)m_cx * (m_granularity + 1);
    const int minH = (int)m_cy * (m_granularity + 1);
    wp->cx = max(wp->cx, minW);
    wp->cy = max(wp->cy, minH);
    return 0;
}

LRESULT
ArrayPicture::OnLMouseDown(LPARAM lParam)
{
    const int x = GET_X_LPARAM(lParam) / (this->m_granularity + 1);
    const int y = GET_Y_LPARAM(lParam) / (this->m_granularity + 1);

    if ((UINT)y < this->m_cy && (UINT)x < this->m_cx)
    {
        this->MarkPosition(x, y);
        this->m_LButtonPressed = TRUE;

        SetCapture(this->m_hWnd);
        SetCursor(this->m_hDrawCursor);

        if (this->m_notify)
        {
            POINT pt{ x, y };
            this->m_notify(this->m_hWnd, &pt, this->m_notifyUser);
        }

        InvalidateRect(this->m_hWnd, nullptr, FALSE);
    }

    return 0;
}

LRESULT
ArrayPicture::OnLMouseUp(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    this->m_LButtonPressed = FALSE;
    ReleaseCapture();
    SetCursor(this->m_hDrawCursor);

    return 0;
}

LRESULT
ArrayPicture::OnMouseMove(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, this->m_hWnd, HOVER_DEFAULT };
    TrackMouseEvent(&tme);

    SetCursor(this->m_hDrawCursor);

    if (this->m_LButtonPressed)
    {
        const int x = GET_X_LPARAM(lParam) / (this->m_granularity + 1);
        const int y = GET_Y_LPARAM(lParam) / (this->m_granularity + 1);

        if ((UINT)y < this->m_cy && (UINT)x < this->m_cx)
        {
            this->MarkPosition(x, y);

            if (this->m_notify)
            {
                POINT pt{ x, y };
                this->m_notify(this->m_hWnd, &pt, this->m_notifyUser);
            }

            InvalidateRect(this->m_hWnd, nullptr, FALSE);
        }
    }
    return 0;
}

LRESULT
ArrayPicture::OnMouseLeave(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    this->m_LButtonPressed = FALSE;
    SetCursor(this->m_hPrevCursor);
    return 0;

}

LRESULT
ArrayPicture::OnCreate(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    this->CreateBackBuffer();

    this->m_hPrevCursor = LoadCursor(NULL, IDC_ARROW);

    if (!this->m_hDrawCursor)
        this->m_hDrawCursor = this->m_hPrevCursor;

    return 0;
}

LRESULT
ArrayPicture::OnDestroy(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    this->FreeBackBuffer();
    SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
    return 0;

}

LRESULT
ArrayPicture::OnSize(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    this->FreeBackBuffer();
    this->CreateBackBuffer();
    return 0;
}

LRESULT
ArrayPicture::OnPaint(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(this->m_hWnd, &ps);

    if (this->m_hMemDC)
    {
        // 1) draw to memory
        this->OnDraw(this->m_hMemDC);
        // 2) blit to screen
        RECT rc{};
        GetClientRect(m_hWnd, &rc);
        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, this->m_hMemDC, 0, 0, SRCCOPY);
    }

    EndPaint(m_hWnd, &ps);
    return 0;

}

LRESULT CALLBACK
ArrayPicture::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ArrayPicture* lpThis = NULL;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);

        lpThis = reinterpret_cast<ArrayPicture*>(cs->lpCreateParams);

        lpThis->m_hWnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpThis);
        return TRUE;
    }

    lpThis = reinterpret_cast<ArrayPicture *>(
        GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (!lpThis)
    {
        return TRUE;
    }

    switch (msg)
    {

    case WM_CREATE:
        return lpThis->OnCreate(lParam);

    case WM_SIZE:
        return lpThis->OnSize(lParam);

    case WM_WINDOWPOSCHANGING:
        return lpThis->WinPosChanging(lParam);

    case WM_LBUTTONDOWN:
        return lpThis->OnLMouseDown(lParam);

    case WM_LBUTTONUP:
        return lpThis->OnLMouseUp(lParam);

    case WM_MOUSEMOVE:
        return lpThis->OnMouseMove(lParam);

    case WM_MOUSELEAVE:
        return lpThis->OnMouseLeave(lParam);

    case WM_ERASEBKGND:
        // own redraw
        return 1;

    case WM_PAINT:
        return lpThis->OnPaint(lParam);

    case WM_DESTROY:
        return lpThis->OnDestroy(lParam);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

BOOL
ArrayPicture::registerClass(HINSTANCE hInstance)
{
    s_hInstance = hInstance;

    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ArrayPicture::WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = ArrayPicture::ARRAYPICTURE_WNDCLASS;

    return RegisterClassEx(&wc) != 0;
}

HWND
ArrayPicture::CreateWnd(HWND hParent, RECT& rect)
{
    HWND hWnd = CreateWindowEx(
        0,
        ArrayPicture::ARRAYPICTURE_WNDCLASS,
        _T(""),
        WS_CHILD | WS_VISIBLE,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        hParent,
        nullptr,
        (HINSTANCE)s_hInstance,
        (LPVOID)this
    );

    if (hWnd == NULL)
    {
        DWORD dwError = GetLastError();

        dwError = dwError;

    }

    return hWnd;
}

BOOL
ArrayPicture::GetCell(UINT x, UINT y, ARRAYPICTURE_CELL* outCell)
{
    if (x < this->m_cx && y < this->m_cy && outCell)
    {
        *outCell = this->m_input[y][x];
        return TRUE;
    }

    return FALSE;
}

BOOL
ArrayPicture::SetCell(UINT x, UINT y, const ARRAYPICTURE_CELL* cell)
{
    if (x < this->m_cx && y < this->m_cy && cell)
    {
        this->m_input[y][x] = *cell;
        return TRUE;
    }
    return FALSE;
}

void
ArrayPicture::SetSymbolFont(HFONT hFont)
{
    this->m_hSymbolFont = hFont;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}
void
ArrayPicture::SetSymbolColor(COLORREF rgb)
{
    this->m_symbolColor = rgb;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}
void
ArrayPicture::setGranularity(LONG gran)
{
    m_granularity = std::max<LONG>(0, gran);
    InvalidateRect(m_hWnd, NULL, TRUE);
}

void
ArrayPicture::setInputArray(const std::vector<std::vector<ARRAYPICTURE_CELL>>& input)
{
    if (input.empty() || input[0].empty())
        return;

    m_cy = (UINT)input.size();
    m_cx = (UINT)input[0].size();
    m_input = input;

    RECT rc{};
    
    GetWindowRect(m_hWnd, &rc);

    //const int minW = (int)m_cx * (m_granularity + 1);
    //const int minH = (int)m_cy * (m_granularity + 1);

    InvalidateRect(m_hWnd, NULL, TRUE);
}

void
ArrayPicture::clear()
{
    ARRAYPICTURE_CELL  emptycell;

    m_input.resize(m_cy);

    for (auto& row : m_input)
    {

        row.resize(m_cx, emptycell);
    }


    for (auto& row : m_input)
        std::fill(row.begin(), row.end(), emptycell);

    InvalidateRect(m_hWnd, NULL, TRUE);
}

void
ArrayPicture::setCursor(HCURSOR hCursor)
{
    m_hDrawCursor = hCursor ? hCursor : LoadCursor(nullptr, IDC_ARROW);
    SetCursor(m_hDrawCursor);
}

void
ArrayPicture::setNotify(ARRAYPICTURE_NOTIFY cb, void* user)
{
    this->m_notify = cb;
    this->m_notifyUser = user;
}

int ArrayPicture::getWidth() const
{
    return m_cx;
}

int
ArrayPicture::getHeight() const
{
    return m_cy;
}

std::vector<ArrayPicture::ARRAYPICTURE_CELL>
ArrayPicture::Serialize()
{
    std::vector<ARRAYPICTURE_CELL> out;

    out.reserve((size_t)this->m_cx * (size_t)this->m_cy);

    for (UINT y = 0; y < this->m_cy; ++y)
    {
        for (UINT x = 0; x < this->m_cx; ++x)
        {
            out.push_back(this->m_input[y][x]);
        }
    }

    return out;
}


ArrayPicture::ArrayPicture(ArrayPicture&& other) noexcept
{
    moveFrom(std::move(other));
}

ArrayPicture&
ArrayPicture::operator=(ArrayPicture&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        moveFrom(std::move(other));
    }

    return *this;
}
