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

