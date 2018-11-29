TARGET_KERNEL_SOURCE="/home/elo/WORKSPACE/LINUX_KERNEL/elo-android-7.1.1-nougat-android-kernel/msm-3.18"
export OEM_HEADER_FILE_PATH="/home/elo/WORKSPACE/LINUX_KERNEL/oem_header"
OUT="/home/elo/WORKSPACE/LINUX_KERNEL/OUT_KERNEL"
CROSS_COMPILE_PATH="/home/elo/PAYPOINT_REFRESH_7.1.1_ELO_WORKSPACE/android/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-"
KERNEL_DEFAULT_CONFIG_FILE="elo_defconfig"

COMMON_MAKEFLAGS="				\
	O=${OUT}				\
	ARCH=arm64                              \
	CROSS_COMPILE=${CROSS_COMPILE_PATH}     \
	KCFLAGS=-mno-android                    \
	-j4					\
	"

echo "Starting kernel_build.sh ..."

if [ ! -d ${OUT} ]; then
	mkdir -p ${OUT};
fi

echo "Clean out directory ..."
cd ${OUT}
make mrproper
make distclean

echo "Clean kernel directory ..."
cd ${TARGET_KERNEL_SOURCE}
make mrproper
make distclean

echo "Compile kernel ..."
cd ${TARGET_KERNEL_SOURCE}
make -C ${TARGET_KERNEL_SOURCE} $COMMON_MAKEFLAGS ${KERNEL_DEFAULT_CONFIG_FILE}
make -C ${TARGET_KERNEL_SOURCE} $COMMON_MAKEFLAGS kernel
make -C ${TARGET_KERNEL_SOURCE} $COMMON_MAKEFLAGS

echo "Successfully Compiled ..."
