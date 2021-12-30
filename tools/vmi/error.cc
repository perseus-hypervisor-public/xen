#include <cstring>

#include "error.h"

int Error::lastErrorCode()
{
	return errno;
}

std::string Error::message(int errorCode)
{
	char errmsg[256];
	return ::strerror_r(errorCode, errmsg, sizeof(errmsg));
}

std::string Error::message()
{
	return message(lastErrorCode());
}
