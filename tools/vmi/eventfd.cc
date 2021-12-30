#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "error.h"
#include "eventfd.h"
#include "exception.h"

IoEvent::IoEvent(unsigned int initval, int flags) {
	fd = ::eventfd(initval, flags);
	if (fd == -1) {
		throw std::runtime_error(Error::message());
	}
}

IoEvent::~IoEvent() {
	if (fd != -1) {
		::close(fd);
	}
}

auto IoEvent::close() -> void {
	::close(fd);
}

auto IoEvent::send() -> void {
	const std::uint64_t val = 1;
	if (::write(fd, &val, sizeof(val)) == -1) {
		throw std::runtime_error(Error::message());
	}
}

auto IoEvent::receive() -> void {
	std::uint64_t val;
	if (::read(fd, &val, sizeof(val)) == -1) {
		throw std::runtime_error(Error::message());
	}
}