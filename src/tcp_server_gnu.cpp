#include "tcp_server.h"

#include <stdexcept>
#include <cstdio> // perror
#include <cstdarg>
#include <memory> // make_unique
#include <thread>
#include <csignal>
#include <chrono>
#include <ctime>

using namespace std;

// OS-specific
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h> // close

static int g_socket;
static void sigIntHandler(int)
{
  auto socket = g_socket;
  g_socket = -1;
  shutdown(socket, SHUT_RD); // unblock call to 'accept' below
  close(socket);
}

void runTcpServer(int tcpPort, std::function<void(std::unique_ptr<IStream> s)> clientFunc)
{
  struct SocketStream : IStream
  {
    SocketStream(int fd_) : fd(fd_)
    {
    }

    ~SocketStream()
    {
      close(fd);
    }

    void write(const uint8_t* data, size_t len) override
    {
      int flags = MSG_WAITALL;
#ifdef MSG_NOSIGNAL
      flags |= MSG_NOSIGNAL;
#endif
      ::send(fd, data, len, flags);
    }

    size_t read(uint8_t* data, size_t len) override
    {
      int flags = MSG_WAITALL;
#ifdef MSG_NOSIGNAL
      flags |= MSG_NOSIGNAL;
#endif
      return ::recv(fd, data, len, flags);
    }

    const int fd;
  };

  auto clientThread = [clientFunc] (int clientSocket) {
      auto s = make_unique<SocketStream>(clientSocket);
      clientFunc(std::move(s));
    };

  const int sock = socket(AF_INET, SOCK_STREAM, 0);

  if(sock < 0)
  {
    perror("socket");
    throw runtime_error("can't create socket");
  }

  {
    g_socket = sock;
    std::signal(SIGINT, sigIntHandler);
  }

  {
    int one = 1;
    int ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);

    if(ret < 0)
    {
      perror("setsockopt");
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
      perror("bind");
      throw runtime_error("Can't bind");
    }
  }

  {
    int ret = listen(sock, 64);

    if(ret < 0)
    {
      perror("listen");
      throw runtime_error("Can't listen");
    }
  }

  DbgTrace("Server listening on: %d\n", tcpPort);

  while(1)
  {
    sockaddr_in client_address;
    socklen_t address_len = sizeof(client_address);

    int clientSocket = accept(sock, (sockaddr*)&client_address, &address_len);

    if(clientSocket < 0)
    {
      if(g_socket == -1)
        break; // exit thread

      perror("accept");
    }

    auto t = thread(clientThread, clientSocket);
    t.detach();
  }

  DbgTrace("Server closed\n");
}

void DbgTrace(const char* format, ...)
{
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_r(&t, &tm);

    // Print timestamp
    fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d.%03lld ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count());

    // Print log message
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

