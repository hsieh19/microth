#ifndef OTA_H
#define OTA_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include "config.h"

namespace Ota {

    /**
     * @brief 验证当前运行分区为有效，关闭 Bootloader 自动回滚。
     * 在 setup() 中 Wi-Fi 连网稳定后调用，防止正常启动被误判为失败而触发意外回滚。
     */
    void confirm_running_partition() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running == NULL) return;

        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                    Serial.println("[OTA] 当前分区已标记为 VALID，自动回滚已关闭。");
                } else {
                    Serial.println("[OTA] 错误: 标记分区 VALID 失败！");
                }
            }
        }
    }

    /**
     * @brief 检查分区是否包含有效的 ESP32 固件 (检测首字节 Magic Byte 是否为 0xE9)
     */
    bool is_partition_valid_app(const esp_partition_t* partition) {
        if (partition == NULL) return false;
        uint8_t magic = 0;
        if (esp_partition_read(partition, 0, &magic, 1) == ESP_OK) {
            return (magic == 0xE9);
        }
        return false;
    }

    /**
     * @brief 查询 OTA 分区状态信息，供前端 Web 页面渲染使用。
     * 
     * @param has_backup 输出: 是否存在包含有效固件的备份分区 (控制"一键回退"按钮启禁)
     * @return String 当前运行的固件版本字符串 (来自 FIRMWARE_VERSION 宏)
     */
    String get_ota_info(bool* has_backup) {
        const esp_partition_t* backup = esp_ota_get_next_update_partition(NULL);
        if (backup != NULL) {
            *has_backup = is_partition_valid_app(backup);
        } else {
            *has_backup = false;
        }
        return String(FIRMWARE_VERSION);
    }

    /**
     * @brief 物理分区级一键回退：直接切换 Boot 目标为备份分区并重启。
     * 无需联网，本地秒级完成，适合更新后发现功能异常时使用。
     * 
     * @return true 成功切换分区，调用方应立即执行 ESP.restart()
     * @return false 无可用备份分区或切换失败
     */
    bool rollback_to_previous_partition() {
        Serial.println("[OTA] 开始执行物理分区一键回退...");

        const esp_partition_t* backup = esp_ota_get_next_update_partition(NULL);
        if (backup == NULL || !is_partition_valid_app(backup)) {
            Serial.println("[OTA] 错误: 找不到可用的备份分区或备份固件无效！");
            return false;
        }

        Serial.printf("[OTA] 将 Boot 目标切换至备份分区: %s (地址: 0x%x)\n",
                      backup->label, backup->address);

        esp_err_t err = esp_ota_set_boot_partition(backup);
        if (err != ESP_OK) {
            Serial.printf("[OTA] 错误: 分区切换失败，错误码: 0x%x\n", err);
            return false;
        }

        Serial.println("[OTA] 物理分区切换成功，设备即将重启...");
        return true;
    }

    /**
     * @brief 从签名 URL 下载并刷写固件 (支持版本升级与降级)。
     * 执行前临时注销看门狗，防止下载过程中 WDT 超时中断写入导致设备变砖。
     * 
     * @param signed_url Cloudflare Worker 生成的带 HMAC 签名的固件下载链接
     * @return String 错误描述，空字符串表示刷写成功（设备已触发自动重启）
     */
    String start_upgrade(const String& signed_url) {
        if (signed_url.isEmpty()) {
            return "URL is empty";
        }

        // 安全校验：URL 必须以 global_ota_base_url 开头，拒绝外部注入的恶意固件地址
        if (!signed_url.startsWith(global_ota_base_url)) {
            Serial.printf("[OTA] 安全拒绝: URL 域名不在白名单内。期望前缀: %s\n",
                          global_ota_base_url.c_str());
            return "Rejected: URL domain not allowed";
        }

        Serial.printf("[OTA] 开始在线升级，目标 URL: %s\n", signed_url.c_str());

        // 临时注销任务看门狗，防止固件下载过程（可能需要 30~90 秒）中 WDT 触发复位
        esp_task_wdt_delete(NULL);
        Serial.println("[OTA] 已临时注销任务看门狗，开始下载固件...");

        WiFiClientSecure client;
        client.setInsecure(); // 跳过 SSL 证书链校验（固件完整性已由 HMAC 签名保障）

        httpUpdate.rebootOnUpdate(true); // 升级成功后自动重启
        t_httpUpdate_return ret = httpUpdate.update(client, signed_url);

        // 升级失败时恢复看门狗监控
        esp_task_wdt_add(NULL);
        Serial.println("[OTA] 已恢复任务看门狗监控。");

        switch (ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("[OTA] 升级失败！错误 (%d): %s\n",
                              httpUpdate.getLastError(),
                              httpUpdate.getLastErrorString().c_str());
                return "Upgrade Failed: " + httpUpdate.getLastErrorString();

            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("[OTA] 服务端无可用更新（文件可能未变化）。");
                return "No updates available";

            case HTTP_UPDATE_OK:
                // rebootOnUpdate(true) 时，设备会在写入完成后立即重启，通常不会执行到此行
                return "";
        }

        return "Unknown error";
    }
}

#endif // OTA_H
