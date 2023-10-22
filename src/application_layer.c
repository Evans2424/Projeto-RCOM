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
    sleep(5);
    llclose(fd, linkLayer);

    switch(linkLayer.role)
    {
        case LlTx:
           // FILE* file = fopen(filename, "r");
            //int filesize = ftell(file);
            //fseek(file, 0L, SEEK_END);
           //buildControlPacket(linkLayer, filesize, filename);

            break;
        case LlRx:
            //TODO
            break;
    }


 }
