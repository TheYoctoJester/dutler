// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef CONSOLE_H
#define CONSOLE_H

// Control port (USB-CDC port 1): newline-terminated text transport. Drains the
// CDC RX, assembles complete lines, and hands each to the command interpreter;
// also provides the reply-writing primitive used by the command handlers.
void console_task(void);            // pump from the super-loop
void console_print(const char *s);  // write + flush a reply to the control port
void console_drain(void);           // push the CDC TX FIFO onto the wire (bounded)

#endif  // CONSOLE_H
