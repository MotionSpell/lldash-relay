#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <cstring> // memcpy
#include <sstream>
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "tcp_server.h"

using namespace std;

///////////////////////////////////////////////////////////////////////////////
// http_client.cpp

struct HttpRequest
{
  string method; // e.g: PUT, POST, GET
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
  ss >> r.method;
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

// A growing in-memory file, concurrently writeable and readable.
// Read operations that go beyond the currently available data will block,
// until more data becomes available or the end of file is signaled
// by the producer.
struct Resource
{
  Resource() = default;

  Resource(Resource const &) = delete;
  Resource & operator = (Resource const &) = delete;

  /////////////////////////////////////////////////////////////////////////////
  // producer side
  /////////////////////////////////////////////////////////////////////////////
  void resBegin()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_complete = false;
    m_data.clear();
  }

  void resAppend(const uint8_t* src, size_t len)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto offset = m_data.size();
    m_data.resize(offset + len);
    memcpy(&m_data[offset], src, len);
    m_dataAvailable.notify_all();
  }

  void resEnd()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_complete = true;
    m_dataAvailable.notify_all();
  }

  /////////////////////////////////////////////////////////////////////////////
  // consumer side
  /////////////////////////////////////////////////////////////////////////////

  // pushes the whole resource data to 'sendingFunc', possibly in chunks,
  // and possibly blocking until the resource is completely uploaded.
  void sendWhole(std::function<void(const uint8_t* dst, size_t len)> sendingFunc)
  {
    int sentBytes = 0;

    while(1)
    {
      std::vector<uint8_t> toSend;

      {
        std::unique_lock<std::mutex> lock(m_mutex);

        while(sentBytes == m_data.size() && !m_complete)
          m_dataAvailable.wait(lock);

        if(m_complete && sentBytes == m_data.size())
          break;

        toSend.assign(m_data.data() + sentBytes, m_data.data() + m_data.size());
      }

      sendingFunc((const uint8_t*)toSend.data(), (int)toSend.size());
      sentBytes += toSend.size();
    }
  }

private:
  std::string m_data;
  std::mutex m_mutex;
  std::condition_variable m_dataAvailable;
  bool m_complete = false;
};

std::mutex g_mutex;
std::map<std::string, std::unique_ptr<Resource>> resources;

Resource* getResource(string url)
{
  std::unique_lock<std::mutex> lock(g_mutex);
  auto i_res = resources.find(url);

  if(i_res == resources.end())
    return nullptr;

  return i_res->second.get();
}

Resource* createResource(string url)
{
  std::unique_lock<std::mutex> lock(g_mutex);
  resources[url] = make_unique<Resource>();
  return resources[url].get();
}

void httpClientThread_GET(HttpRequest req, IStream* s)
{
  DbgTrace("Get '%s'\n", req.url.c_str());

  auto const res = getResource(req.url);

  if(!res)
  {
    writeLine(s, "HTTP/1.1 404 Not Found");
    writeLine(s, "");
    return;
  }

  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "Transfer-Encoding: chunked");
  writeLine(s, "");

  auto onSend = [s] (const uint8_t* buf, int len)
    {
      char sizeLine[256];
      snprintf(sizeLine, sizeof sizeLine, "%X", len);
      writeLine(s, sizeLine);

      s->write(buf, len);
      writeLine(s, "");
    };

  res->sendWhole(onSend);

  // last chunk
  writeLine(s, "0");
  writeLine(s, "");
}

void httpClientThread_PUT(HttpRequest req, IStream* s)
{
  auto const res = createResource(req.url);

  res->resBegin();

  bool needsContinue = false;

  if(req.headers["Transfer-Encoding"] == "chunked")
    needsContinue = true;

  if(req.headers["Expect"] == "100-continue")
    needsContinue = true;

  if(needsContinue)
  {
    writeLine(s, "HTTP/1.1 100 Continue");
    writeLine(s, "");
  }

  if(req.headers["Transfer-Encoding"] == "chunked")
  {
    while(1)
    {
      auto sizeLine = readLine(s);

      if(sizeLine.empty())
        break;

      int size = 0;
      int ret = sscanf(sizeLine.c_str(), "%x", &size);

      if(ret != 1)
        break;

      if(size > 0)
      {
        std::vector<uint8_t> buffer(size);
        s->read(buffer.data(), buffer.size());

        res->resAppend(buffer.data(), buffer.size());
      }

      uint8_t eol[2];
      s->read(eol, sizeof eol);

      if(size == 0)
        break;
    }
  }
  else
  {
    auto size = atoi(req.headers["Content-Length"].c_str());

    if(size > 0)
    {
      std::vector<uint8_t> buffer(size);
      s->read(buffer.data(), buffer.size());

      res->resAppend(buffer.data(), buffer.size());
    }
  }

  res->resEnd();

  DbgTrace("Added '%s'\n", req.url.c_str());

  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "Content-Length: 0");
  writeLine(s, "");
}

void httpClientThread_NotImplemented(IStream* s)
{
  writeLine(s, "HTTP/1.1 500 Not implemented");
  writeLine(s, "Content-Length: 0");
  writeLine(s, "");
}

void httpClientThread(IStream* s)
{
  auto req = parseRequest(s);

  if(0)
  {
    DbgTrace("[Request] '%s' '%s' '%s'\n", req.method.c_str(), req.url.c_str(), req.version.c_str());

    for(auto& hdr : req.headers)
      DbgTrace("[Header] '%s' '%s'\n", hdr.first.c_str(), hdr.second.c_str());
  }

  if(req.method == "GET")
    httpClientThread_GET(req, s);
  else if(req.method == "PUT" || req.method == "POST")
    httpClientThread_PUT(req, s);
  else
  {
    DbgTrace("Method not implemented: '%s'\n", req.method.c_str());
    httpClientThread_NotImplemented(s);
  }
}

///////////////////////////////////////////////////////////////////////////////
// TLS wrapper: adds encryption layer, and forwards to httpClientThread (above)

#include <openssl/ssl.h>
#include <openssl/err.h>

// Allows OpenSSL to talk to a IStream
struct BioAdapter
{
  IStream* tcpStream {};

  // SSL wants to read data
  static int staticRead(BIO* bio, char* buf, int size)
  {
    auto pThis = (BioAdapter*)BIO_get_data(bio);
    return pThis->tcpStream->read((uint8_t*)buf, size);
  }

  // SSL wants to write data
  static int staticWrite(BIO* bio, const char* buf, int size)
  {
    auto pThis = (BioAdapter*)BIO_get_data(bio);
    pThis->tcpStream->write((const uint8_t*)buf, size);
    return size;
  }

  static long staticCtrl(BIO*, int, long, void*)
  {
    return 0;
  }
};

// Allows IStream users to talk to OpenSSL
struct StreamAdapter : IStream
{
  SSL* sslStream;

  // HTTP wants to write data
  void write(const uint8_t* data, size_t len) override
  {
    SSL_write(sslStream, data, len);
  }

  size_t read(uint8_t* data, size_t len) override
  {
    return SSL_read(sslStream, data, len);
  }
};

void httpTlsClientThread(IStream* tcpStream)
{
  auto ctx = std::shared_ptr<SSL_CTX>(SSL_CTX_new(SSLv23_server_method()), &SSL_CTX_free);

  if(!ctx)
  {
    perror("Unable to create SSL context");
    throw runtime_error("Unable to create SSL context");
  }

  SSL_CTX_set_ecdh_auto(ctx.get(), 1);

  // Set certification
  if(SSL_CTX_use_certificate_file(ctx.get(), "cert.pem", SSL_FILETYPE_PEM) <= 0)
  {
    ERR_print_errors_fp(stderr);
    throw runtime_error("TLS: can't load certificate 'cert.pem'");
  }

  // Set private key
  if(SSL_CTX_use_PrivateKey_file(ctx.get(), "key.pem", SSL_FILETYPE_PEM) <= 0)
  {
    ERR_print_errors_fp(stderr);
    throw runtime_error("TLS: can't load private key 'key.pem'");
  }

  auto ssl = std::shared_ptr<SSL>(SSL_new(ctx.get()), &SSL_free);
  auto biom = std::shared_ptr<BIO_METHOD>(BIO_meth_new(1234, "MyStream"), &BIO_meth_free);

  if(!biom)
  {
    ERR_print_errors_fp(stderr);
    throw runtime_error("TLS: can't create custom BIO method");
  }

  BIO_meth_set_read(biom.get(), &BioAdapter::staticRead);
  BIO_meth_set_write(biom.get(), &BioAdapter::staticWrite);
  BIO_meth_set_ctrl(biom.get(), &BioAdapter::staticCtrl);

  auto bio = BIO_new(biom.get());

  if(!bio)
  {
    ERR_print_errors_fp(stderr);
    throw runtime_error("TLS: can't create new BIO");
  }

  StreamAdapter streamAdapter {};
  streamAdapter.sslStream = ssl.get();

  BioAdapter bioAdapter {};
  bioAdapter.tcpStream = tcpStream;

  BIO_set_data(bio, &bioAdapter);

  SSL_set_bio(ssl.get(), bio, bio);

  if(SSL_accept(ssl.get()) <= 0)
  {
    ERR_print_errors_fp(stderr);
    throw runtime_error("TLS: can't accept connection");
  }

  httpClientThread(&streamAdapter);
}

///////////////////////////////////////////////////////////////////////////////
// main.cpp

struct Config
{
  int port = 9000;
  bool tls = false;
};

Config parseCommandLine(int argc, char const* argv[])
{
  Config cfg {};

  auto pop = [&] () -> string
    {
      if(argc <= 0)
        throw runtime_error("unexpected end of command line");

      string word = argv[0];
      argc--;
      argv++;

      return word;
    };

  pop();

  while(argc > 0)
  {
    auto word = pop();

    if(word == "--port")
      cfg.port = atoi(pop().c_str());
    else if(word == "--tls")
      cfg.tls = true;
    else
      throw runtime_error("invalid command line");
  }

  if(cfg.port <= 0 || cfg.port >= 65536)
    throw runtime_error("Invalid TCP port");

  return cfg;
}

int main(int argc, char const* argv[])
{
  try
  {
    auto cfg = parseCommandLine(argc, argv);

    auto clientFunction = &httpClientThread;

    if(cfg.tls)
      clientFunction = &httpTlsClientThread;

    auto clientFunctionCatcher = [&] (IStream* stream)
      {
        try
        {
          clientFunction(stream);
        }
        catch(std::exception const& e)
        {
          fprintf(stderr, "Error: %s\n", e.what());
        }
      };

    runTcpServer(cfg.port, clientFunctionCatcher);
    return 0;
  }
  catch(exception const& e)
  {
    fprintf(stderr, "Fatal: %s\n", e.what());
    return 1;
  }
}

