#!/bin/bash
# nRF52840 Power Debug Script
# Run this while device is in "sleep" state between Zigbee polls

echo "=== nRF52840 Power Consumption Debug ==="
echo ""

echo "--- CLOCK Status ---"
echo "HFCLKSTAT (should be 0x00010001 if HFXO running, 0 if stopped):"
nrfjprog --memrd 0x4000040C --n 4
echo "LFCLKSTAT (0x00010001 = LFXO, 0x00010000 = LFRC):"
nrfjprog --memrd 0x40000418 --n 4

echo ""
echo "--- POWER: RAM sections (0x0000FFFF = all on, lower = some off) ---"
for i in 0 1 2 3 4 5 6 7 8; do
  addr=$((0x40000900 + i*16))
  printf "RAM%d (0x%08X): " $i $addr
  nrfjprog --memrd $addr --n 4 2>/dev/null | grep -oE '0x[0-9A-Fa-f]+'
done

echo ""
echo "--- Peripheral ENABLE registers (0 = disabled, non-zero = ENABLED/BAD) ---"
echo -n "UARTE0: "; nrfjprog --memrd 0x40002500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "UARTE1: "; nrfjprog --memrd 0x40028500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "SPIM0:  "; nrfjprog --memrd 0x40003500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "SPIM1:  "; nrfjprog --memrd 0x40004500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "SPIM2:  "; nrfjprog --memrd 0x40023500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "SPIM3:  "; nrfjprog --memrd 0x4002F500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "TWIM0:  "; nrfjprog --memrd 0x40003500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "TWIM1:  "; nrfjprog --memrd 0x40004500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "SAADC:  "; nrfjprog --memrd 0x40007500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "PWM0:   "; nrfjprog --memrd 0x4001C500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "PWM1:   "; nrfjprog --memrd 0x40021500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "PWM2:   "; nrfjprog --memrd 0x40022500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "PWM3:   "; nrfjprog --memrd 0x4002D500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "QSPI:   "; nrfjprog --memrd 0x40029500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "NFCT:   "; nrfjprog --memrd 0x40005500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1

echo ""
echo "--- RADIO ---"
echo -n "RADIO STATE (0=disabled, 2=rxidle, 3=rx, 11=txidle): "
nrfjprog --memrd 0x40001550 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "RADIO POWER (should be 0 in sleep): "
nrfjprog --memrd 0x40001FFC --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1

echo ""
echo "--- DCDC/Regulator (CRITICAL) ---"
echo -n "DCDCEN (1=DCDC enabled, 0=LDO mode): "
nrfjprog --memrd 0x40000578 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "MAINREGSTATUS (0=normal, 1=high voltage): "
nrfjprog --memrd 0x40000640 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1

echo ""
echo "--- USB ---"
echo -n "USBREGSTATUS: "
nrfjprog --memrd 0x40000438 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1
echo -n "USBD ENABLE: "
nrfjprog --memrd 0x40027500 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1

echo ""
echo "--- GPIOTE (can prevent sleep if tasks pending) ---"
echo -n "GPIOTE INTENSET: "
nrfjprog --memrd 0x40006304 --n 4 | grep -oE '0x[0-9A-Fa-f]+' | tail -1

echo ""
echo "=== Analysis ==="
echo "Look for:"
echo "  - Any peripheral ENABLE != 0x00000000 (except GPIOTE for button)"
echo "  - DCDCEN = 0 means LDO mode (higher power)"
echo "  - RADIO POWER = 1 means radio is on"
echo "  - HFCLKSTAT showing HFXO running when should be stopped"
echo ""
