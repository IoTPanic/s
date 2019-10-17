# s - Little Stream - Embedded streaming layer for UDPX

**NOTE - This network layer is in development**

## Overview
s, also known as little stream, is a super simple data transport layer that is meant to be used in the UDPX project that both is small as possible, and made for real-time applications, which has the ability to be compressed. This is only meant for streaming, as command and control of devices should be over MQTT or another protocol. 

| Communication Stack |
|---------------------|
| Application Data    |
| s                   |
| UDP                 |

## Fragmentation 

The ESP32 does not have UDP fragmentation when using the Arduino platform, but to keep the project accessible, fragmentation support is a part of _s_. The first packet will contain a size, checksum, fragment, and frame value, while the following packets will contain a fragment and frame value. On delivery of the first packet, a buffer will be allocated in accordance with the unsigned 16 bit size value, and as each packet comes in they will be copied into the buffer. Once all the fragments have been collected, a fast and simple checksum operation will ensure the data is correct, and the message will be submitted to the application. All of this is done on a normal computer or the IDF framework, this just adds it in for Arduino in a light and real-time manner.

If the checksum is incorrect, the packet is dropped, and all buffers will have a short TTL to prevent late packets from creating memory issues or stream issues.

## Sessions
A session is started via MQTT, and must be agreed upon by both the client and the application before using this layer. To create a session, a NODE_ID, SESSION_ID, and VERSION must be set.  The NODE_ID is a unsigned eight bit integer to identify a stream, the SESSION_ID is used to ensure the correct session is in operation after being set by MQTT, and the VERSION is used to prevent different versions from communicating when incompatible.

## Protocol

### Header

| Version       | Downstream Flag | Compressed Flag | Type       |
|---------------|-----------------|-----------------|------------|
| 3-bit version | 1 bit           | 1 bit           | 3 bit flag |

The first byte of the header consists of a three bit version, a downstream flag, and a compressed flag, than a 3 bit type value. After that, a nodeID byte, session byte, frame counter, and fragment number are passed to complete the header. After that, the rest of length is a part of a message that is not related to the header.

If the compression bit is set, the rest of the message has received brotli compression and requires decompression. If the message is going downstream meaning it is going *down to a device*, the downstream bit must be set. The session ID byte must both match the set session ID

### Message

| Version, Flags, and Type | Node ID      | Session      | Frame        | Fragment     |
|--------------------------|--------------|--------------|--------------|--------------|
| Header                   | Unsigned Int | Unsigned Int | Unsigned Int | Unsigned Int |

One every fragment zero of a transaction, the first two bytes of the message is the byte length as an unsigned 16-bit integer. After that, a 8-bit XOR checksum follows and the payload than follows.

If the zero index packet is not received, the message is considered late and out of order and they will be dropped the same as a when the frame number received is smaller them the last received unless is a current transaction.

Once the number of bytes in the receiving buffer is the same as the length of the transaction as defined by the transaction size value in the header of the first packet, the message can than be passed to the callback function after the checksum is verified. This callback function can be defined after initialization with `setCallback()` which can accept a pointer to a function with the signature `void callback(uint8_t *pyld, uint16_t length)`. 

There is a constant `TTL` in `s.h` that will set the number of milliseconds from the first packet in a transaction until it is considered invalid. When the TTL is reached, the message is devalidated and the buffer is deallocated. 

### Details

### Sessions

A zero session value is invalid, the s library will return a `RECEIVE_FAIL_ZERO_SESS` constant.

Whenever a session ID is changed using `setSessionID(uint8_t)`, the frame counter resets so the next frame should be zero.


### Compression 

When the compression bit is set, the compression method used is Brotli compression which can be found at the https://github.com/google/brotli which lowers the amount of data sent, especially when the data is in a pattern without much difficulty.
