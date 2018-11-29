Steps To Compile:


1. Extract kernel source and oem_header source and place under separate folder

2. Copy elo_defconfig file to kernel source configs

3. Edit below variable in kernel_build.sh script and make sure below folder exists

TARGET_KERNEL_SOURCE="=== FOLDER PATH OF KERNEL SOURCE ==="
export OEM_HEADER_FILE_PATH="=== FOLDER PATH OF OEM HEADER FILES ==="
OUT="=== FOLDER PATH OF KERNEL OUT DIRECTORY  ==="
CROSS_COMPILE_PATH="~/android/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-"

4. Execute the script using below command

   source kernel_build.sh
