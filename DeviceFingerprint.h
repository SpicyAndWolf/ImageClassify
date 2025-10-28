// DeviceFingerprint.h
#pragma once
#include <QString>

namespace DeviceFingerprint {
  QString machineId(); // 稳定指纹：HostName + 主物理网卡MAC → SHA256(hex)
}
