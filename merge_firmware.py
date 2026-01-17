#!/usr/bin/env python3
"""
合并ESP32固件脚本
将bootloader、分区表和应用程序合并为一个完整的固件文件
"""

import os
import sys
import subprocess
import shutil

def find_esptool():
    """查找esptool.py路径"""
    # 常见的PlatformIO esptool路径
    possible_paths = [
        r"C:\Users\Administrator\.platformio\packages\tool-esptoolpy@1.30000.201119\esptool.py",
        r"C:\Users\Administrator\.platformio\packages\tool-esptoolpy\esptool.py"
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    return None

def find_bootloader_files():
    """查找bootloader和分区表文件"""
    pio_dir = r"F:\BaiduNetdiskDownload\code\GRBL\Grbl_Esp32\.pio"
    
    # 搜索可能的bootloader文件
    bootloader_bin = None
    partition_bin = None
    app_bin = None
    
    for root, dirs, files in os.walk(pio_dir):
        for file in files:
            if file == "bootloader.bin":
                bootloader_bin = os.path.join(root, file)
            elif file == "partitions.bin" or file == "partition-table.bin":
                partition_bin = os.path.join(root, file)
            elif file == "firmware.bin":
                app_bin = os.path.join(root, file)
    
    return bootloader_bin, partition_bin, app_bin

def merge_firmware():
    """合并固件文件"""
    esptool = find_esptool()
    if not esptool:
        print("错误: 找不到esptool.py")
        return False
    
    bootloader, partition, app = find_bootloader_files()
    
    print(f"Bootloader: {bootloader}")
    print(f"Partition: {partition}")  
    print(f"Application: {app}")
    
    if not all([bootloader, partition, app]):
        print("错误: 找不到完整的固件文件")
        print("请先运行: pio run -e release")
        return False
    
    output_file = "firmware_merged.bin"
    
    # 合并命令
    cmd = [
        "python", esptool,
        "--chip", "esp32",
        "merge_bin",
        "--output", output_file,
        "0x1000", bootloader,
        "0x8000", partition, 
        "0x10000", app
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"成功生成合并固件: {output_file}")
        
        # 显示文件大小
        size = os.path.getsize(output_file)
        print(f"固件大小: {size:,} 字节 ({size/1024:.1f} KB)")
        
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"合并失败: {e}")
        print(f"输出: {e.stdout}")
        print(f"错误: {e.stderr}")
        return False

if __name__ == "__main__":
    print("ESP32 固件合并工具")
    print("=" * 40)
    
    success = merge_firmware()
    
    if success:
        print("\n合并完成！")
        print("使用方法:")
        print("esptool.py --chip esp32 --port COM16 --baud 921600 write_flash --flash_mode qio --flash_size 4MB 0x0 firmware_merged.bin")
    else:
        print("\n合并失败！")
        sys.exit(1)