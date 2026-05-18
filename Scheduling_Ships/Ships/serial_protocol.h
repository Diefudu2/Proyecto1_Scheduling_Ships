#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

void serial_protocol_init(void);
void serial_protocol_poll(void);
void serial_protocol_send_line(const char *line);

#endif