#
# https://source.codeaurora.org/quic/la/kernel/msm-3.18/log/?h=LA.BR.5.6
#

##############################################################################
# Helper methods
##############################################################################

SHOW_HELP=false
GCC_ARGS="-C . O=out ARCH=arm64 SUBARCH=arm64 CROSS_COMPILE=$(pwd)/../../prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-"

BUILD_MENUCONFIG=false
BUILD_CLEAN=false

# compile the standalone kernel
function compile()
{
  if [ "$BUILD_CLEAN" = true ]; then
    echo " * Removing outdated precompiled files..."
    make $GCC_ARGS clean
    make $GCC_ARGS mrproper
    make $GCC_ARGS distclean
    rm -rf out
  fi

  echo " * Creating output directory..."
  if [ ! -d "out" ]; then
    mkdir out
  fi

  # generate a clean new config
  export OEM_HEADER_FILE_PATH=/mnt/android/PlatformElo/kernel/msm-3.18/elo/oem_header/
  make $GCC_ARGS elo_defconfig

  # check if the user wishes to run menuconfig
  if [ "$BUILD_MENUCONFIG" = true ] ; then
    make $GCC_ARGS menuconfig
  fi

  # starting the actual compilation
  echo " * Starting compilation of the kernel..."
  make $GCC_ARGS -j8
}

# show usage message to the user
function show_help()
{
  echo "Working with jfltexx kernel for Galaxy S4"
  echo "Usage: ./build.sh <options>"
  echo "Following options are available:"
  echo "  --clean Use this option to remove outdated precompiled files"
  echo "  --menu Use this option start menuconfig before compilation"
  exit 1
}

##############################################################################
# Script entry point
##############################################################################

# process user's wish
for arg
do
  case $arg in
    "--menu")  BUILD_MENUCONFIG=true
               ;;
    "--clean") BUILD_CLEAN=true
               ;;
    *        ) echo "ERR: provided option is invalid ($arg)!"
               SHOW_HELP=true
               ;;
  esac
done

# show usage message, if required
if [ "$SHOW_HELP" = true ] ; then
  show_help
  exit 1
fi

# otherwise we can compile
compile
