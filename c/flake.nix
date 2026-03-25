{
  description = "Pool allocator development environment";

  inputs = {
    nixpkgs.url = "github:cachix/devenv-nixpkgs/rolling";
    devenv.url = "github:cachix/devenv";

    flake-utils.url = "github:numtide/flake-utils";
  };

  nixConfig = {
    extra-substituters = "https://devenv.cachix.org";
    extra-trusted-public-keys = "devenv.cachix.org-1:w1cLUi8dv3hnoSPGAuibQv+f9TZLr6cv/Hm9XgU50cw=";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      devenv,
    }@inputs:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      with pkgs;
      {
        devShells.default = devenv.lib.mkShell {
          inherit inputs pkgs;

          modules = [
            (
              { pkgs, config, ... }:
              {
                # Most packages come pre-built with binaries provided by the official Nix binary
                # cache. If you're modifying a package or using a package that's not built
                # upstream, Nix will build it from source instead of downloading a binary.
                # To prevent packages from being built more than once, devenv provides seamless
                # integration with binary caches hosted by Cachix.
                #
                # Devenv will automatically configure Cachix caches for you, or guide you how to
                # add the caches to Nix manually. Any caches set up by devenv are used in addition
                # to the caches configured in Nix, for example, in /etc/nix/nix.conf.
                cachix = {
                  enable = true;

                  # devenv.cachix.org is added to the list of pull caches by default. It mirrors
                  # the official NixOS cache and is designed to provide caching for the
                  # devenv-nixpkgs/rolling nixpkgs input.
                  #
                  # Some languages and integrations may automatically add caches when enabled.
                  pull = [ ];
                };

                packages = with pkgs; [
                  zig
                ];
              }
            )
          ];
        };
      }
    );
}
