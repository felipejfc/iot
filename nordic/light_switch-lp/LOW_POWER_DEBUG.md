# Low Power Mode Debugging Guide

## Current Issue: 12mA Consumption (Should be <10µA)

If you're seeing ~12mA in sleepy mode, here are the most common causes:

---

## ✅ Fixes Applied

1. **LED power drain fixed**
   - Network LED now turns off in low-power mode
   - All LEDs disabled when entering sleepy mode
   - This alone should save 5-10mA

2. **Power management enabled**
   - `CONFIG_PM=y` and `CONFIG_PM_DEVICE=y` in prj_lp.conf
   - Tickless kernel enabled
   - UART/USB/Console disabled

3. **Zigbee sleepy configuration**
   - 5-minute keep-alive timeout (vs 3 seconds)
   - Sleepy end device behavior enabled

---

## 🔍 Debugging Steps

### 1. **Check LEDs are Actually Off**
After the device boots and joins the network:
- **All LEDs should be OFF** (except control LED if you turned it on)
- If any LED is on, it's consuming 5-10mA
- **Fix**: Physically verify LED0 (P0.06) and LED1 (P0.08) are off

### 2. **Verify You Built with prj_lp.conf**
```bash
# Clean and rebuild with low-power config
west build -b promicro_nrf52840/nrf52840/uf2 -p -- -DCONF_FILE=prj_lp.conf
```

**Verify no console output:**
- After flashing, the device should be **silent** (no USB serial)
- If you see console output, you didn't use prj_lp.conf

### 3. **Hold Button During Boot**
The device enters sleepy mode **ONLY** if you hold the button during boot:
1. Disconnect power
2. **Hold button on P0.02**
3. Connect power (while still holding)
4. Release after ~2 seconds

**Without holding the button**, the device stays in normal mode (12mA is expected).

### 4. **Device Must Join Zigbee Network**
The device can't sleep until it joins a network:
- Use Zigbee2MQTT or a coordinator
- Device should join automatically
- **In sleepy mode**: Parent router buffers messages
- **Current keep-alive**: 5 minutes (300 seconds)

### 5. **Measure After Joining**
Power consumption timeline:
- **0-30 seconds**: Joining network (~15-20mA - radio active)
- **30-60 seconds**: Initial keep-alive messages (~12mA average)
- **After 1-2 minutes**: Should drop to <10µA if sleeping properly

**Wait 2-3 minutes** after boot before measuring power consumption.

### 6. **Check for Wake-Ups**
Even in sleepy mode, the device wakes up:
- Every 5 minutes for keep-alive (brief spike to ~12mA for <1 second)
- When button is pressed
- When Zigbee command is received

**Average power** over 5 minutes should be <10µA.

---

## 📊 Expected Power Consumption

| Mode | Condition | Current | Notes |
|------|-----------|---------|-------|
| **Development** | USB enabled | 10-20mA | USB prevents sleep |
| **Normal** | No sleepy mode | 10-20mA | Radio always on |
| **Sleepy (active)** | During keep-alive | 12-15mA | Brief spike every 5 min |
| **Sleepy (sleep)** | Between keep-alives | **<10µA** | Target consumption |
| **Button press** | Waking from sleep | 15mA | Brief spike |

---

## 🐛 Common Problems

### Problem: Always 12mA, Never Drops
**Cause**: Device not sleeping, likely still in normal mode
**Solutions**:
1. ✅ Hold button during boot (see step 3 above)
2. ✅ Verify built with `prj_lp.conf`
3. ✅ Wait 2-3 minutes after joining
4. Check if LED1 (P0.08) is still on - disconnect it if needed

### Problem: 5-8mA Consumption
**Cause**: One LED still on
**Solution**:
- Check physical LED on P0.06 and P0.08
- Disconnect LEDs if they're drawing power

### Problem: Device Not Joining Network
**Cause**: Zigbee coordinator not in pairing mode
**Solution**:
- Put Zigbee2MQTT in pairing mode
- Factory reset device (hold button >5 seconds during operation)
- Try again

### Problem: 15-20mA Even in Sleepy Mode
**Cause**: USB is still enabled
**Solution**:
- Rebuild with `prj_lp.conf`
- Verify USB cable is only for power (not data)
- Check prj_lp.conf has `CONFIG_USB_DEVICE_STACK=n`

---

## 🔬 Advanced Debugging

### Measure Current Properly
1. **Use multimeter in series** with VCC
2. **Measure over time**: Set multimeter to record min/max
3. **Wait 5+ minutes**: Average should be <10µA
4. **Expect spikes**: Every 5 minutes there will be a brief ~12mA spike

### Check Zigbee2MQTT
Device should appear as:
- **Type**: EndDevice (not Router!)
- **Power source**: Battery
- **Manufacturer**: Nordic
- **Model**: Cava test dev
- **LQI**: Should update only every 5 minutes

### Re-pair After Changes
After rebuilding with prj_lp.conf:
1. Remove device from Zigbee2MQTT
2. Factory reset device (hold button >5 seconds)
3. Re-pair device
4. Device should now report as "sleepy end device"

---

## ✨ Expected Behavior When Working

1. Device boots (no console output in low-power mode)
2. All LEDs off
3. Joins network silently (~30 seconds)
4. Radio goes silent
5. **Current drops to <10µA**
6. Every 5 minutes: brief radio activity (check-in)
7. Button still works (wakes device, toggles LED, sends Zigbee update)
8. Zigbee2MQTT commands work (device wakes, executes, sleeps again)

---

## 📝 Final Checklist

- [ ] Built with `west build -- -DCONF_FILE=prj_lp.conf`
- [ ] No USB console output (silent operation)
- [ ] Held button during boot to enable sleepy mode
- [ ] All LEDs are off
- [ ] Device joined Zigbee network
- [ ] Waited 2-3 minutes after joining
- [ ] Measuring average current over 5+ minutes
- [ ] Device appears as "EndDevice" in Zigbee2MQTT

If all above are checked and still >1mA, check:
- External pull-up resistors on unused pins
- Physical LED connections
- Regulator efficiency on your board
