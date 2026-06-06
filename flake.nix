# timui.h — nix flake dev shell.
# The Makefile is the sole entry point: `nix develop -c make <target>`.
{
  description = "timui.h — single-header C99 immediate-mode TUI for modern terminals";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            nativeBuildInputs = [ pkgs.gnumake ];
            buildInputs       = [ pkgs.clang ];
          };
        });
    };
}
