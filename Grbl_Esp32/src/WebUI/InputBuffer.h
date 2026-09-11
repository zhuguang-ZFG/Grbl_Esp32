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
        // 回移植自主线 ed9073b（只摘缓冲放大一项）：原 256B 客户端输入缓冲小于 S3 管道
        // 512B 在途窗口，解析稍慢即溢出丢字节 → 行粘连 → error:21（2026-09-11 量产机
        // 三次实机流内中断的根触发形态）。2048B 与现役写字机一致。
        static const int RXBUFFERSIZE = 2048;

        uint8_t  _RXbuffer[RXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;
    };

    extern InputBuffer inputBuffer;
}
