case "$2" in
    "Debug")
        build_type=debug
        ;;
    "Release")
        build_type=release
        ;;
    "RelWithDebInfo")
        build_type=debugoptimized
        ;;
    "MinSizeRel")
        build_type=minsize
        ;;
    *)
        echo "Unknown build type: $2" >&2
        exit 1
esac
