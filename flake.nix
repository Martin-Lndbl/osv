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
              # build-essentials
              curl
              flex
              gawk
              gdb
              genromfs
              git
              # gnutls-bin
              gnugrep

              boost # libboost-all-dev
              # libedit-dev
              # libmaven-shade-plugin-java
              # libncurses5-dev
              libtool
              # libyaml-cpp-dev
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

            CAPSTAN_QEMU_PATH = "${pkgs.qemu}/bin/qemu-system-x86_64";
            boost_base = "${pkgs.boost}/lib";

            shellHook = ''
            '';
          };
        }
      );
}
