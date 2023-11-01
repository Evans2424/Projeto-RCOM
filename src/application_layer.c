// Application layer protocol implementation

#include "application_layer.h"


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort,serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    int fd = llopen(linkLayer);
    if(fd < 0){
        printf("Error opening connection\n");
        exit(1);
    }

    switch(linkLayer.role)
    {
        case LlTx:
           printf("im here\n");
           FILE* file = fopen(filename, "r");

            if(file == NULL){
                printf("Error opening file\n");
                exit(-1);}

            int start = ftell(file); //Point to the beginning of the file
            fseek(file, 0L, SEEK_END); //Point to the end of the file
            long int filesize = ftell(file) - start; //Get the size of the file
            fseek(file, start, SEEK_SET); //Point to the beginning of the file

            //Build the control packet
            unsigned char sizeofFile = sizeof(filesize);
            unsigned char len = strlen(filename);

            unsigned int packetSize = sizeofFile + len + 5;
            unsigned char* controlPacket = malloc(packetSize);

            unsigned int pos = 0;
            controlPacket[pos++] = 0x02; //START control packet
            controlPacket[pos++] = 0x00; //Tamanho do ficheiro
            controlPacket[pos++] = sizeofFile;
            int j = 3;
            for (unsigned char i = 0; i < sizeofFile; i++) controlPacket[j + i] = (filesize >> (8 * i)) & 0xFF; //Put filesize in bytes
            controlPacket[j++] = 0x01;
            controlPacket[j++] = len;
            
            memcpy(controlPacket + j + sizeofFile, filename, len);
            
            //read file
            char *buffer = (char *)malloc(filesize);
            fread(buffer, filesize, 1, file);
            fclose(file);

            //write packet
            if(llwrite(fd, controlPacket, packetSize) < 0){ //o write do dobby tem mais um parametro (?)
                printf("Error sending control packet\n");
                exit(-1);
            }

            long int bytesSent = 0;
            

            while(bytesSent < filesize){
                long int bytesLeft = filesize - bytesSent;
                int dataSize = bytesLeft > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
                
                unsigned char* data = (unsigned char*) malloc(dataSize);
                memcpy(data, buffer, dataSize);

                int packetSize;
                unsigned char* packet = getDataPacket(data, dataSize, &packetSize);

                if(llwrite(fd, packet, packetSize) < 0){
                printf("Error sending packet\n");
                exit(-1);
            }
                bytesSent += dataSize;
                free(packet);
            }
            free(buffer);

            //build the final part of control packet
            unsigned char *controlPacketFinal = malloc(packetSize);
            memcpy(controlPacketFinal, controlPacket, packetSize);
            controlPacketFinal[0] = 0x03; // control field end

            //write control packet
            if(llwrite(fd, controlPacketFinal, packetSize) == -1) { 
                printf("Error in end control packet\n");
                exit(-1);
            }

            free(controlPacket);
            free(controlPacketFinal);

            llclose(fd, linkLayer);

            break;
        
        case LlRx:
            
            unsigned char *packet = malloc(MAX_PAYLOAD_SIZE);
            int packetSize2 = -1;

            while(packetSize2 < 0){
                packetSize2 = llread(fd, packet);
            }

            unsigned long fileSizeRx = 0;
            unsigned char *fileName = parseCtrlPacket(packet, packetSize2, &fileSizeRx);

            FILE* newFile = fopen((char *)fileName, "wb");

            while(TRUE) {
                
                while (packetSize2 < 0) {
                    packetSize2 = llread(fd, packet);
                }

                if(packetSize2 == 0) break;

                else if(packet[0] != 3) {
                    unsigned char *buff = malloc(packetSize2);

                    memcpy(buff, packet + 4, packetSize2 - 4);
                    fwrite(buff, 1, packetSize2 - 4, newFile);
                    free(buff);
                }
                else continue;
            }

            fclose(newFile);
            break;
    }
 }

 unsigned char* getDataPacket(unsigned char *data, int dataSize, int *packetSize){
        *packetSize = 3 + dataSize;
        unsigned char* packet = malloc(3 + dataSize);
        packet[0] = 0x01; //control
        //packet[1] e packet[2] vao conter o numero de bytes dos dados
        packet[1] = (dataSize >> 8) & 0xFF;
        packet[2] = dataSize & 0xFF;
        memcpy(packet + 3, data, dataSize);
        return packet;

 }

 unsigned char* parseCtrlPacket(unsigned char* packet, int size, unsigned long int *fileSize) {

    unsigned char fileSizeBytes = packet[2];
    unsigned char sizeAux[fileSizeBytes];

    memcpy(sizeAux, packet + 3, fileSizeBytes);
    for (unsigned int i = 0; i < fileSizeBytes; i++) *fileSize |= (sizeAux[fileSizeBytes - i - 1] << (8 * i));

    unsigned char fileNameBytes = packet[3 + fileSizeBytes+1];
    unsigned char *name = (unsigned char *) malloc(fileNameBytes);

    memcpy(name, packet + 3 + fileSizeBytes + 2, fileNameBytes);

    return name;
 }