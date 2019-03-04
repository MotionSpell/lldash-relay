#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <stdexcept>
#include <functional>
#include <thread>
#include <map>
#include <string>
#include <sstream>

using namespace std;

void DbgTrace(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

///////////////////////////////////////////////////////////////////////////////
// i_stream.h

struct IStream
{
  virtual ~IStream() = default;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* data, size_t len) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// http_client.cpp

struct HttpRequest
{
  string action; // e.g: PUT, POST, GET
  string url; // e.g: /toto/dash.mp4
  string version; // e.g: HTTP/1.1

  map<string, string> headers;
};

string readLine(IStream* s)
{
  string r;

  while(1)
  {
    uint8_t buffer[1];
    long ret = s->read(buffer, sizeof buffer);

    if(ret == 0)
      break;

    if(buffer[0] == '\n')
      break;

    r += buffer[0];
  }

  if(!r.empty() && r.back() == '\r')
    r.pop_back();

  return r;
};

void writeLine(IStream* s, const char* l)
{
  string line = l;
  line += "\r\n";
  s->write((const uint8_t*)line.c_str(), line.size());
}

HttpRequest parseRequest(IStream* s)
{
  HttpRequest r;

  string req = readLine(s);

  stringstream ss(req);
  ss >> r.action;
  ss >> std::ws;
  ss >> r.url;
  ss >> std::ws;
  ss >> r.version;

  while(1)
  {
    auto line = readLine(s);

    if(line.empty())
      break;

    string name, value;
    stringstream ss(line);
    ss >> name;

    if(!name.empty() && name.back() == ':')
      name.pop_back();

    ss >> std::ws;
    ss >> value;

    r.headers[name] = value;
  }

  return r;
}

void httpClientThread(IStream* s)
{
  DbgTrace("HttpClientThread\n");

  auto req = parseRequest(s);

  DbgTrace("[Request] '%s' '%s' '%s'\n", req.action.c_str(), req.url.c_str(), req.version.c_str());

  for(auto& hdr : req.headers)
    DbgTrace("[Header] '%s' '%s'\n", hdr.first.c_str(), hdr.second.c_str());

  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "Connection: close");
  writeLine(s, "Content-Length: 0");
  writeLine(s, "");

  DbgTrace("HttpClientThread exited\n");
}

///////////////////////////////////////////////////////////////////////////////
// tcp_server.h

void runTcpServer(int tcpPort, function<void(IStream* s)> clientFunc);

///////////////////////////////////////////////////////////////////////////////
// main.cpp

int main()
{
  try
  {
    runTcpServer(9000, &httpClientThread);
    return 0;
  }
  catch(exception const& e)
  {
    fprintf(stderr, "Fatal: %s\n", e.what());
    return 1;
  }
}

///////////////////////////////////////////////////////////////////////////////
// tcp_server.c

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h> // read, write

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
  assert(sock >= 0);

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
      perror("accept");

    auto t = thread(clientThread, clientSocket);
    t.detach();
  }
}

