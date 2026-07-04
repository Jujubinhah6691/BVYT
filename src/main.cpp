#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wininet.h>
#include <string>
#include <sstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>

#include "resource.h" // Importando os IDs!

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// IDs da Janela Principal
#define IDC_URL_EDIT        101
#define IDC_RADIO_VIDEO     102
#define IDC_RADIO_VIDEO_ONLY 103
#define IDC_RADIO_AUDIO     104
#define IDC_DOWNLOAD_BTN    105
#define IDC_STATUS_LABEL    106
#define IDC_PROGRESS        107
#define IDC_OPEN_FOLDER_BTN 108
#define IDC_PASTE_BTN       109

// Variáveis Globais
HINSTANCE g_hInstance = nullptr;
HWND g_hWnd = nullptr;
HWND g_hUrlEdit = nullptr;
HWND g_hRadioVideo = nullptr;
HWND g_hRadioVideoOnly = nullptr;
HWND g_hRadioAudio = nullptr;
HWND g_hDownloadBtn = nullptr;
HWND g_hStatusLabel = nullptr;
HWND g_hProgress = nullptr;
HWND g_hOpenFolderBtn = nullptr;
HWND g_hPasteBtn = nullptr;

std::wstring g_pendingUrl = L"";

std::atomic<bool> g_downloading(false);
std::wstring g_downloadsPath;
std::wstring g_ytdlpPath;

// Modo de Seleção
int g_selectedMode = 0; // 0: Video+Audio, 1: VideoOnly, 2: AudioOnly

// Ponteiro para o procedimento original da EDIT
WNDPROC g_OrigEditProc = nullptr;

// Estilização
HBRUSH g_hBkgBrush = nullptr;
HBRUSH g_hPanelBrush = nullptr;
HFONT  g_hFontMain = nullptr;
HFONT  g_hFontTitle = nullptr;
HFONT  g_hFontSmall = nullptr;

const COLORREF CLR_BG = RGB(15, 15, 20);
const COLORREF CLR_PANEL = RGB(22, 22, 30);
const COLORREF CLR_ACCENT = RGB(255, 50, 50);
const COLORREF CLR_TEXT = RGB(240, 240, 240);
const COLORREF CLR_TEXT_DIM = RGB(140, 140, 155);
const COLORREF CLR_EDIT_BG = RGB(28, 28, 38);
const COLORREF CLR_BORDER = RGB(50, 50, 65);

// Fix da seleção (Ctrl+A)
LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(hWnd, EM_SETSEL, 0, -1);
        return 0;
    }
    return CallWindowProcW(g_OrigEditProc, hWnd, uMsg, wParam, lParam);
}

// Helpers
std::wstring GetDownloadsFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, path))) {
        std::wstring base(path);
        size_t pos = base.rfind(L'\\');
        if (pos != std::wstring::npos) {
            std::wstring downloads = base.substr(0, pos) + L"\\Downloads";
            if (GetFileAttributesW(downloads.c_str()) != INVALID_FILE_ATTRIBUTES)
                return downloads;
        }
    }
    wchar_t* userProfile = nullptr;
    size_t len = 0;
    _wdupenv_s(&userProfile, &len, L"USERPROFILE");
    if (userProfile) {
        std::wstring downloads = std::wstring(userProfile) + L"\\Downloads";
        free(userProfile);
        return downloads;
    }
    return L"C:\\Users\\Public\\Downloads";
}

std::wstring GetAppDataFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::wstring appData = std::wstring(path) + L"\\BVYT";
        CreateDirectoryW(appData.c_str(), nullptr);
        return appData;
    }
    return L".";
}

void SetStatus(const std::wstring& msg, COLORREF color = CLR_TEXT_DIM) {
    SetWindowTextW(g_hStatusLabel, msg.c_str());
    InvalidateRect(g_hStatusLabel, nullptr, TRUE);
}

bool DownloadFile(const std::wstring& url, const std::wstring& destPath) {
    HINTERNET hInternet = InternetOpenW(L"BVYT/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return false; }

    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return false; }

    char buffer[8192];
    DWORD bytesRead = 0;
    bool ok = true;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        DWORD written;
        if (!WriteFile(hFile, buffer, bytesRead, &written, nullptr)) { ok = false; break; }
    }

    CloseHandle(hFile); InternetCloseHandle(hUrl); InternetCloseHandle(hInternet);
    return ok;
}

bool EnsureYtDlp() {
    std::wstring appData = GetAppDataFolder();
    g_ytdlpPath = appData + L"\\yt-dlp.exe";
    if (GetFileAttributesW(g_ytdlpPath.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    SetStatus(L"\u2B07  Baixando yt-dlp pela primeira vez...", CLR_TEXT_DIM);
    const std::wstring url = L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
    return DownloadFile(url, g_ytdlpPath);
}

bool RunYtDlp(const std::wstring& args) {
    std::wstring cmdLine = L"\"" + g_ytdlpPath + L"\" " + args;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end()); cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe); CloseHandle(hWritePipe); return false;
    }
    CloseHandle(hWritePipe);

    char buf[512]; DWORD read; std::string lastLine;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
        buf[read] = '\0';
        std::string chunk(buf);
        size_t nl = chunk.rfind('\n');
        if (nl != std::string::npos) lastLine = chunk.substr(nl + 1); else lastLine += chunk;

        while (!lastLine.empty() && (lastLine.back() == '\r' || lastLine.back() == '\n')) lastLine.pop_back();
        if (!lastLine.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, lastLine.c_str(), -1, nullptr, 0);
            std::wstring wline(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, lastLine.c_str(), -1, wline.data(), wlen);
            std::wstring* pMsg = new std::wstring(wline);
            PostMessageW(g_hWnd, WM_APP + 1, (WPARAM)pMsg, 0);
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(hReadPipe); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exitCode == 0;
}

// Thread de Download
void DownloadThread(std::wstring url, int mode, std::wstring quality, std::wstring format) {
    PostMessageW(g_hWnd, WM_APP + 2, 1, 0);
    PostMessageW(g_hWnd, WM_APP + 3, 0, 0);

    if (!EnsureYtDlp()) {
        std::wstring* err = new std::wstring(L"\u274C  Falha ao baixar yt-dlp. Verifique sua conex\u00E3o.");
        PostMessageW(g_hWnd, WM_APP + 1, (WPARAM)err, 0);
        PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
        PostMessageW(g_hWnd, WM_APP + 3, 1, 0);
        g_downloading = false;
        return;
    }

    std::wstring outputTemplate = L"\"" + g_downloadsPath + L"\\%(title)s.%(ext)s\"";
    std::wstring args;

    // Filtro de resolução
    std::wstring qFilter = L"";
    if (quality == L"1080p") qFilter = L"[height<=1080]";
    else if (quality == L"720p") qFilter = L"[height<=720]";
    else if (quality == L"480p") qFilter = L"[height<=480]";

    if (mode == 0) { // Vídeo + Áudio
        args = L"-f \"bestvideo" + qFilter + L"+bestaudio/best\" --merge-output-format " + format + L" -o " + outputTemplate + L" \"" + url + L"\"";
    }
    else if (mode == 1) { // Somente Vídeo
        args = L"-f \"bestvideo" + qFilter + L"/best\" --remux-video " + format + L" -o " + outputTemplate + L" \"" + url + L"\"";
    }
    else { // Somente Áudio
        args = L"-f \"bestaudio\" -x --audio-format " + format + L" -o " + outputTemplate + L" \"" + url + L"\"";
    }

    {
        std::wstring* msg = new std::wstring(L"\u23F3  Iniciando download...");
        PostMessageW(g_hWnd, WM_APP + 1, (WPARAM)msg, 0);
    }

    bool ok = RunYtDlp(args);

    std::wstring* final = new std::wstring(ok
        ? L"\u2705  Download conclu\u00EDdo! Arquivo salvo em Downloads."
        : L"\u274C  Erro no download. Verifique o link e tente novamente.");
    PostMessageW(g_hWnd, WM_APP + 1, (WPARAM)final, 0);
    PostMessageW(g_hWnd, WM_APP + 2, 0, 0);
    PostMessageW(g_hWnd, WM_APP + 3, 1, 0);

    g_downloading = false;
}

// Procedimento do DialogBox importado do .rc
INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        HWND hCmbQual = GetDlgItem(hDlg, IDC_CMB_QUALITY);
        HWND hCmbFmt = GetDlgItem(hDlg, IDC_CMB_FORMAT);

        if (g_selectedMode == 2) { // Áudio
            SetDlgItemTextW(hDlg, IDC_LBL_QUALITY, L"Qualidade (Automático para Áudio):");
            EnableWindow(hCmbQual, FALSE);
            SendMessageW(hCmbQual, CB_ADDSTRING, 0, (LPARAM)L"Melhor Áudio");
            SendMessageW(hCmbQual, CB_SETCURSEL, 0, 0);

            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"mp3");
            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"m4a");
            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"wav");
            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"flac");
            SendMessageW(hCmbFmt, CB_SETCURSEL, 0, 0); // Default: mp3
        }
        else { // Vídeo
            SetDlgItemTextW(hDlg, IDC_LBL_QUALITY, L"Qualidade do Vídeo:");
            SendMessageW(hCmbQual, CB_ADDSTRING, 0, (LPARAM)L"Melhor (Automático)");
            SendMessageW(hCmbQual, CB_ADDSTRING, 0, (LPARAM)L"1080p");
            SendMessageW(hCmbQual, CB_ADDSTRING, 0, (LPARAM)L"720p");
            SendMessageW(hCmbQual, CB_ADDSTRING, 0, (LPARAM)L"480p");
            SendMessageW(hCmbQual, CB_SETCURSEL, 0, 0); // Default: Melhor

            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"mp4");
            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"mkv");
            SendMessageW(hCmbFmt, CB_ADDSTRING, 0, (LPARAM)L"webm");
            SendMessageW(hCmbFmt, CB_SETCURSEL, 0, 0); // Default: mp4
        }
        return (INT_PTR)TRUE;
    }

    // Mantendo o visual Escuro da nossa UI!
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        return (INT_PTR)g_hPanelBrush;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            wchar_t qBuf[64] = L"", fBuf[64] = L"";
            GetDlgItemTextW(hDlg, IDC_CMB_QUALITY, qBuf, 63);
            GetDlgItemTextW(hDlg, IDC_CMB_FORMAT, fBuf, 63);

            g_downloading = true;
            std::thread(DownloadThread, g_pendingUrl, g_selectedMode, std::wstring(qBuf), std::wstring(fBuf)).detach();

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    }
    return (INT_PTR)FALSE;
}

// Janela Principal
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFontTitle = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontMain = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        
        g_hBkgBrush = CreateSolidBrush(CLR_BG);
        g_hPanelBrush = CreateSolidBrush(CLR_PANEL);

        int x = 30, y = 70;

        HWND hLabel = CreateWindowW(L"STATIC", L"URL do v\u00EDdeo", WS_CHILD | WS_VISIBLE, x, y, 300, 20, hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        g_hUrlEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, x, y + 25, 360, 34, hWnd, (HMENU)IDC_URL_EDIT, g_hInstance, nullptr);
        SendMessageW(g_hUrlEdit, WM_SETFONT, (WPARAM)g_hFontMain, TRUE);

        g_OrigEditProc = (WNDPROC)SetWindowLongPtrW(g_hUrlEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

        g_hPasteBtn = CreateWindowW(L"BUTTON", L"Colar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, x + 365, y + 25, 65, 34, hWnd, (HMENU)IDC_PASTE_BTN, g_hInstance, nullptr);
        SendMessageW(g_hPasteBtn, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        y += 80;

        HWND hFmtLabel = CreateWindowW(L"STATIC", L"Formato de Sa\u00EDda Base", WS_CHILD | WS_VISIBLE, x, y, 200, 20, hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hFmtLabel, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        y += 25;

        g_hRadioVideo = CreateWindowW(L"BUTTON", L"  V\u00EDdeo + \u00C1udio", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_OWNERDRAW | WS_GROUP | WS_TABSTOP, x, y, 260, 26, hWnd, (HMENU)IDC_RADIO_VIDEO, g_hInstance, nullptr);
        SendMessageW(g_hRadioVideo, WM_SETFONT, (WPARAM)g_hFontMain, TRUE);

        g_hRadioVideoOnly = CreateWindowW(L"BUTTON", L"  Somente V\u00EDdeo", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_OWNERDRAW | WS_TABSTOP, x, y + 32, 280, 26, hWnd, (HMENU)IDC_RADIO_VIDEO_ONLY, g_hInstance, nullptr);
        SendMessageW(g_hRadioVideoOnly, WM_SETFONT, (WPARAM)g_hFontMain, TRUE);

        g_hRadioAudio = CreateWindowW(L"BUTTON", L"  Somente \u00C1udio", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_OWNERDRAW | WS_TABSTOP, x, y + 64, 280, 26, hWnd, (HMENU)IDC_RADIO_AUDIO, g_hInstance, nullptr);
        SendMessageW(g_hRadioAudio, WM_SETFONT, (WPARAM)g_hFontMain, TRUE);

        y += 115;

        g_hDownloadBtn = CreateWindowW(L"BUTTON", L"Baixar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, x, y, 200, 42, hWnd, (HMENU)IDC_DOWNLOAD_BTN, g_hInstance, nullptr);
        SendMessageW(g_hDownloadBtn, WM_SETFONT, (WPARAM)g_hFontMain, TRUE);

        g_hOpenFolderBtn = CreateWindowW(L"BUTTON", L"Abrir Downloads", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, x + 215, y, 215, 42, hWnd, (HMENU)IDC_OPEN_FOLDER_BTN, g_hInstance, nullptr);
        SendMessageW(g_hOpenFolderBtn, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        y += 40;

        g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_MARQUEE, x, y, 430, 5, hWnd, (HMENU)IDC_PROGRESS, g_hInstance, nullptr);

        y += 18;

        g_hStatusLabel = CreateWindowW(L"STATIC", L"Pronto para baixar.", WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, 430, 40, hWnd, (HMENU)IDC_STATUS_LABEL, g_hInstance, nullptr);
        SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC  hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        if (hCtrl == g_hRadioVideo || hCtrl == g_hRadioVideoOnly || hCtrl == g_hRadioAudio) {
            SetTextColor(hdc, CLR_TEXT);
        }
        else {
            SetTextColor(hdc, CLR_TEXT_DIM);
        }
        return (LRESULT)g_hBkgBrush;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, CLR_EDIT_BG);
        SetTextColor(hdc, CLR_TEXT);
        static HBRUSH hEditBrush = CreateSolidBrush(CLR_EDIT_BG);
        return (LRESULT)hEditBrush;
    }

    case WM_CTLCOLORBTN:
        return (LRESULT)g_hBkgBrush;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType != ODT_BUTTON) break;

        HWND hBtn = dis->hwndItem;
        bool isRadio = (hBtn == g_hRadioVideo || hBtn == g_hRadioVideoOnly || hBtn == g_hRadioAudio);
        if (!isRadio) break;

        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;

        bool checked = false;
        if (hBtn == g_hRadioVideo && g_selectedMode == 0) checked = true;
        if (hBtn == g_hRadioVideoOnly && g_selectedMode == 1) checked = true;
        if (hBtn == g_hRadioAudio && g_selectedMode == 2) checked = true;

        bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;

        FillRect(hdc, &rc, g_hBkgBrush);

        int cy = (rc.top + rc.bottom) / 2;
        int cx = rc.left + 10;
        int r = 7;

        HPEN hPenOuter = CreatePen(PS_SOLID, 1, hot ? CLR_ACCENT : RGB(160, 160, 180));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenOuter);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPenOuter);

        if (checked) {
            HBRUSH hFill = CreateSolidBrush(CLR_ACCENT);
            HPEN   hDot = CreatePen(PS_SOLID, 1, CLR_ACCENT);
            SelectObject(hdc, hFill);
            SelectObject(hdc, hDot);
            int ri = r - 3;
            Ellipse(hdc, cx - ri, cy - ri, cx + ri, cy + ri);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOldPen);
            DeleteObject(hFill);
            DeleteObject(hDot);
        }
        else {
            SelectObject(hdc, hOldBr);
        }

        wchar_t text[256] = {};
        GetWindowTextW(hBtn, text, 255);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        SelectObject(hdc, g_hFontMain);
        RECT textRc = { rc.left + 22, rc.top, rc.right, rc.bottom };
        DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        return TRUE;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hBkgBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT titleRc = { 0, 0, 470, 58 };
        FillRect(hdc, &titleRc, g_hPanelBrush);

        RECT accentRc = { 0, 0, 4, 58 };
        HBRUSH hAccBrush = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &accentRc, hAccBrush);
        DeleteObject(hAccBrush);

        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_hFontTitle);
        SetTextColor(hdc, CLR_TEXT);
        TextOutW(hdc, 20, 14, L"BVYT", 4);

        SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, CLR_ACCENT);
        TextOutW(hdc, 22, 43, L"feito com yt-dlp", 16);

        HPEN hPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, 58, nullptr);
        LineTo(hdc, 470, 58);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_APP + 1: {
        std::wstring* pMsg = (std::wstring*)wParam;
        if (pMsg) { SetWindowTextW(g_hStatusLabel, pMsg->c_str()); delete pMsg; }
        return 0;
    }

    case WM_APP + 2: {
        if (wParam) { ShowWindow(g_hProgress, SW_SHOW); SendMessageW(g_hProgress, PBM_SETMARQUEE, TRUE, 30); }
        else { SendMessageW(g_hProgress, PBM_SETMARQUEE, FALSE, 0); ShowWindow(g_hProgress, SW_HIDE); }
        return 0;
    }

    case WM_APP + 3: {
        EnableWindow(g_hDownloadBtn, (BOOL)wParam);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == IDC_RADIO_VIDEO || id == IDC_RADIO_VIDEO_ONLY || id == IDC_RADIO_AUDIO) {
            if (id == IDC_RADIO_VIDEO) g_selectedMode = 0;
            else if (id == IDC_RADIO_VIDEO_ONLY) g_selectedMode = 1;
            else if (id == IDC_RADIO_AUDIO) g_selectedMode = 2;

            InvalidateRect(g_hRadioVideo, nullptr, TRUE);
            InvalidateRect(g_hRadioVideoOnly, nullptr, TRUE);
            InvalidateRect(g_hRadioAudio, nullptr, TRUE);
        }

        if (id == IDC_PASTE_BTN) {
            if (OpenClipboard(hWnd)) {
                HANDLE hClip = GetClipboardData(CF_UNICODETEXT);
                if (hClip) {
                    wchar_t* pText = (wchar_t*)GlobalLock(hClip);
                    if (pText) { SetWindowTextW(g_hUrlEdit, pText); GlobalUnlock(hClip); }
                }
                CloseClipboard();
            }
        }

        if (id == IDC_OPEN_FOLDER_BTN) {
            ShellExecuteW(nullptr, L"open", g_downloadsPath.c_str(), nullptr, nullptr, SW_SHOW);
        }

        if (id == IDC_DOWNLOAD_BTN) {
            if (g_downloading) return 0;

            wchar_t urlBuf[2048] = {};
            GetWindowTextW(g_hUrlEdit, urlBuf, 2047);
            std::wstring url(urlBuf);

            size_t s = url.find_first_not_of(L" \t\r\n");
            if (s != std::wstring::npos) url = url.substr(s);
            size_t e = url.find_last_not_of(L" \t\r\n");
            if (e != std::wstring::npos) url = url.substr(0, e + 1);

            if (url.empty()) { SetStatus(L"\u26A0  Cole o link do v\u00EDdeo antes de baixar."); return 0; }

            // Salva a URL e puxa o Dialog direto do .rc
            g_pendingUrl = url;
            DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_OPTIONS_DIALOG), hWnd, OptionsDlgProc, 0);
        }
        return 0;
    }

    case WM_DESTROY:
        DeleteObject(g_hFontMain); DeleteObject(g_hFontTitle); DeleteObject(g_hFontSmall); 
        DeleteObject(g_hBkgBrush); DeleteObject(g_hPanelBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// WinMain
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInstance = hInstance;
    g_downloadsPath = GetDownloadsFolder();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = L"BVYTClass";
    RegisterClassExW(&wc);

    RECT wr = { 0, 0, 470, 380 };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int W = wr.right - wr.left;
    int H = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExW(0, L"BVYTClass", L"Baixador de v\u00EDdeo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - W) / 2, (sy - H) / 2, W, H, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
