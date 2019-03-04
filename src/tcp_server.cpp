#include "tcp_server.h"

#include <stdexcept>
#include <cstdio> // perror
#include <cstdarg>
#include <thread>
#include <csignal>

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
  close(socket); // unblock call to 'accept' below
}

void runTcpServer(int tcpPort, function<void(IStream*)> clientFunc)
{
  auto clientThread = [clientFunc] (int clientSocket) {
      struct FileStream : IStream
      {
        void write(const uint8_t* data, size_t len) override
        {
          ::send(fd, data, len, MSG_NOSIGNAL);
        }

        size_t read(uint8_t* data, size_t len) override
        {
          return ::recv(fd, data, len, MSG_NOSIGNAL);
        }

        int fd;
      };

      FileStream s;
      s.fd = clientSocket;
      clientFunc(&s);
      close(clientSocket);
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

    int ret = bind(sock, (struct sockaddr*)&serverAddress, sizeof serverAddress);

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
}

void DbgTrace(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

