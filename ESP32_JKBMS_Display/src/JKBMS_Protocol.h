#ifndef JKBMS_PROTOCOL_H
#define JKBMS_PROTOCOL_H

#include <stdint.h>
#include <Arduino.h>

#define JK_FRAME_HEADER_0 0x4A
#define JK_FRAME_HEADER_1 0x4B
#define JK_FRAME_HEADER_2 0xFF
#define JK_FRAME_HEADER_3 0x5A

#pragma pack(push, 1)

typedef struct {
    uint8_t header[4];
    uint8_t data_length[2];
    uint8_t frame_type;
    uint8_t product_info[10];
    uint16_t total_voltage;
    int16_t current;
    uint16_t capacity_remaining;
    uint16_t capacity_total;
    uint8_t cycle_count[2];
    uint16_t charge_status;
    uint16_t discharge_status;
    uint8_t cell_count;
    uint8_t temperature_sensors;
    int16_t temps[15];
    uint16_t cell_voltages[32][2];
    uint8_t bms_status[8];
    uint8_t protection_status[8];
    uint8_t version[4];
    uint8_t checksum;
} JK02Frame;

typedef struct {
    float totalVoltage;
    float current;
    float capacityRemaining;
    float capacityTotal;
    uint16_t cycleCount;
    uint8_t cellCount;
    int16_t temperature1;
    int16_t temperature2;
    float cellVoltages[32];
    uint8_t chargeStatus;
    uint8_t dischargeStatus;
    uint16_t protectionFlags;
    uint8_t soc;
    float power;
} JKBMSData;

class JKBMSProtocol {
public:
    JKBMSProtocol();
    bool parseFrame(const uint8_t* data, size_t len);
    JKBMSData getData();
    bool isDataValid();
    uint8_t getCellCount();
    
private:
    JKBMSData data;
    bool dataValid;
    uint16_t calculateCRC(const uint8_t* data, size_t len);
    uint16_t readUint16BE(const uint8_t* ptr);
    int16_t readInt16BE(const uint8_t* ptr);
};

#endif
