#pragma once

#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <memory>

struct IStream
{
  IStream(int long_poll_timeout_ms_) : long_poll_timeout_ms(long_poll_timeout_ms_) {}
  virtual ~IStream() = default;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* data, size_t len) = 0;

  int long_poll_timeout_ms;
};

void runTcpServer(int tcpPort, int long_poll_timeout_ms, std::function<void(std::unique_ptr<IStream> s)> clientFunc);

void DbgTrace(const char* format, ...);

