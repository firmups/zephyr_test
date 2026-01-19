# # zsdk.nix
# {
#   stdenv,
#   fetchzip,
#   wget,
#   cmake,
#   which,
#   python3,
# }:
# let
#   version = "0.17.4";
#   host_arch = "linux-x86_64";
#   toolchain = "x86_64-zephyr-elf";
#   toolchainDir = fetchzip {
#     url = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${version}/toolchain_${host_arch}_${toolchain}.tar.xz";
#     sha256 = "sha256-rTQXH4mK+qCRgvJRRdrfZNLZFXCR+9lh7kMbdVpfdkg=";
#   };
# in
# stdenv.mkDerivation {  
#   pname = "zsdk";
#   inherit version;
#   src = fetchzip {
#     url = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${version}/zephyr-sdk-${version}_linux-x86_64_minimal.tar.xz";
#     sha256 = "sha256-f6jyPd79PxHXN9HmBf1BRxRCMuNNAHM5ZcDpXrAQdUA=";
#   };
#   dontConfigure = true;
#   dontBuild = true;
#   doInstallCheck = true;
#   hardeningDisable = [ "all" ]; # hosttools lead to buffer overflows otherwise

#   buildInputs = [ wget cmake which python3 ];

#   installPhase = ''
#     runHook preInstall

#     ln -s ${toolchainDir} ${toolchain}
#     sed -i 's/-n100/-n10 /g' ./zephyr-sdk-x86_64-hosttools-standalone-0.10.sh
#     bash -c "./zephyr-sdk-x86_64-hosttools-standalone-0.10.sh -y -D -R -d ."
#     cmake -P cmake/zephyr_sdk_export.cmake

#     runHook postInstall
#   '';

#   installCheckPhase = ''
#     runHook preInstallCheck

#     $out/bin/wmic --help >/dev/null

#     runHook postInstallCheck
#   '';
# }