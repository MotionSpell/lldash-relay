///////////////////////////////////////////////////////////////////////////////
// TLS wrapper: adds encryption layer, and forwards to httpMain (above)

#include "tcp_server.h" // IStream
#include <memory>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

extern void httpMain(IStream* s);

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

  static long staticCtrl(BIO*, int cmd, long, void*)
  {
    switch(cmd)
    {
    case BIO_CTRL_FLUSH:
      return 1;

    case BIO_CTRL_PUSH:
    case BIO_CTRL_POP:
      return -1;

    default:
      fprintf(stderr, "BioAdapter: unhandled BIO_CTRL: %d\n", cmd);
      return 0;
    }
  }
};

// Allows IStream users to talk to OpenSSL
struct StreamAdapter : IStream
{
  SSL* sslStream;

  // HTTP wants to write data
  void write(const uint8_t* data, size_t len) override
  {
    auto remaining = (int)len;

    while(remaining > 0)
    {
      auto writtenBytes = SSL_write(sslStream, data, remaining);

      if(writtenBytes < 0)
        throw runtime_error("SSL write error");

      remaining -= writtenBytes;
      data += writtenBytes;
    }
  }

  // HTTP wants to read data
  size_t read(uint8_t* data, size_t len) override
  {
    auto remaining = (int)len;

    while(remaining > 0)
    {
      auto readBytes = SSL_read(sslStream, data, remaining);

      if(readBytes < 0)
        throw runtime_error("SSL read error");

      remaining -= readBytes;
      data += readBytes;
    }

    return len;
  }
};

void tlsMain(IStream* tcpStream)
{
  auto ctx = std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), &SSL_CTX_free);

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
  BIO_set_init(bio, 1);

  SSL_set_bio(ssl.get(), bio, bio);

  int ret = SSL_accept(ssl.get());

  if(ret <= 0)
  {
    ERR_print_errors_fp(stderr);
    fprintf(stderr, "SSL_accept failed: %d '%d'\n",
            ret,
            (SSL_get_error(ssl.get(), ret))
            );

    throw runtime_error("TLS: can't accept connection");
  }

  httpMain(&streamAdapter);
}

