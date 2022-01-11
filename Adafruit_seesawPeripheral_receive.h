#if CONFIG_FHT && defined(MEGATINYCORE)
// If ADC sampling rate or MUX channel is changed, this function gets
// called to discard a couple of initial ADC readings (which are invalid
// immdiately after such a change) and reset the FHT buffer counter to
// the beginning.
static void restart_sampling(void) {
  for(uint8_t i=0; i<3; i++) {             // Discard initial readings
    while(!ADC0.INTFLAGS & ADC_RESRDY_bm); // In the INTFLAG register,
    ADC0.INTFLAGS |= ADC_RESRDY_bm;        // setting bit clears flag!
    // (ADC is still free-running and will set RESRDY bit,
    // it's just not triggering interrupts right now.)
  }
  fht_counter = 0;                         // Restart at beginning of buf
  ADC0.INTCTRL |= ADC_RESRDY_bm;           // Enable result-ready IRQ
}
#endif

/***************************** data write */

void receiveEvent(int howMany) {
  //EESAW_DEBUG(F("Received "));
  //SEESAW_DEBUG(howMany);
  //SEESAW_DEBUG(F(" bytes:"));
  for (uint8_t i=howMany; i<sizeof(i2c_buffer); i++) {
    i2c_buffer[i] = 0;
  }

  if ((uint32_t)howMany > sizeof(i2c_buffer)) {
    SEESAW_DEBUGLN();
    return;
  }
  for (uint8_t i=0; i<howMany; i++) {
    i2c_buffer[i] = Wire.read();
    //SEESAW_DEBUG(F("0x"));
    //SEESAW_DEBUG(i2c_buffer[i], HEX);
    //SEESAW_DEBUG(F(" "));
  }
  //SEESAW_DEBUG("\n");

  uint8_t base_cmd = i2c_buffer[0];
  uint8_t module_cmd = i2c_buffer[1];
  
  if (base_cmd == SEESAW_STATUS_BASE) {
    if (module_cmd == SEESAW_STATUS_SWRST) {
      Adafruit_seesawPeripheral_reset();
      SEESAW_DEBUGLN(F("Resetting"));
    }
  }
  else if (base_cmd == SEESAW_GPIO_BASE) {
    uint32_t temp;
    temp = i2c_buffer[2];
    temp <<= 8;
    temp |= i2c_buffer[3];
    temp <<= 8;
    temp |= i2c_buffer[4];
    temp <<= 8;
    temp |= i2c_buffer[5];

    switch (module_cmd) {
      case SEESAW_GPIO_DIRSET_BULK:
      case SEESAW_GPIO_DIRCLR_BULK:
      case SEESAW_GPIO_BULK_SET:
      case SEESAW_GPIO_BULK_CLR:
      case SEESAW_GPIO_PULLENSET:
      case SEESAW_GPIO_PULLENCLR:
      case SEESAW_GPIO_INTENSET:
          temp &= VALID_GPIO;
          for (uint8_t pin=0; pin<32; pin++) {
            if ((temp >> pin) & 0x1) {
              SEESAW_DEBUG(F("Set pin "));
              SEESAW_DEBUG(pin);
              if (module_cmd == SEESAW_GPIO_DIRSET_BULK) {
                pinMode(pin, OUTPUT);
                SEESAW_DEBUGLN(F(" OUTPUT"));
              }
              else if (module_cmd == SEESAW_GPIO_DIRCLR_BULK) {
                pinMode(pin, INPUT);
                SEESAW_DEBUGLN(F(" INPUT"));
              }
              else if (module_cmd == SEESAW_GPIO_BULK_SET) {
                digitalWrite(pin, 1);
                SEESAW_DEBUGLN(F(" HIGH"));
              }
              else if (module_cmd == SEESAW_GPIO_BULK_CLR) {
                digitalWrite(pin, 0);
                SEESAW_DEBUGLN(F(" LOW"));
              }
              else if (module_cmd == SEESAW_GPIO_PULLENSET) {
                pinMode(pin, INPUT_PULLUP);
                SEESAW_DEBUGLN(F(" PULL"));
              }
              else if (module_cmd == SEESAW_GPIO_PULLENCLR) {
                pinMode(pin, INPUT);
                SEESAW_DEBUGLN(F(" NOPULL"));
              }
#if CONFIG_INTERRUPT
              else if (module_cmd == SEESAW_GPIO_INTENSET) {
                g_irqGPIO |= 1UL << pin;
                SEESAW_DEBUGLN(F(" INTEN"));
#if USE_PINCHANGE_INTERRUPT
                attachInterrupt(digitalPinToInterrupt(pin), Adafruit_seesawPeripheral_changedGPIO, CHANGE);
#endif
              }
              else if (module_cmd == SEESAW_GPIO_INTENCLR) {
                g_irqGPIO &= ~(1UL << pin);
                SEESAW_DEBUGLN(F(" INTCLR"));
#if USE_PINCHANGE_INTERRUPT
                detachInterrupt(digitalPinToInterrupt(pin));
#endif
              }
#endif
            }
          }
    }
  }

#if CONFIG_PWM
  else if (base_cmd == SEESAW_TIMER_BASE) {
    uint8_t pin = i2c_buffer[2];
    uint16_t value = i2c_buffer[3];
    value <<= 8;
    value |= i2c_buffer[4];
    if (! (VALID_PWM & (1UL << pin))) {
      g_pwmStatus = 0x1; // error, invalid pin!
    } else if (module_cmd == SEESAW_TIMER_PWM) {
      // its valid!
      value >>= 8;  // we only support 8 bit analogwrites
      SEESAW_DEBUG(F("PWM "));
      SEESAW_DEBUG(pin);
      SEESAW_DEBUG(F(": "));
      SEESAW_DEBUGLN(value);
      
      pinMode(pin, OUTPUT);
      analogWrite(pin, value);
      g_pwmStatus = 0x0;
    }
    else if (module_cmd == SEESAW_TIMER_FREQ) {
      SEESAW_DEBUG(F("Freq "));
      SEESAW_DEBUG(pin);
      SEESAW_DEBUG(F(": "));
      SEESAW_DEBUGLN(value);
      tone(pin, value);
      g_pwmStatus = 0x0;
    }
  }
#endif

#if CONFIG_NEOPIXEL
  else if (base_cmd == SEESAW_NEOPIXEL_BASE) {
    if (module_cmd == SEESAW_NEOPIXEL_SPEED) {
      // we only support 800khz anyways
    }
    else if (module_cmd == SEESAW_NEOPIXEL_BUF_LENGTH) {
      uint16_t value = i2c_buffer[2];
      value <<= 8;
      value |= i2c_buffer[3];
      // dont let it be larger than the internal buffer, of course
      g_neopixel_bufsize = min((uint16_t)CONFIG_NEOPIXEL_BUF_MAX, value);
      SEESAW_DEBUG(F("Neolen "));
      SEESAW_DEBUGLN(g_neopixel_bufsize);
    }
    if (module_cmd == SEESAW_NEOPIXEL_PIN) {
      g_neopixel_pin = i2c_buffer[2];
      SEESAW_DEBUG(F("Neopin "));
      SEESAW_DEBUGLN(g_neopixel_pin);
    }
    if (module_cmd == SEESAW_NEOPIXEL_BUF) {
      uint16_t offset = i2c_buffer[2];
      offset <<= 8;
      offset |= i2c_buffer[3];

      for (uint8_t i=0; i<howMany-4; i++) {
        if (offset+i < CONFIG_NEOPIXEL_BUF_MAX) {
          g_neopixel_buf[offset+i] = i2c_buffer[4+i];
        }
      }
    }
    if (module_cmd == SEESAW_NEOPIXEL_SHOW) {
      //SEESAW_DEBUGLN(F("Neo show!"));
      pinMode(g_neopixel_pin, OUTPUT);
      tinyNeoPixel_show(g_neopixel_pin, g_neopixel_bufsize, (uint8_t *)g_neopixel_buf);
    }
  }
#endif

#if CONFIG_EEPROM
  else if (base_cmd == SEESAW_EEPROM_BASE) {
    // special case for 1 byte at -1 (i2c addr)
    if ((module_cmd == 0xFF) && (howMany == 3)) {
      EEPROM.write(EEPROM.length()-1, i2c_buffer[2]);
    } else {
      // write the data
      for (uint8_t i=0; i<howMany-2; i++) {
        if ((module_cmd+i) < EEPROM.length()) {
          EEPROM.write(module_cmd+i, i2c_buffer[2+i]);
          SEESAW_DEBUG(F("EEP $"));
          SEESAW_DEBUG(module_cmd+i, HEX);
          SEESAW_DEBUG(F(" <- 0x"));
          SEESAW_DEBUGLN(i2c_buffer[2+i], HEX);
        }
      }
    }
  }
#endif

#if CONFIG_FHT && defined(MEGATINYCORE)
  else if (base_cmd == SEESAW_SPECTRUM_BASE) {
    if ((module_cmd == SEESAW_SPECTRUM_RATE) && (howMany == 3)) {
      ADC0.INTCTRL &= ~ADC_RESRDY_bm;          // Disable result-ready IRQ
      uint8_t rate = i2c_buffer[2];            // Requested rate index
      if (rate > 31) rate = 31;                // Clip rate between 0-31
      ADC0.SAMPCTRL = rate & 31;               // Set ADC sample control
      restart_sampling();                      // Purge recording, start over
    } else if ((module_cmd == SEESAW_SPECTRUM_CHANNEL) && (howMany == 3)) {
      ADC0.INTCTRL &= ~ADC_RESRDY_bm;          // Disable result-ready IRQ
      uint8_t channel = i2c_buffer[2];         // Requested ADC channel
      // TO DO: clip channel to valid range. Most likely this will just be
      // 0 or 1 for mic vs. line-in. Value should then be mapped through a
      // const table to either an Arduino pin number (which is then mapped
      // through digitalPinToAnalogInput()) or an ADC MUX value directly
      // (via the datasheet and how the corresponding board gets routed).
      // For now though, for the sake of initial testing, it's taken as a
      // direct ADC MUX value, valid or not. Final changes are only needed
      // here, not in the Adafruit_Seesaw library.
      ADC0.MUXPOS = channel;                   // Set ADC input MUX
      restart_sampling();                      // Purge recording, start over
    }
  }
#endif
}
