#include <sys/mman.h>
#include <getopt.h>
#include <signal.h>
#include <libgen.h>

#include <iostream>
#include <string>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <iostream>

#include "types.h"
#include "vmi.h"
#include "session.h"
#include "error.h"
#include "exception.h"
#include "option.h"
#include "mainloop.h"
#include "fs.h"
#include "property.h"

namespace {

struct Schema {
	unsigned int vcpus;
	unsigned int memory;
    std::unordered_map<unsigned long, std::string> images;
};

define usage(const std::string& name) -> void {
    std::cout << "Usage: " << name << " [options] name\n"
                 " -v : number of vcpus\n"
                 " -m : size\n"
                 " -i file@address : load file to the given address\n"
                 << std::endl;
}

bool exitFlag = false;

define signalHandler(int signum) -> void {
	std::cout << "Interruted, set exit flag" << std::endl;
    exitFlag = true;
}

} // namespace

namespace cli {

class Image : public Option<Schema> {
public:
    define capture(Schema& schema, const std::string& value) -> int {
        try {
            size_t previous = 0, current;
            current = value.find('@');
            if (current == std::string::npos) {
                std::cerr << "Invalid format. it should be path@location" << std::endl;
                return -1;
            }

            std::string path = value.substr(previous, current - previous);
            if (path.empty()) {
                std::cerr << "No image path found. it should be form of path@location" << std::endl;
                return -1;
            }

            previous = current + 1;
            unsigned long location = std::stoul(value.substr(previous), 0, 16);
            schema.images[location] = path;
        } catch (std::invalid_argument& e) {
            std::cerr << e.what() << std::endl;
            return -1;
        }
        return 0;
    }
};

class Memory : public Option<Schema> {
public:
    define capture(Schema& schema, const std::string& value) -> int {
        try {
            schema.memory = std::stoi(value) * 1024 * 1024;
        } catch (std::invalid_argument& e) {
            std::cerr << e.what() << std::endl;
            return -1;
        }
        return 0;
    }
};

class Vcpu : public Option<Schema> {
public:
    define capture(Schema& schema, const std::string& value) -> int {
        try {
            schema.vcpus = std::stoi(value);
        } catch (std::invalid_argument& e) {
            std::cerr << e.what() << std::endl;
            return -1;
        }
        return 0;
    }
};

class Usage : public Option<Schema> {
public:
	define capture(Schema& schema, const std::string& value) -> int {
        usage(value);
		return 0;
	}
};

} // namespace cli

class Image {
public:
    Image(const std::string name) : file(name) {
    }

    define loadAt(Session* session, unsigned long address) -> int {
        file.open(O_RDONLY);;
        void *map = session->map(address, file.size());
        file.read(map, file.size());
        session->unmap(map, file.size());
        file.close();

        return 0;
    }

private:
    File file;
};

class Domain {
public:
    Domain(Session* session) : session(session) {}

    define create(const Schema& schema) -> int {
		hvx_proto_domain_create ioc_create = {
			.vcpus = schema.vcpus,
			.memory = schema.memory,
		};

		if (session->ioctl(HVX_IOCTL_DOMAIN_CREATE, &ioc_create) < 0) {
			std::cerr << "Failed to create domain" << std::endl;
			return -1;
		}

		for (auto it : schema.images) {
			Image(it.second).loadAt(session, it.first);
		}

        return 0;
    }

    define start(const Schema& schema) -> int {
		int ret;
		hvx_proto_vcpu_context ioc_vcpu = {
			.entry = 0,
			.affinity = 0,
			.contextid = 0,
		};

		if ((ret = session->ioctl(HVX_IOCTL_VCPU_CONTEXT, &ioc_vcpu)) < 0) {
			std::cerr << "Failed to create domain" << std::endl;
			return -1;
		}

		return ret;
	}

	define destroy() -> int {
        hvx_proto_domain_destroy ioc_destroy;

		if (session->ioctl(HVX_IOCTL_DOMAIN_DESTROY, &ioc_destroy) < 0) {
			std::cerr << "Failed to destroy domain" << std::endl;
			return -1;
		}

		return 0;
	}

private:
    Session* session;
};

class Instance {
public:
	define run(const Schema& schema, bool& stop) -> int {
		int ret = -1;
		Session* session = Session::create();
		Domain domain(session);
		domain.create(schema);
        int handle = domain.start(schema);
		if (handle > 0) {
			ret = session->eventloop(stop);
		}
		domain.destroy();

        return ret;
	}
};

define main(int argc, char* argv[]) -> int {
	OptionRegistry<Schema> options;
    options.createOption<cli::Usage>('h', false, "Show usage")
	       .createOption<cli::Vcpu>('v', true, "number of vcpus")
	       .createOption<cli::Memory>('m', true, "memory size")
	       .createOption<cli::Image>('i', true, "image to be loaded");

	if (options.parse(--argc, &argv[1]) != 0) {
        usage(argv[0]);
		return EXIT_FAILURE;
	}

	::signal(SIGINT, signalHandler);

    try {
        if (Instance().run(options.schema, exitFlag) < 0) {
			std::cerr << "Failed to launch domain" << std::endl;
			return EXIT_FAILURE;
		}
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}
