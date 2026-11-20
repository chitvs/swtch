{
  description = "A minimal terminal stopwatch";

  # packages come from the stable NixOS 26.05 branch
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      # build for the usual Linux and macOS architectures
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      # `nix build` and `nix run` use this
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "swtch";
          version = "1.0.1";
          src = self;

          makeFlags = [ "PREFIX=$(out)" ];

          meta = {
            description = "Minimal terminal stopwatch";
            homepage = "https://github.com/chitvs/swtch";
            license = nixpkgs.lib.licenses.mit;
            mainProgram = "swtch";
            platforms = nixpkgs.lib.platforms.unix;
          };
        };
      });

      # `nix develop` for a shell with a C compiler and make
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell { };
      });
    };
}
