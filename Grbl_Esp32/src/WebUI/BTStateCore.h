#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class BTState : uint8_t {
    Idle,
    Advertising,
    Connected,
    Congested,
    Recovering,
};

enum class BTLinkEvent : uint8_t {
    Started,
    Stopped,
    Opened,
    Closed,
    CongestionChanged,
    WriteCompleted,
    DataReceived,
};

inline BTState bt_state_reduce(BTState current, BTLinkEvent event, bool congested = false) {
    switch (event) {
        case BTLinkEvent::Started:
            return BTState::Advertising;
        case BTLinkEvent::Stopped:
            return BTState::Idle;
        case BTLinkEvent::Opened:
            return BTState::Connected;
        case BTLinkEvent::Closed:
            return BTState::Advertising;
        case BTLinkEvent::CongestionChanged:
        case BTLinkEvent::WriteCompleted:
            return congested ? BTState::Congested : BTState::Connected;
        case BTLinkEvent::DataReceived:
            return current == BTState::Advertising ? BTState::Connected : current;
        default:
            return current;
    }
}

inline bool bt_tx_message_is_critical_core(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    if (text[0] == 'o' && text[1] == 'k') {
        return true;
    }
    if (std::strncmp(text, "error:", 6) == 0 || std::strncmp(text, "error ", 6) == 0) {
        return true;
    }
    if (text[0] == '<') {
        return true;
    }
    if (std::strncmp(text, "[MSG:", 5) == 0) {
        const char* inner = text + 5;
        if (std::strncmp(inner, "[BT-EOL", 7) == 0 || std::strncmp(inner, "[PaperDiag]", 11) == 0 ||
            std::strncmp(inner, "[BTState]", 9) == 0) {
            return false;
        }
        return true;
    }
    return std::strncmp(text, "ALARM:", 6) == 0 || std::strncmp(text, "ALM:", 4) == 0;
}

class BTTxRing {
public:
    BTTxRing(uint8_t* storage, size_t capacity) : _storage(storage), _capacity(capacity) {}

    void initialize() {
        _head       = 0;
        _tail       = 0;
        _used       = 0;
        _generation = 0;
    }

    void reset() {
        _head = 0;
        _tail = 0;
        _used = 0;
        ++_generation;
    }

    size_t   used() const { return _used; }
    size_t   free() const { return _capacity - _used; }
    uint32_t generation() const { return _generation; }

    bool push(const char* data, size_t length) {
        if (data == nullptr || length > free() || _capacity == 0) {
            return false;
        }
        for (size_t index = 0; index < length; ++index) {
            _storage[_head] = static_cast<uint8_t>(data[index]);
            _head           = (_head + 1) % _capacity;
        }
        _used += length;
        return true;
    }

    size_t copy_contiguous(uint8_t* output, size_t output_capacity, uint32_t* generation) const {
        if (generation != nullptr) {
            *generation = _generation;
        }
        if (output == nullptr || output_capacity == 0 || _used == 0 || _capacity == 0) {
            return 0;
        }
        size_t contiguous = _head > _tail ? _head - _tail : _capacity - _tail;
        if (contiguous > _used) {
            contiguous = _used;
        }
        if (contiguous > output_capacity) {
            contiguous = output_capacity;
        }
        std::memcpy(output, &_storage[_tail], contiguous);
        return contiguous;
    }

    bool advance(size_t length, uint32_t expected_generation) {
        if (expected_generation != _generation) {
            return false;
        }
        if (length > _used) {
            length = _used;
        }
        if (_capacity != 0) {
            _tail = (_tail + length) % _capacity;
        }
        _used -= length;
        return true;
    }

private:
    uint8_t* _storage;
    size_t   _capacity;
    size_t   _head       = 0;
    size_t   _tail       = 0;
    size_t   _used       = 0;
    uint32_t _generation = 0;
};
