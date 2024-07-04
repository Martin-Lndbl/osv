{ buildGoModule
, fetchFromGitHub
, musl
, glibc
}:

buildGoModule {
  pname = "capstan";
  version = "1.0.0";
  src = fetchFromGitHub {
    owner = "cloudius-systems";
    repo = "capstan";
    rev = "6bb7574c54296a47a30984604dcbb41c8cf8ad8a";
    sha256 = "sha256-neP3xEbVgKSCVJbY5rSjTuh4rS8gGNTq3rhAziW9xN4=";
  };
  vendorHash = "sha256-AHIPpTT8BugnAiEwcOBCpPRLhjmfosa9hKs+7zSJ4MU=";

  GO111MODULE = "on";
  nativeBuildInputs = [ musl ];

  ldflags = [
    "-linkmode external"
    "-extldflags '-static -L${glibc.static}/lib'"
  ];

  checkPhase = "";
}

