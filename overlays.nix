{ inputs, ... }:

final: _prev: {
  capstan = _prev.callPackage ./pkgs/capstan.nix { };
  boost = _prev.boost.override { enableStatic = true; };
}
