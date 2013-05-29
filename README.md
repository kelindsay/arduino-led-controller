arduino-led-controller
======================

To compile and run outside the Arduino:

Edit midi_led.c and modify ARDUINO_MODE to 1

Debug:      gcc midi_led.c -lm -Wall -g -o midi_led
Optimized:  gcc midi_led.c -O3 -lm -Wall -o midi_led
