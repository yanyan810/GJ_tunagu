#pragma once

#include <string>
#include "WinApp.h"
#include <format>

namespace StringUtility {

	std::wstring ConvertString(const std::string& str);


	std::string ConvertString(const std::wstring& str);
	


}