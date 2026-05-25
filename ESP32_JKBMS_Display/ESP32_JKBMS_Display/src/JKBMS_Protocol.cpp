#include "JKBMS_Protocol.h"

JKBMSProtocol::JKBMSProtocol() {
    memset(&data, 0, sizeof(JKBMSData));
    dataValid = false;
}

uint16_t JKBMSProtocol::readUint16BE(const uint8_t* ptr) {
    return ((uint16_t)ptr[0] << 8) | ptr[1];
}

int16_t JKBMSProtocol::readInt16BE(const uint8_t* ptr) {
    return ((int16_t)ptr[0] << 8) | ptr[1];
}

uint16_t JKBMSProtocol::calculateCRC(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool JKBMSProtocol::parseFrame(const uint8_t* frameData, size_t len) {
    if (len < 50) return false;
    
    if (frameData[0] != JK_FRAME_HEADER_0 || 
        frameData[1] != JK_FRAME_HEADER_1 ||
        frameData[2] != JK_FRAME_HEADER_2 ||
        frameData[3] != JK_FRAME_HEADER_3) {
        return false;
    }
    
    uint16_t dataLen = readUint16BE(&frameData[4]);
    if (dataLen > len - 8) return false;
    
    uint16_t calculatedCRC = calculateCRC(frameData, dataLen + 6);
    uint16_t frameCRC = readUint16BE(&frameData[dataLen + 6]);
    
    if (calculatedCRC != frameCRC) {
        Serial.println("CRC mismatch");
        return false;
    }
    
    uint8_t frameType = frameData[6];
    
    if (frameType == 0x03) {
        data.totalVoltage = readUint16BE(&frameData[20]) / 100.0f;
        data.current = readInt16BE(&frameData[22]) / 100.0f;
        data.capacityRemaining = readUint16BE(&frameData[24]) / 100.0f;
        data.capacityTotal = readUint16BE(&frameData[26]) / 100.0f;
        data.cycleCount = readUint16BE(&frameData[28]);
        
        uint8_t tempSensorCount = frameData[33];
        if (tempSensorCount >= 2) {
            int16_t temp1Raw = readInt16BE(&frameData[35]);
            int16_t temp2Raw = readInt16BE(&frameData[37]);
            data.temperature1 = temp1Raw - 2731;
            data.temperature2 = temp2Raw - 2731;
        }
        
        uint8_t cellCount = frameData[45];
        data.cellCount = cellCount;
        
        for (int i = 0; i < cellCount && i < 32; i++) {
            uint16_t cellVoltageRaw = readUint16BE(&frameData[47 + i * 2]);
            data.cellVoltages[i] = cellVoltageRaw / 1000.0f;
        }
        
        data.chargeStatus = frameData[120];
        data.dischargeStatus = frameData[121];
        data.protectionFlags = readUint16BE(&frameData[122]);
        
        if (data.capacityTotal > 0) {
            data.soc = (data.capacityRemaining / data.capacityTotal) * 100.0f;
        }
        
        data.power = data.totalVoltage * data.current;
        
        dataValid = true;
        return true;
    }
    
    return false;
}

JKBMSData JKBMSProtocol::getData() {
    return data;
}

bool JKBMSProtocol::isDataValid() {
    return dataValid;
}

uint8_t JKBMSProtocol::getCellCount() {
    return data.cellCount;
}
