/**
 * s - Little Stream - A simple data transport protocol
 * 
 * Please read the README for more information
 */
#ifndef s_h
#define s_h

#include <Arduino.h>
#include "brotli/decode.h"

#define VERSION 0
#define BROTLI_DECOMPRESSION_BUFFER 3000
#define DEFAULT_TRANSACTION_BUFFER_SIZE 5 // We need to find this sweet spot

#define PKT_ACK 0x0
#define PKT_STREAM 0x1

#define RECEIVE_NO_FAIL 0x0 // Good packet
#define RECEIVE_FAIL_ZERO_SESS 0x1 // The session was zero
#define RECEIVE_FAIL_BAD_HEADER 0x2 // Bad header, ie bad version or not a s packet
#define RECEIVE_FAIL_INVALID_SESSION 0x3 // Session did not match with the current session
#define RECEIVE_FAIL_INCORRECT_NODE 0x4 // Packet was meant for another node
#define RECEIVE_FAIL_BAD_FRAME_ID 0x5 // Frame was late or out of order
#define RECEIVE_FAIL_COMPRESSION 0x6
#define RECEIVE_FAIL_OTHER 0x7

#define FRAME_SIZE_LIMITATION 1500
#define MAX_PAYLOAD_SIZE FRAME_SIZE_LIMITATION - 5 // WiFi frame with ESP, may need to make a little smaller due to overhead in the UDP lib
#define TTL 1000

#define DEBUG

class s
{
    public:
    typedef struct{
        bool downstream = false;
        bool compressed = false;
        uint8_t type = 0x0;
        uint8_t version = 0x0;
        uint8_t nodeID = 0;
        uint8_t session = 0;
        uint8_t frame = 0;
        uint8_t fragment = 0;
        bool valid = false;
    } header;

    typedef struct{
        header h;
        uint8_t *pyld;
        uint16_t len;
        uint16_t transactionLength = 0;
        uint8_t transactionChecksum = 0x0;
        bool valid = false;
    } message;

    typedef struct {
        unsigned index = 0;
        uint16_t received = 0; // How many bytes have been received
        unsigned chunksReceived = 0;
        uint16_t size = 0; // Total transaction size
        uint8_t *pyld = NULL;
        uint8_t checksum = 0x0;
        uint8_t frame = 0;
        bool valid = false;
        unsigned long started = 0; // Millis when started
    } transaction;

    // Constructor
    s();
    // Takes in a node id and a current session. The sess ID can be ignored and provided later as well
    s(uint8_t nodeId, uint8_t currentSession = 0x0, bool compress = false);

    void setCallback(void(*callbackFnc)(uint8_t *pyld, uint16_t len));
    void setNodeID(uint8_t ID);
    void setSessionID(uint8_t ID);
    // Use this if you want to add the ability to disable compression using the control layer
    // such as MQTT, or want to disable it from startup. The client must be aware of this.
    void disableCompression();
    // Enables compression, use this to re-enable compression when using the function above
    void enableCompression();

    uint8_t receive(uint8_t *pyld, uint16_t len);

    private:
    message parseMessage(uint8_t *pyld, uint16_t len);
    header parseHeader(uint8_t *pyld);

    // Init transaction in buffer
    bool initTransaction(transaction *t);
    // See if a transaction is buffered by frame number
    bool recentTransaction(uint8_t frame);
    //
    void devalidateTransaction(transaction *t);
    bool addToTransaction(message *m, transaction *t);

    uint8_t *compress(uint8_t *pyld, uint16_t *len);
    uint8_t *decompress(uint8_t *pyld, uint16_t *len);

    // Check if a message is valid based off TTL; returns if transaction is valid
    bool checkTTL(transaction *t);
    transaction *getTransactionByFrameFBuffer(uint8_t frame);
    uint8_t calcChecksum(uint8_t *pyld, uint16_t len);
    bool submitTransaction(transaction *t);

    uint8_t lastDwnFrame = 0;
    uint8_t lastUpFrame = 0;

    uint8_t lastSubmittedFrame = 0;

    // NodeID must not be 0
    uint8_t nodeID = 0;
    uint8_t currentSession = 0x0;

    transaction *transactionBuffer;
    unsigned transactionBufferSize = 0;
    unsigned long transactionIndex = 0; // Used as sort of an ID


    BrotliDecoderResult brotli_result;

    bool compressData = false;

    void (*callback)(uint8_t *pyld, uint16_t len) = NULL;
};
#endif