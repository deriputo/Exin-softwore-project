#ifndef I2C_BUS_CONFIG_H
#define I2C_BUS_CONFIG_H
/* I2C1 remapped to PB8 (SCL), PB9 (SDA), with external pullups to 3.3 V. */
/* Breadboard/Dupont wiring: start with standard-mode timing for signal margin. */
#define I2C_BUS_CLOCK_HZ 100000u
#define I2C_BUS_TIMEOUT_MS 30u
#endif
