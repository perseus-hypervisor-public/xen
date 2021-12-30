#pragma once

#include <string>
#include <unordered_map>

#include "property.h"

template<typename Schema>
class Option {
public:
    Option() {}
	virtual ~Option() {}

	virtual int capture(Schema& schema, const std::string& option) = 0;

    inline bool isValueRequired() {
        return valueRequired;
    }

   template<class T> friend class OptionRegistry;
private:
    bool valueRequired{false};
};

template<typename Schema>
class OptionRegistry {
public:
    template<typename Command>
    auto createOption(const char id, bool needValue, const std::string& desc) -> OptionRegistry& {
        Command *cmd = new Command();
        cmd->valueRequired = needValue;
        commandMap[id] = cmd;
        return *this;
    }

    auto parse(int argc, char *argv[]) -> int {
        if (commandMap.size() == 0)
            return 0;

        if (argc == 0) {
            std::cerr << "No argument found" << std::endl;
            return -1;
        }

        int idx = 0;
        while (idx < argc) {
            char *ptr = argv[idx];
            if (*ptr == '-') {
                char key = *(++ptr);
                ptr++;
                while (*ptr == ' ') ptr++;
                if (!commandMap.count(key)) {
                    std::cerr << "unknown option" << std::endl;
                    return -1;
                }

                Option<Schema> *option = commandMap[key];
                std::string value;
                if (option->isValueRequired()) {
                    idx++;
                    if (idx >= argc || *argv[idx] == '-') {
                        std::cerr << "Option -" << key << " requires value" << std::endl;
                        return -1;
                    }
                    value = argv[idx]; 
                }
                if (option->capture(schema, value) != 0) {
                    return -1;
                }
            } else {
                std::cerr << "Invalid argument" << std::endl;
            }

            idx++;
        }

        return 0;
    }

    Schema schema;

private:
    std::unordered_map<char, Option<Schema> *> commandMap;
};
