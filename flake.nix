{
  description = "OSv flake";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs?ref=26.05";
    nixpkgs-2311.url = "github:nixos/nixpkgs?ref=23.11";
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
        devShells = rec {
          default = common;

          minimal = pkgs.mkShell {
            buildInputs = with pkgs; [
              bash
              bison
              cmake
              flex
              just
              ncurses
              niwa-pkgs.driverctl
              python3
              pkgsStatic.boost181
              qemu_kvm
            ];

            # Required for OSv kernel build
            boost_base = "${pkgs.pkgsStatic.boost181}";
          };

          common = pkgs.mkShell {
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
              qemu_full
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
              tlx
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
