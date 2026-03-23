#pragma once

/*
  InputBuffer.h -  inputbuffer functions class

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

#include <Print.h>
#include <cstring>

namespace WebUI {
    class InputBuffer : public Print {
    public:
        InputBuffer();

        size_t        write(uint8_t c);
        size_t        write(const uint8_t* buffer, size_t size);
        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }
        void          begin();
        void          end();
        int           available();
        int           availableforwrite();
        int           peek(void);
        int           read(void);
        bool          push(const char* data);
        void          flush(void);

        operator bool() const;

        ~InputBuffer();

    private:
        // 蓝牙/网络类客户端更容易出现突发小包，原 256B 缓冲不足会导致解析节奏抖动（表现为运动卡顿）
        // 将其增大可降低溢出/等待概率，同时对内存开销可控。
        // 提高客户端输入缓冲，降低 BT 突发小包导致的缓冲跟不上/解析节奏抖动
        static const int RXBUFFERSIZE = 2048;

        uint8_t  _RXbuffer[RXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;
    };

    extern InputBuffer inputBuffer;
}
