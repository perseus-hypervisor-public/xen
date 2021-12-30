#include <unistd.h>
#include <assert.h>
#include <sys/epoll.h>

#include <cstring>
#include <iostream>

#include "error.h"
#include "exception.h"
#include "mainloop.h"

#define MAX_EPOLL_EVENTS	16

Mainloop::Mainloop() :
	pollFd(::epoll_create1(EPOLL_CLOEXEC)) {
	if (pollFd == -1) {
		throw std::runtime_error(Error::message());
	}
}

Mainloop::~Mainloop()
{
}

auto Mainloop::addEventSource(int fd, const Event events, Callback&& cb) -> void {
	epoll_event event;
	std::lock_guard<std::recursive_mutex> lock(mutex);

	if (callbacks.find(fd) != callbacks.end()) {
		throw std::runtime_error("event source already registered");
	}

	::memset(&event, 0, sizeof(epoll_event));

	event.events = events;
	event.data.fd = fd;

	if (::epoll_ctl(pollFd, EPOLL_CTL_ADD, fd, &event) == -1) {
		throw std::runtime_error(Error::message());
	}

	callbacks.insert({fd, std::make_shared<Callback>(std::move(cb))});
}

auto Mainloop::removeEventSource(int fd) -> void {
	std::lock_guard<std::recursive_mutex> lock(mutex);

	auto iter = callbacks.find(fd);
	if (iter == callbacks.end()) {
		return;
	}

	callbacks.erase(iter);

	::epoll_ctl(pollFd, EPOLL_CTL_DEL, fd, NULL);
}

auto Mainloop::dispatch(int timeout) -> bool {
	int nfds = 0;
	epoll_event event[MAX_EPOLL_EVENTS];

	do {
		nfds = ::epoll_wait(pollFd, event, MAX_EPOLL_EVENTS, timeout);
        if (errno == EINTR)
            return true;
	} while (nfds == -1);

	if (nfds <= 0) {
		return false;
	}

	for (int i = 0; i < nfds; i++) {
		std::lock_guard<std::recursive_mutex> lock(mutex);

		auto iter = callbacks.find(event[i].data.fd);
		if (iter == callbacks.end()) {
			continue;
		}

		std::shared_ptr<Callback> callback(iter->second);
		try {
			if ((event[i].events & (EPOLLHUP | EPOLLRDHUP))) {
				event[i].events &= ~EPOLLIN;
			}

			(*callback)(event[i].data.fd, event[i].events);
		} catch (std::exception& e) {
			std::cerr << e.what() << std::endl;
		}
	}

	return true;
}

auto Mainloop::run(bool& stopped, int timeout) -> void {
	bool done = false;

	while (!stopped && !done) {
		done = !dispatch(timeout);
	}
}
