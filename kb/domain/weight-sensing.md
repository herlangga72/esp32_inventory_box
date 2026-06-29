---
type: Domain Logic
title: Weight Sensing and Tool Matching
description: HX711 signal chain, moving average filter, calibration mode, and MatchingService tool identification algorithm.
tags: [weight, hx711, sensing, filtering, matching]
timestamp: 2026-06-29T00:00:00Z
---

# Weight Sensing and Tool Matching

## Signal Chain

```
HX711 load cell
  DT (GPIO16), SCK (GPIO17)
  DRDY (GPIO36) → InterruptManager::isHX711Ready()
       │
       ▼
  hx711.readRaw() → 24-bit signed int
       │
       ▼
  applyMovingAverage() → 10-sample running average
       │
       ▼
  ws_onRawReading()
    ├── Calibration mode → accumulate to calibrationSum
    └── Normal mode → processWeight()
          │
          ├── WEIGHT_CHANGE → StateManager (delta + currentWeight)
          ├── WEIGHT_UPDATE → DisplayManager (currentWeight + delta)
          └── EventBus publish (legacy)
```

## Moving Average Filter

```cpp
readings[10] — circular buffer
filterIndex  — current write position
filterSum    — running sum for O(1) average
```

Algorithm:
- Subtract oldest: `filterSum -= readings[filterIndex]`
- Store new: `readings[filterIndex] = raw * calibrationFactor`
- Add new: `filterSum += readings[filterIndex]`
- Advance: `filterIndex = (filterIndex + 1) % 10`
- Return: `filterSum / 10`

10 samples at 10 Hz → ~1 second to 90% of step response.

## Calibration Mode

Triggered by `START_CALIBRATION(N)` message:
1. `calibrating=1`, `calibrationSamples=N`
2. Each raw reading accumulates to `calibrationSum`
3. After N samples: `baseline = calibrationSum / N`
4. Publishes CALIBRATION_COMPLETE
5. Resumes normal processing

During calibration, `processWeight()` is NOT called.

## Tool Matching (MatchingService)

Stateless free functions operating on cached Tool arrays:

```cpp
// Weight-based match: |delta - tool.weight| <= tool.tolerance
int ms_matchByWeight(tools, count, delta, tolerance,
                     int* outIds, int maxResults);

// Closest match with confidence: 1.0 - (bestDiff / bestTolerance), clamped [0,1]
int ms_matchClosest(tools, count, delta, int* outId, float* confidence);
```

### Placement Matching Flow

```
ANALYZING + motion SETTLED
  → tr_findAll() → get all active tools from NVS cache
  → ms_matchByWeight(delta, DEFAULT_TOLERANCE=5.0)
  → match found(1+):
      addToContents(matchIds[0])
      if multiple: addToContents(matchIds[1..N])
      TOOL_PLACED state
  → no match:
      UNKNOWN_ITEM state
      log unknown weight
```

### Removal Matching Flow

```
REMOVING + motion SETTLED
  → ms_matchClosest(fabs(delta), &matchId, &confidence)
  → confidence > 0.3:
      removeFromContents(matchId)
      TOOL_REMOVED event
  → confidence <= 0.3:
      clearAllContents (box was rearranged)
      IDLE state
```

## Key Tuning Parameters

| Parameter | Default | Config Key | Effect |
|-----------|---------|-----------|--------|
| CALIBRATION_FACTOR | -471.0 | Compile | Raw → grams scaling |
| FILTER_SIZE | 10 | Compile | Smoothing window |
| WEIGHT_THRESHOLD_GRAMS | 2.0 | `cfg_threshold` | Min delta for state change |
| SETTLING_TIME_MS | 3000 | `cfg_settling` | Max wait for motion settle |
| DEFAULT_TOLERANCE | 5.0 | `cfg_tolerance` | ±grams match window |

## Weight Stability Detection

```cpp
bool ws_isWeightStable(const WeightServiceMemory* mem) {
    return abs(mem->currentWeight - mem->previousWeight)
           < Config::WEIGHT_THRESHOLD_GRAMS;
}
```

Used by StateManager during door-close re-evaluation and settling checks.

# Citations

[1] src/domain/services/WeightService.cpp — Signal processing
[2] src/domain/services/MatchingService.cpp — Matching algorithms
[3] src/hal/HX711Driver.cpp — Low-level driver
[4] docs/services.md — Service documentation
