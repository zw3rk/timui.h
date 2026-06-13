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
            # pkg-config lets `make vt-test` resolve libvterm (and is harmless
            # where libvterm is absent — the Makefile warns gracefully).
            nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config ];
            # libvterm is Linux-only in nixpkgs (meta.platforms excludes
            # darwin), so add it conditionally — otherwise `nix develop` would
            # fail to evaluate on macOS. On darwin, `make vt-test` reports the
            # missing dep; the core `make`/test/goldens targets work everywhere.
            buildInputs = [ pkgs.clang ]
              ++ pkgs.lib.optional pkgs.stdenv.isLinux pkgs.libvterm;
          };
        });
    };
}
