#!/bin/bash

SETUP_DIR=$(dirname $(readlink -e $0))
source "$SETUP_DIR/variable_setup.sh"

#runtime variable


build_kernel_module()
{
    local kernel_src_dir=$KERNEL_SRC_PATH
    local result=

    echo "[task] Build kernel module..."

    if [[ -z $kernel_src_dir ]]; then
       kernel_src_dir=$DEFAULT_KERNEL_SRC_DIR
    fi

    make -C$DEFAULT_KERNEL_MODULE_DIR \
         KERNEL_SRC_DIR=$kernel_src_dir \
         MODULE_DIR=$DEFAULT_KERNEL_MODULE_DIR clean default

    result=$?
    if (( $result != 0 )); then
        echo "[error] Failed to build kernel module: $result"
        exit $result
    fi

    echo "[success] Kernel module building completed."
}

build_memory_optimizer()
{
    local result=

    echo "[task] Build memory optimizer..."

    make -C$DEFAULT_SYS_REFS_DIR clean sys-refs
    result=$?
    if (( $result != 0 )); then
        echo "[error] Failed to build memory optimizer: $result"
        exit $result
    fi

    echo "[success] memory optimizer building completed."
}

check_binary_tool_dependency()
{
    local exist=
    local error=0
    local dependence=(
        ruby
        numactl
        taskset
        gnuplot
        stdbuf
        migratepages
        awk
        sed
        grep
        uniq
        uname
        make
    )

    echo "[task] Check binary tools dependence..."
    for i in "${dependence[@]}"; do
        exist=$(which $i 2>&1)
        exist=$?
        if (( $exist != 0 )); then
            echo "[error] Not found tool: $i"
            error=1
        fi
    done

    if (( $error == 1 )); then
        exit $error
    fi

    echo "[success] Dependence checking completed."
}


build_kernel_module
build_memory_optimizer
check_binary_tool_dependency

cat <<EOF
Build completed successfully.
Please run profile.sh without parameters to get more information.
EOF
