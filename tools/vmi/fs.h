#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>

#include <string>

class File {
public:
	File() :
		descriptor(-1)
	{
	}

	File(const std::string& pathname) :
		descriptor(-1), path(pathname)
	{
	}

	virtual ~File();

	File(const File& that) = delete;

	File& operator=(const File& that) = delete;

	File& operator=(const std::string& pathname) {
		path = pathname;
		return *this;
	}

	bool operator==(const File& that) const {
		return (path == that.path);
	}

	bool operator!=(const File& that) const {
		return !(path == that.path);
	}

	bool exists() const;
	bool canRead() const;
	bool canWrite() const;
	bool canExecute() const;

	bool isLink() const;
	bool isFile() const;
	bool isDirectory() const;
	bool isDevice() const;

	mode_t getMode() const;
	uid_t getUid() const;
	gid_t getGid() const;
	ino_t getInode() const;
	dev_t getDevice() const;

	off_t size() const;

	void create(mode_t mode);
	void open(int flags);
	void close();
	int fcntl(int cmd, int arg = 0);
	void read(void *buffer, const size_t size) const;
	void write(const void *buffer, const size_t size) const;
	void lseek(off_t offset, int whence) const;
	void remove(bool recursive = false);

	void lock() const;
	void unlock() const;

	void chown(uid_t uid, gid_t gid, bool recursive = false);
	void chmod(mode_t mode, bool recursive = false);

	const std::string readLink() const;

	const std::string& getPath() const {
		return path;
	}

	const std::string getName() const {
		return path.substr(path.rfind('/') + 1);
	}

	template<typename T>
	T* mmap(off_t offset, size_t size, int prot, int flags) {
		void *ptr = ::mmap(NULL, size, prot, flags, descriptor, offset);
		return static_cast<T *>(ptr);
	}

	template<typename T>
	int munmap(T* ptr, size_t size) {
		return ::munmap(ptr, size);
	}

	template<typename T>
	int ioctl(unsigned long request, T* param) {
		return ::ioctl(descriptor, request, param);
	}

private:
	int descriptor;
	std::string path;
};

class DirectoryIterator {
public:
	DirectoryIterator() :
	    directory_handle(nullptr) {
	}

	DirectoryIterator(const std::string& dir);

	~DirectoryIterator();

	DirectoryIterator& operator=(const std::string& dir);
	DirectoryIterator& operator++();

	bool operator==(const DirectoryIterator& iterator) const {
		return (current == iterator.current);
	}

	bool operator!=(const DirectoryIterator& iterator) const {
		return !(current == iterator.current);
	}

	const File& operator*() const {
		return current;
	}

	File& operator*() {
		return current;
	}

	const File* operator->() const {
		return &current;
	}

	File* operator->() {
		return &current;
	}

private:
	void next();
	void reset(const std::string& dir);

	File current;
	DIR* directory_handle;
	std::string basename;
};
