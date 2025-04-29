#include "tcp_server.h"

#include <stdexcept>
#include <cstdio> // perror
#include <cstdarg>
#include <memory> // make_unique
#include <thread>
#include <csignal>
#include <arpa/inet.h> // for inet_ntoa
#include <sys/select.h>

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
      // Set up select() for timeout
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(fd, &rfds);

      struct timeval tv;
      tv.tv_sec = 10; // 10 second timeout
      tv.tv_usec = 0;

      int retval = select(fd + 1, &rfds, NULL, NULL, &tv);
      if (retval == -1) {
        perror("select()");
        return 0;
      } else if (retval == 0) {
        // Timeout
        return 0;
      }

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

    // Log connection info
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_address.sin_addr), ip, INET_ADDRSTRLEN);
    int port = ntohs(client_address.sin_port);
    DbgTrace("Accepted connection: fd=%d from %s:%d\n", clientSocket, ip, port);

    auto t = thread(clientThread, clientSocket);
    t.detach();
  }

  DbgTrace("Server closed\n");
}

void DbgTrace(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

