#pragma once

#include <sys/eventfd.h>

class IoEvent {
public:
	IoEvent(unsigned int initval = 0, int flags = 0);
	~IoEvent();

	IoEvent(const IoEvent&) = delete;
	IoEvent& operator=(const IoEvent&) = delete;

	auto send() -> void;
	auto receive() -> void;
	auto close() -> void;

	auto getFd() const -> int {
		return fd;
	}

private:
	int fd;
};
