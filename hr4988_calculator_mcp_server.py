#!/usr/bin/env python3
"""
HR4988 Current Calculator MCP Server
Provides precise current calculation tools for HR4988 stepper drivers
"""

import sys
import os
import json
import math

def calculate_vref_from_current(current_amps, sense_resistor=0.05):
    """Calculate VREF voltage from desired current"""
    # I_TRIP = V_REF / (8 × R_SENSE)
    # V_REF = I_TRIP × 8 × R_SENSE
    vref = current_amps * 8 * sense_resistor
    return {
        "current_amps": current_amps,
        "vref_voltage": round(vref, 3),
        "sense_resistor": sense_resistor,
        "formula": f"VREF = {current_amps}A × 8 × {sense_resistor}Ω = {round(vref, 3)}V"
    }

def calculate_current_from_vref(vref_voltage, sense_resistor=0.05):
    """Calculate motor current from VREF voltage"""
    # I_TRIP = V_REF / (8 × R_SENSE)
    current = vref_voltage / (8 * sense_resistor)
    return {
        "vref_voltage": vref_voltage,
        "current_amps": round(current, 3),
        "sense_resistor": sense_resistor,
        "formula": f"Current = {vref_voltage}V / (8 × {sense_resistor}Ω) = {round(current, 3)}A"
    }

def recommend_current_for_motor(motor_specs):
    """Recommend safe current settings based on motor specifications"""
    rated_current = motor_specs.get("rated_current", 1.0)
    holding_torque = motor_specs.get("holding_torque", 0)
    
    # Safety recommendations
    recommendations = {
        "idle_current": round(rated_current * 0.3, 2),  # 30% for holding
        "normal_current": round(rated_current * 0.7, 2),  # 70% for normal operation
        "high_torque_current": round(rated_current * 0.9, 2),  # 90% for high torque
        "max_current": round(rated_current, 2)  # 100% maximum
    }
    
    # Calculate VREF values
    vref_values = {}
    for mode, current in recommendations.items():
        calc = calculate_vref_from_current(current)
        vref_values[mode] = calc["vref_voltage"]
    
    return {
        "motor_specs": motor_specs,
        "current_recommendations": recommendations,
        "vref_values": vref_values,
        "safety_note": "Never exceed motor rated current. High currents may cause overheating."
    }

def calculate_power_consumption(current_amps, supply_voltage=24):
    """Calculate power consumption and heat dissipation"""
    # Power = V × I
    total_power = supply_voltage * current_amps
    # HR4988 efficiency approximately 80%
    motor_power = total_power * 0.8
    driver_power = total_power * 0.2  # Heat dissipated in driver
    
    return {
        "current_amps": current_amps,
        "supply_voltage": supply_voltage,
        "total_power_watts": round(total_power, 2),
        "motor_power_watts": round(motor_power, 2),
        "driver_heat_watts": round(driver_power, 2),
        "efficiency_percent": 80
    }

def generate_dac_settings(target_voltage, dac_resolution=8, vref_dac=3.3):
    """Generate DAC settings for ESP32 to achieve target VREF"""
    max_dac_value = (2 ** dac_resolution) - 1
    # ESP32 DAC: 0-255 maps to 0-3.3V
    dac_value = int((target_voltage / vref_dac) * max_dac_value)
    actual_voltage = (dac_value / max_dac_value) * vref_dac
    
    return {
        "target_voltage": target_voltage,
        "dac_value": dac_value,
        "actual_voltage": round(actual_voltage, 3),
        "voltage_error": round(abs(actual_voltage - target_voltage), 3),
        "dac_code_hex": f"0x{dac_value:02X}",
        "resolution": dac_resolution
    }

def analyze_thermal_performance(current_amps, ambient_temp=25):
    """Analyze thermal performance at given current"""
    # HR4988 thermal resistance (junction-to-ambient)
    r_theta_ja = 40  # °C/W typical without heatsink
    # Power dissipation in driver
    power = current_amps * 2.5 * 0.2  # Approximate voltage drop × efficiency loss
    temperature_rise = power * r_theta_ja
    junction_temp = ambient_temp + temperature_rise
    
    return {
        "current_amps": current_amps,
        "ambient_temp_c": ambient_temp,
        "power_dissipation_w": round(power, 2),
        "temperature_rise_c": round(temperature_rise, 1),
        "junction_temp_c": round(junction_temp, 1),
        "thermal_status": "Safe" if junction_temp < 85 else "Warning",
        "recommendation": "Consider heatsink" if junction_temp > 70 else "Normal operation"
    }

def main():
    """Main MCP server loop"""
    print("HR4988 Current Calculator MCP Server started", file=sys.stderr)
    
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
                                "name": "calculate_vref_from_current",
                                "description": "Calculate VREF voltage from desired current"
                            },
                            {
                                "name": "calculate_current_from_vref",
                                "description": "Calculate motor current from VREF voltage"
                            },
                            {
                                "name": "recommend_current_for_motor",
                                "description": "Recommend safe current settings for motor"
                            },
                            {
                                "name": "calculate_power_consumption",
                                "description": "Calculate power consumption and heat dissipation"
                            },
                            {
                                "name": "generate_dac_settings",
                                "description": "Generate DAC settings for ESP32 VREF control"
                            },
                            {
                                "name": "analyze_thermal_performance",
                                "description": "Analyze thermal performance at given current"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                arguments = request["params"].get("arguments", {})
                
                if tool_name == "calculate_vref_from_current":
                    current = arguments.get("current_amps", 0.5)
                    sense_r = arguments.get("sense_resistor", 0.05)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(calculate_vref_from_current(current, sense_r), indent=2)}]}}
                elif tool_name == "calculate_current_from_vref":
                    voltage = arguments.get("vref_voltage", 0.2)
                    sense_r = arguments.get("sense_resistor", 0.05)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(calculate_current_from_vref(voltage, sense_r), indent=2)}]}}
                elif tool_name == "recommend_current_for_motor":
                    specs = arguments.get("motor_specs", {"rated_current": 1.0, "holding_torque": 0})
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(recommend_current_for_motor(specs), indent=2)}]}}
                elif tool_name == "calculate_power_consumption":
                    current = arguments.get("current_amps", 0.5)
                    voltage = arguments.get("supply_voltage", 24)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(calculate_power_consumption(current, voltage), indent=2)}]}}
                elif tool_name == "generate_dac_settings":
                    target_v = arguments.get("target_voltage", 0.2)
                    resolution = arguments.get("dac_resolution", 8)
                    vref_dac = arguments.get("vref_dac", 3.3)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(generate_dac_settings(target_v, resolution, vref_dac), indent=2)}]}}
                elif tool_name == "analyze_thermal_performance":
                    current = arguments.get("current_amps", 0.5)
                    ambient = arguments.get("ambient_temp", 25)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(analyze_thermal_performance(current, ambient), indent=2)}]}}
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