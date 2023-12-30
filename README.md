# meredith-hardware
An Arduino-based utility for managing timed triggers

Meredith is a timed trigger management tool. The timed triggers fire https requests after the trigger has been activated longer than a given threadhold. The timed triggers can be overridden by long-pressing on the health check button.

This util is built with two included triggers (trigger_left and trigger_right). More triggers can be added by defining new `TimedTriggerConfig` structures.

Components:

[Arduino Nano 33 IoT](https://store-usa.arduino.cc/products/arduino-nano-33-iot)
