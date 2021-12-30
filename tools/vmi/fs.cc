#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sendfile.h>

#include <string>
#include <sstream>
#include <iostream>

#include "error.h"
#include "exception.h"
#include "fs.h"

File::~File() {
	close();
}

bool File::exists() const {
	struct stat st;
	return (::lstat(path.c_str(), &st) == 0);
}

bool File::canRead() const {
	if (::access(path.c_str(), R_OK) == 0) {
		return true;
	}

	return false;
}

bool File::canWrite() const {
	if (::access(path.c_str(), W_OK) == 0) {
		return true;
	}

	return false;
}

bool File::canExecute() const {
	if (::access(path.c_str(), X_OK) == 0) {
		return true;
	}

	return false;
}

bool File::isLink() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return S_ISLNK(st.st_mode);
}

bool File::isFile() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return S_ISREG(st.st_mode);
}

bool File::isDirectory() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return S_ISDIR(st.st_mode);
}

bool File::isDevice() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode));
}

mode_t File::getMode() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_mode;
}

uid_t File::getUid() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_uid;
}

gid_t File::getGid() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_gid;
}

ino_t File::getInode() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_ino;
}

dev_t File::getDevice() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_dev;
}

off_t File::size() const {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		throw std::runtime_error(Error::message());
	}

	return st.st_size;
}

void File::create(mode_t mode) {
	if (descriptor != -1) {
		close();
	}

	while (1) {
		descriptor = ::creat(path.c_str(), mode);
		if (descriptor == -1) {
			if (errno == EINTR) {
				continue;
			}
			throw std::runtime_error(Error::message());
		}
		return;
	}
}

void File::open(int flags) {
	if (descriptor != -1) {
		close();
	}

	while (1) {
		descriptor = ::open(path.c_str(), flags);
		if (descriptor == -1) {
			if (errno == EINTR) {
				continue;
			}
			throw std::runtime_error(Error::message());
		}
		return;
	}
}

void File::close() {
	if (descriptor != -1) {
		while ((::close(descriptor) == -1) && (errno == EINTR));
		descriptor = -1;
	}
}

void File::read(void *buffer, const size_t size) const {
	size_t total = 0;

	while (total < size) {
		int bytes = ::read(descriptor, reinterpret_cast<char*>(buffer) + total, size - total);
		if (bytes >= 0) {
			total += bytes;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			continue;
		} else {
			throw std::runtime_error(Error::message());
		}
	}
}

void File::write(const void *buffer, const size_t size) const {
	size_t written = 0;

	while (written < size) {
		int bytes = ::write(descriptor, reinterpret_cast<const char*>(buffer) + written, size - written);
		if (bytes >= 0) {
			written += bytes;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			continue;
		} else {
			throw std::runtime_error(Error::message());
		}
	}
}

int File::fcntl(int cmd, int arg) {
	return ::fcntl(descriptor, cmd, arg);
}

void File::lseek(off_t offset, int whence) const {
	if (::lseek(descriptor, offset, whence) == -1) {
		throw std::runtime_error(Error::message());
	}
}

void File::remove(bool recursive) {
	if (isDirectory()) {
		if (recursive) {
			DirectoryIterator iter(path), end;
			while (iter != end) {
				iter->remove(true);
				++iter;
			}
		}
		if (::rmdir(path.c_str()) != 0) {
			throw std::runtime_error(Error::message());
		}
	} else {
		if (::unlink(path.c_str()) != 0) {
			throw std::runtime_error(Error::message());
		}
	}
}

void File::chown(uid_t uid, gid_t gid, bool recursive) {
	if (::chown(path.c_str(), uid, gid) != 0) {
		throw std::runtime_error(Error::message());
	}

	if (recursive && isDirectory()) {
		DirectoryIterator iter(path), end;
		while (iter != end) {
			iter->chown(uid, gid, true);
			++iter;
		}
	}
}

void File::chmod(mode_t mode, bool recursive) {
	if (::chmod(path.c_str(), mode) != 0) {
		throw std::runtime_error(Error::message());
	}

	if (recursive && isDirectory()) {
		DirectoryIterator iter(path), end;
		while (iter != end) {
			iter->chmod(mode, true);
			++iter;
		}
	}
}

const std::string File::readLink() const {
	char buf[PATH_MAX + 1];
	ssize_t ret = ::readlink(path.c_str(), buf, PATH_MAX);
	if (ret == -1) {
		throw std::runtime_error(Error::message());
	}

	buf[ret] = '\0';
	return buf;
}

void File::lock() const {
	if (::flock(descriptor, LOCK_EX) == -1) {
		throw std::runtime_error(Error::message());
	}
}

void File::unlock() const {
	if (::flock(descriptor, LOCK_UN) == -1) {
		throw std::runtime_error(Error::message());
	}
}

DirectoryIterator::DirectoryIterator(const std::string& dir)
	: directory_handle(nullptr) {
	reset(dir);
}

DirectoryIterator::~DirectoryIterator() {
	if (directory_handle != nullptr) {
		::closedir(directory_handle);
	}
}

void DirectoryIterator::reset(const std::string& dir) {
	if (directory_handle != nullptr) {
		::closedir(directory_handle);
		directory_handle = nullptr;
	}

	basename = dir;
	directory_handle = ::opendir(basename.c_str());
	if (directory_handle == nullptr) {
		throw std::runtime_error(Error::message());
	}

	next();
}

void DirectoryIterator::next() {
	std::string name;
	struct dirent *ent;

	while (1) {
		ent = readdir(directory_handle);
		if (ent == NULL)
			break;

		if (ent->d_name[0] == '.' && ent->d_name[1] == '\0') {
			continue;
		}

		if (ent->d_name[0] == '.' &&
				ent->d_name[1] == '.' && ent->d_name[2] == '\0') {
			continue;
		}

		name = basename + "/" + std::string(ent->d_name);
		break;
	}

	current = name;
}

DirectoryIterator& DirectoryIterator::operator=(const std::string& dir) {
	reset(dir);
	return *this;
}

DirectoryIterator& DirectoryIterator::operator++() {
	next();
	return *this;
}
