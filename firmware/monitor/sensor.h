#ifndef SENSOR_H
#define SENSOR_H

#include <Wire.h>
#include "config.h"

// SHT40 I2C 默认 7 位地址
#define SHT40_I2C_ADDR 0x44

// SHT40 高精度温湿度测量命令
#define SHT40_MEASURE_HIGH_PRECISION 0xFD

namespace Sensor {

    // 传感器连续读取失败计数 (static 限定为文件内部链接，防止多重定义链接错误)
    static int failed_count = 0;

    /**
     * @brief Sensirion 标准 CRC8 校验算法
     * 
     * @param data 需要计算 CRC 的数据缓冲区
     * @param count 字节数
     * @return uint8_t 计算出的 CRC8 值
     */
    uint8_t calculate_crc(const uint8_t* data, uint16_t count) {
        uint8_t crc = 0xFF; // 初始值为 0xFF
        for (uint16_t i = 0; i < count; ++i) {
            crc ^= data[i];
            for (uint8_t bit = 8; bit > 0; --bit) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0x31; // 多项式 0x31 (x^8 + x^5 + x^4 + 1)
                } else {
                    crc = (crc << 1);
                }
            }
        }
        return crc;
    }

    /**
     * @brief 初始化 SHT40 传感器的 I2C 总线
     */
    void init() {
        Serial.println("[Sensor] 初始化 I2C 总线...");
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        failed_count = 0;
    }

    /**
     * @brief 软重启 I2C 总线，隔离干扰导致的锁死
     */
    void reset_bus() {
        Serial.println("[Sensor] 警告: I2C 读取连续失败, 尝试重建 I2C 总线...");
        Wire.end();
        delay(100);
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        failed_count = 0;
    }

    /**
     * @brief 读取 SHT40 温湿度数据
     * 
     * @param temperature 输出温度变量指针
     * @param humidity 输出湿度变量指针
     * @return true 读取并校验成功
     * @return false 读取或校验失败
     */
    bool read(float* temperature, float* humidity) {
        // 1. 发送高精度测温指令
        Wire.beginTransmission(SHT40_I2C_ADDR);
        Wire.write(SHT40_MEASURE_HIGH_PRECISION);
        if (Wire.endTransmission() != 0) {
            failed_count++;
            Serial.printf("[Sensor] I2C 写入命令失败 (连续失败 %d 次)\n", failed_count);
            if (failed_count >= 5) {
                reset_bus();
            }
            return false;
        }

        // 2. 等待测量完成 (SHT40 高精度模式最长 8.3ms，这里等待 15ms 确保就绪)
        delay(15);

        // 3. 请求读取 6 字节数据
        // Byte 0-1: 温度原始值, Byte 2: 温度 CRC
        // Byte 3-4: 湿度原始值, Byte 5: 湿度 CRC
        uint8_t buffer[6];
        uint8_t bytes_read = Wire.requestFrom(SHT40_I2C_ADDR, 6);
        
        if (bytes_read != 6) {
            failed_count++;
            Serial.printf("[Sensor] I2C 请求数据字节数错误: %d (连续失败 %d 次)\n", bytes_read, failed_count);
            if (failed_count >= 5) {
                reset_bus();
            }
            return false;
        }

        for (int i = 0; i < 6; i++) {
            buffer[i] = Wire.read();
        }

        // 4. CRC8 校验温度数据
        if (calculate_crc(&buffer[0], 2) != buffer[2]) {
            failed_count++;
            Serial.printf("[Sensor] 温度校验和 (CRC) 错误 (连续失败 %d 次)\n", failed_count);
            if (failed_count >= 5) {
                reset_bus();
            }
            return false;
        }

        // 5. CRC8 校验湿度数据
        if (calculate_crc(&buffer[3], 2) != buffer[5]) {
            failed_count++;
            Serial.printf("[Sensor] 湿度校验和 (CRC) 错误 (连续失败 %d 次)\n", failed_count);
            if (failed_count >= 5) {
                reset_bus();
            }
            return false;
        }

        // 6. 校验通过，重置失败计数
        failed_count = 0;

        // 7. 计算物理量
        uint16_t raw_temp = (buffer[0] << 8) | buffer[1];
        uint16_t raw_humi = (buffer[3] << 8) | buffer[4];

        // Sensirion 官方转换公式
        *temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
        *humidity = -6.0f + 125.0f * ((float)raw_humi / 65535.0f);

        // 湿度物理限制校正 [0, 100]%
        if (*humidity < 0.0f) *humidity = 0.0f;
        if (*humidity > 100.0f) *humidity = 100.0f;

        return true;
    }
}

#endif // SENSOR_H
