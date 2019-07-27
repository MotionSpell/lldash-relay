#!/bin/sh
set -eu

params=()
for p in "$@" ; do
  case $p in
    -Wl,--no-undefined)
        params+=("-undefined")
        params+=("error")
      ;;
    -static-libstdc++)
      # avoid warning from clang, which ignores this option
      ;;
    -Wl,--version-script=*)
      # BSD ld doesn't support version scripts, export everything
      ;;
    *)
      params+=("$p")
      ;;
  esac
done

ccache "/usr/bin/g++" "${params[@]}"
