{
  description = "Development environment for firmups frontend";
  inputs.nixpkgs.url = "github:nixos/nixpkgs?ref=25.11";
  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
        config.segger-jlink.acceptLicense = true;
      };
      venvDir = ".pyenv";
      ZSDK_VERSION = "1.0.1";
      zsdkDir = "zephyr-sdk-${ZSDK_VERSION}";
    in
    rec {
      #zsdk = pkgs.callPackage ./nix/pkgs/zsdk/zsdk.nix {};
      devShells.x86_64-linux.default = pkgs.mkShell {
        hardeningDisable = [ "all" ];
        buildInputs = with pkgs; [
          git
          cmake
          ninja
          gperf
          ccache
          dfu-util
          wget
          python313Packages.setuptools
          python313Packages.tkinter
          python313Packages.pyelftools
          dtc
          minicom
          bashInteractive
        ];

        shellHook = ''
                    export WEST_WORKSPACE=$(git rev-parse --show-toplevel)
                    export PROJECT_ROOT=$WEST_WORKSPACE/firmware

                    if [ -d "./${zsdkDir}" ]; then
                      echo "Skipping ZSDK download, '${zsdkDir}' already exists"
                    else
                      wget --quiet --show-progress --progress=dot:giga \
                          https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZSDK_VERSION}/zephyr-sdk-${ZSDK_VERSION}_linux-x86_64_minimal.tar.xz
                      wget --quiet --show-progress --progress=dot:giga --output-document - \
                          https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZSDK_VERSION}/sha256.sum \
                    |     shasum --check --ignore-missing 
                      tar --extract --file zephyr-sdk-${ZSDK_VERSION}_linux-x86_64_minimal.tar.xz
                      rm zephyr-sdk-${ZSDK_VERSION}_linux-x86_64_minimal.tar.xz
                      cd ${zsdkDir}
                      ./setup.sh -h -t x86_64-zephyr-elf -t arm-zephyr-eabi
                      cd ..
                    fi

                    export ZEPHYR_SDK_INSTALL_DIR=$PWD/${zsdkDir}
                    export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

                    if [ -d "${venvDir}" ]; then
                      echo "Skipping venv creation, '${venvDir}' already exists"
                    else
                      echo "Creating new venv environment in path: '${venvDir}'"
                      python -m venv "${venvDir}"
                    fi

                    # activate our virtual env.
                    source "${venvDir}/bin/activate"
                    pip install --upgrade pip
                    pip install west
                    pip install --upgrade certifi
                    pip install pyocd

                    if [ -d "$WEST_WORKSPACE/.west" ];
                    then
                        echo $WEST_WORKSPACE
                        echo "West already initialized"
                    else
                        west init --local "$PROJECT_ROOT"
                    fi

                    if [ "$RUN_OFFLINE" != "true" ]; then
                      west update
                      if [ ! -f "${venvDir}/.pyocd_lock" ]; then
                        pyocd pack update
                        pyocd pack install STM32H573IIKxQ
                        touch "${venvDir}/.pyocd_lock"
                      fi
                    fi
                    west zephyr-export
                    west packages pip --install
                    west blobs fetch -l rw61x.*

          	  export PS1=(firmups-zephyr-test)$PS1
                    echo ""
                    echo ""
                    echo "############################"
                    echo "# Welcome to the devShell! #"
                    echo "############################"
        '';
      };
    };
}
