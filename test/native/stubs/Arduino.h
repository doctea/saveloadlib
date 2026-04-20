#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <string>

#define F(x) x

class String {
public:
    String() = default;
    String(const char* s) : data_(s ? s : "") {}
    String(const std::string& s) : data_(s) {}

    const char* c_str() const { return data_.c_str(); }

    void toCharArray(char* out, size_t len) const {
        if (!out || len == 0) return;
        std::strncpy(out, data_.c_str(), len - 1);
        out[len - 1] = '\0';
    }

private:
    std::string data_;
};

struct SerialStub {
    template <typename... Args>
    int printf(const char* fmt, Args... args) {
        return std::printf(fmt, args...);
    }

    void println(const char* s) {
        if (s) std::printf("%s\n", s);
        else std::printf("\n");
    }

    explicit operator bool() const { return true; }
};

extern SerialStub Serial;
