// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <stdio.h>
#include <zip.h>

#include "PS4Load.hpp"
#include <Utils/Kdlsym.hpp>
#include <Utils/Logger.hpp>

using namespace Mira::Plugins;

#define SELF_PATH               "/data/ps4load.tmp"
#define VERSION                 "v1.0.0"
#define PORT                    4299
#define MAX_ARG_COUNT           0x100
#define CHUNK                   0x4000
#define PKZIP                   0x04034B50

#pragma region INTERFACE Members
PS4Load::PS4Load(uint16_t p_Port, char* p_Device):
    m_Socket(-1),
    m_Port(p_Port),
    m_Thread(nullptr),
    m_Running(false)
{
    // Zero out the address
    memset(&m_Address, 0, sizeof(m_Address));

    // Get the current device path
    auto s_DevicePath = p_Device == nullptr ? DEFAULT_PATH : p_Device;

    // Calcualte the path length and cap it
    auto s_DevicePathLength = strlen(s_DevicePath);
    if (s_DevicePathLength >= sizeof(m_Device))
        s_DevicePathLength = sizeof(m_Device) - 1;
    
    memcpy(m_Device, s_DevicePath, s_DevicePathLength);
}

PS4Load::~PS4Load()
{

}

bool PS4Load::OnLoad()
{
	WriteLog(LL_Info, "PS4Load::OnLoad() " VERSION);
	return true;
}

bool PS4Load::OnUnload()
{
		return true;
}

bool PS4Load::OnSuspend()
{
		return true;
}

bool PS4Load::OnResume()
{
		return true;
}


bool PS4Load::Startup()
{
    WriteLog(LL_Error, "here");

    auto s_MainThread = Mira::Framework::GetFramework()->GetMainThread();
    if (s_MainThread == nullptr)
    {
        WriteLog(LL_Error, "could not get main thread");
        return false;
    }

    WriteLog(LL_Error, "here");

    auto kthread_add = (int(*)(void(*func)(void*), void* arg, struct proc* procptr, struct thread** tdptr, int flags, int pages, const char* fmt, ...))kdlsym(kthread_add);
    WriteLog(LL_Error, "m_Socket (%d) &m_Socket (%p) s_MainThread (%p)", m_Socket, &m_Socket, s_MainThread);

    // Create a socket
    m_Socket = ksocket_t(AF_INET, SOCK_STREAM, 0, s_MainThread);
    WriteLog(LL_Error, "socket: (%d)", m_Socket);
    if (m_Socket < 0)
    {
        WriteLog(LL_Error, "could not initialize socket (%d).", m_Socket);
        return false;
    }
    WriteLog(LL_Info, "socket created: (%d).", m_Socket);

    // Set our server to listen on 0.0.0.0:<port>
    memset(&m_Address, 0, sizeof(m_Address));
    m_Address.sin_family = AF_INET;
    m_Address.sin_addr.s_addr = htonl(INADDR_ANY);
    m_Address.sin_port = htons(m_Port);
    m_Address.sin_len = sizeof(m_Address);

    WriteLog(LL_Error, "here");

    // Bind to port
    auto s_Ret = kbind_t(m_Socket, reinterpret_cast<struct sockaddr*>(&m_Address), sizeof(m_Address), s_MainThread);
    WriteLog(LL_Error, "ret (%d).", s_Ret);
    if (s_Ret < 0)
    {
        WriteLog(LL_Error, "could not bind socket (%d).", s_Ret);
        kshutdown_t(m_Socket, SHUT_RDWR, s_MainThread);
        kclose_t(m_Socket, s_MainThread);
        m_Socket = -1;
        return false;
    }
    WriteLog(LL_Info, "socket (%d) bound to port (%d).", m_Socket, m_Port);

    // Listen on the port for new connections
    s_Ret = klisten_t(m_Socket, 1, s_MainThread);
    if (s_Ret < 0)
    {
        WriteLog(LL_Error, "could not listen on socket (%d).", s_Ret);
        kshutdown_t(m_Socket, SHUT_RDWR, s_MainThread);
        kclose_t(m_Socket, s_MainThread);
        m_Socket = -1;
        return false;
    }

    // Create the new server processing thread, 8MiB stack
    s_Ret = kthread_add(PS4Load::ServerThread, this, Mira::Framework::GetFramework()->GetInitParams()->process, reinterpret_cast<thread**>(&m_Thread), 0, 200, "LogServer");
    WriteLog(LL_Debug, "logserver kthread_add returned (%d).", s_Ret);

    return s_Ret == 0;
}

bool PS4Load::Teardown()
{
    auto s_MainThread = Mira::Framework::GetFramework()->GetMainThread();
    if (s_MainThread == nullptr)
    {
        WriteLog(LL_Error, "could not get main thread");
        return false;
    }

    // Set that we no longer want to run this server instance
    m_Running = false;
    WriteLog(LL_Debug, "we are no longer running...");

    // Close the server socket
    if (m_Socket > 0)
    {
        kshutdown_t(m_Socket, SHUT_RDWR, s_MainThread);
        kclose_t(m_Socket, s_MainThread);
        m_Socket = -1;
    }

    WriteLog(LL_Debug, "socket is killed");

    return true;
}
#pragma endregion

#pragma region Server Thread
void PS4Load::ServerThread(void* p_UserArgs)
{
    auto kthread_exit = (void(*)(void))kdlsym(kthread_exit);

    auto s_MainThread = Mira::Framework::GetFramework()->GetMainThread();
    if (s_MainThread == nullptr)
    {
        WriteLog(LL_Error, "no main thread");
        kthread_exit();
        return;
    }

    //auto _mtx_lock_flags = (void(*)(struct mtx *m, int opts, const char *file, int line))kdlsym(_mtx_lock_flags);
    //auto _mtx_unlock_flags = (void(*)(struct mtx *m, int opts, const char *file, int line))kdlsym(_mtx_unlock_flags);

    // Check for invalid usage
    PS4Load* s_PS4Load = static_cast<PS4Load*>(p_UserArgs);
    if (s_PS4Load == nullptr)
    {
        WriteLog(LL_Error, "invalid usage, userargs are null.");
        kthread_exit();
        return;
    }

    // Set our running state
    s_PS4Load->m_Running = true;

    // Create our timeout
    struct timeval s_Timeout
    {
        .tv_sec = 3,
        .tv_usec = 0
    };

    int s_ClientSocket = -1;
    struct sockaddr_in s_ClientAddress = { 0 };
    size_t s_ClientAddressLen = sizeof(s_ClientAddress);
    memset(&s_ClientAddress, 0, s_ClientAddressLen);
    s_ClientAddress.sin_len = s_ClientAddressLen;
    char s_Buffer[2] = { 0 };

    WriteLog(LL_Info, "Opening %s", s_PS4Load->m_Device);
    auto s_LogDevice = kopen_t(s_PS4Load->m_Device, 0x00, 0, s_MainThread);
    if (s_LogDevice < 0)
    {
        WriteLog(LL_Error, "could not open %s for reading (%d).", s_PS4Load->m_Device, s_LogDevice);
        kclose_t(s_ClientSocket, s_MainThread);
        goto cleanup;
    }

    WriteLog(LL_Error, "here");
    // Loop and try to accept a client
    while ((s_ClientSocket = kaccept_t(s_PS4Load->m_Socket, reinterpret_cast<struct sockaddr*>(&s_ClientAddress), &s_ClientAddressLen, s_MainThread)) > 0)
    {
        WriteLog(LL_Error, "here");
        if (!s_PS4Load->m_Running)
            break;

        WriteLog(LL_Error, "here");
        // SO_LINGER
        s_Timeout.tv_sec = 0;
        auto result = ksetsockopt_t(s_ClientSocket, SOL_SOCKET, SO_LINGER, (caddr_t)&s_Timeout, sizeof(s_Timeout), s_MainThread);
        if (result < 0)
        {
            WriteLog(LL_Error, "could not set send timeout (%d).", result);
            kshutdown_t(s_ClientSocket, SHUT_RDWR, s_MainThread);
            kclose_t(s_ClientSocket, s_MainThread);
            continue;
        }

        WriteLog(LL_Error, "here");

        uint32_t l_Addr = (uint32_t)s_ClientAddress.sin_addr.s_addr;

        WriteLog(LL_Debug, "got new log connection (%d) from IP (%03d.%03d.%03d.%03d).", s_ClientSocket, 
            (l_Addr & 0xFF),
            (l_Addr >> 8) & 0xFF,
            (l_Addr >> 16) & 0xFF,
            (l_Addr >> 24) & 0xFF);

        // Loop reading the data from the klog
        auto bytesRead = 0;
        while ((bytesRead = kread_t(s_LogDevice, s_Buffer, 1, s_MainThread)) > 0)
        {
            if (kwrite_t(s_ClientSocket, s_Buffer, 1, s_MainThread) <= 0)
                break;

            memset(s_Buffer, 0, sizeof(s_Buffer));
        }

        WriteLog(LL_Error, "here");

        WriteLog(LL_Debug, "log connection (%d) disconnected from IP (%03d.%03d.%03d.%03d).", s_ClientSocket, 
            (l_Addr & 0xFF),
            (l_Addr >> 8) & 0xFF,
            (l_Addr >> 16) & 0xFF,
            (l_Addr >> 24) & 0xFF);
        
        // Close down the client socket that was created
        kshutdown_t(s_ClientSocket, SHUT_RDWR, s_MainThread);
        kclose_t(s_ClientSocket, s_MainThread);

        WriteLog(LL_Error, "here");
    }

cleanup:
    // Disconnects all clients, set running to false
    WriteLog(LL_Debug, "logserver tearing down");
    s_PS4Load->Teardown();

    WriteLog(LL_Debug, "logserver exiting cleanly");
    kthread_exit();
}

#endregion

#pragma region ZIP/zlib
/* Decompress from source data to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int inflate_data(int source, uint32_t filesize, FILE *dest)
{
    int ret;
    uint32_t have = 0;
    char strr[50];
    z_stream strm;
    uint8_t src[CHUNK];
    uint8_t out[CHUNK];

    /* allocate inflate state */
    memset(&strm, 0, sizeof(z_stream));
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    do {
        strm.avail_in = MIN(CHUNK, filesize);
        ret = read(source, src, strm.avail_in);
        if(ret < 0) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }

        strm.avail_in = ret;
        if(strm.avail_in == 0) break;
        strm.next_in = src;
        filesize -= ret;

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);

            switch(ret)
            {
                case Z_STREAM_ERROR:
                case Z_NEED_DICT:
                    ret = Z_DATA_ERROR;     /* and fall through */
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    (void)inflateEnd(&strm);
                    return ret; 
            }

            have = CHUNK - strm.avail_out;
            sprintf(strr, "%s %d\n", "abc", have);
            if (fwrite(out, 1, have, dest) != have) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while(strm.avail_out == 0);

    } while(ret != Z_STREAM_END && filesize);

    (void)inflateEnd(&strm);
    return (ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR);
}

int dump_data(int source, uint32_t filesize, FILE *dest)
{
    uint8_t data[CHUNK];
    uint32_t count, pkz = PKZIP;

    while (filesize > 0)
    {
        count = MIN(CHUNK, filesize);
        int ret = read(source, data, count);
        if (ret < 0)
            return Z_DATA_ERROR;

        if (pkz == PKZIP)
            pkz = memcmp(data, &pkz, 4);

        fwrite(data, ret, 1, dest);
        filesize -= ret;
    }

    return (pkz ? Z_OK : PKZIP);
}
#pragma endregion