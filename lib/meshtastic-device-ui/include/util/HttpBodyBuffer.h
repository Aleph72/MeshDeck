#pragma once

#include <Stream.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

class HttpBodyBuffer final : public Stream
{
  public:
    HttpBodyBuffer(uint8_t *data, size_t capacity) : data(data), capacity(capacity) {}

    size_t write(uint8_t value) override { return write(&value, 1); }

    size_t write(const uint8_t *source, size_t count) override
    {
        if (count > capacity - used) {
            overflow = true;
            return 0;
        }
        if (count)
            memcpy(data + used, source, count);
        used += count;
        return count;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    size_t size() const { return used; }
    bool overflowed() const { return overflow; }

  private:
    uint8_t *data;
    size_t capacity;
    size_t used = 0;
    bool overflow = false;
};
