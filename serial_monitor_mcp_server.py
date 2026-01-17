#!/usr/bin/env python3
"""
Serial Monitor MCP Server
Provides real-time serial communication and monitoring tools
"""

import sys
import os
import json
import serial
import serial.tools.list_ports
import time
import threading
from datetime import datetime

class SerialMonitor:
    def __init__(self):
        self.active_connections = {}
        self.monitor_active = False
        self.monitor_buffer = []
        self.monitor_thread = None
        
    def list_ports(self):
        """List all available serial ports"""
        ports = []
        for port in serial.tools.list_ports.comports():
            ports.append({
                "device": port.device,
                "description": port.description,
                "hwid": port.hwid,
                "vid": port.vid,
                "pid": port.pid if hasattr(port, 'pid') else None
            })
        return ports
    
    def connect_port(self, port, baudrate=115200, timeout=1):
        """Connect to a serial port"""
        try:
            if port in self.active_connections:
                return {"success": False, "error": f"Port {port} already connected"}
                
            ser = serial.Serial(port, baudrate, timeout=timeout)
            self.active_connections[port] = ser
            return {
                "success": True,
                "port": port,
                "baudrate": baudrate,
                "message": f"Connected to {port} at {baudrate} baud"
            }
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def disconnect_port(self, port):
        """Disconnect from a serial port"""
        try:
            if port in self.active_connections:
                self.active_connections[port].close()
                del self.active_connections[port]
                return {"success": True, "message": f"Disconnected from {port}"}
            else:
                return {"success": False, "error": f"Port {port} not connected"}
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def send_command(self, port, command, wait_response=True):
        """Send command to serial port and optionally wait for response"""
        try:
            if port not in self.active_connections:
                return {"success": False, "error": f"Port {port} not connected"}
            
            ser = self.active_connections[port]
            
            # Clear input buffer
            ser.reset_input_buffer()
            
            # Send command
            command_str = command + '\r\n'
            ser.write(command_str.encode())
            
            response = ""
            if wait_response:
                # Wait for response with timeout
                time.sleep(0.1)  # Wait for device to process
                while ser.in_waiting > 0:
                    response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                    time.sleep(0.05)
            
            return {
                "success": True,
                "command": command,
                "response": response.strip(),
                "timestamp": datetime.now().isoformat()
            }
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def start_monitor(self, port, max_lines=100):
        """Start monitoring serial port output"""
        try:
            if port not in self.active_connections:
                return {"success": False, "error": f"Port {port} not connected"}
            
            self.monitor_active = True
            self.monitor_buffer = []
            
            def monitor_thread():
                ser = self.active_connections[port]
                while self.monitor_active and port in self.active_connections:
                    if ser.in_waiting > 0:
                        data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        
                        for line in data.split('\n'):
                            if line.strip():
                                self.monitor_buffer.append({
                                    "timestamp": timestamp,
                                    "data": line.strip()
                                })
                                
                                # Keep buffer size limited
                                if len(self.monitor_buffer) > max_lines:
                                    self.monitor_buffer.pop(0)
                    
                    time.sleep(0.01)  # Small delay to prevent high CPU usage
            
            self.monitor_thread = threading.Thread(target=monitor_thread)
            self.monitor_thread.daemon = True
            self.monitor_thread.start()
            
            return {"success": True, "message": f"Started monitoring {port}"}
        except Exception as e:
            return {"success": False, "error": str(e)}
    
    def stop_monitor(self):
        """Stop serial monitoring"""
        self.monitor_active = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=1)
        return {"success": True, "message": "Monitoring stopped"}
    
    def get_monitor_data(self):
        """Get buffered monitor data"""
        return {"success": True, "data": self.monitor_buffer.copy()}
    
    def clear_monitor_buffer(self):
        """Clear monitor buffer"""
        self.monitor_buffer = []
        return {"success": True, "message": "Monitor buffer cleared"}

# Global monitor instance
monitor = SerialMonitor()

def main():
    """Main MCP server loop"""
    print("Serial Monitor MCP Server started", file=sys.stderr)
    
    while True:
        try:
            line = input()
            if not line:
                continue
                
            request = json.loads(line)
            method = request.get("method", "")
            
            if method == "tools/list":
                response = {
                    "result": {
                        "tools": [
                            {
                                "name": "list_serial_ports",
                                "description": "List all available serial ports"
                            },
                            {
                                "name": "connect_serial_port",
                                "description": "Connect to a serial port"
                            },
                            {
                                "name": "disconnect_serial_port",
                                "description": "Disconnect from a serial port"
                            },
                            {
                                "name": "send_serial_command",
                                "description": "Send command to serial port"
                            },
                            {
                                "name": "start_serial_monitor",
                                "description": "Start monitoring serial port output"
                            },
                            {
                                "name": "stop_serial_monitor",
                                "description": "Stop serial monitoring"
                            },
                            {
                                "name": "get_monitor_data",
                                "description": "Get buffered monitor data"
                            },
                            {
                                "name": "clear_monitor_buffer",
                                "description": "Clear monitor buffer"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                arguments = request["params"].get("arguments", {})
                
                if tool_name == "list_serial_ports":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.list_ports(), indent=2)}]}}
                elif tool_name == "connect_serial_port":
                    port = arguments.get("port")
                    baudrate = arguments.get("baudrate", 115200)
                    timeout = arguments.get("timeout", 1)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.connect_port(port, baudrate, timeout), indent=2)}]}}
                elif tool_name == "disconnect_serial_port":
                    port = arguments.get("port")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.disconnect_port(port), indent=2)}]}}
                elif tool_name == "send_serial_command":
                    port = arguments.get("port")
                    command = arguments.get("command")
                    wait_response = arguments.get("wait_response", True)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.send_command(port, command, wait_response), indent=2)}]}}
                elif tool_name == "start_serial_monitor":
                    port = arguments.get("port")
                    max_lines = arguments.get("max_lines", 100)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.start_monitor(port, max_lines), indent=2)}]}}
                elif tool_name == "stop_serial_monitor":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.stop_monitor(), indent=2)}]}}
                elif tool_name == "get_monitor_data":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.get_monitor_data(), indent=2)}]}}
                elif tool_name == "clear_monitor_buffer":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(monitor.clear_monitor_buffer(), indent=2)}]}}
                else:
                    response = {"error": {"code": -32601, "message": f"Unknown tool: {tool_name}"}}
            else:
                response = {"error": {"code": -32601, "message": f"Unknown method: {method}"}}
                
            print(json.dumps(response), flush=True)
            
        except EOFError:
            break
        except Exception as e:
            error_response = {"error": {"code": -32603, "message": str(e)}}
            print(json.dumps(error_response), flush=True)

if __name__ == "__main__":
    main()