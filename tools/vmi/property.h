#pragma once

template<typename T>
class Property {
public:
    Property() : value() {}
    Property(T v) : value(v) {}

    operator T() {
        return value;
    }

    auto operator =(T v) -> T {
        value = v;
    }

private:
    T value;
};
