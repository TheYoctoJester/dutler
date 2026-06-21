#ifndef BRIDGE_H
#define BRIDGE_H

// Transparent USB-CDC (port 0) <-> hardware-UART bridge.
void bridge_init(void);
void bridge_task(void);

#endif  // BRIDGE_H
