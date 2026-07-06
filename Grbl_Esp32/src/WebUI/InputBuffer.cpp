/*
  InputBuffer.cpp -  inputbuffer functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../Config.h"
#include "InputBuffer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace WebUI {
    InputBuffer inputBuffer;

    // push 由 controlCheckTask / WebServer 任务调用，read 由 protocol 主任务调用；
    // 双核并发下对 _RXbufferSize/_RXbufferpos 的读-改-写会丢更新，用临界区保护。
    static portMUX_TYPE _inputBufMux = portMUX_INITIALIZER_UNLOCKED;

    InputBuffer::InputBuffer() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    void InputBuffer::begin() {
        portENTER_CRITICAL(&_inputBufMux);
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
        portEXIT_CRITICAL(&_inputBufMux);
    }

    void InputBuffer::end() {
        portENTER_CRITICAL(&_inputBufMux);
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
        portEXIT_CRITICAL(&_inputBufMux);
    }

    InputBuffer::operator bool() const { return true; }

    int InputBuffer::available() {
        portENTER_CRITICAL(&_inputBufMux);
        int n = _RXbufferSize;
        portEXIT_CRITICAL(&_inputBufMux);
        return n;
    }

    int InputBuffer::availableforwrite() {
        portENTER_CRITICAL(&_inputBufMux);
        int n = RXBUFFERSIZE - _RXbufferSize;
        portEXIT_CRITICAL(&_inputBufMux);
        return n;
    }

    size_t InputBuffer::write(uint8_t c) {
        portENTER_CRITICAL(&_inputBufMux);
        if ((1 + _RXbufferSize) <= RXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > RXBUFFERSIZE) {
                current = current - RXBUFFERSIZE;
            }
            if (current > (RXBUFFERSIZE - 1)) {
                current = 0;
            }
            _RXbuffer[current] = c;
            _RXbufferSize += 1;
            portEXIT_CRITICAL(&_inputBufMux);
            return 1;
        }
        portEXIT_CRITICAL(&_inputBufMux);
        return 0;
    }

    size_t InputBuffer::write(const uint8_t* buffer, size_t size) {
        //No need currently
        //keep for compatibility
        return size;
    }

    int InputBuffer::peek(void) {
        portENTER_CRITICAL(&_inputBufMux);
        if (_RXbufferSize > 0) {
            int v = _RXbuffer[_RXbufferpos];
            portEXIT_CRITICAL(&_inputBufMux);
            return v;
        } else {
            portEXIT_CRITICAL(&_inputBufMux);
            return -1;
        }
    }

    bool InputBuffer::push(const char* data) {
        int data_size = strlen(data);
        portENTER_CRITICAL(&_inputBufMux);
        if ((data_size + _RXbufferSize) <= RXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > RXBUFFERSIZE) {
                current = current - RXBUFFERSIZE;
            }
            for (int i = 0; i < data_size; i++) {
                if (current > (RXBUFFERSIZE - 1)) {
                    current = 0;
                }
                _RXbuffer[current] = data[i];
                current++;
            }
            _RXbufferSize += data_size;
            portEXIT_CRITICAL(&_inputBufMux);
            return true;
        }
        portEXIT_CRITICAL(&_inputBufMux);
        return false;
    }

    int InputBuffer::read(void) {
        portENTER_CRITICAL(&_inputBufMux);
        if (_RXbufferSize > 0) {
            int v = _RXbuffer[_RXbufferpos];
            _RXbufferpos++;
            if (_RXbufferpos > (RXBUFFERSIZE - 1)) {
                _RXbufferpos = 0;
            }
            _RXbufferSize--;
            portEXIT_CRITICAL(&_inputBufMux);
            return v;
        } else {
            portEXIT_CRITICAL(&_inputBufMux);
            return -1;
        }
    }

    void InputBuffer::flush(void) {
        //No need currently
        //keep for compatibility
    }

    InputBuffer::~InputBuffer() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }
}
