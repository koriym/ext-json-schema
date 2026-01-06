#!/bin/bash

clean() {
    echo "Cleaning..."
    make clean 2>/dev/null
    phpize --clean
}

prepare() {
    echo "Preparing..."
    phpize
    ./configure --enable-json_schema
}

build() {
    echo "Building..."
    make
}

install() {
    echo "Installing..."
    make install
}

test() {
    echo "Running tests..."
    make test TESTS=tests/
}

run() {
    echo "Run..."
    php -dextension=modules/json_schema.so -ddisplay_errors=1 smoke.php
}

case $1 in
    clean)
        clean
        ;;
    prepare)
        prepare
        ;;
    build)
        build
        ;;
    install)
        install
        ;;
    test)
        test
        ;;
    run)
        run
        ;;
    all)
        clean
        prepare
        build
        run
        ;;
    *)
        echo "Usage: $0 {clean|prepare|build|install|test|run|all}"
        exit 1
        ;;
esac
