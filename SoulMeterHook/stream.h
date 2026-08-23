#pragma once

// Ring buffer of self-delimiting records ([u32 LE length][frame bytes]) feeding
// the writer thread. Records are pushed and popped whole - a frame that does not
// fit is dropped entirely rather than truncated, since a partial frame would
// desync the meter's reader for every packet after it. The record layout is the
// pipe protocol, so a batch of records is a valid stream for the meter.

#include <windows.h>
#include <cstdint>
#include <cstring>

#ifndef SMH_HEADER_SIZE
#define SMH_HEADER_SIZE 6
#endif

#define SMH_MAX_RECORD (65535 + 4)

class ByteQueue {
public:
    explicit ByteQueue(size_t capacity = 8 * 1024 * 1024)
        : _cap(capacity), _buf(new uint8_t[capacity]) {
        InitializeCriticalSection(&_cs);
    }
    ~ByteQueue() {
        DeleteCriticalSection(&_cs);
        delete[] _buf;
    }

    bool PushFrame(const uint8_t* data, uint32_t len) {
        if (!data || len == 0 || len > 65535)
            return false;
        EnterCriticalSection(&_cs);
        bool ok = (Used() + (size_t)len + 4 <= _cap - 1);
        if (ok) {
            const uint8_t hdr[4] = { (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF),
                                     (uint8_t)((len >> 16) & 0xFF), (uint8_t)((len >> 24) & 0xFF) };
            WriteRaw(hdr, 4);
            WriteRaw(data, len);
        } else {
            _dropped++;
        }
        LeaveCriticalSection(&_cs);
        return ok;
    }

    uint32_t PopRecord(uint8_t* out, uint32_t maxLen) {
        EnterCriticalSection(&_cs);
        uint32_t total = 0;
        size_t used = Used();
        if (used >= 4) {
            uint32_t len = ReadU32AtTail();
            if (len > 0 && (size_t)len + 4 <= used && len + 4 <= maxLen) {
                total = len + 4;
                for (uint32_t i = 0; i < total; i++)
                    out[i] = _buf[(_tail + i) % _cap];
                _tail = (_tail + total) % _cap;
            }
        }
        LeaveCriticalSection(&_cs);
        return total;
    }

    size_t Available() {
        EnterCriticalSection(&_cs);
        size_t n = Used();
        LeaveCriticalSection(&_cs);
        return n;
    }

    uint64_t Dropped() {
        EnterCriticalSection(&_cs);
        uint64_t d = _dropped;
        LeaveCriticalSection(&_cs);
        return d;
    }

private:
    size_t Used() const { return (_head + _cap - _tail) % _cap; }

    void WriteRaw(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++)
            _buf[(_head + i) % _cap] = data[i];
        _head = (_head + len) % _cap;
    }

    uint32_t ReadU32AtTail() const {
        return (uint32_t)_buf[_tail % _cap] | ((uint32_t)_buf[(_tail + 1) % _cap] << 8) |
               ((uint32_t)_buf[(_tail + 2) % _cap] << 16) |
               ((uint32_t)_buf[(_tail + 3) % _cap] << 24);
    }

    size_t _cap;
    uint8_t* _buf;
    size_t _head = 0;
    size_t _tail = 0;
    uint64_t _dropped = 0;
    CRITICAL_SECTION _cs;
};
