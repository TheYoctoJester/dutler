# Source this before building:  source env.sh
# Self-contained Pico build environment for this project.
export PICO_SDK_PATH="/Users/tyj/Projects/pico/pico-sdk"
export PICO_TOOLCHAIN_PATH="/Users/tyj/Projects/pico/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi"
export picotool_DIR="/opt/homebrew/opt/picotool/lib/cmake/picotool"
export PATH="$PICO_TOOLCHAIN_PATH/bin:$PATH"
