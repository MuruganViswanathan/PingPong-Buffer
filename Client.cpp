/*
 * Description: Start a TCP Client to connect to a TCP Server at <ipaddress>.
 * Receive packets from Server and do network performance profiling
 *
 * $DateTime: 2020/10/13 17:34:34 $
 * $Revision: #3 $
 *   $Author: murugan $
 *
 */

#include "Client.h"

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

//#include<sys/un.h>
//#include<netdb.h>
#include <sys/time.h>
//#include <time.h>


std::atomic<MVClient*> MVClient::instance;
std::atomic<NetClient*> NetClient::instance;


//------------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////
// ZPingPongBuffer
//////////////////////////////////////////////////////////////////////

ZPingPongBuffer::ZPingPongBuffer()
{
    try {
        m_pBuffer[PING] = new uint8_t[ZPP_BUFFER_SIZE];
    }  catch(int e) {
        printf("ZPingPongBuffer() - Could not allocate PING memory! error: %d\n", e);
        m_pBuffer[PING] = NULL;
    }

    try {
        m_pBuffer[PONG] = new uint8_t[ZPP_BUFFER_SIZE];
    }  catch(int e) {
        printf("ZPingPongBuffer() - Could not allocate PONG memory! error: %d\n", e);
        m_pBuffer[PONG] = NULL;
    }

    m_BufferSize[PING] = 0;
    m_BufferSize[PONG] = 0;
    m_writeIndex.store(PING, std::memory_order_relaxed);
    m_bPingDataReady.store(false, std::memory_order_relaxed);
    m_bPongDataReady.store(false, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------

ZPingPongBuffer::~ZPingPongBuffer()
{
    if(m_pBuffer[PING] != NULL)
        delete [] m_pBuffer[PING];

    if(m_pBuffer[PONG] != NULL)
        delete [] m_pBuffer[PONG];

}

//------------------------------------------------------------------------------

void ZPingPongBuffer::Reset()
{
    m_BufferSize[PING] = 0;
    m_BufferSize[PONG] = 0;
    m_writeIndex.store (PING, std::memory_order_relaxed);
    m_bPingDataReady.store (false, std::memory_order_relaxed);
    m_bPongDataReady.store (false, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------

int ZPingPongBuffer::BeginWrite(uint8_t* pBuffer, int size)
{
    if(!BufferCreated())
    {
        std::cout << "ZPingPongBuffer: Buffer memory not created!" << std::endl;
        return -1;
    }

    if( !pBuffer || size == 0)
        //if( !pBuffer || pBuffer[0] == 0 || size == 0)
    {
        std::cout << "BeginWrite: ZPingPongBuffer: Wrong params!" << std::endl;
        return -1;
    }

    if(size > ZPP_BUFFER_SIZE)
    {
        std::cout << "ZPingPongBuffer::BeginWrite - Buffer size " << size << " too large!" << std::endl;
        return -1;
    }

    if(m_writeIndex.load(std::memory_order_acquire) == PING)  // if writing to ping buffer
    {
        if(!m_bPingDataReady.load(std::memory_order_acquire))  // if ping buffer is available for write
        {
            if(m_BufferSize[PING]+size <= ZPP_BUFFER_SIZE) //
            {
                memcpy(m_pBuffer[PING]+m_BufferSize[PING], pBuffer, size);
                m_BufferSize[PING] += size;
                //printf("ZPingPongBuffer::BeginWrite - wrote %d to PING buffer - m_pBuffer[PING]=0x%x, m_BufferSize[PING]=%d \n",
                //       size, *(unsigned int*)(&m_pBuffer[PING]), m_BufferSize[PING]);
            }
            else
            {
                std::cout << "ZPingPongBuffer::BeginWrite - PING Buffer full!! cannot write any more!" << std::endl;
                return -1;
            }
        }
        else
        {
            //std::cout << "ZPingPongBuffer::BeginWrite -- skipping -- PING buffer not yet read" << std::endl;
            return -1;
        }
    }
    else // PONG
    {
        if(!m_bPongDataReady.load(std::memory_order_acquire))
        {
            if(m_BufferSize[PONG] <= ZPP_BUFFER_SIZE)
            {
                memcpy(m_pBuffer[PONG]+m_BufferSize[PONG], pBuffer, size);
                m_BufferSize[PONG] += size;
                //printf("ZPingPongBuffer::BeginWrite - wrote %d to PONG buffer - m_pBuffer[PONG]=0x%x, m_BufferSize[PONG]=%d \n",
                //       size, *(unsigned int*)(&m_pBuffer[PONG]), m_BufferSize[PONG]);
            }
            else
            {
                std::cout << "ZPingPongBuffer: PONG Buffer full!! cannot write any more!" << std::endl;
                return -1;
            }
        }
        else
        {
            //std::cout << "ZPingPongBuffer::BeginWrite -- skipping -- PONG buffer not yet read" << std::endl;
            return -1;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------

int ZPingPongBuffer::EndWrite()
{
    if(!BufferCreated())
    {
        std::cout << "ZPingPongBuffer::EndWrite - Buffer memory not created!" << std::endl;
        return -1;
    }

    if(m_writeIndex.load(std::memory_order_acquire) == PING)
    {
        m_bPingDataReady.store(true, std::memory_order_release);
        if(!m_bPongDataReady.load(std::memory_order_acquire))  // switch if PONG is ready for write
        {
            //std::cout << "ZPingPongBuffer::EndWrite - switch to PONG" << std::endl;
            m_writeIndex.store(PONG, std::memory_order_release);
        }
        else
        {
            //std::cout << "ZPingPongBuffer::EndWrite -- PONG Data still being read --" << std::endl;
        }
    }
    else
    {
        m_bPongDataReady.store(true, std::memory_order_release);
        if(!m_bPingDataReady.load(std::memory_order_acquire))  // switch if PING is ready for write
        {
            //std::cout << "ZPingPongBuffer::EndWrite - switch to PING" << std::endl;
            m_writeIndex.store(PING, std::memory_order_release);
        }
        else
        {
            //std::cout << "ZPingPongBuffer::EndWrite -- PING Data still being read --" << std::endl;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------

int ZPingPongBuffer::BeginRead(uint8_t* &pBuffer, int &bufSize)
{
    if(!BufferCreated())
    {
        std::cout << "ZPingPongBuffer::BeginRead - Buffer memory not created!" << std::endl;
        return -1;
    }

    pBuffer = NULL;
    bufSize = 0;

    if(m_writeIndex.load(std::memory_order_acquire) == PING)
    {
        //if Write index is PING, then the read index is PONG
        // Read data only if the PONG buffer is full
        if(m_bPongDataReady.load(std::memory_order_acquire))
        {
            pBuffer = m_pBuffer[PONG];
            bufSize = m_BufferSize[PONG];
        }
    }
    else
    {
        //if Write index is PONG then the read index is PING
        // Read data only if the PING buffer is full
        if(m_bPingDataReady.load(std::memory_order_acquire))
        {
            pBuffer = m_pBuffer[PING];
            bufSize = m_BufferSize[PING];
        }
    }

    return 0;
}

//------------------------------------------------------------------------------
int ZPingPongBuffer::EndRead()
{
    if(!BufferCreated())
    {
        std::cout << "ZPingPongBuffer::EndRead - Buffer memory not created!" << std::endl;
        return -1;
    }

    //if Write index is PING, then the read index is PONG
    if(m_writeIndex.load(std::memory_order_acquire) == PING)
    {
        m_BufferSize[PONG] = 0; // reading PONG complete. reset size.
        //std::cout << "ZPingPongBuffer::EndRead - making PONG buffer empty" << std::endl;
        m_bPongDataReady.store(false, std::memory_order_release); // indicate buffer empty
    }
    else
    {
        m_BufferSize[PING] = 0; // reading PING complete. reset size.
        //std::cout << "ZPingPongBuffer::EndRead - making PING buffer empty" << std::endl;
        m_bPingDataReady.store(false, std::memory_order_release);  // indicate buffer empty
    }

    return 0;
}


/////////////////////////////////////////////////////////////////////////////
// NetClient
/////////////////////////////////////////////////////////////////////////////

NetClient::NetClient() :
        m_sockfd(-1),
        m_bConfigured(false),
        m_bConnected(false)
{
}

//------------------------------------------------------------------------------

NetClient::~NetClient()
{
    if(m_sockfd != -1)
    {
        close(m_sockfd);
    }
    m_bConfigured = false;
    m_bConnected = false;
}

//------------------------------------------------------------------------------

NetClient* NetClient::getInstance()
{
    NetClient* pNC = instance.load(std::memory_order_acquire);
    if(!pNC)
    {
        pNC = new NetClient();
        instance.store(pNC, std::memory_order_release);
    }

    return pNC;
}

//------------------------------------------------------------------------------

int NetClient::Configure()
{
    if(m_bConfigured)
    {
        printf("NetClient - already configured");
        return 0;
    }

    //1. create socket
    //
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_sockfd < 0)
    {
        printf("Error: socket creation failed!\n");
        return -1;
    }
    printf("socket created successfully\n");


    //2. bind
    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(ZSC_CLIENTPORTNUM);
    if(bind(m_sockfd, (struct sockaddr*)&my_addr, sizeof(my_addr)) != 0)
    {
        printf("Error: bind error!\n");
        close(m_sockfd);
        return -1;
    }
    printf("bind successful...\n");

    printf("NetClient - configured.\n"); fflush(stdout);

    m_bConfigured = true;
    return 0;
}

//------------------------------------------------------------------------------

int NetClient::Connect(char* servAddr, char* servSubnet, unsigned short servPort)
{
    if(!m_bConfigured)
    {
        printf("NetClient - Not yet configured! Configure first.\r\n");
        return -1;
    }

    if(m_bConnected)
        return 0; // already connected, return

    //TODO: validate server IP
    if(servAddr == NULL)
    {
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    strncpy(ip, servAddr, INET_ADDRSTRLEN);

    printf("Connecting to server: %s, port %d\n", ip, servPort);

    // server
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(servPort);
    if(connect(m_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0)
    {
        printf("Error: could not connect to server at %s\n", ip);
        close(m_sockfd);
        return -1;
    }
    printf("connected to server at %s...\n", ip); fflush(stdout);

    m_bConnected = true;
    return 0;
}

//------------------------------------------------------------------------------

int NetClient::Stream(uint8_t* databuffer, int size)
{
    if(!m_bConfigured || !m_bConnected)
    {
        printf("Error: No connection to server. Connect to server first.\n");
        return -1;
    }

    if(!databuffer || size == 0)
    {
        //printf("NetClient::Stream: -- empty data buffer -- not calling send..\n");
        return -1;
    }

    // Send
    static int i;
    i++;
    int rc = send(m_sockfd, databuffer, size, 0);
    int errnum = errno;
    if(rc == -1)
    {
        if(errnum == EBADF || errnum == ENOTSOCK || errnum == ENOTCONN)
        {
            printf("Socket fd error. check client socket.  \n");
        }
        printf("Received error for data block %d... exiting.  \n", i); fflush(stdout);
        return -1;
    }
    else
    {
        printf(">>>>>>>>> Transmitted data block %d (size %d) to server... \n", i, rc);
    }

    return rc;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
//  MVClient
//////////////////////////////////////////////////////////////////////////////////////////////////////
MVClient::MVClient() :
        m_bConfigured(false),
        m_bConnected(false),
        m_bStreaming(false),
        m_bReaderRunning(false),
        m_bWriterRunning(false),
        m_bStreamingOn(false)
{
}

MVClient::~MVClient()
{
    m_bConfigured = false;
    m_bConnected = false;
    m_bStreaming = false;
}


MVClient* MVClient::getInstance()
{
    MVClient* pSC = instance.load(std::memory_order_acquire);
    if(!pSC)
    {
        pSC = new MVClient();
        instance.store(pSC, std::memory_order_release);
    }

    return pSC;
}

void MVClient::Initialize()
{
    // Start Reader
    printf("MVClient::Initialize() start.\r\n");
    RunReaderThread();
    printf("MVClient::Initialize() return.\r\n");
}

void MVClient::ConfigureStreaming(char* servIPv4Addr, char* servSubnet, unsigned short servPort)
{
    if(m_bConfigured.load(std::memory_order_acquire))
    {
        printf("MVClient - already configured.\r\n");
        return;
    }

    if(NetClient::getInstance()->Configure() == -1)
    {
        printf("MVClient - could not create NetClient socket!\r\n");
        return;
    }

    if(NetClient::getInstance()->Connect(servIPv4Addr, servSubnet, servPort) == -1)
    {
        printf("MVClient - could not connect to server!\r\n");
        return;
    }

    m_bConfigured.store(true, std::memory_order_release);

}

void MVClient::StartStreaming()
{
    // Notify Streaming started
    {
        std::lock_guard<std::mutex> lk(m);
        m_bStreamingOn = true;
        std::cout << "MVClient::StartStreaming - Streaming ON. " << std::endl;
    }
    cv.notify_one();
}

void MVClient::StopStreaming()
{
    {
        std::lock_guard<std::mutex> lk(m);
        m_bStreamingOn = false;
        std::cout << "MVClient::StopStreaming - Streaming OFF. " << std::endl;
    }
    cv.notify_one();
}

bool MVClient::IsStreamingOn()
{
    bool ret = false;
    {
        std::lock_guard<std::mutex> lk(m);
        ret = m_bStreamingOn;
    }
    return ret;
}

//  spawns a thread to do the reading
void MVClient::RunReaderThread()
{
    if(m_bReaderRunning.load(std::memory_order_acquire))
        return; // dont start again - only one reader allowed

    //Thread  -- Reader
    m_Reader_thread = std::thread(&MVClient::Reader, this);

}

//  spawns a thread to do the writing
void MVClient::RunWriterThread()
{
    if(m_bWriterRunning.load(std::memory_order_acquire))
        return; // dont start again - only one reader allowed

    //Thread  -- Writer
   m_Writer_thread = std::thread(&MVClient::Writer, this);

}

void MVClient::Reader()
{
    m_bReaderRunning.store(true, std::memory_order_release);
    printf("=====> MVClient::Reader(): started \n");

    uint8_t* pBuf = NULL;
    int size = 0;

    while(1)
    {
        // Wait until m_bStreamingOn is true
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&]{return MVClient::m_bStreamingOn;});
        lk.unlock();

        //Streaming is turned on at this point
        //Prepare for reading data
        pBuf = NULL;
        size = 0;

        ZPingPongBuffer::Instance().BeginRead(pBuf, size);
        if(pBuf && size != 0)
        {
            printf("=> Reader: read pBuf 0x%x, size %d \n", *(unsigned int*)(&pBuf), size);

            //Call Net StreamingClient and send over network
            NetClient* pClient = NetClient::getInstance();
            if (pClient && pClient->IsConnected())
            {
                if(pClient->Stream(pBuf, size) != -1)
                {
                    // Buffer read complete. Make it available for Write
                    ZPingPongBuffer::Instance().EndRead();
                }
                else
                {
                    printf( "NetStreaming not successful... will retry same data again...");
                }
            }
            else
            {
                printf("NetStreaming client not connected.. will try again after 1 sec \n");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    printf("MVClient::Reader() returning... \n");
}

void MVClient::Writer()
{
    m_bWriterRunning.store(true, std::memory_order_release);
    printf("=====> MVClient::Writer(): started \n");
    uint8_t* databuffer = NULL;
    const unsigned int DATASIZE = ZPP_BUFFER_SIZE;

    try{
        databuffer = new uint8_t[DATASIZE];
    } catch(int e) {
        printf("Writer: could not allocate memory of size: %u ! error:%d\n", DATASIZE, e);
        return;
    }

    while(1)
    {
        // dummy data 1-99
        static int i;
        i++;
        memset(databuffer, i%100, DATASIZE);

        if( ZPingPongBuffer::Instance().BeginWrite(databuffer, DATASIZE) >= 0)
        {
            printf("=> Writer: wrote block %d databuffer 0x%x, size %d \n", i, *(unsigned int*)(&databuffer), DATASIZE);
        }

        ZPingPongBuffer::Instance().EndWrite();
    }

    return;

     printf("MVClient::Writer() returning... \n");
}


//------------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////
// TEST - PING PONG Buffer - Reader and Writer Threads - Remote Server
//////////////////////////////////////////////////////////////////////

//#ifdef TESTSTREAMING

// Writer writes large data into PingPong buffer continuously
void Writer()
{
    uint8_t* databuffer = NULL;
    const unsigned int DATASIZE = ZPP_BUFFER_SIZE;

    try{
        databuffer = new uint8_t[DATASIZE];
    } catch(int e) {
        printf("Writer: could not allocate memory of size: %u ! error:%d\n", DATASIZE, e);
        return;
    }

    while(1)
    {
        // dummy data 1-99
        static int i;
        i++;
        memset(databuffer, i%100, DATASIZE);

        if( ZPingPongBuffer::Instance().BeginWrite(databuffer, DATASIZE) >= 0)
        {
            printf("=> Writer: wrote block %d databuffer 0x%x, size %d \n", i, *(unsigned int*)(&databuffer), DATASIZE);
        }

        ZPingPongBuffer::Instance().EndWrite();
    }

    return;
}

// Reader reads the data from Ping Pong buffer,
//        creates Client and sends it over network
void Reader(NetClient* pClient)
{

    while(1)
    {
        uint8_t* databuffer = NULL;
        int size = 0;

        ZPingPongBuffer::Instance().BeginRead(databuffer, size);
        if(size != 0 && databuffer)
        {
            printf("=> Reader: read databuffer 0x%x, size %d \n", *(unsigned int*)(&databuffer), size);

            //send over network
            if(pClient->Stream(databuffer, size) == -1)
            {
                std::cout << "Reader: pClient->Stream() returned -1 " << std::endl;
            }
        }

        ZPingPongBuffer::Instance().EndRead();
    }

}

void g_RWThreadCreate()
{
    //Thread 1 -- Writer
    std::thread PingPongWriter([](){
        uint8_t* databuffer = NULL;
        const unsigned int DATASIZE = ZPP_BUFFER_SIZE;

        try{
            databuffer = new uint8_t[DATASIZE];
        } catch(int e) {
            printf("Writer: could not allocate memory of size: %u ! error:%d\n", DATASIZE, e);
            return;
        }

        while(1)
        {
            // dummy data 1-99
            static int i;
            i++;
            memset(databuffer, i%100, DATASIZE);

            if( ZPingPongBuffer::Instance().BeginWrite(databuffer, DATASIZE) >= 0)
            {
                printf("=====> Writer: wrote block %d databuffer 0x%x, size %d \n", i, *(unsigned int*)(&databuffer), DATASIZE);
            }

            ZPingPongBuffer::Instance().EndWrite();
        }

        return;
    });

    //Thread 2 -- Reader
    std::thread PingPongReader([](){
        printf("=====> PingPongReader: started \n");
        uint8_t* pBuf = NULL;
        int size = 0;
        while(1)
        {
            //if(m_bStreamingRequest) // TODO - replace this busy wait

            pBuf = NULL;
            size = 0;
            ZPingPongBuffer::Instance().BeginRead(pBuf, size);
            if(pBuf && size != 0)
            {
                printf("=====> Reader: read pBuf 0x%x, size %d \n", *(unsigned int*)(&pBuf), size);

                //Call Net StreamingClient and send over network
                NetClient* pClient = NetClient::getInstance();
                if (pClient && pClient->IsConnected())
                {
                    if(pClient->Stream(pBuf, size) != -1)
                    {
                        // Buffer read complete. Make it available for Write
                        ZPingPongBuffer::Instance().EndRead();
                    }
                    else
                    {
                        printf( "NetStreaming not successful... will retry same data again...");
                    }
                }
                else
                {
                    printf("NetStreaming client not connected \n");
                }
            }
        }
    });

    //PingPongWriter.join();
    //PingPongReader.join();
    //PingPongWriter.detach();
    //PingPongReader.detach();

    printf("g_RWThreadCreate() returning... \n");
}



//////////////////////////////////////////////////////////////////////////////////////////////////////
/* main */
int main(int argc, char* argv[])
{
    //validate
    if (argc != 2)
    {
        printf ("Usage: %s <server IP addr> \n", argv[0]);
        return -1;
    }

    char ip[INET_ADDRSTRLEN], subnet[INET_ADDRSTRLEN];
    strncpy(ip, argv[1], INET_ADDRSTRLEN);
    strncpy(subnet, "255.255.255.0", INET_ADDRSTRLEN);


    // Initialize - PindPongBuffer. Start Reader thread.
    MVClient::getInstance()->Initialize();

    //Thread -- Writer will start writing dummy data to ZPingPongBuffer
    std::thread writer_thread(Writer);

    // Configure NetClient & Remote server
    MVClient::getInstance()->ConfigureStreaming(ip, subnet);

    //Start streaming
    std::cout << "Start Streaming" << std::endl;
    MVClient::getInstance()->StartStreaming();

    sleep(5);

    std::cout << "Stop Streaming after 5 sec" << std::endl;
    MVClient::getInstance()->StopStreaming();

    sleep(10);
    std::cout << "Restart Streaming after 10 sec" << std::endl;
    MVClient::getInstance()->StartStreaming();

    sleep(1);
    std::cout << "Stop after 1 sec" << std::endl;
    MVClient::getInstance()->StopStreaming();

    /*
    ///<TEST2> - Send using PingPong Buffers
   NetClient* pClient = NetClient::getInstance();
    if (pClient && pClient->Connect(ip, subnet) < 0)
    {
        printf ("pClient->Connect: failed - ip %s \n", ip);
        return -1;
    }
    //Thread 1 -- Writer
    std::thread writer_thread(Writer);

    //Thread 2 -- Reader
    std::thread reader_thread(Reader, pClient);

    writer_thread.join();
    reader_thread.join();


    ///</TEST2> - End
     */

    /*
    ///<TEST3>
    g_RWThreadCreate();
    ///</TEST3> - end
     */


    //Thread to listen to user input and stop streaming

    while(1)
    {
        printf("main () sleeping...<ctrl>+c to terminate \n");
        sleep(1);
    }
    printf("main() returning....\n");

    return 0;

}

int maintest(int argc, char* argv[])
{
    printf("Simple thread test to join..\r\n");

    std::thread Thread1([](){
        static int i;
        while(i<10)
        {
            // dummy data 1-99

            i++;

            printf("=====> Thread1: %d \n", i);

            sleep(1);
        }

        return;
    });

    printf("main() detached Thread1....\n");
    Thread1.detach();

    printf("main() sleeping for 3 sec....\n");
    sleep(3);

    printf("main() waiting to join Thread1....\n");
    Thread1.join();

    printf("main() returning....\n");
    return 0;
}

//#endif
