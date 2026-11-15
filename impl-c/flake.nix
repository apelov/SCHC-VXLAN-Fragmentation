{
  description = "schcvxd: userspace C implementation of SCHC VXLAN fragmentation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/8fd9daa3db09ced9700431c5b7ad0e8ba199b575";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (system: pkgs: {
        default = pkgs.callPackage ./nix/package.nix { };
      });

      nixosModules.default = { pkgs, lib, ... }: {
        imports = [ ./nix/module.nix ];
        services.schcvx.package = lib.mkDefault self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      };

      devShells = forAllSystems (system: pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            meson ninja pkg-config gcc python3
            iproute2 iputils iperf3 ethtool tcpdump
          ];
        };
      });

      checks = forAllSystems (system: pkgs: {
        default = self.packages.${system}.default;
      });
    };
}
