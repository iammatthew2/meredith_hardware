# meredith-hardware

An Arduino-based utility for managing timed triggers

Meredith is a timed trigger management tool. The timed triggers fire https requests after the trigger has been activated longer than a given threadhold. The timed triggers can be overridden by long-pressing on the health check button.

This util is built with two included triggers (trigger_left and trigger_right). More triggers can be added by defining new `TimedTriggerConfig` structures.

This utility will either be paired with a web app to handle the http trigger actions events or it will fire against the Twilio API directly, TBD


![main_1](https://github.com/iammatthew2/meredith_hardware/assets/1512727/2087663b-f7f8-44f8-b015-8417a4de62cd)


## Components

- 1 [Arduino Nano 33 IoT](https://store-usa.arduino.cc/products/arduino-nano-33-iot)
- 1 [6mm Illuminated Pushbutton - Red Momentary](https://www.adafruit.com/product/1439)
- 2 buttons, any type
- 4 LEDs
- 3 10k ohm resistors (1 per button)
- 5 220 ohm resistors (1 per LED, including the illuminated pushbutton)
