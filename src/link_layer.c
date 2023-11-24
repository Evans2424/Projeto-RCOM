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
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

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
    printf("Alarm ringing!\n");

    alarmActivated = TRUE;
    alarmCount++;
}

int supervisionWriter(int fd, unsigned char flag, unsigned char a, unsigned char c) {
    unsigned char frame[5] = {0};
    frame[0] = flag;
    frame[1] = a;
    frame[2] = c;
    frame[3] = a ^ c ;
    frame[4] = flag;
    return write(fd, frame, 5);
}

int llopen(LinkLayer connectionParameters) {
    (void) signal(SIGALRM, alarmHandler); // Subscribe the alarm interruptions. When it receives an interruption the alarmHandler is called, and alarmActivated is set to TRUE.

    LinkLayerState state = START;
    timeout = connectionParameters.timeout;
    retransmitions = connectionParameters.nRetransmissions;

    int fd = connect(connectionParameters.serialPort);
    if (fd < 0){
        printf("Error connecting to the serial port");
        return -1; 
    }

     // Variable to store each byte of the received command. 

    switch (connectionParameters.role) {
        case LlTx: {;  
            int tries = 0;
            while (tries < connectionParameters.nRetransmissions && state != STOP) { // This loop is going to try to send the SET command, and wait for the UA response from the receiver
                printf("Trial number: %d\n", tries);
                if (supervisionWriter(fd, 0x7E, 0x03, 0x03) < 0){// Construction of SET Supervision command. 
                    printf("Error writing to serial port\n");
                }

                alarmActivated = FALSE; 
                alarm(connectionParameters.timeout); // Sets the alarm to the timeout value, so that it waits n seconds to try to retrieve the UA commands
                 // Sets the alarmActivated to FALSE so that it enters the while loop below.
                unsigned char byte;

                while (alarmActivated == FALSE && state != STOP) { // Cycle to read the UA Command from receiver, it stops when it reaches the STOP state, or the alarm is activated (timeout).s
                    if (read(fd, &byte, 1) < 0) {// Reads one byte at a time
                         perror("read");
                         exit(-1);
                    }
                        switch (state) {
                            case START:
                                if (byte == 0x7E){
                                    state = FLAG_RCV;
                                } 
                                break;
                            case FLAG_RCV:
                                if (byte == 0x01){
                                    state = A_RCV;
                                } 
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
                    tries++; 
            }
            
                  
            
            if (state != STOP) return -1; 
            printf("Connection \n");
            return fd;
        }

        case LlRx:

            while(state != STOP) {
                unsigned char byte;
                if(read(fd, &byte, 1) < 0){
                perror("read");
                exit(-1);
            }
                    switch (state) {
                        case START:
                            if (byte == 0x7E) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if(byte == 0x03) state = A_RCV;
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x03){

                                state = C_RCV;
                            } 
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            //Print state
                            break;
                        case C_RCV:
                            if(byte == (0x03 ^ 0x03)){
                                
                                 state = BCC1_OK;
                            }
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if(byte == 0x7E){
                                state = STOP;
                            } 
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }   
           
            supervisionWriter(fd, 0x7E, 0x01, 0x07);
            break;
        
        default:
            break;
        }

return fd;
}

    
////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(int fd, unsigned char *buf, int bufSize)
{
	
    int frameSize = bufSize + 6;
    unsigned char *frame = (unsigned char *)malloc(frameSize);

    frame[0] = 0x7E;
    frame[1] = 0x03;
    frame[2] = NTx == 0 ? 0x00 : 0x40;
    frame[3] = frame[1] ^ frame[2];

    unsigned char bcc2 = 0x00;
    for (int i = 0; i < bufSize; i++) { //XOR between all the data bytes in buf
        bcc2 ^= buf[i];
    }
    frameSize = stuffing(buf, bufSize, frame, frameSize, bcc2);
        
    int nTransmission = 0;
    int accepted = FALSE;

    
    while(nTransmission < retransmitions){
        alarmActivated = FALSE;
        alarm(timeout);
        
		accepted = FALSE;
        
        while (alarmActivated == FALSE && !accepted){
            write(fd, frame, frameSize); //Write I(0) frame to serial port
            
            unsigned char res = readResponse(fd);
            printf("res: %x\n", res);
            if(!res){
                printf("Error reading response\n");
                continue;
            }
            
            else if (res == 0x05 || res == 0x85){
               
					accepted = TRUE;
                    NTx = (NTx + 1) % 2;
                    
				}
            else if (res == 0x01 || res == 0x81){  
					accepted = FALSE;
				}
			else continue;                                                
        }
        if(accepted) return frameSize;
    nTransmission++;
    }
    free(frame);
    if(accepted) return frameSize;
    else{
        printf("Error sending frame\n");
        exit(-1);
    } 
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////


int llread(int fd, unsigned char *packet){

    static char sequenceNumber = 0;

    unsigned char byte, sequenceNumberReceived;
    int packet_position = 0;
    LinkLayerState state = START;

    while (state != STOP){
        if(read(fd, &byte, 1) > 0){
            switch(state){
                case START:
                    if(byte == 0x7E){
                        state = FLAG_RCV;
                    }
                    break;
                case FLAG_RCV:
                    if(byte == 0x03){
                        state = A_RCV;
                    }else if(byte != 0x7E){
                        state = START;
                    }
                    break;
                case A_RCV:
                    if(byte == 0x00){
                        state = C_RCV;
                        sequenceNumberReceived = 0;
                    }else if(byte == 0x40){
                        state = C_RCV;
                        sequenceNumberReceived = 1;
                    }else if(byte == 0x7E){
                        state = FLAG_RCV;
                    }else{
                        state = START;
                    }
                    break;
                case C_RCV:
                    if(sequenceNumberReceived == 0 && byte == (0x03 ^ 0x00)){
                        state = DATA_FOUND;
                    }else if(sequenceNumberReceived == 1 && byte == (0x03 ^ 0x40)){
                        state = DATA_FOUND;
                    }else if(byte == 0x7E){
                        state = FLAG_RCV;
                    }else{
                        state = START;
                    }
                    break;
                case DATA_FOUND:
                    if(byte == 0x7D){
                        state = DESTUFFING;
                    }else if(byte == 0x7E){
                        unsigned char bcc2 = packet[--packet_position];
                        packet[packet_position] = '\0';
                        
                        unsigned char xor = 0x00;
                        for(int j = 0; j < packet_position; j++){
                            xor ^= packet[j];
                        }


                        if(xor == bcc2){
                            state = STOP;
                            // send RR
                            unsigned char C_RR = sequenceNumberReceived == 0 ? 0x85 : 0x05;
                            supervisionWriter(fd, 0x7E, 0x03,C_RR);


                            // check if it is not a repeated packet
                            if(sequenceNumberReceived == sequenceNumber){
                                sequenceNumber = (sequenceNumber + 1) % 2;
                                return packet_position;
                            }
                            return 0;
                        }else{
                            // send REJ (i_n)
                            unsigned char C_RR =  sequenceNumberReceived == 0 ? 0x01 : 0x81;
                            supervisionWriter(fd, 0x7E, 0x03,C_RR);
                            state = START;
                            return -1;
                        }
                    }else{
                        packet[packet_position++] = byte;
                    }
                    break;
                case DESTUFFING:
                    if(byte == 0x5E){
                        packet[packet_position++] = 0x7E;
                    }else if(byte == 0x5D){
                        packet[packet_position++] = 0x7D;
                    }else{
                        packet[packet_position++] = byte;
                    }
                    state = DATA_FOUND;
                    break;
                default:
                    state = START;
                    break;
            }
        }
    }
    return -1;
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
        case LlTx: {
            
            printf("Closing as transmitter\n");
            (void) signal(SIGALRM, alarmHandler); // Subscribe the alarm interruptions. When it receives an interruption the alarmHandler is called, and alarmActivated is set to TRUE.
            int tries = 0;
            while ((tries < connectionParameters.nRetransmissions) && (state != STOP)) { // This loop is going to try to send the DISC command, and wait for the DISC response from the receiver
                supervisionWriter(fd, 0x7E, 0x03, 0x0B); // Construction of DISC Supervision command. 
                alarm(connectionParameters.timeout); // Sets the alarm to the timeout value, so that it waits n seconds to try to retrieve the DISC command
                alarmActivated = FALSE; // Sets the alarmActivated to FALSE so that it enters the while loop below.
            
                while (state != STOP && alarmActivated == FALSE) { // Cycle to read the DISC Command from receiver, it stops 
                    if (read(fd, &byte, 1) < 0) { 
                        perror("read");
                        exit(-1);
                        }
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
                                if (byte == (0x01 ^ 0x0B)) state = BCC1_OK;
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
                 tries++; // increment number of tries
            }
            if (state != STOP) return -1; 
            supervisionWriter(fd, 0x7E, 0x03, 0x07); // Construction of UA Supervision command.
            return close(fd);
            break;
    }

        case LlRx: {

            printf("Closing as receiver\n");
            //Receiving DISC command
            unsigned char byte;
            state = START;

            while(state != STOP) {
                
                if(read( fd, &byte, 1) < 0) {
                    perror("read");
                    exit(-1);

                }

                    switch (state) {
                        case START:
                            
                            if (byte == 0x7E){
                             state = FLAG_RCV;
                            }
                            break;
                        case FLAG_RCV:                            
                            if(byte == 0x03){
                                state = A_RCV;
                            }
                             
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x0B){
                                state = C_RCV;
                            }
                             
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if(byte == (0x03 ^ 0x0B)) state = BCC1_OK;
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if(byte == 0x7E){
                                printf("Read disc commmand successfully\n");
                                state = STOP;
                            } 
                            else state = START;
                            break;
                        default:
                            break;
                    }
            }
                
            if (state != STOP) return -1;
            supervisionWriter(fd, 0x7E, 0x01, 0x0B); // Construction of DISC Supervision command.
                
            state = START;

            while(state != STOP) {

                if(read( fd, &byte, 1) < 0) {
                    perror("read");
                    exit(-1);
                }
                    
                    switch (state) {
                        case START:
                            if (byte == 0x7E){
                                state = FLAG_RCV;
                            }
                             
                            break;

                        case FLAG_RCV:
                            if(byte == 0x03){
                                state = A_RCV;
                            } 
                            else if (byte != 0x7E) state = START;
                            break;
                        case A_RCV:
                            if(byte == 0x07){
                             state = C_RCV;
                            }
                            else if (byte == 0x7E) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if(byte == (0x03 ^ 0x07)) state = BCC1_OK;
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

             if (state != STOP) return -1;

                printf("Connection closed\n");
                return close(fd);
                
        }
    }
    return 1;
}


int stuffing(unsigned char *buf, int bufSize, unsigned char *frame, int frameSize, unsigned char bcc2) {

    int i = 4;
    for (int j = 0; j < bufSize; j++) { //Percorre o buffer / dados 
        if(buf[j] == 0x7E || buf[j] == 0x7D) {
            frame = (unsigned char *) realloc(frame, ++frameSize );
            frame[i++] = 0x7D;
            frame[i++] = buf[j] ^ 0x20;
        }

        else {
            frame[i++] = buf[j];
        }
    }

    // BCC2 Stuffing
    if(bcc2 == 0x7E || bcc2 == 0x7D){
        frame = (unsigned char *) realloc(frame, ++frameSize);
        frame[i++] = 0x7D;
        frame[i++] = bcc2 ^ 0x20;
    }else{
        frame[i++] = bcc2;
    }
    frame[i++] = 0x7E;

    return frameSize;
}


unsigned char readResponse(int fd){
    
    unsigned char byte, controlByte;
    LinkLayerState state = START;
    
    while (state != STOP && alarmActivated == FALSE) {  
        if (read(fd, &byte, 1) < 0){
            perror("read");
            exit(-1);
        } 
            switch (state) {

                case START:
                    
                    if (byte == 0x7E){
                        state = FLAG_RCV;
                    }
                    break;
                case FLAG_RCV:
                    
                    if (byte == 0x03){

                        state = A_RCV;
                    }                                         
                    else if (byte != 0x7E) state = START;
                    break;
                case A_RCV:
                    //print byte content
                    if (byte == 0x05 || byte == 0x85 || byte == 0x01 || byte == 0x81){
                        state = C_RCV;
                        controlByte = byte;   
                    }
                    else if (byte == 0x7E) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (0x03 ^ controlByte)){
                        
                        state = BCC1_OK;
                    }
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
        
    return controlByte;
} 
    
