{ inputs, ... }:

final: _prev: {
  capstan = _prev.callPackage ./pkgs/capstan.nix { };
  osv-boost = _prev.boost181.override { enableStatic = true; };
  osv-ssl = inputs.nixpkgs-2211.legacyPackages.${_prev.system}.openssl_1_1.out;
  osv-ssl-hdr = inputs.nixpkgs-2211.legacyPackages.${_prev.system}.openssl_1_1.dev;
  tlx = _prev.stdenv.mkDerivation rec {
    pname = "tlx";
    version = "0.6.1";

    src = _prev.fetchFromGitHub {
      owner = "tlx";
      repo = "tlx";
      rev = "v${version}";
      hash = "sha256-qH6gHPX1GdxMKbsrKeJpKOwRdPHVjPZGIWzcOX8HxwA=";
    };

    nativeBuildInputs = with _prev; [ cmake ];

    cmakeFlags = [
      "-DCMAKE_INSTALL_LIBDIR=lib"
      "-DCMAKE_INSTALL_INCLUDEDIR=include"
      "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    ];
  };
}
