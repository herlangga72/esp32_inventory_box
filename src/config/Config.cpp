#include "Config.h"

namespace Config {
    const char* NVS_NAMESPACE = "inventory_box";
    const int MAX_TOOLS = 20;
    const int MAX_USERS = 50;
    const int MAX_LOGS = 500;
    const float DEFAULT_TOLERANCE = 5.0f;
    const float CALIBRATION_FACTOR = -471.0f;  // Adjust for your load cell
}