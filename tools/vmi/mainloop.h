#pragma once

#include <sys/epoll.h>

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

#include "eventfd.h"

class Mainloop {
public:
	typedef unsigned int Event;
	typedef std::function<void(int fd, Event event)> Callback;

	Mainloop();
	~Mainloop();

	auto addEventSource(int fd, const Event events, Callback&& cb) -> void;
	auto removeEventSource(int fd) -> void;
	auto dispatch(int timeout) -> bool;
	auto run(bool& stopped, int timeout = -1) -> void;

private:
	void prepare();

private:
	std::unordered_map<int, std::shared_ptr<Callback>> callbacks;
	std::recursive_mutex mutex;
	int pollFd;
};
