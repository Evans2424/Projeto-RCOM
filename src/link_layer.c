// Link layer protocol implementation

#include "link_layer.h"


int STOP_ = FALSE;
unsigned char tramaTx = 0;
unsigned char tramaRx = 1;
int alarmCount = 0;
int alarmActivated = FALSE;
int timeout = 0;
int retransmitions = 0;


// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source


void alarmHandler(int signal){
    alarmActivated = TRUE;
    alarmCount++;
}

int llopen(LinkLayer connectionParameters) {

    LinkLayerState state = START;
    timeout = connectionParameters.timeout;
    retransmitions = connectionParameters.nRetransmissions;

    int fd = open(connectionParameters.serialPort, O_RWDR | O_NOCCTY);
    if (fd < 0){
        printf("Error connecting to the serial port");
        return -1; 
    }

    switch (connectionParameters.role) {
        case LlTx:  
            (void) signal(SIGALRM, alarmHandler); // Subscribe the alarm interruptions. When it receives an interruption the alarmHandler is called, and alarmActivated is set to TRUE.
            unsigned char byte; // Variable to store each byte of the received command. 

            unsigned char frame[5] = {0x7E, 0x03, 0x03, 0x03 ^ 0x03, 0x7E}; // Construction of SET Supervision command. 
            write(fd, frame, 5);// Send the command to the receiver

            while (state != STOP_ && alarmActivated == FALSE) { // Cycle to read the UA Command from receiver, it stops when it reaches the STOP_ state, or the alarm is activated (timeout).
                if (read(fd, &byte, 1) > 0) { // Reads one byte at a time
                    switch (state) {
                        case START:
                            if (byte == 0x7E) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == 0x01) state = A_RCV;
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if (byte == 0x07) state = C_RCV;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == 0x01 ^ 0x07) state = BCC1_OK;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if (byte = 0x7E) state = STOP_;
                            else state = START;
                            break;
                        default:
                            break;
                     }
                }
            }

            connectionParameters.nRetransmissions -= 1; // Decrements the number of retransmissions
        
        case LlRx:

            while(state != STOP_) {
                if(read( fd, &byte, 1) > 0) {
                    switch (state) {
                        case START:
                            if (byte == 0x7E) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if(byte = 0x03) state = A_RCV;
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x07) state = C_RCV;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if(byte == 0x03 ^ 0x03) state = BCC1_OK;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if(byte == 0x7E) state = STOP_;
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }
            }   

            unsigned char frame[5] = {0x7E, 0x01, 0x07, 0x01 ^ 0x07, 0x7E}; 
            write(fd, frame, 5);

        }
    return 1;
}

    
////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}
