# Evanescent : a low-latency HTTP server

Usage: ```LD_LIBRARY_PATH=$DIR $DIR/evanescent.exe [--tls] [--port port_number]```

If you use TLS don't forget to generate your own certificates for each server instance:

```
openssl req -newkey rsa:2048 -new -nodes -x509 -days 3650 -keyout key.pem -out cert.pem
```

When deleting resources you can use a wildcard:
```curl -X DELETE http://127.0.0.1:9000/aaaa*bbbb```


# Dependencies

## Build dependencies

```
$ sudo apt install libssl-dev
```

## Code formatter (optional)

Install [uncrustify](https://github.com/uncrustify/uncrustify)

```
$ git clone https://github.com/uncrustify/uncrustify.git
$ cd uncrustify
$ git checkout uncrustify-0.64
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make -j $(nproc)
$ sudo make install
```

# Running

Usage examples:

```sh
$ evanescent --port 10333
$ evanescent --tls --port 10777
$ evanescent --long-poll 5000 # accepts client connections on non-existing resources, value in ms. 
$ evanescent --help
```

# Building

## Native build

```
$ make
```

## Cross-compiling

Simply override CXX and PKG_CONFIG_LIBDIR to use your target toolchain:

```
$ export PKG_CONFIG_LIBDIR=/my_w32_sysroot
$ export CXX=x86_64-w64-mingw32-g++
$ make
```

Beware: if you don't override PKG_CONFIG_LIBDIR, the build will use your system
headers, which will lead to strange compilation errors.

# Testing

```
./check
```

# Computing coverage

```
./scripts/cov.sh
```

# Useful links

 - HTTP 1.1: https://tools.ietf.org/html/rfc7231
 - OpenSSL manual: https://www.openssl.org/docs/manmaster/man3/
