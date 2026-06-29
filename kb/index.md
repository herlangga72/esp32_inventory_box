---
okf_version: "0.1"
type: Bundle
title: ESP32 Inventory Box
description: Smart inventory tracking via weight (HX711) + motion (MPU6050) + fingerprint access (R307). ESP32, Arduino framework, PlatformIO.
tags: [esp32, embedded, freertos, iot, inventory]
timestamp: 2026-06-29T00:00:00Z
---

# ESP32 Inventory Box

Smart tool box that tracks inventory via weight sensing, motion detection, and fingerprint-authenticated access.

## Core Concepts

- [Architecture](/kb/ARCHITECTURE.md) — System design, microkernel pattern, task architecture

## API Reference

- [REST API](/kb/api/rest-api.md) — All HTTP endpoints, request/response schemas

## Domain Logic

- [State Machines](/kb/domain/state-machines.md) — Box state, access control, door, power
- [Weight Sensing](/kb/domain/weight-sensing.md) — HX711 signal chain, filtering, tool matching
- [Access Control](/kb/domain/access-control.md) — Fingerprint auth, server auth, enrollment

## Kernel / System

- [Service Registry](/kb/kernel/service-registry.md) — Microkernel IPC, message types, pool allocation
- [Boot Sequence](/kb/kernel/boot-sequence.md) — Dual-core boot, init stages, conditional tasks
- [Power Management](/kb/kernel/power-management.md) — Light/deep sleep, wake sources, activity tracking

## Data Layer

- [Data Layer](/kb/data/data-layer.md) — NVS storage, entity serialization, SPIFFS logging
- [Entities](/kb/data/entities.md) — Tool, User, LogEntry struct definitions
- [Repositories](/kb/data/repositories.md) — ToolRepo, UserRepo, LogRepo patterns

## Hardware

- [Hardware Map](/kb/HARDWARE-MAP.md) — Pin assignments, bus topology, driver summary

## Change Log

- [Log](/kb/log.md) — Update history
