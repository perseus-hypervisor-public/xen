#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>

#include "vmi.h"
#include "session.h"
#include "fs.h"

Session::Session() : device("/dev/hvx") {
}

Session::~Session() {
	dispose();
}

auto Session::hypercall(struct hvx_proto_hypercall *hc) -> int {
	int ret = -1;

	if ((ret = device.ioctl(HVX_IOCTL_HYPERCALL, hc)) < 0) {
		std::cerr << "Hypercall failed " << std::endl;
	}

	return ret;
}

auto Session::create() -> Session* {
	Session* session = new Session();
	session->create(true);
	return session;
}

auto Session::create(bool closeOnExit) -> int {
	int flags;

	try {
		device.open(O_RDWR);

		if (closeOnExit) {
			flags = device.fcntl(F_GETFD);
			flags |= FD_CLOEXEC;
			device.fcntl(F_SETFD, flags);
		}
		return 0;
	} catch (std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return -1;
	}
}

auto Session::dispose() -> void {
	device.close();
}

auto Session::map(off_t addr, size_t len) -> void* {
	return device.mmap<void>(addr, len, PROT_READ | PROT_WRITE, MAP_SHARED);
}

auto Session::unmap(void *addr, size_t len) -> void {
	device.munmap(addr, len);
}
