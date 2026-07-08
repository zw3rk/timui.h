# timui.h — nix flake dev shell.
# The Makefile is the sole entry point: `nix develop -c make <target>`.
{
  description = "timui.h — single-header C99 immediate-mode TUI for modern terminals";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      forPkgs = system: import nixpkgs { inherit system; };
      version = "0.2.0";
      src = nixpkgs.lib.cleanSource ./.;
      nativeInputs = pkgs: [ pkgs.clang pkgs.gawk pkgs.gnumake pkgs.pkg-config ];
      conptyCrossInputs = pkgs: [ pkgs.pkgsCross.mingwW64.stdenv.cc ];
      buildInputs = pkgs: [ pkgs.libvterm-neovim ];
      mkWww = system:
        let pkgs = forPkgs system;
        in pkgs.stdenv.mkDerivation {
          pname = "timui-www";
          inherit version src;

          nativeBuildInputs = nativeInputs pkgs;
          buildInputs = buildInputs pkgs;

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            make www
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            cp -R www/. "$out"/
            runHook postInstall
          '';
        };
      mkCiCheck = system:
        let
          pkgs = forPkgs system;
          hostCc = "${pkgs.stdenv.cc}/bin/cc";
        in pkgs.stdenv.mkDerivation {
          pname = "timui-ci-check";
          inherit version src;

          nativeBuildInputs = nativeInputs pkgs
            ++ conptyCrossInputs pkgs;
          buildInputs = buildInputs pkgs;

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            cp -R tests/golden "$TMPDIR/golden.before"
            cp www/timui.h "$TMPDIR/www-timui.before"
            make CC=${hostCc} check
            make CC=${hostCc} release-check
            make CC=${hostCc} goldens
            diff -ru "$TMPDIR/golden.before" tests/golden
            make CC=${hostCc} www
            cmp -s "$TMPDIR/www-timui.before" www/timui.h
            cmp -s release/timui.h www/timui.h
            mkdir -p build
            awk 'BEGIN{n=0; emit=0}
                 /^```c$/{n++; if(n==2){emit=1; next}}
                 /^```$/{if(emit) exit}
                 emit{print}' www/llms.txt > build/llms_smoke.c
            cc -std=c99 -Wall -Wextra -Wpedantic -O2 -pthread \
              -Iwww build/llms_smoke.c -o build/llms_smoke
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            touch "$out/ok"
            runHook postInstall
          '';
        };
    in {
      packages = forAllSystems (system: {
        default = self.packages.${system}.www;
        www = mkWww system;
      });

      checks = forAllSystems (system: {
        ci = mkCiCheck system;
        www = self.packages.${system}.www;
      });

      # ci.zw3rk.com consumes Hydra-style flake jobs.  Keep the static site as a
      # first-class build artifact, while the CI check enforces generated-file
      # freshness without depending on a git checkout.
      hydraJobs = forAllSystems (system: {
        ci = self.checks.${system}.ci;
        www = self.packages.${system}.www;
      });

      devShells = forAllSystems (system:
        let pkgs = forPkgs system;
        in {
          default = pkgs.mkShell {
            # pkg-config lets `make vt-test` resolve the neovim/Paul Evans
            # libvterm API used by the Tier A round-trip tests.
            # asciinema records a real terminal session's raw byte stream to a
            # .cast for `make rec-<name>` (feedable to the render verifier).
            nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config pkgs.asciinema ]
              ++ conptyCrossInputs pkgs;
            buildInputs = [ pkgs.clang ]
              ++ buildInputs pkgs;
          };
        });
    };
}
