#pragma once

#include <stdexcept>
#include <string>

class Exception : public std::runtime_error {
public:
	Exception(const std::string& error) :
		std::runtime_error(error)
	{
	}
};
