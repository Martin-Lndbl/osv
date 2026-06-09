{
  description = "OSv flake";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs?ref=23.11";
    nixpkgs-2211.url = "github:nixos/nixpkgs?ref=22.11";
    nur-niwa.url = "github:Meandres/nur-niwa";
    nur-niwa.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      nur-niwa,
      ...
    }@inputs:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ (import ./overlays.nix { inherit inputs; }) ];
        };
        niwa-pkgs = nur-niwa.packages.${system};
      in
      {
        devShells = {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              ack # grep tool
              ant # java dev lib
              autoconf
              automake
              bash
              binutils
              bisoncpp
              gcc13
              gdb # gnu debugger
              cmake
              gnumake
              gnupatch
              flamegraph # code hierarchy visualization
              libedit
              libgcc # Compiler
              libtool
              libvirt
              lua53Packages.lua
              ncurses
              pax-utils # elf security library
              python3
              python311Packages.requests
              p11-kit # PKCS#11 loader
              qemu_full # hypervisor
              readline # interactive line editing
              unzip
              zulu8 # Java jdk
              clang
              osv-ssl
              osv-ssl-hdr
              yaml-cpp
              xz.out
              krb5.out
              libselinux.out
              libz
              osv-boost
              unixODBC
              numactl
              python311Packages.numpy
              python311Packages.pandas
              python311Packages.matplotlib
              virtiofsd
              just
              flex
              bison
              ninja
              tbb
              dpdk
              snappy
              zstd
              zlib
              bzip2
              curl
              glog
              lz4
              openssl
              niwa-pkgs.driverctl
            ];

            buildInputs = with pkgs; [
              osv-boost # C++ libraries
              readline # interactive line editing
              libaio # I/O library
              osv-ssl # SSL/TLS library
              clang-tools # language server
            ];

            LD_LIBRARY_PATH = "${pkgs.readline}/lib";
            LUA_LIB_PATH = "${pkgs.lua53Packages.lua}/lib";
            GOMP_DIR = pkgs.libgcc.out;
            STATIC_LIBC = pkgs.glibc.static;
            boost_base = "${pkgs.osv-boost}";
            BOOST_SO_DIR = "${pkgs.osv-boost}/lib";
            OPENSSL_DIR = "${pkgs.osv-ssl}";
            OPENSSL_HDR = "${pkgs.osv-ssl-hdr}/include";
            KRB5_DIR = "${pkgs.krb5.out}";
            XZ_DIR = "${pkgs.xz.out}";
            LIBZ_DIR = "${pkgs.libz}";
            LIBSELINUX_DIR = "${pkgs.libselinux.out}";
            DPDK_DIR = "${pkgs.dpdk}";
          };

          minimal = pkgs.mkShell {
            buildInputs = with pkgs; [
              ack # grep tool
              autoconf
              automake
              bash
              binutils
              bisoncpp
              bison
              clang-tools # language server
              cmake
              gdb
              gnumake
              gnupatch
              libedit
              libtool
              ncurses
              pax-utils # elf security library
              python3
              p11-kit # PKCS#11 loader
              qemu_kvm
              readline # interactive line editing
              unzip
              osv-ssl
              osv-ssl-hdr
              yaml-cpp
              libz
              libaio # I/O library
              pkgsStatic.boost181
              virtiofsd
              just
              flex
              ninja
              tbb
              snappy
              zstd
              zlib
              bzip2
              curl
              glog
              lz4
              niwa-pkgs.driverctl
            ];

            # Required for scripts/loader.py
            GOMP_DIR = pkgs.libgcc.out;

            # Required for OSv kernel build
            boost_base = "${pkgs.pkgsStatic.boost181}";

            # Required for modules/openssl
            OPENSSL_DIR = "${pkgs.osv-ssl}";

            # Required for modules/openssl
            KRB5_DIR = "${pkgs.krb5.out}";

            # Required for modules/openssl
            XZ_DIR = "${pkgs.xz.out}";

            # Required for modules/openssl
            LIBSELINUX_DIR = "${pkgs.libselinux.out}";
          };
        };
      }
    );
}
