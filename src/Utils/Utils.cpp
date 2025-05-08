#include "Utils.h"
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>
#include <codecvt>
#include <locale>

namespace Nexile {
    namespace Utils {

        std::wstring StringToWideString(const std::string& str) {
            if (str.empty()) {
                return L"";
            }

            int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
            if (size <= 0) {
                return L"";
            }

            std::vector<wchar_t> buffer(size);
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buffer.data(), size);

            return std::wstring(buffer.data());
        }

        std::string WideStringToString(const std::wstring& wstr) {
            if (wstr.empty()) {
                return "";
            }

            int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
            if (size <= 0) {
                return "";
            }

            std::vector<char> buffer(size);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, buffer.data(), size, NULL, NULL);

            return std::string(buffer.data());
        }

        std::string GetAppDataPath() {
            wchar_t appDataPath[MAX_PATH];
            // Use the W version to match the wchar_t buffer
            SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath);

            std::wstring wpath = std::wstring(appDataPath) + L"\\Nexile";
            return WideStringToString(wpath);
        }

        std::string GetModulePath() {
            wchar_t path[MAX_PATH];
            // Use the W version to match the wchar_t buffer
            GetModuleFileNameW(NULL, path, MAX_PATH);

            std::wstring wpath(path);
            size_t lastBackslash = wpath.find_last_of(L'\\');
            if (lastBackslash != std::wstring::npos) {
                wpath = wpath.substr(0, lastBackslash);
            }

            return WideStringToString(wpath);
        }

        std::string CombinePath(const std::string& path1, const std::string& path2) {
            std::filesystem::path p1(path1);
            std::filesystem::path p2(path2);
            std::filesystem::path combined = p1 / p2;
            return combined.string();
        }

        bool FileExists(const std::string& path) {
            return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
        }

        bool DirectoryExists(const std::string& path) {
            return std::filesystem::exists(path) && std::filesystem::is_directory(path);
        }

        bool CreateDirectory(const std::string& path) {
            try {
                return std::filesystem::create_directories(path);
            }
            catch (const std::filesystem::filesystem_error&) {
                return false;
            }
        }

        std::vector<std::string> GetFilesInDirectory(const std::string& path, const std::string& extension) {
            std::vector<std::string> files;

            try {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        std::string filePath = entry.path().string();

                        // Check extension if provided
                        if (!extension.empty()) {
                            std::string fileExt = entry.path().extension().string();
                            std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);

                            std::string checkExt = extension;
                            std::transform(checkExt.begin(), checkExt.end(), checkExt.begin(), ::tolower);

                            if (fileExt != checkExt && fileExt != "." + checkExt) {
                                continue;
                            }
                        }

                        files.push_back(filePath);
                    }
                }
            }
            catch (const std::filesystem::filesystem_error&) {
                // Ignore errors
            }

            return files;
        }

        std::string ReadTextFile(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return "";
            }

            std::string content;
            std::string line;
            while (std::getline(file, line)) {
                content += line + "\n";
            }

            file.close();
            return content;
        }

        bool WriteTextFile(const std::string& path, const std::string& content) {
            std::ofstream file(path);
            if (!file.is_open()) {
                return false;
            }

            file << content;
            file.close();

            return true;
        }

        RECT GetWindowRectangle(HWND hwnd) {
            RECT rect = {};
            if (hwnd != NULL) {
                GetWindowRect(hwnd, &rect);
            }
            return rect;
        }

        RECT GetClientRectangle(HWND hwnd) {
            RECT rect = {};
            if (hwnd != NULL) {
                GetClientRect(hwnd, &rect);
            }
            return rect;
        }

        void CenterWindowOnScreen(HWND hwnd) {
            if (hwnd == NULL) {
                return;
            }

            // Get screen dimensions
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);

            // Get window size
            RECT rect;
            GetWindowRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            // Calculate position
            int x = (screenWidth - width) / 2;
            int y = (screenHeight - height) / 2;

            // Set position
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        void CenterWindowOnParent(HWND hwnd, HWND parent) {
            if (hwnd == NULL) {
                return;
            }

            if (parent == NULL) {
                CenterWindowOnScreen(hwnd);
                return;
            }

            // Get parent dimensions
            RECT parentRect;
            GetWindowRect(parent, &parentRect);
            int parentWidth = parentRect.right - parentRect.left;
            int parentHeight = parentRect.bottom - parentRect.top;
            int parentX = parentRect.left;
            int parentY = parentRect.top;

            // Get window size
            RECT rect;
            GetWindowRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            // Calculate position
            int x = parentX + (parentWidth - width) / 2;
            int y = parentY + (parentHeight - height) / 2;

            // Set position
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        void BringWindowToTop(HWND hwnd) {
            if (hwnd != NULL) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetForegroundWindow(hwnd);
            }
        }

        int GetScreenWidth() {
            return GetSystemMetrics(SM_CXSCREEN);
        }

        int GetScreenHeight() {
            return GetSystemMetrics(SM_CYSCREEN);
        }

        HWND GetForegroundWindowHandle() {
            return GetForegroundWindow();
        }

        std::wstring GetWindowClassName(HWND hwnd) {
            if (hwnd == NULL) {
                return L"";
            }

            wchar_t className[256] = {};
            // Use the W version to match the wchar_t buffer
            GetClassNameW(hwnd, className, 256);

            return std::wstring(className);
        }

        std::wstring GetWindowTitle(HWND hwnd) {
            if (hwnd == NULL) {
                return L"";
            }

            int length = GetWindowTextLengthW(hwnd);
            if (length == 0) {
                return L"";
            }

            std::vector<wchar_t> buffer(length + 1);
            // Use the W version to match the wchar_t buffer
            GetWindowTextW(hwnd, buffer.data(), length + 1);

            return std::wstring(buffer.data());
        }

        bool ReadRegistryString(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, std::wstring& value) {
            HKEY key;
            // Use the W version to match the wstring parameters
            LONG result = RegOpenKeyExW(hKey, subKey.c_str(), 0, KEY_READ, &key);
            if (result != ERROR_SUCCESS) {
                return false;
            }

            DWORD type;
            DWORD size = 0;

            // Get size first (using W version)
            result = RegQueryValueExW(key, valueName.c_str(), NULL, &type, NULL, &size);
            if (result != ERROR_SUCCESS || type != REG_SZ) {
                RegCloseKey(key);
                return false;
            }

            // Allocate buffer
            std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);

            // Read value (using W version)
            result = RegQueryValueExW(key, valueName.c_str(), NULL, NULL, reinterpret_cast<LPBYTE>(buffer.data()), &size);
            RegCloseKey(key);

            if (result != ERROR_SUCCESS) {
                return false;
            }

            value = buffer.data();
            return true;
        }

        bool WriteRegistryString(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& value) {
            HKEY key;
            // Use the W version to match the wstring parameters
            LONG result = RegCreateKeyExW(hKey, subKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL);
            if (result != ERROR_SUCCESS) {
                return false;
            }

            // Use the W version to match the wstring parameters
            result = RegSetValueExW(key, valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);

            return result == ERROR_SUCCESS;
        }

        bool ReadRegistryDword(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, DWORD& value) {
            HKEY key;
            // Use the W version to match the wstring parameters
            LONG result = RegOpenKeyExW(hKey, subKey.c_str(), 0, KEY_READ, &key);
            if (result != ERROR_SUCCESS) {
                return false;
            }

            DWORD type;
            DWORD size = sizeof(DWORD);

            // Use the W version to match the wstring parameters
            result = RegQueryValueExW(key, valueName.c_str(), NULL, &type, reinterpret_cast<LPBYTE>(&value), &size);
            RegCloseKey(key);

            return result == ERROR_SUCCESS && type == REG_DWORD;
        }

        bool WriteRegistryDword(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, DWORD value) {
            HKEY key;
            // Use the W version to match the wstring parameters
            LONG result = RegCreateKeyExW(hKey, subKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL);
            if (result != ERROR_SUCCESS) {
                return false;
            }

            // Use the W version to match the wstring parameters
            result = RegSetValueExW(key, valueName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
            RegCloseKey(key);

            return result == ERROR_SUCCESS;
        }

        std::string LoadStringResource(UINT resourceID) {
            HMODULE hModule = GetModuleHandle(NULL);
            HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceID), RT_STRING);
            if (hResource == NULL) {
                return "";
            }

            HGLOBAL hMemory = LoadResource(hModule, hResource);
            if (hMemory == NULL) {
                return "";
            }

            DWORD size = SizeofResource(hModule, hResource);
            char* data = static_cast<char*>(LockResource(hMemory));
            if (data == NULL) {
                return "";
            }

            std::string result(data, size);
            return result;
        }

        std::string LoadHTMLResource(UINT resourceID) {
            HMODULE hModule = GetModuleHandle(NULL);
            HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(resourceID), RT_HTML);
            if (hResource == NULL) {
                return "";
            }

            HGLOBAL hMemory = LoadResource(hModule, hResource);
            if (hMemory == NULL) {
                return "";
            }

            DWORD size = SizeofResource(hModule, hResource);
            char* data = static_cast<char*>(LockResource(hMemory));
            if (data == NULL) {
                return "";
            }

            std::string result(data, size);
            return result;
        }

        std::string EncodeURIComponent(const std::string& str) {
            std::string result;

            for (char c : str) {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    result += c;
                }
                else {
                    char buffer[4];
                    sprintf_s(buffer, "%%%02X", static_cast<unsigned char>(c));
                    result += buffer;
                }
            }

            return result;
        }

        std::string DecodeURIComponent(const std::string& str) {
            std::string result;

            for (size_t i = 0; i < str.length(); i++) {
                if (str[i] == '%' && i + 2 < str.length()) {
                    char buffer[3] = { str[i + 1], str[i + 2], 0 };
                    int value = 0;
                    sscanf_s(buffer, "%x", &value);
                    result += static_cast<char>(value);
                    i += 2;
                }
                else if (str[i] == '+') {
                    result += ' ';
                }
                else {
                    result += str[i];
                }
            }

            return result;
        }

    } // namespace Utils
} // namespace Nexile