# Compass Wiring Layout

<p>
Format:<br>
Component Pin → ESP32 Pin <br>

Voltage and GND connections have mostly been omitted for brevity.
</p>


## Stepper Motor
<p>
IN1 → GPIO25 <br>
IN2 → GPIO26 <br>
IN3 → GPIO32 <br>
IN4 → GPIO33 <br>
</p>

## GPS (UART)
<p>
GPS VDD → 3.3V <br>
GPS GND → GND <br>
GPS TX → GPIO16  (ESP32 RX) <br>
GPS RX → GPIO17  (ESP32 TX) <br>
</p>

## Magnetometer (QMC5883L - I2C)
<p>
VCC → 3.3V <br>
GND → GND <br>
SDA → GPIO21 <br>
SCL → GPIO22 <br>
</p>

## SD Card
<p>
MOSI → GPIO23  <br>
MISO → GPIO19  <br>
SCK  → GPIO18  <br>
CS   → GPIO5   <br>
</p>