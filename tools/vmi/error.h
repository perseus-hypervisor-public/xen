#pragma once

#include <string>

class Error {
public:
	static std::string message();
	static std::string message(int error);
	static int lastErrorCode();
};