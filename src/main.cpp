#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <sstream>

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

struct Resource
{
  Resource() = default;

  Resource(Resource const &) = delete;
  Resource & operator = (Resource const &) = delete;

  std::string data;
};

std::map<std::string, Resource> resources;

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
  auto& data = i_res->second.data;
  char buffer[256];
  snprintf(buffer, sizeof buffer, "Content-Length: %d", (int)data.size());
  writeLine(s, buffer);
  writeLine(s, "");
  s->write((uint8_t*)data.data(), data.size());
}

void httpClientThread_PUT(HttpRequest req, IStream* s)
{
  auto& data = resources[req.url].data;

  if(req.headers["Transfer-Encoding"] == "chunked")
  {
    writeLine(s, "HTTP/1.1 100 Continue");
    writeLine(s, "");

    data.clear();

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
        auto offset = data.size();
        data.resize(offset + size);
        s->read((uint8_t*)&data[offset], size);
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

    data.resize(size);

    if(size)
      s->read((uint8_t*)data.data(), size);
  }

  DbgTrace("Added '%s' (%d bytes)\n", req.url.c_str(), (int)data.size());

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

