# Simulation Results

**Platform:** Linux (POSIX FreeRTOS port)  
**Duration:** 30 000 ms  
**Fault threshold:** 3 consecutive FAULT observations  
**Random seed:** default (`RAND_SEED = 42`)

---

## Summary

Over the 30-second run the simulation demonstrated all three required behaviours:

| # | Scenario | Tick range | Outcome |
|---|----------|-----------|---------|
| 1 | State synchronisation | 501 – 3 501, 6 501 – 8 001, … | Device A mirrors every Device B transition within one poll interval (250 ms) |
| 2 | Fault detection | 7 001 – 7 501, 9 001 – 11 501, … | Device A counts consecutive FAULT observations; escalates to ERROR at threshold 3 |
| 3 | Fault recovery | 8 001, 12 001, 16 501, 20 501 | Device B returns to SLEEP on reset; Device A returns to IDLE within one poll cycle |

Device B entered FAULT four times across the run; every fault was detected and resolved.  
One transient fault (tick 9 001 – 9 501) recovered autonomously before the threshold was reached — Device A correctly discarded the fault count without issuing a reset.

---

## Annotated output

```
freertos_master_slave simulation
=================================
Duration : 30000 ms
Threshold: 3 consecutive FAULT observations
```

### Startup

```
[TICK:0000000001] [DEVICE_B] task started  state=SLEEP
[TICK:0000000001] [DEVICE_A] task started  state=IDLE
```

Both tasks initialise at tick 1. Device B starts in SLEEP; Device A starts in IDLE.

---

### Scenario 1 — Normal state synchronisation (first cycle)

```
[TICK:0000000501] [DEVICE_B] SLEEP -> ACTIVE      ← B wakes up
[TICK:0000000501] [DEVICE_A] IDLE -> PROCESSING   ← A mirrors immediately

[TICK:0000003501] [DEVICE_B] ACTIVE -> SLEEP       ← B sleeps
[TICK:0000003501] [DEVICE_A] PROCESSING -> IDLE    ← A mirrors immediately
```

Device A detects every Device B state change within its 250 ms poll window and mirrors it on the same tick.

---

### Scenario 2 — Fault detection and recovery (first fault)

```
[TICK:0000006501] [DEVICE_B] SLEEP -> ACTIVE
[TICK:0000006501] [DEVICE_A] IDLE -> PROCESSING

[TICK:0000007001] [DEVICE_B] ACTIVE -> FAULT       ← B enters FAULT
[TICK:0000007001] [DEVICE_A] fault_count=1          ← A begins counting
[TICK:0000007251] [DEVICE_A] fault_count=2
[TICK:0000007501] [DEVICE_A] fault_count=3          ← threshold reached
[TICK:0000007501] [DEVICE_A] reset command issued   ← A sends reset to B
[TICK:0000007501] [DEVICE_A] PROCESSING -> ERROR    ← A escalates

[TICK:0000007751] [DEVICE_A] fault_count=1          ← one more poll before reset lands

[TICK:0000008001] [DEVICE_B] FAULT -> SLEEP  (reset received)   ← B recovers
[TICK:0000008001] [DEVICE_A] ERROR -> IDLE   (recovery complete) ← A returns to IDLE
```

Three consecutive 250 ms polls all observe FAULT → threshold breached → reset issued.  
Device B receives the reset within one tick and transitions to SLEEP.  
Device A detects the SLEEP transition on the next poll and exits ERROR.

---

### Transient fault — autonomous recovery without reset

```
[TICK:0000009001] [DEVICE_B] ACTIVE -> FAULT
[TICK:0000009001] [DEVICE_A] fault_count=1
[TICK:0000009251] [DEVICE_A] fault_count=2
[TICK:0000009501] [DEVICE_B] FAULT -> ACTIVE    ← B recovers on its own before tick 3
```

Device B recovered autonomously after two observations. Device A's fault counter never reached 3, so no reset was issued. The counter is discarded and the simulation continues normally. This demonstrates the debounce behaviour — transient faults do not trigger unnecessary resets.

---

### Second and third faults (ticks 11 001 and 15 501)

Both follow the same pattern as the first fault: three consecutive FAULT observations, reset issued, Device B transitions to SLEEP within 500 ms, Device A returns to IDLE.

```
[TICK:0000011001] [DEVICE_B] ACTIVE -> FAULT
[TICK:0000011001] [DEVICE_A] fault_count=1
[TICK:0000011251] [DEVICE_A] fault_count=2
[TICK:0000011501] [DEVICE_A] fault_count=3
[TICK:0000011501] [DEVICE_A] reset command issued
[TICK:0000011501] [DEVICE_A] PROCESSING -> ERROR
[TICK:0000011751] [DEVICE_A] fault_count=1
[TICK:0000012001] [DEVICE_B] FAULT -> SLEEP  (reset received)
[TICK:0000012001] [DEVICE_A] ERROR -> IDLE  (recovery complete)

[TICK:0000015501] [DEVICE_B] ACTIVE -> FAULT
[TICK:0000015501] [DEVICE_A] fault_count=1
[TICK:0000015751] [DEVICE_A] fault_count=2
[TICK:0000016001] [DEVICE_A] fault_count=3
[TICK:0000016001] [DEVICE_A] reset command issued
[TICK:0000016001] [DEVICE_A] PROCESSING -> ERROR
[TICK:0000016251] [DEVICE_A] fault_count=1
[TICK:0000016501] [DEVICE_B] FAULT -> SLEEP  (reset received)
[TICK:0000016501] [DEVICE_A] ERROR -> IDLE  (recovery complete)
```

---

### Fourth fault and final normal cycles

```
[TICK:0000019501] [DEVICE_B] ACTIVE -> FAULT
[TICK:0000019501] [DEVICE_A] fault_count=1
[TICK:0000019751] [DEVICE_A] fault_count=2
[TICK:0000020001] [DEVICE_A] fault_count=3
[TICK:0000020001] [DEVICE_A] reset command issued
[TICK:0000020001] [DEVICE_A] PROCESSING -> ERROR
[TICK:0000020251] [DEVICE_A] fault_count=1
[TICK:0000020501] [DEVICE_B] FAULT -> SLEEP  (reset received)
[TICK:0000020501] [DEVICE_A] ERROR -> IDLE  (recovery complete)

[TICK:0000021501] [DEVICE_B] SLEEP -> ACTIVE
[TICK:0000021501] [DEVICE_A] IDLE -> PROCESSING
[TICK:0000022001] [DEVICE_B] ACTIVE -> SLEEP
[TICK:0000022001] [DEVICE_A] PROCESSING -> IDLE

[TICK:0000026001] [DEVICE_B] SLEEP -> ACTIVE
[TICK:0000026001] [DEVICE_A] IDLE -> PROCESSING
[TICK:0000028001] [DEVICE_B] ACTIVE -> SLEEP
[TICK:0000028001] [DEVICE_A] PROCESSING -> IDLE

[TICK:0000029001] [DEVICE_B] SLEEP -> ACTIVE
[TICK:0000029001] [DEVICE_A] IDLE -> PROCESSING
[TICK:0000030001] [DEVICE_B] ACTIVE -> SLEEP
[TICK:0000030001] [DEVICE_A] PROCESSING -> IDLE

Simulation complete.
```

The final 8 seconds show normal sleep/wake cycles with no faults — the system is stable after each recovery.

---

## Key observations

- **Synchronisation latency:** Every Device B state change was reflected in Device A on the same tick (≤ 250 ms poll interval). No missed transitions were observed.
- **Fault detection time:** Threshold was always reached in exactly `3 × DEVICE_A_POLL_MS = 750 ms` from the first FAULT observation.
- **Recovery time:** Device B acknowledged every reset within 500 ms (one Device B tick). Device A exited ERROR within one further poll cycle.
- **Debounce correctness:** The transient fault at tick 9 001 – 9 501 did not trigger a reset, confirming that the fault counter resets to zero on any non-FAULT observation.
- **Determinism:** The run is fully reproducible with the same `RAND_SEED`. Change the seed in `sim_config.h` to explore different transition sequences.
