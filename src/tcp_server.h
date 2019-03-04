#pragma once

#include <cstddef> // size_t
#include <cstdint>
#include <functional>

struct IStream
{
  virtual ~IStream() = default;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* data, size_t len) = 0;
};

void runTcpServer(int tcpPort, std::function<void(IStream* s)> clientFunc);

void DbgTrace(const char* format, ...);

