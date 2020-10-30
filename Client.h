/*
 * Description: Start a Streaming Client (TCP SOCK_STREAM) and stream data to a TCP Server at <ipaddress>.
 * Use PingPong buffer for Write/Read data.
 *
 * $DateTime: 2020/10/13 17:34:34 $
 * $Revision: #3 $
 *   $Author: murugan $
 *
 */
#ifndef __Z_STREAMCLIENT_H__
#define __Z_STREAMCLIENT_H__

#include <cstdint>
#include <iostream>
#include <string>
// For Reader Writer threads
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#define ZSC_SERVERPORTNUM 12000
#define ZSC_CLIENTPORTNUM 12010


// PingPong buffer to hold the below data
#define ZPP_IMAGE_WIDTH     960
#define ZPP_IMAGE_HEIGHT    720
#define ZPP_PIXEL_SIZE      4
#define ZPP_BUFFER_SIZE     ((ZPP_IMAGE_WIDTH)*(ZPP_IMAGE_HEIGHT)*(ZPP_PIXEL_SIZE))

/* PING PONG BUFFER - singleton class for Write/Read buffer
 * Can be used for fast writer and slow reader
 * Read / Write operations can be parallelized to increase throughput
 */
class ZPingPongBuffer
{
public:
    static ZPingPongBuffer& Instance()
    {
        static ZPingPongBuffer instance;
        return instance;
    }

    // Write / Put data into the pingpong buffer
    // Caller needs to provide buffer and size.
    //       IN buffer - data to be placed into the buffer
    //       IN size - caller should indicate the size of buffer
    // Return: 0 on success and -1 on failure
    // Data is copied into the pingpong buffer and
    // the Writer (caller) can free the buffer once the call returns.
    // This call should be followed by EndWrite() - see below.
    int BeginWrite(uint8_t* pBuffer, int size);

    // Call this to indicate the Write is done
    // and it's available for read.
    int EndWrite(void);

    // Read / Get data from the pingpong buffer
    // Reader (caller) will get a pointer to the buffer and the buffer size.
    // After processing buffer, the Reader(caller) should indicate that the
    // the buffer has been processed, - so could be released and available for
    // writing - by calling EndReading - see below.
    //        OUT pBuffer - pointer to buffer returned to the caller.
    //        OUT bufSize - buffer size returned to the caller
    int BeginRead(uint8_t* &pBuffer, int &bufSize);

    // After the buffer has been processed call below to indicate reading complete
    int EndRead(void);

private:
    ZPingPongBuffer();
    virtual ~ZPingPongBuffer();
    ZPingPongBuffer(const ZPingPongBuffer&)= delete;
    ZPingPongBuffer& operator=(const ZPingPongBuffer&)= delete;

    bool BufferCreated(){ return m_pBuffer[PING] != NULL && m_pBuffer[PONG] != NULL; }
    void Reset();

    enum
    {
        PING = 0,
        PONG = 1
    };

    uint8_t *m_pBuffer[2]; // ping(0) pong(1) buffers
    int m_BufferSize[2];

    std::atomic<int> m_writeIndex; // ping or pong. read index = ~(write index).
    std::atomic<bool>  m_bPingDataReady;
    std::atomic<bool>  m_bPongDataReady;
};


class NetClient
{

public:
    static NetClient* getInstance();

    int Configure();
    int Connect(char* servAddr, char* servSubnet, unsigned short servPort = ZSC_SERVERPORTNUM);
    int Stream(uint8_t* databuffer, int size);

    bool IsConfigured(){return m_bConfigured;}
    bool IsConnected(){return m_bConnected;}

private:
    NetClient();
    virtual ~NetClient();

    static std::atomic<NetClient*> instance;

    int m_sockfd;
    std::atomic<bool> m_bConfigured;
    std::atomic<bool> m_bConnected;

};


class MVClient
{
public:
    static MVClient* getInstance();

    void Initialize();
    void ConfigureStreaming(char* servIPv4Addr, char* servSubnet, unsigned short servPort = ZSC_SERVERPORTNUM);
    void StartStreaming();
    void StopStreaming();
    bool IsStreamingOn();


private:
    MVClient();
    virtual ~MVClient();

    void Reader();
    void Writer();
    void RunReaderThread();
    void RunWriterThread();

    static std::atomic<MVClient*> instance;

    std::atomic<bool> m_bConfigured;
    std::atomic<bool> m_bConnected;
    std::atomic<bool> m_bStreaming;

    //Reader, Writer threads
    std::atomic<bool> m_bReaderRunning;
    std::atomic<bool> m_bWriterRunning;
    std::thread m_Reader_thread;
    std::thread m_Writer_thread;

    bool m_bStreamingOn;
    std::mutex m;
    std::condition_variable cv;

};

#endif  //__Z_STREAMCLIENT_H__
