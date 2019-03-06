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
  void clear()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_complete = false;
    m_data.clear();
  }

  void append(const uint8_t* src, size_t len)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto offset = m_data.size();
    m_data.resize(offset + len);
    memcpy(&m_data[offset], src, len);
    m_dataAvailable.notify_all();
  }

  void conclude()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_complete = true;
    m_dataAvailable.notify_all();
  }

  size_t size() const
  {
    // Don't take the mutex: this cannot be made thread-safe anyway
    // (in a concurrent context, the returned value would always be invalid).
    return m_data.size();
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

std::map<std::string, std::unique_ptr<Resource>> resources;

void httpClientThread_GET(HttpRequest req, IStream* s)
{
  DbgTrace("Get '%s'\n", req.url.c_str());
  auto i_res = resources.find(req.url);

  if(i_res == resources.end())
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

  auto& res = i_res->second;
  res->sendWhole(onSend);

  // last chunk
  writeLine(s, "0");
  writeLine(s, "");
}

void httpClientThread_PUT(HttpRequest req, IStream* s)
{
  resources[req.url] = make_unique<Resource>();

  auto& res = resources[req.url];

  if(req.headers["Transfer-Encoding"] == "chunked")
  {
    writeLine(s, "HTTP/1.1 100 Continue");
    writeLine(s, "");

    res->clear();

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

        res->append(buffer.data(), buffer.size());
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

      res->append(buffer.data(), buffer.size());
    }
  }

  res->conclude();

  DbgTrace("Added '%s' (%d bytes)\n", req.url.c_str(), (int)res->size());

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
    DbgTrace("[Request] '%s' '%s' '%s'\n", req.action.c_str(), req.url.c_str(), req.version.c_str());

    for(auto& hdr : req.headers)
      DbgTrace("[Header] '%s' '%s'\n", hdr.first.c_str(), hdr.second.c_str());
  }

  if(req.action == "GET")
    httpClientThread_GET(req, s);
  else if(req.action == "PUT" || req.action == "POST")
    httpClientThread_PUT(req, s);
  else
    httpClientThread_NotImplemented(s);
}

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

