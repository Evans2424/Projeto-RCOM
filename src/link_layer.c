// Link layer protocol implementation

#include "link_layer.h"



unsigned char NTx = 0; //N do transmissor (I0 ou I1)
unsigned char NRx = 1; // N do recetor (RR0 ou RR1 ou REJ0 ou REJ1)
int alarmCount = 0;
int alarmActivated = FALSE;
int timeout = 0;
int retransmitions = 0;


// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

int connect(const char* serialPort){
    int fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPort);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    return fd;
}

void alarmHandler(int signal){
    alarmActivated = TRUE;
    alarmCount++;
}

int supervisionWriter(int fd, unsigned char flag, unsigned char a, unsigned char c) {
    unsigned char frame[5];
    frame[0] = flag;
    frame[1] = a;
    frame[2] = c;
    frame[3] = a ^ c ;
    frame[4] = flag;
    return write(fd, frame, 5);
}

int llopen(LinkLayer connectionParameters) {
    
    LinkLayerState state = START;
    timeout = connectionParameters.timeout;
    retransmitions = connectionParameters.nRetransmissions;

    int fd = connect(connectionParameters.serialPort);
    if (fd < 0){
        printf("Error connecting to the serial port");
        return -1; 
    }

    unsigned char byte; // Variable to store each byte of the received command. 

    switch (connectionParameters.role) {
        case LlTx:  
            (void) signal(SIGALRM, alarmHandler); // Subscribe the alarm interruptions. When it receives an interruption the alarmHandler is called, and alarmActivated is set to TRUE.
            while (connectionParameters.nRetransmissions != 0 && state != STOP) { // This loop is going to try to send the SET command, and wait for the UA response from the receiver
                supervisionWriter(fd, 0x7E, 0x03, 0x03); // Construction of SET Supervision command. 
                alarm(connectionParameters.timeout); // Sets the alarm to the timeout value, so that it waits n seconds to try to retrieve the UA commands
                alarmActivated = FALSE; // Sets the alarmActivated to FALSE so that it enters the while loop below.
            
                while (state != STOP && alarmActivated == FALSE) { // Cycle to read the UA Command from receiver, it stops when it reaches the STOP state, or the alarm is activated (timeout).s

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
                                if (byte == (0x01 ^ 0x07)) state = BCC1_OK;
                                else if (byte == 0x7E) state = FLAG_RCV;
                                else state = START;
                                break;
                            case BCC1_OK:
                                if (byte == 0x7E) state = STOP;
                                else state = START;
                                break;
                            default:
                                break;
                        }
                        
                    }
            }
                 connectionParameters.nRetransmissions -= 1; // Decrements the number of retransmissions
            }
            if (state != STOP) return -1; 
        case LlRx:

            while(state != STOP) {
                if(read( fd, &byte, 1) > 0) {
                    printf("reading");
                    switch (state) {
                        case START:
                            if (byte == 0x7E) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if(byte == 0x03) state = A_RCV;
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x07) state = C_RCV;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if(byte == (0x03 ^ 0x03)) state = BCC1_OK;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if(byte == 0x7E) state = STOP;
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }
            }   

            supervisionWriter(fd, 0x7E, 0x01, 0x07);
        }

return 1;
}

    
////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(int fd, const unsigned char *buf, int bufSize)
{
    int frameSize = bufSize + 6;
    unsigned char frame[frameSize];

    frame[0] = 0x7E;
    frame[1] = 0x03;
    frame[2] = 0x00; //Transmitter n= 0
    frame[3] = frame[0] ^ frame[2];

    unsigned char bcc2 = buf[0];
    for (int i = 1; i < bufSize; i++) { //XOR between all the data bytes in buf
        bcc2 ^= buf[i];
    }

    unsigned char* message = (unsigned char*) malloc(frameSize * 2 * sizeof(unsigned char));
    frameSize = stuffing(frame, 1, frameSize, message);
    message[frameSize++] = bcc2;
    message[frameSize++] = 0x7E;
    
    int nTransmission = 0;
    int rejected = FALSE, accepted = FALSE;
    while(nTransmission < retransmitions){
        alarmActivated = FALSE;
        alarm(timeout);
        rejected = FALSE;
        accepted = FALSE;
        while (!(alarmActivated == TRUE || rejected || accepted)){
            write(fd, message, frameSize); //Write I(0) frame to serial port
            unsigned char res = readResponse(fd);
            if(!res) continue;
            switch(res){
                case 0x05 | 0x85:
                    accepted = TRUE;
                    NTx = (NTx + 1) % 2;
                    break;
                case 0x01 | 0x81:
                    rejected = TRUE;
                    break;
                default:
                    break;
            }
        }
        if(accepted) break;
        nTransmission++;
    }
    free(message);
    if(accepted) return frameSize;
    else{
        printf("Error sending frame\n");
        return -1;
    } 
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(int fd, unsigned char *packet)
{


    
    /*LinkLayerState state = START;
    unsigned char byte;
    unsigned char controlByte;

    while(state != STOP) {
        if(read(fd, &byte ,1) > 0) {
            switch (state) {
                
                case START:
                    if (byte == 0x7E) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == 0x03) state = A_RCV;
                    else if (byte != 0x7E) state = START;                        
                    break;

                case A_RCV:
                    if (byte == 0x7E) state = FLAG_RCV;

                    else if (byte == 0x0B) {
                        supervisionWriter(fd, 0x7E, 0x01, 0x0B);
                        return 0;
                        }

                    else if (byte == 0x00 || byte == 0x01) { // <--- O PROBLEMA
                        state = C_RCV;
                        controlByte = byte;
                    }

                    else state = START;
                    break;
                    
                case C_RCV:
                    if (byte == 0x7E) state = FLAG_RCV;
                    else if (byte == 0x03 ^ controlByte) state = READING;
                    else state = START;
                    break;
                
                case READING:
                    if (byte == 0x7D) state = DATA_FOUND;
                    
                    else if (byte == 0x7E) {
                        // TODO
                    }
                    else {
                        // TODO
                    }
                    break;
                
                case DATA_FOUND:
                    state = READING;

                    

            }
        }
    }*/

    static int packet = 0;
    unsigned char message

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int fd, LinkLayer connectionParameters)
{
    LinkLayerState state = START;
    (void) signal(SIGALRM, alarmHandler);

    unsigned char byte; // Variable to store each byte of the received command. 

    switch (connectionParameters.role) {
        case LlTx:  
            (void) signal(SIGALRM, alarmHandler); // Subscribe the alarm interruptions. When it receives an interruption the alarmHandler is called, and alarmActivated is set to TRUE.
            while (connectionParameters.nRetransmissions != 0 && state != STOP) { // This loop is going to try to send the DISC command, and wait for the DISC response from the receiver
                supervisionWriter(fd, 0x7E, 0x03, 0x0B); // Construction of DISC Supervision command. 
                alarm(connectionParameters.timeout); // Sets the alarm to the timeout value, so that it waits n seconds to try to retrieve the DISC command
                alarmActivated = FALSE; // Sets the alarmActivated to FALSE so that it enters the while loop below.
            
                while (state != STOP && alarmActivated == FALSE) { // Cycle to read the DISC Command from receiver, it stops 
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
                                if (byte == 0x0B) state = C_RCV;
                                else if (byte == 0x7E) state = FLAG_RCV;
                                else state = START;
                                break;
                            case C_RCV:
                                if (byte == (0x01 ^ 0x0)) state = BCC1_OK;
                                else if (byte == 0x7E) state = FLAG_RCV;
                                else state = START;
                                break;
                            case BCC1_OK:
                                if (byte == 0x7E) state = STOP;
                                else state = START;
                                break;
                            default:
                                break;
                        }
                        
                    } 
            }
                 connectionParameters.nRetransmissions -= 1; // Decrements the number of retransmissions
            }
            if (state != STOP) return -1; 
            supervisionWriter(fd, 0x7E, 0x03, 0x07); // Construction of UA Supervision command.

        case LlRx:

            while(state != STOP) {
                if(read( fd, &byte, 1) > 0) {
                    printf("reading");
                    switch (state) {
                        case START:
                            if (byte == 0x7E) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if(byte == 0x03) state = A_RCV;
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x07) state = C_RCV;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if(byte == (0x03 ^ 0x03)) state = BCC1_OK;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if(byte == 0x7E) state = STOP;
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }
            }   

            supervisionWriter(fd, 0x7E, 0x03, 0x0B);
        }

return close(fd);
}

int stuffing(unsigned char* buf, int start, int length, unsigned char* message) {
    int msgSize = 0;

    for (int i = 0; i < start; i++) {
        message[msgSize] = buf[i];
        msgSize++;
    }

    for (int i = start; i < length; i++) {
        if(buf[i] == 0x7E || buf[i] == 0x7D) {
            message[msgSize] = 0x7D;
            msgSize++;
            message[msgSize] = buf[i] ^ 0x20;
            msgSize++;
        }

        else {
            message[msgSize++] = buf[i];
        }
    }

    return msgSize;
}

int destuffing(unsigned char* buf, int start, int length, unsigned char* message) {
    int msgSize = 0;

    for (int i = 0; i < start; i++) {
        message[msgSize] = buf[i];
        msgSize++;
    }

    for (int i = start; i < length; i++) {
        if(buf[i] == 0x7D) {
            message[msgSize] = buf[i+1] ^ 0x20;
            msgSize++;
            i++;
        }
        else {
            message[msgSize] = buf[i];
            msgSize++;
        }
    }
    return msgSize;
}

int readResponse(int fd){
    
    unsigned char byte, controlByte = 0;
    LinkLayerState state = START;
    
    while (state != STOP && alarmActivated == FALSE) {  
        if (read(fd, &byte, 1) > 0 || 1) {
            switch (state) {
                case START:
                    if (byte == 0x7E) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == 0x01) state = A_RCV;
                    else if (byte != 0x7E) state = START;
                    break;
                case A_RCV:
                    if (byte == 0x05 || byte == 0x85 || byte == 0x01 || byte == 0x81){
                        state = C_RCV;
                        controlByte = byte;   
                    }
                    else if (byte == 0x7E) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (0x01 ^ controlByte)) state = BCC1_OK;
                    else if (byte == 0x7E) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC1_OK:
                    if (byte == 0x7E){
                        state = STOP;
                    }
                    else state = START;
                    break;
                default: 
                    break;
            }
        } 
    } 
    return controlByte;
}