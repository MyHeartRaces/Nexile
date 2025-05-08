#pragma once

#include <string>
#include <vector>
#include <Windows.h>

namespace Nexile {
	namespace Utils {

		// String conversion functions
		std::wstring StringToWideString(const std::string& str);
		std::string WideStringToString(const std::wstring& wstr);

		// Path functions
		std::string GetAppDataPath();
		std::string GetModulePath();
		std::string CombinePath(const std::string& path1, const std::string& path2);

		// File functions
		bool FileExists(const std::string& path);
		bool DirectoryExists(const std::string& path);
		bool CreateDirectory(const std::string& path);
		std::vector<std::string> GetFilesInDirectory(const std::string& path, const std::string& extension = "");
		std::string ReadTextFile(const std::string& path);
		bool WriteTextFile(const std::string& path, const std::string& content);

		// Window functions
		RECT GetWindowRectangle(HWND hwnd);
		RECT GetClientRectangle(HWND hwnd);
		void CenterWindowOnScreen(HWND hwnd);
		void CenterWindowOnParent(HWND hwnd, HWND parent);
		void BringWindowToTop(HWND hwnd);

		// System functions
		int GetScreenWidth();
		int GetScreenHeight();
		HWND GetForegroundWindowHandle();
		std::wstring GetWindowClassName(HWND hwnd);
		std::wstring GetWindowTitle(HWND hwnd);

		// Registry functions
		bool ReadRegistryString(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, std::wstring& value);
		bool WriteRegistryString(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& value);
		bool ReadRegistryDword(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, DWORD& value);
		bool WriteRegistryDword(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName, DWORD value);

		// Resource functions
		std::string LoadStringResource(UINT resourceID);
		std::string LoadHTMLResource(UINT resourceID);

		// URI functions
		std::string EncodeURIComponent(const std::string& str);
		std::string DecodeURIComponent(const std::string& str);

	} // namespace Utils
} // namespace Nexile