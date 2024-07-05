{
  description = "OSv flake";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs, flake-utils, }@inputs:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = import nixpkgs {
            inherit system;
            overlays = [ (import ./overlays.nix { inherit inputs; }) ];
          };
        in
        {
          devShell = pkgs.mkShell {
            buildInputs = with pkgs; [
              ant
              autoconf
              automake
              binutils
              bison
              curl
              flex
              gawk
              gdb
              genromfs
              git
              gnugrep
              osv-boost
              libtool
              cmake
              jdk8
              maven
              openssl
              p11-kit
              python312Packages.dpkt
              python312Packages.requests
              qemu
              qemu-utils
              tcpdump
              unzip
              wget
            ];


            LD_LIBRARY_PATH = "${pkgs.readline}/lib";
            LUA_LIB_PATH = "${pkgs.lua53Packages.lua}/lib";
            GOMP_DIR = pkgs.libgcc.lib;

            CAPSTAN_QEMU_PATH = "${pkgs.qemu}/bin/qemu-system-x86_64";

            shellHook = ''
              /bin/bash --version >/dev/null 2>&1 || {
                echo >&2 "Error: /bin/bash is required but was not found.  Aborting."
                echo >&2 "If you're using NixOs, consider using https://github.com/Mic92/envfs."
                exit 1
                }

              mkdir $TMP/openssl-all
              ln -rsf ${pkgs.openssl}/* $TMP/openssl-all
              ln -rsf ${pkgs.openssl.dev}/* $TMP/openssl-all
              ln -rsf ${pkgs.openssl.out}/* $TMP/openssl-all
              export OPENSSL_DIR="$TMP/openssl-all";
              export OPENSSL_LIB_PATH="$TMP/openssl-all/lib";

              mkdir $TMP/libboost
              ln -s ${pkgs.osv-boost}/lib/* $TMP/libboost/
              for file in $TMP/libboost/*-x64*; do mv "$file" "''${file//-x64/}"; done
              export boost_base="$TMP/libboost"
            '';
          };
        }
      );
}
