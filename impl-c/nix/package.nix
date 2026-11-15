{ lib, stdenv, meson, ninja, pkg-config, python3 }:

let
  # The Meson tests read draft/revised/wire-vectors.json from the repository
  # root (../../draft relative to impl-c/tests), so the source is the
  # repository root restricted to impl-c plus that one file.
  repoRoot = ../..;
  src = lib.fileset.toSource {
    root = repoRoot;
    fileset = lib.fileset.unions [
      ../.
      (repoRoot + "/draft/revised/wire-vectors.json")
    ];
  };
in
stdenv.mkDerivation {
  pname = "schcvxd";
  version = "0.1.0";

  inherit src;
  sourceRoot = "${src.name}/impl-c";

  nativeBuildInputs = [ meson ninja pkg-config python3 ];

  # Warnings must not break the build on a different compiler version.
  mesonFlags = [ "-Dwerror=false" ];

  doCheck = true;

  meta = {
    description = "SCHC VXLAN fragmentation daemon (functional and interoperability testing)";
    platforms = lib.platforms.linux;
    mainProgram = "schcvxd";
  };
}
