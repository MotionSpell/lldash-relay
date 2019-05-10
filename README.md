# Evanescent : a low-latency HTTP server

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

# Usefull links

 - HTTP 1.1: https://tools.ietf.org/html/rfc7231
 - OpenSSL manual: https://www.openssl.org/docs/manmaster/man3/
