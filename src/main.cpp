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
#include <thread> // std::this_thread
#include <chrono> // std::chrono::milliseconds

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
    size_t sentBytes = 0;

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
std::map<std::string, std::shared_ptr<Resource>> resources;

std::shared_ptr<Resource> getResource(string url)
{
  std::unique_lock<std::mutex> lock(g_mutex);
  auto i_res = resources.find(url);

  if(i_res == resources.end())
    return nullptr;

  return i_res->second;
}

bool deleteResource(string url)
{
  size_t wildcardPos = url.find("*");

  if(wildcardPos == string::npos)
  {
    std::unique_lock<std::mutex> lock(g_mutex);
    auto i_res = resources.find(url);

    if(i_res == resources.end())
      return false;

    resources.erase(i_res);
    return true;
  }
  else
  {
    DbgTrace("Found wildcard '*' in '%s'\n", url.c_str());
    std::unique_lock<std::mutex> lock(g_mutex);

    bool res = false;

    auto r = resources.begin();

    while (r != resources.end())
    {
      auto start = r->first.find(url.substr(0, wildcardPos));
      auto end = r->first.find(url.substr(wildcardPos + 1));

      if(start != string::npos && end != string::npos)
        r = resources.erase(r);
      else
        r++;
    }

    return res;
  }
}

std::shared_ptr<Resource> createResource(string url)
{
  std::unique_lock<std::mutex> lock(g_mutex);
  resources[url] = make_shared<Resource>();
  return resources[url];
}

void httpClientThread_GET(HttpRequest req, IStream* s)
{
  DbgTrace("event=request_received method=GET url=%s version=%s\n", req.url.c_str(), req.version.c_str());
  auto res = getResource(req.url);

  // Long polling: wait up to 5 seconds if resource does not exist
  if (!res)
  {
    const int timeout_ms = 5000;
    const int interval_ms = 100;
    int waited = 0;
    while (waited < timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
      waited += interval_ms;
      res = getResource(req.url);
      if (res) {
        DbgTrace("event=resource_appeared url=%s waited_ms=%d\n", req.url.c_str(), waited);
        break;
      }
    }
  }

  if (!res)
  {
    DbgTrace("event=error_reply method=GET url=%s status=404 reason=not_found\n", req.url.c_str());
    writeLine(s, "HTTP/1.1 404 Not Found");
    writeLine(s, "");
    return;
  }

  DbgTrace("event=resource_served url=%s\n", req.url.c_str());
  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "Transfer-Encoding: chunked");
  writeLine(s, "");

  auto onSend = [s, req](const uint8_t* buf, int len)
  {
    char sizeLine[256];
    snprintf(sizeLine, sizeof sizeLine, "%X", len);
    writeLine(s, sizeLine);

    s->write(buf, len);
    writeLine(s, "");
    DbgTrace("event=chunk_sent url=%s chunk_size=%d\n", req.url.c_str(), len);
  };

  res->sendWhole(onSend);

  // last chunk
  writeLine(s, "0");
  writeLine(s, "");
  DbgTrace("event=request_completed method=GET url=%s status=200\n", req.url.c_str());
}

void httpClientThread_DELETE(HttpRequest req, IStream* s)
{
  DbgTrace("event=request_received method=DELETE url=%s\n", req.url.c_str());

  auto const res = deleteResource(req.url);

  if(!res)
  {
    DbgTrace("event=error_reply method=DELETE url=%s status=404 reason=not_found\n", req.url.c_str());
    writeLine(s, "HTTP/1.1 404 Not Found");
    writeLine(s, "");
    return;
  }

  DbgTrace("event=resource_deleted url=%s\n", req.url.c_str());
  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "");
  DbgTrace("event=request_completed method=DELETE url=%s status=200\n", req.url.c_str());
}

void httpClientThread_PUT(HttpRequest req, IStream* s)
{
  DbgTrace("event=request_received method=PUT url=%s\n", req.url.c_str());
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
        DbgTrace("event=resource_chunk_received url=%s chunk_size=%d\n", req.url.c_str(), size);
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
      DbgTrace("event=resource_chunk_received url=%s chunk_size=%d\n", req.url.c_str(), size);
      res->resAppend(buffer.data(), buffer.size());
    }
  }

  res->resEnd();

  DbgTrace("event=resource_created url=%s\n", req.url.c_str());
  writeLine(s, "HTTP/1.1 200 OK");
  writeLine(s, "Content-Length: 0");
  writeLine(s, "");
  DbgTrace("event=request_completed method=PUT url=%s status=200\n", req.url.c_str());
}

void httpClientThread_NotImplemented(IStream* s, const std::string& method)
{
  DbgTrace("event=error_reply method=%s status=500 reason=not_implemented\n", method.c_str());
  writeLine(s, "HTTP/1.1 500 Not implemented");
  writeLine(s, "Content-Length: 0");
  writeLine(s, "");
}

void httpMain(IStream* s)
{
  auto req = parseRequest(s);

  if(req.method == "GET")
    httpClientThread_GET(req, s);
  else if(req.method == "DELETE")
    httpClientThread_DELETE(req, s);
  else if(req.method == "PUT" || req.method == "POST")
    httpClientThread_PUT(req, s);
  else
  {
    httpClientThread_NotImplemented(s, req.method);
  }
}

///////////////////////////////////////////////////////////////////////////////
// main.cpp

extern void tlsMain(IStream* tcpStream);

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

#ifndef VERSION
#define VERSION "0"
#endif

    DbgTrace("event=server_start port=%d version=%s\n", cfg.port, VERSION);

    auto clientFunction = &httpMain;

    if(cfg.tls)
      clientFunction = &tlsMain;

    auto clientFunctionCatcher = [&] (std::unique_ptr<IStream> stream)
      {
        try
        {
          clientFunction(stream.get());
        }
        catch(std::exception const& e)
        {
          DbgTrace("event=connection_error error=%s\n", e.what());
        }
        DbgTrace("event=connection_closed reason=client_closed\n");
      };

    runTcpServer(cfg.port, clientFunctionCatcher);
    DbgTrace("event=server_closed\n");
    return 0;
  }
  catch(exception const& e)
  {
    DbgTrace("event=server_fatal error=%s\n", e.what());
    return 1;
  }
}

