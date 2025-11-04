/***************************************************************************************\
*   File:                                                                               *
*       arraypicture.h                                                                  *
*                                                                                       *
*   Abstract:                                                                           *
*       ArrayPicture – pure WinAPI custom control                                       *
*                                                                                       *
*   Author:                                                                             *
*       GShabanov ()    15-Mar-2025                                                     *
*                                                                                       *
*   Revision History:                                                                   *
*        port to WinAPI: 2025-11-03                                                     *
\***************************************************************************************/
#pragma once

#include <vector>

class ArrayPicture
{
public:
    // callback prototype (optional)
    typedef void (WINAPI *ARRAYPICTURE_NOTIFY)(
        HWND from,            // control HWND
        const POINT* cell,    // cell (x,y) in coordinates
        void* user            // user context
        );

    // init parameters
    struct ARRAYPICTURE_INIT
    {
        UINT cx = 64;                 // width of cage (in cells)
        UINT cy = 64;                 // height of cage (in cells)
        int  markerSize = 1;          // 1 – dot, 2 – cross, 3 – «sector» 3x3
        LONG granularity = 6;       // granularity
        ARRAYPICTURE_NOTIFY notify = NULL;
        void* notifyUser = NULL;
        HCURSOR drawCursor = NULL; // cursor for drawing (or NULL — system)
        HFONT   symbolFont = NULL;
        COLORREF symbolColor = RGB(0, 0, 0);
    };

    struct ARRAYPICTURE_CELL
    {
        COLORREF  color {RGB(0xFF, 0xFF, 0xFF)};
        WCHAR     symbol{ 0 };
        PVOID     data { NULL };
    };

private:

    static HINSTANCE s_hInstance;


    UINT m_cx;
    UINT m_cy;
    int  m_markerSize;
    LONG m_granularity;           // cell = (granularity + 1) px

    HFONT   m_hSymbolFont = nullptr;
    COLORREF m_symbolColor = RGB(0, 0, 0);

    HCURSOR m_hDrawCursor;
    HCURSOR m_hPrevCursor;
    BOOL    m_LButtonPressed;

    ARRAYPICTURE_NOTIFY m_notify;
    void*               m_notifyUser;

    // double buffering
    HDC      m_hMemDC;
    HBITMAP  m_hMemBmp;
    HBITMAP  m_hOldBmp;

    HWND     m_hWnd;
    HWND     m_parent;

    // data
    std::vector<std::vector<ARRAYPICTURE_CELL>> m_input; // [y][x]

public:
    // register Wnd class (call once on process)
    static BOOL registerClass(HINSTANCE hInstance);

    // properties
    void setGranularity(LONG gran); // set granularity: (gran+1)
    void setInputArray(const std::vector<std::vector<ARRAYPICTURE_CELL>>& input);
    void clear();
    void setCursor(HCURSOR hCursor);
    void setNotify(ARRAYPICTURE_NOTIFY cb, void* user);

    BOOL GetCell(UINT x, UINT y, ARRAYPICTURE_CELL* outCell);
    BOOL SetCell(UINT x, UINT y, const ARRAYPICTURE_CELL* cell);
    void SetSymbolFont(HFONT hFont);
    void SetSymbolColor(COLORREF rgb);


    // parameters access
    int  getWidth() const;   // in cells
    int  getHeight() const;  // in cells

    // serialization to leaner massive (row-major)
    std::vector<ARRAYPICTURE_CELL> Serialize();

    ArrayPicture();
    ~ArrayPicture();

    ArrayPicture(const ArrayPicture&) = delete;
    ArrayPicture& operator=(const ArrayPicture&) = delete;

    ArrayPicture(ArrayPicture&& other) noexcept;
    ArrayPicture& operator=(ArrayPicture&& other) noexcept;

    // create the control
    bool create(HWND parent, RECT *rect, const ARRAYPICTURE_INIT *s = NULL);

    // attach to exiting window HWND
    bool attach(HWND hWnd, const ARRAYPICTURE_INIT *s = NULL);

    // detach from window
    HWND detach() noexcept;

    // destroy window
    void destroy() noexcept;

    // accessors
    HWND hwnd() const noexcept { 
        return m_hWnd;
    }

    explicit operator bool() const noexcept { return m_hWnd != nullptr; }

    int width()  const { return m_hWnd ? getWidth() : 0; }
    int height() const { return m_hWnd ? getHeight() : 0; }

private:
    // creating control
    HWND CreateWnd(HWND hParent, RECT& rect);

    // Wnd class name
    static const LPCTSTR ARRAYPICTURE_WNDCLASS;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT OnCreate(LPARAM lParam);
    LRESULT OnDestroy(LPARAM lParam);
    LRESULT OnSize(LPARAM lParam);
    LRESULT OnPaint(LPARAM lParam);
    LRESULT OnLMouseDown(LPARAM lParam);
    LRESULT OnLMouseUp(LPARAM lParam);
    LRESULT OnMouseMove(LPARAM lParam);
    LRESULT OnMouseLeave(LPARAM lParam);
    void OnDraw(HDC cdc);
    void CreateBackBuffer();
    void FreeBackBuffer();
    void MarkPosition(int x, int y);
    LRESULT WinPosChanging(LPARAM lParam);

    void moveFrom(ArrayPicture&& o) noexcept;
};
