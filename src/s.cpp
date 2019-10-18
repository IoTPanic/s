#include <s.h>

s::s(){
    transactionBuffer = new transaction[DEFAULT_TRANSACTION_BUFFER_SIZE];
    transactionBufferSize = DEFAULT_TRANSACTION_BUFFER_SIZE;
}

s::s(uint8_t nodeId, uint8_t sessID, bool compress){
    nodeID = nodeId;
    currentSession = sessID;
    transactionBuffer = new transaction[DEFAULT_TRANSACTION_BUFFER_SIZE];
    transactionBufferSize = DEFAULT_TRANSACTION_BUFFER_SIZE;
    compressData = compress;
}

uint8_t s::receive(uint8_t *pyld, uint16_t len){
    s::message m;
    m = parseMessage(pyld, len);
    if(m.h.version!=VERSION){
        return RECEIVE_FAIL_BAD_HEADER;
    }
    if(m.h.session==0){
        return RECEIVE_FAIL_ZERO_SESS;
    }
    if(m.h.session!=currentSession){
        return RECEIVE_FAIL_INVALID_SESSION;
    }
    if(m.h.nodeID!=nodeID){
        return RECEIVE_FAIL_INCORRECT_NODE;
    }
    if(!m.h.downstream){
        return RECEIVE_FAIL_OTHER;
    }

    if(m.h.type==PKT_STREAM){
        bool rt = recentTransaction(m.h.frame);
        if((m.h.frame<lastDwnFrame&&!rt)||(m.h.frame==0&&lastDwnFrame<=255)){ // Message was not received before but it's frame number is smaller than the last one, therefore considered out of order, unless the counter is wrapping around
            return RECEIVE_FAIL_BAD_FRAME_ID;
        }
        #if DEBUG
        Serial.println("[ s ] Got valid message");
        #endif
        // If it isn't a recent transaction, we want to add it to our buffer
        if(!rt&&m.h.fragment==0){
            uint16_t received = 0;
            s::transaction t;
            // It is so dumb GCC 5 disallows default init of structs but not also brace enclosed inits.
            t.index = transactionIndex++;
            t.received = len;
            t.chunksReceived++;
            t.size= m.transactionLength;
            t.pyld = new uint8_t[m.transactionLength];
            t.checksum = m.transactionChecksum;
            t.frame = m.h.frame;
            t.valid = true;
            t.started = millis();
            memcpy(t.pyld, m.pyld, m.len);
            if(!initTransaction(&t)){
                #if DEBUG
                Serial.println("[ s ] Failed to create transaction");
                #endif
                return RECEIVE_FAIL_OTHER;
            }
        }else{
            if(!addToTransaction(&m, getTransactionByFrameFBuffer(m.h.frame))){
                #if DEBUG
                Serial.println("[ s ] Failed to add message to transaction");
                #endif
                return RECEIVE_FAIL_OTHER;
            }
        }
    }
    return RECEIVE_NO_FAIL;
}

void s::setCallback(void(*callbackFnc)(uint8_t *pyld, uint16_t len)){
    callback = callbackFnc;
}

void s::setNodeID(uint8_t ID){
    nodeID = ID;
}

void s::setSessionID(uint8_t sessionID){
    currentSession = sessionID;
    lastDwnFrame = 0;
    lastUpFrame = 0;
    lastSubmittedFrame = 0xff; // Reset frame counter
}

void s::disableCompression(){
    compressData = false;
    #if DEBUG
    Serial.println("[ s ] Compression disabled");
    #endif
}

void s::enableCompression(){
    compressData = true;
    #if DEBUG
    Serial.println("[ s ] Compression enabled");
    #endif
}

s::message s::parseMessage(uint8_t *pyld, uint16_t len){
    s::message m;
    // TODO reject impossibly small messages before they get to parse header
    m.h = parseHeader(pyld);
    if (!m.h.valid){
        m.valid = false;
        return m;
    }
    if(m.h.compressed&&len<5){ // If the header is set, and not just a header
        uint8_t p[MAX_PAYLOAD_SIZE];
        brotli_result = BrotliDecoderDecompress(len, pyld, (unsigned *)m.len, p);
        if(!brotli_result){
            return m;
        }
    }
    if(m.h.fragment==0){ // If this is the first fragment in the transaction, we want to gather the size and checksum
        m.transactionLength = pyld[5] | pyld[6]<<8;
        m.transactionChecksum = pyld[7];
        m.pyld = &pyld[8];
        m.len = len - 8;
    }else{
        m.pyld = &pyld[5];
        m.len = len - 5;
    }
    m.valid = true;
    return m;
}


s::header s::parseHeader(uint8_t *pyld){
    s::header h;
    h.version = pyld[0]>>5;
    if (h.version!=VERSION){
        h.valid = false;
        return h;
    }
    uint8_t flags = h.version <<5 ^ pyld[0]; // Store direction and compression flags
    if (flags >> 4 == 1){
        h.downstream = true;
        flags = flags ^ 0b00010000; // First time I've used binary literals, I wanna see if they work well
    }
    if (flags>>3==1){
        h.compressed = true;
        flags = flags ^ 0b00001000;
    }
    h.type = flags;
    h.nodeID = pyld[1];
    h.session = pyld[2];
    h.frame = pyld[3];
    h.fragment = pyld[4];
    h.valid = true;
    return h;
}

bool s::recentTransaction(uint8_t frame){
    for(unsigned i =0; i<transactionBufferSize;i++){
        if(transactionBuffer[i].valid){
            if(!checkTTL(&transactionBuffer[i])){
                // If time to live is passed, delete entry
                devalidateTransaction(&transactionBuffer[i]);
            }else if(transactionBuffer[i].frame==frame){\
                // If we found the frame, return true
                return true;
            }
        }
    }
    return false;
}

bool s::initTransaction(s::transaction *t){
    if(t->size<=t->received){
        #if DEBUG
        Serial.println("Was a single packet, submitting to application instead of adding to buffer");
        #endif
        return submitTransaction(t);
    }
    for(unsigned i =0; i<transactionBufferSize; i++){
        if(!checkTTL(&transactionBuffer[i])){
            // If time to live is passed, delete entry
            #if DEBUG
            Serial.print("Devalidating transaction with frame ");
            Serial.print(transactionBuffer[i].frame);
            Serial.println(" due to TTL");
            #endif
            devalidateTransaction(&transactionBuffer[i]);
        }
        if(!transactionBuffer[i].valid){
            memcpy(&transactionBuffer[i], t, sizeof(transaction));
            return true;
        }
    }
    return false;
}

bool s::addToTransaction(s::message *m, s::transaction *t){
    if(m==NULL||t==NULL){
        return false;
    }
    // Calculate the location in the buffer for the packet, they can come out of order as long as chunk 0 is received first
    // This assumes all packets between chunk n+0 and n-1 are MAX_PAYLOAD_SIZE
    unsigned loc = (MAX_PAYLOAD_SIZE - 3) + (MAX_PAYLOAD_SIZE*(t->chunksReceived-1));
    memcpy(&t->pyld[loc], m->pyld, m->len);
    t->received += m->len;
    if(t->received==t->size){
        return submitTransaction(t);
    }
    return true;
}

bool s::submitTransaction(s::transaction *t){
    if(callback==NULL){
        return false;
    }
    if(lastSubmittedFrame<=t->frame&&t->frame!=0){
        #if DEBUG
        Serial.println("[ s ] Was about to submit out of order frame");
        #endif
        return false;
    }
    else{
        if(calcChecksum(t->pyld, t->size)==t->checksum){
            callback(t->pyld, t->size);
        }else{
            #if DISABLE_CHECKSUM
            callback(t->pyld, t->size);
            #endif
        }
    }
    devalidateTransaction(t);
    return true;
}

void s::devalidateTransaction(s::transaction *t){
    if (t==NULL){
        return;
    }
    t->valid = false;
    delete []t->pyld;
    return;
}

bool s::checkTTL(transaction *t){
    if (t==NULL){
        return false;
    }
    if(millis()-t->started>=TTL){
        return false;
    }
    return true;
}

s::transaction *s::getTransactionByFrameFBuffer(uint8_t frame){
    for(unsigned i=0; i<transactionBufferSize; i++){
        if(transactionBuffer[i].frame==frame,transactionBuffer[i].valid){
            return &transactionBuffer[i];
        }
    }
}

uint8_t s::calcChecksum(uint8_t *pyld, uint16_t len){
    uint8_t c = 0;
    for(unsigned i=0; i<len;i++){
        c = c ^ pyld[i];
    }
    return c;
}