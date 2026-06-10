// R307 / AS608 Fingerprint Sensor simulator (basic)
// Protocol: 57600 baud, 8N1
// Packet: 0xEF01 [addr4] [pkgID] [len2] [data] [chksum2]
//
// This sim: responds to HandShake (0x01) and ReadSysPara (0x1A)
// so the firmware's initWithRetry succeeds. All other commands get ACK.
// Enrollment and scan are NOT simulated — returns "no finger" for image requests.

class FingerprintChip {
  constructor() {
    this.rxBuffer = [];
    this.packet = [];
    this.inPacket = false;
    this.packetLen = 0;
    this.packetIdx = 0;
  }

  // Called by Wokwi when data arrives on RX pin
  rxByte(byte) {
    this.rxBuffer.push(byte);

    // Detect start of packet: 0xEF 0x01
    if (!this.inPacket && this.rxBuffer.length >= 2) {
      if (this.rxBuffer[this.rxBuffer.length - 2] === 0xEF &&
          this.rxBuffer[this.rxBuffer.length - 1] === 0x01) {
        this.inPacket = true;
        this.packet = [0xEF, 0x01];
        this.packetIdx = 2;
        // Need: addr(4) + pkgID(1) + len(2) + data + chksum(2)
        // Minimum: 2 + 4 + 1 + 2 = 9 bytes before data
      }
      return;
    }

    if (this.inPacket) {
      this.packet.push(byte);
      this.packetIdx++;

      // Once we have header + addr + pkgID + len, we know total size
      if (this.packetIdx >= 9) {
        const dataLen = (this.packet[7] << 8) | this.packet[8];
        const totalLen = 9 + dataLen + 2; // header(2)+addr(4)+pkg(1)+len(2)+data+chk(2)
        if (this.packetIdx >= totalLen) {
          this.processPacket();
          this.inPacket = false;
          this.packet = [];
          this.packetIdx = 0;
        }
      }
    }
  }

  processPacket() {
    const pkgID = this.packet[6];
    const dataLen = (this.packet[7] << 8) | this.packet[8];
    const cmd = dataLen > 0 ? this.packet[9] : 0;

    // Build response header: 0xEF01 + addr(4)[0xFF*4]
    const addr = [0xFF, 0xFF, 0xFF, 0xFF];

    switch (pkgID) {
      case 0x01: // Command packet
        switch (cmd) {
          case 0x01: // HandShake / VfyPwd
            this.sendAck(addr, 0x01);
            break;
          case 0x1A: // ReadSysPara
            this.sendSysParams(addr);
            break;
          case 0x02: // SetPwd
          case 0x03: // SetAddr
          case 0x06: // SetSysPara
          case 0x0C: // TemplateNum
          case 0x0D: // ReadIndexTable (empty)
            this.sendAck(addr, cmd);
            break;
          case 0x01: // GenImg — returns "no finger" (0x02)
            this.sendConfirm(addr, 0x02); // NO_FINGER
            break;
          default:
            this.sendAck(addr, cmd);
        }
        break;
      case 0x07: // Data packet / End of data
        // Usually part of multi-packet transfer, just ack
        break;
      default:
        this.sendAck(addr, 0x00);
    }
  }

  // ACK packet: 0xEF01 + addr + 0x07 + len(2) + 0x00 + chksum(2)
  sendAck(addr, confirmCode) {
    // Confirmation code: 0x00 = OK
    const data = [confirmCode];
    this.sendResponse(addr, 0x07, data);
  }

  sendConfirm(addr, code) {
    const data = [code];
    this.sendResponse(addr, 0x07, data);
  }

  sendSysParams(addr) {
    // System parameters response (16 bytes):
    // status(2) + sysId(2) + libSize(2) + security(2) + addr(4) +
    // packetLen(2) + baud(2) + chipAddr(4) + pwdLen(2) + pwd(4)
    const data = [
      0x00, 0x00,  // status = OK
      0x00, 0x0A,  // system identifier
      0x00, 0x80,  // library size = 128
      0x00, 0x01,  // security level = 1
      0xFF, 0xFF, 0xFF, 0xFF,  // device address
      0x00, 0x80,  // max packet length = 128
      0x00, 0x07,  // baud rate: 57600 (7 = 9600*N, N=6 → 57600... simplified)
      0xFF, 0xFF, 0xFF, 0xFF,  // chip address
      0x00, 0x04,  // password length
      0x00, 0x00, 0x00, 0x00   // password (none)
    ];
    this.sendResponse(addr, 0x07, data);
  }

  sendResponse(addr, pkgID, data) {
    const len = data.length;
    const lenHi = (len >> 8) & 0xFF;
    const lenLo = len & 0xFF;

    // Calculate checksum over pkgID(1) + len(2) + data
    let sum = pkgID + lenHi + lenLo;
    for (let i = 0; i < data.length; i++) sum += data[i];
    const chkHi = (sum >> 8) & 0xFF;
    const chkLo = sum & 0xFF;

    const packet = [
      0xEF, 0x01,
      ...addr,
      pkgID, lenHi, lenLo,
      ...data,
      chkHi, chkLo
    ];

    // Send after a short delay (sensor processing time)
    const self = this;
    setTimeout(() => {
      for (const b of packet) {
        self.txByte(b);
      }
    }, 15); // ~15ms response time typical for R307
  }

  // Called by framework — chip sends byte to ESP32
  txByte(byte) {
    if (this.txPin) {
      // UART TX from sensor → ESP32 RX (pin 5)
      this.txPin.setDigital(byte & 1);
      // Wokwi UART simulation handles the actual serial framing
    }
  }
}

// Wokwi entry point
let chip = null;

export function setup() {
  chip = new FingerprintChip();

  return {
    rx: (byte) => chip.rxByte(byte),
    // tx is set up by Wokwi wiring
  };
}

// Let Wokwi know which pin is TX (output from chip)
export const pinSettings = {
  TX: { direction: "output" },
  RX: { direction: "input" }
};
