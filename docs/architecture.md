# ESP32 Inventory Box — Architecture

## Boot Sequence

```mermaid
sequenceDiagram
    participant HW as Hardware
    participant S as setup()
    participant SS as SystemStatus
    participant SM as StorageManager
    participant HX as HX711Driver
    participant MPU as MPU6050Driver
    participant DSP as SSD1306Driver
    participant IM as InterruptManager
    participant WS as WeightService
    participant MS as MotionService
    participant PM as PowerManager
    participant WM as WiFiManager
    participant WSM as WebServerManager
    participant FP as FingerprintDriver
    participant SC as ServerClient
    participant AC as AccessController
    participant DS as DoorService
    participant EV as EventBus

    HW->>S: Cold boot / Wake
    S->>S: Boot LED blink (3x)
    S->>S: Check BOOT button → force AP mode?
    S->>SM: storage.begin() (NVS)
    S->>SS: systemStatus.begin()

    S->>HX: hx711.begin()
    HX-->>S: testRead → OK or ERROR
    S->>SS: markOK/markError("HX711")

    S->>MPU: mpu.begin()
    MPU-->>S: I2C detect → OK or ERROR
    S->>SS: markOK/markError("MPU6050")

    S->>DSP: display.init()
    DSP-->>S: I2C detect → OK or ERROR
    S->>SS: markOK/markError("Display")

    S->>IM: InterruptManager::begin()
    S->>WS: weightService.begin()
    S->>MS: motionService.begin()
    S->>PM: powerManager.begin()

    S->>WM: wifiManager.begin()
    WM->>WM: startConfigPortal() (AP mode)
    WM-->>S: AP mode active

    S->>WSM: webServer.begin()
    WSM->>WSM: Register 30+ API routes
    WSM->>WSM: server.begin()

    S->>FP: fpDriver.begin()
    FP-->>S: UART2 handshake → OK or ERROR
    S->>SS: markOK/markError("Fingerprint")

    S->>DS: doorService.begin()
    S->>SS: markOK("Door")

    S->>SC: serverClient.begin(url, token)
    S->>SS: markOK/markWarning("ServerClient")

    S->>AC: accessController.begin()
    S->>SS: markOK("AccessController")

    S->>S: xTaskCreate × 6-8 tasks
    S->>S: Boot complete
```

## Task Architecture

```mermaid
graph TB
    subgraph Core0["Core 0 (PRO CPU)"]
        ST["State Task<br/>Prio: 10<br/>50ms cycle"]
        WT["Weight Task<br/>Prio: 8<br/>100ms cycle"]
        MT["Motion Task<br/>Prio: 8<br/>10ms cycle"]
        WF["WiFi Task<br/>Prio: 6<br/>10ms cycle"]
        WEB["Web Task<br/>Prio: 5<br/>1ms cycle"]
        ACC["Access Task<br/>Prio: 9<br/>200ms cycle"]
        LOG["Logger Task<br/>Prio: 2<br/>Event-driven"]
    end

    subgraph Core1["Core 1 (APP CPU)"]
        DSPT["Display Task<br/>Prio: 3<br/>1s cycle"]
    end

    subgraph Loop["loop() — Core 0"]
        L1["ArduinoOTA.handle()"]
        L2["cli.handle()"]
        L3["powerManager.update()"]
        L4["systemStatus.update()"]
        L5["LED status blinker"]
    end

    ST --> EVB[EventBus<br/>Singleton]
    WT --> EVB
    MT --> EVB
    DSPT --> EVB
    WEB --> EVB
    ACC --> EVB
```

## Event Flow

```mermaid
flowchart LR
    subgraph Sensors
        HX711[HX711<br/>Weight Sensor]
        MPU6050[MPU6050<br/>Motion Sensor]
        FPS["R307<br/>Fingerprint"]
        REED["Reed Switch<br/>Door Sensor"]
        RLY["Relay<br/>Solenoid Lock"]
    end

    subgraph HAL["Hardware Abstraction"]
        HXD[HX711Driver]
        MPUD[MPU6050Driver]
        FPD[FingerprintDriver]
        INT[InterruptManager]
    end

    subgraph Domain["Domain Layer"]
        WSV[WeightService]
        MSV[MotionService]
        EVB[EventBus]
        SMGR[StateManager]
        MATCH[MatchingService]
        AC[AccessController]
        DS[DoorService]
    end

    subgraph Data["Data Layer"]
        TR[ToolRepository]
        UR[UserRepository]
        LR[LogRepository]
        STM[StorageManager<br/>NVS]
    end

    subgraph Presentation["Presentation Layer"]
        WSM[WebServer<br/>REST API]
        DM[DisplayManager<br/>OLED]
        CLI[SerialCLI]
    end

    subgraph Kernel["Kernel"]
        WMGR[WiFiManager<br/>AP mode]
        PMGR[PowerManager<br/>Sleep]
        SSTAT[SystemStatus]
        SRVC[ServerClient<br/>HTTP]
    end

    HX711 --> HXD --> WSV
    MPU6050 --> MPUD --> MSV
    HX711 --> INT
    MPU6050 --> INT
    FPS --> FPD --> AC
    REED --> DS
    AC --> RLY

    WSV -->|WEIGHT_UPDATED| EVB
    MSV -->|MOTION_DETECTED| EVB
    SMGR -->|STATE_CHANGED| EVB
    SMGR -->|TOOL_PLACED| EVB
    SMGR -->|TOOL_REMOVED| EVB

    EVB --> SMGR
    EVB --> DM
    EVB --> LR
    EVB --> AC
    AC --> EVB
    DS --> EVB

    SMGR --> MATCH --> TR
    SMGR --> TR
    SMGR --> LR

    WSM --> TR
    WSM --> UR
    WSM --> LR
    WSM --> WSV
    WSM --> SMGR
    WSM --> WMGR
    WSM --> AC
    WSM --> SRVC
    AC --> SRVC --> WSM

    DM --> EVB

    TR --> STM
    UR --> STM
    WMGR --> STM
```

## State Machine

```mermaid
stateDiagram-v2
    [*] --> INIT
    INIT --> IDLE: System ready

    IDLE --> ANALYZING: Weight delta > threshold
    IDLE --> SLEEP: Idle timeout

    ANALYZING --> TOOL_PLACED: Weight matched to tool
    ANALYZING --> UNKNOWN_ITEM: No match found
    ANALYZING --> IDLE: Settled below threshold

    TOOL_PLACED --> REMOVING: Negative weight delta
    TOOL_PLACED --> IDLE: Weight returned to baseline

    REMOVING --> IDLE: Settling complete

    UNKNOWN_ITEM --> IDLE: Acknowledged / timeout

    SLEEP --> IDLE: Motion wake / timer wake
```

## Access Control State Machine

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> SCANNING: Fingerprint driver ready

    SCANNING --> CHECKING_SERVER: Fingerprint matched (ID)
    SCANNING --> SCANNING: No finger / retry
    SCANNING --> IDLE: Sensor not operational

    CHECKING_SERVER --> GRANTED: Server says "allowed"
    CHECKING_SERVER --> DENIED: Server says "denied"
    CHECKING_SERVER --> LOCAL_AUTH_CHECK: Server unreachable + fallback on
    CHECKING_SERVER --> DENIED: Server unreachable + fallback off

    LOCAL_AUTH_CHECK --> GRANTED: User found with matching fpId
    LOCAL_AUTH_CHECK --> DENIED: No matching user

    GRANTED --> UNLOCKING: Relay energized
    UNLOCKING --> UNLOCKED: Door open
    UNLOCKED --> IDLE: Door closed + relay deactivated

    DENIED --> IDLE: Display timeout (2s)

    IDLE --> ENROLLING: Begin enrollment (API)
    ENROLLING --> IDLE: Enrollment complete / failed / timeout
```

## API Routes

```mermaid
graph LR
    subgraph Static["SPIFFS Static Files"]
        S1["/ → index.html"]
        S2["/styles.css"]
        S3["/app.js"]
        S4["/pages/*.html × 7"]
    end

    subgraph Tools["/api/tools"]
        T1["GET — list all"]
        T2["POST — create"]
        T3["GET /:id — read"]
        T4["PUT /:id — update"]
        T5["DELETE /:id — remove"]
    end

    subgraph Users["/api/users"]
        U1["GET — list all"]
        U2["POST — create"]
        U3["POST /login"]
        U4["POST /logout"]
        U5["DELETE /:id"]
    end

    subgraph System["System API"]
        SY1["GET /api/status"]
        SY2["GET /api/logs"]
        SY3["POST /api/logs/clear"]
        SY4["GET /api/logs/download"]
        SY5["POST /api/calibrate"]
        SY6["GET/POST /api/config"]
        SY7["GET /api/diagnostics"]
        SY8["POST /api/restart"]
    end

    subgraph AccessCtrl["Access Control API"]
        A1["GET /api/access/status"]
        A2["GET/POST /api/access/server"]
        A3["POST /api/fingerprint/enroll"]
        A4["GET /api/fingerprint/enroll/status"]
        A5["POST /api/fingerprint/delete"]
        A6["GET /api/door"]
        A7["POST /api/door/unlock"]
    end
```

## Flash Layout

```mermaid
pie title 4MB Flash Partition (default.csv)
    "Bootloader" : 16
    "Partition Table" : 4
    "NVS (Storage)" : 20
    "OTA App (ota_0)" : 1280
    "SPIFFS (Web UI)" : 1400
    "Reserved" : 1376
```

## Power States

```mermaid
flowchart TD
    ACTIVE[Active<br/>All sensors + WiFi running]
    LIGHT[Light Sleep<br/>WiFi on, sensors reduced]
    DEEP[Deep Sleep<br/>Wake from GPIO/timer only]

    ACTIVE -->|10s idle| LIGHT
    LIGHT -->|Activity detected| ACTIVE
    LIGHT -->|60s idle| DEEP
    DEEP -->|MPU motion interrupt| ACTIVE
    DEEP -->|Timer wake| ACTIVE
```
