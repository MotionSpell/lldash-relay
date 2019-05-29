#include "tcp_server.h"

#include <stdexcept>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <csignal>

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

void runTcpServer(int tcpPort, function<void(IStream*)> clientFunc)
{
  struct SocketStream : IStream
  {
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

    ~SocketStream()
    {
      closesocket(clientSocket);
    }

    SOCKET fd;
  };

  auto clientThread = [clientFunc] (int clientSocket) {
      SocketStream s;
      s.fd = clientSocket;
      clientFunc(&s);
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
      if(g_socket == -1)
        break; // exit thread

      fprintf(stderr, "accept() last error: %d\n", WSAGetLastError());
    }

    auto t = thread(clientThread, clientSocket);
    t.detach();
  }

  WSACleanup();

  DbgTrace("Server closed\n");
}

void DbgTrace(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

