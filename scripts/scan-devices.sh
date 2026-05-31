#!/usr/bin/env bash
# Scan for connected ESP32-family devices
# Detects: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6 via USB VID/PID

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}ESP32 Device Scanner${NC}"
echo "=============================="
echo ""

FOUND=0

# Known ESP32-family USB identifiers
# Format: "VID:PID|Description"
declare -a CHIPS=(
    "10c4:ea60|Silicon Labs CP210x UART Bridge (ESP32 classic)"
    "303a:1001|Espressif USB-JTAG/Serial (ESP32-S3/C3/C6 built-in)"
    "303a:0002|Espressif USB Bridge (ESP32-S2/S3)"
    "0403:6001|FTDI FT232R (common ESP32 dev board)"
    "0403:6010|FTDI FT2232H (ESP-Prog debug probe)"
    "0403:6014|FTDI FT231X"
    "1a86:7523|QinHeng CH340 (ESP32-CAM/common clone boards)"
    "1a86:55d4|QinHeng CH343 (newer ESP32 boards)"
    "2e8a:000a|Raspberry Pi Pico (RP2040, not ESP32)"
    "2341:0043|Arduino (ATmega16U2, not ESP32)"
)

# Method 1: PlatformIO device list (if available)
if command -v pio &>/dev/null 2>&1; then
    echo -e "${CYAN}[pio device list]${NC}"
    pio device list 2>/dev/null | while IFS= read -r line; do
        if [[ "$line" =~ /dev/tty ]]; then
            echo "  $line"
        fi
    done
    echo ""
fi

# Method 2: Scan /dev/tty* ports
echo -e "${CYAN}[USB Serial Ports]${NC}"

shopt -s nullglob
for dev in /dev/ttyUSB* /dev/ttyACM* /dev/tty.usb*; do
    [[ -e "$dev" ]] || continue
    FOUND=$((FOUND + 1))

    # Get udev info
    VID=""
    PID=""
    MANUF=""
    PRODUCT=""
    SERIAL=""

    if command -v udevadm &>/dev/null 2>&1; then
        DEVPATH=$(udevadm info --query path --name "$dev" 2>/dev/null || true)
        if [[ -n "$DEVPATH" ]]; then
            # Walk up to find USB device info
            PARENT="$DEVPATH"
            while [[ "$PARENT" != "/" && "$PARENT" != "." ]]; do
                INFO=$(udevadm info -q property -p "$PARENT" 2>/dev/null || true)
                if echo "$INFO" | grep -q "ID_VENDOR_ID="; then
                    VID=$(echo "$INFO" | grep "ID_VENDOR_ID=" | cut -d= -f2)
                    PID=$(echo "$INFO" | grep "ID_MODEL_ID=" | cut -d= -f2)
                    MANUF=$(echo "$INFO" | grep "ID_VENDOR_FROM_DATABASE=" | cut -d= -f2)
                    PRODUCT=$(echo "$INFO" | grep "ID_MODEL_FROM_DATABASE=" | cut -d= -f2)
                    SERIAL=$(echo "$INFO" | grep "ID_SERIAL_SHORT=" | cut -d= -f2)
                    break
                fi
                PARENT=$(dirname "$PARENT")
            done
        fi
    fi

    # Identify chip
    CHIP_NAME="Unknown USB device"
    CHIP_IS_ESP32=false
    KEY="${VID}:${PID}"
    for entry in "${CHIPS[@]}"; do
        if [[ "${entry%%|*}" == "$KEY" ]]; then
            CHIP_NAME="${entry##*|}"
            CHIP_IS_ESP32=true
            break
        fi
    done

    if $CHIP_IS_ESP32; then
        echo -e "  ${GREEN}✓ $dev${NC}"
        echo -e "    ${BOLD}$CHIP_NAME${NC}"
    else
        echo -e "  ${YELLOW}? $dev${NC}"
        echo -e "    $CHIP_NAME"
        if [[ -n "$MANUF" ]]; then
            echo -e "    Manufacturer: $MANUF  |  Product: $PRODUCT"
        fi
    fi

    if [[ -n "$VID" ]]; then
        echo -e "    VID:PID = ${VID}:${PID}  |  Serial: ${SERIAL:-N/A}"
    fi

    # Permission check
    if [[ -r "$dev" && -w "$dev" ]]; then
        echo -e "    Permissions: ${GREEN}OK (rw)${NC}"
    else
        echo -e "    Permissions: ${RED}NO ACCESS${NC} — run: sudo chmod a+rw $dev"
    fi
    echo ""
done

if [[ $FOUND -eq 0 ]]; then
    echo -e "  ${RED}No USB serial devices found.${NC}"
    echo ""
    echo "  Check:"
    echo "    1. Is the ESP32 plugged in via USB?"
    echo "    2. Does the cable support data? (some are power-only)"
    echo "    3. Try: lsusb | grep -i 'cp210\|ch340\|ftdi\|espressif'"
    echo ""
fi

echo -e "${CYAN}[USB Bus (lsusb filter)]${NC}"
if command -v lsusb &>/dev/null 2>&1; then
    lsusb 2>/dev/null | grep -iE 'cp210|ch340|ch343|ftdi|ft232|espressif|silicon' || echo "  No ESP32-family USB chips found on bus."
else
    echo "  lsusb not available"
fi
echo ""

# Method 3: Check for ESP32 network devices (OTA-capable)
echo -e "${CYAN}[Network (mDNS scan)]${NC}"
if command -v avahi-browse &>/dev/null 2>&1; then
    avahi-browse -t -r _arduino._tcp 2>/dev/null | grep -A5 "=" | head -20 || echo "  No Arduino OTA devices found."
elif command -v dns-sd &>/dev/null 2>&1; then
    dns-sd -B _arduino._tcp 2>/dev/null | head -10 || echo "  No Arduino OTA devices found."
else
    # Try ping to common mDNS names
    for name in inventory-box.local esp32-inventory.local; do
        if ping -c1 -W1 "$name" &>/dev/null 2>&1; then
            IP=$(getent hosts "$name" 2>/dev/null | awk '{print $1}')
            echo -e "  ${GREEN}✓ $name → $IP${NC}"
        fi
    done
fi
echo ""

echo "=============================="
echo -e "Use with: ${BOLD}make upload PORT=<device>${NC}"
echo -e "Example:  ${BOLD}make upload PORT=/dev/ttyUSB0${NC}"
