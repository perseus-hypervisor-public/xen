#pragma once
#include <iostream>
#include <functional>
#include <utility>

#include "fs.h"
#include "mainloop.h"
#include "exception.h"
#include "../hvx/hvx.h"

class Session {
public:
	typedef std::function<void(int fd)> Callback;
	~Session();

    auto dispose() -> void;

	static auto create() -> Session*;

	auto create(bool closeOnExit) -> int;

    template<typename T>
    auto hypercall(int id, const T* param) -> int;

	template<typename T>
	auto ioctl(unsigned long cmd, T* param) -> int;

	int addCallback(int fd, Callback&& callback) {
		try {
			mainloop.addEventSource(fd, EPOLLIN | EPOLLRDHUP,
									[callback](int fd, Mainloop::Event) {
				callback(fd);
			});
		} catch (Exception& e) {
			std::cerr << e.what() << std::endl;
			return -1;
		}
		return 0;
	}

	int eventloop(bool& stopped) {
		mainloop.run(stopped);
		return 0;
	}

	void *map(off_t addr, size_t len);
	void unmap(void *map, size_t len);

protected:
	Session();
	auto hypercall(struct hvx_proto_hypercall *hc) -> int;

private:
    File device;
	Mainloop mainloop;
};

template<typename T>
auto Session::ioctl(unsigned long cmd, T* param) -> int {
	return device.ioctl(cmd, param);
}

template<typename T>
auto Session::hypercall(int op, const T* param) -> int {
    struct hvx_proto_hypercall hc;

    hc.op     = op;
    hc.params[0] = (unsigned long)(param);

    return hypercall(&hc);
}
