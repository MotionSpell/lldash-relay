#include "tcp_server.h"

#include <stdexcept>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <csignal>
#include <mutex>
#include <chrono>
#include <ctime>

using namespace std;

// OS-specific
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

static SOCKET g_socket;
static void sigIntHandler(int)
{
  auto socket = g_socket;
  g_socket = -1;
  closesocket(socket); // unblock call to 'accept' below
}

void runTcpServer(int tcpPort, int long_poll_timeout_ms, std::function<void(std::unique_ptr<IStream> s)> clientFunc)
{
  struct SocketStream : IStream
  {
    SocketStream(SOCKET fd_, int long_poll_timeout_ms) : IStream(long_poll_timeout_ms), fd(fd_)
    {
    }

    ~SocketStream()
    {
      closesocket(fd);
    }

    void write(const uint8_t* data, size_t len) override
    {
      auto res = ::send(fd, (const char*)data, len, 0);

      if(res < 0)
      {
        fprintf(stderr, "send() last error: %d\n", WSAGetLastError());
        throw runtime_error("socket error on send()");
      }
    }

    size_t read(uint8_t* data, size_t len) override
    {
      auto res = ::recv(fd, (char*)data, len, MSG_WAITALL);

      if(res < 0)
      {
        fprintf(stderr, "recv() error: %d\n", WSAGetLastError());
        throw runtime_error("socket error on recv()");
      }

      return res;
    }

    const SOCKET fd;
  };

  auto clientThread = [clientFunc, long_poll_timeout_ms] (int clientSocket) {
      auto s = make_unique<SocketStream>(clientSocket, long_poll_timeout_ms);
      clientFunc(move(s));
    };

  WSADATA Data;

  if(WSAStartup(0x0202, &Data) != 0)
    throw runtime_error("can't initialize Winsock 2");

  const int sock = socket(AF_INET, SOCK_STREAM, 0);

  if(sock < 0)
  {
    fprintf(stderr, "socket() last error: %d\n", WSAGetLastError());
    throw runtime_error("can't create socket");
  }

  {
    g_socket = sock;
    std::signal(SIGINT, sigIntHandler);
  }

  {
    char one = 1;
    int ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    if(ret < 0)
    {
      fprintf(stderr, "setsockopt() last error: %d\n", WSAGetLastError());
      throw runtime_error("Can't setsockopt");
    }
  }

  {
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(tcpPort);

    int ret = ::bind(sock, (struct sockaddr*)&serverAddress, sizeof serverAddress);

    if(ret < 0)
    {
      fprintf(stderr, "bind() last error: %d\n", WSAGetLastError());
      throw runtime_error("Can't bind");
    }
  }

  {
    int ret = listen(sock, 64);

    if(ret < 0)
    {
      fprintf(stderr, "listen() last error: %d\n", WSAGetLastError());
      throw runtime_error("Can't listen");
    }
  }

  DbgTrace("Server listening on: %d\n", tcpPort);

  while(1)
  {
    sockaddr_in client_address;
    int address_len = sizeof(client_address);

    int clientSocket = accept(sock, (sockaddr*)&client_address, &address_len);

    if(clientSocket < 0)
    {
      if(g_socket == (SOCKET)-1)
        break; // exit thread

      fprintf(stderr, "accept() last error: %d\n", WSAGetLastError());
    }

    auto t = thread(clientThread, clientSocket);
    t.detach();
  }

  WSACleanup();

  DbgTrace("Server closed\n");
}

static std::mutex g_debugTraceMutex;

void DbgTrace(const char* format, ...)
{
    std::unique_lock<std::mutex> lock(g_debugTraceMutex);

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &t);

    // Print timestamp
    fprintf(stderr, "lldash-relay: t=%04d-%02d-%02dT%02d:%02d:%02d.%03lld ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ms.count()));

    // Print log message
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

