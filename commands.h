#ifndef COMMANDS_H
#define COMMANDS_H

#define ACK  0xAF
#define NACK 0xA0

#define CMD_CHKRDY      0x00
#define CMD_WRITE       0x01      // + 4 bytes destination address + 4 bytes size
#define CMD_READ        0x02      // + 4 bytes source address + 4 bytes size
#define CMD_SPI_LOAD    0x03      // load program from spi and execute
#define CMD_JUMP        0x04      // + 4 bytes destination address

#endif