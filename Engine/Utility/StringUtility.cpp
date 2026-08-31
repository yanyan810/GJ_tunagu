#include "StringUtility.h"

namespace StringUtility {

	// std::wstring -> std::string の変換
	std::string ConvertString(const std::wstring& str) {
		if (str.empty()) {
			return {};
		}

		int size_needed = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size_needed <= 0) {
			return {};
		}
		// The Win32 conversion writes the null terminator as part of
		// size_needed, so reserve that byte and remove it afterwards.
		std::string result(static_cast<size_t>(size_needed), '\0');
		if (WideCharToMultiByte(
			CP_UTF8, 0, str.c_str(), -1,
			result.data(), size_needed, nullptr, nullptr) == 0) {
			return {};
		}
		result.pop_back();
		return result;
	}

	// std::string -> std::wstring の変換
	std::wstring ConvertString(const std::string& str) {
		if (str.empty()) {
			return {};
		}

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
		if (size_needed <= 0) {
			return {};
		}
		// As above, include space for the null terminator written by Win32.
		std::wstring result(static_cast<size_t>(size_needed), L'\0');
		if (MultiByteToWideChar(
			CP_UTF8, 0, str.c_str(), -1,
			result.data(), size_needed) == 0) {
			return {};
		}
		result.pop_back();
		return result;
	}

}
