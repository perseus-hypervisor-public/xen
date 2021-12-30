#pragma once

#include <cstring>
#include <string>
#include <memory>
#include <sstream>
#include <iostream>

#include "logsink.h"

enum class LogLevel : int {
	silent,
	error,
	warning,
	debug,
	info,
	trace
};

struct LogRecord {
	LogLevel severity;
	std::string file;
	unsigned int line;
	std::string func;
	std::string message;
};

class Logger {
public:
	static void log(LogSink* sink, const LogRecord record);
};

std::string LogLevelToString(const LogLevel level);
loglevel StringToLoglevel(const std::string& level);

#ifndef __FILENAME__
#define __FILENAME__                                                  \
(::strrchr(__FILE__, '/') ? ::strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define KSINK nullptr

#define FORMAT(items)                                                  \
(static_cast<std::ostringstream &>(std::ostringstream() << items)).str()

#define LOG(sink, message, level)                                      \
do {                                                                   \
	LogRecord record = {                                               \
		LogLevel::level,                                               \
		__FILENAME__,                                                  \
		__LINE__,                                                      \
		__func__,                                                      \
		FORMAT(message) };                                             \
	Logger::log(sink, record);                                         \
} while (0)

#define ERROR2(logsink, message) LOG(logsink, message, Error)
#define ERROR1(message)          LOG(nullptr, message, Error)
#define WARN2(logsink, message)  LOG(logsink, message, Warning)
#define WARN1(message)           LOG(nullptr, message, Warning)

#if !defined(NDEBUG)
#define INFO2(logsink, message)  LOG(logsink, message, Info)
#define INFO1(message)           LOG(nullptr, message, Info)
#define DEBUG2(logsink, message) LOG(logsink, message, Debug)
#define DEBUG1(message)          LOG(nullptr, message, Debug)
#define TRACE2(logsink, message) LOG(logsink, message, Trace)
#define TRACE1(message)          LOG(nullptr, message, Trace)
#else
#define INFO2(logsink, message)  do {} while (0)
#define INFO1(message)           do {} while (0)
#define DEBUG2(logsink, message) do {} while (0)
#define DEBUG1(message)          do {} while (0)
#define TRACE2(logsink, message) do {} while (0)
#define TRACE1(message)          do {} while (0)
#endif //NDEBUG

#define GET_MACRO(_1, _2, macro, ...) macro
#define ERROR(args...) GET_MACRO(args, ERROR2, ERROR1)(args)
#define WARN(args...)  GET_MACRO(args, WARN2, WARN1)(args)
#define DEBUG(args...) GET_MACRO(args, DEBUG2, DEBUG1)(args)
#define INFO(args...)  GET_MACRO(args, INFO2, INFO1)(args)
#define TRACE(args...) GET_MACRO(args, TRACE2, TRACE1)(args)

}
