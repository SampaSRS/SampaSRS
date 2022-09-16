# Extracted from: https://stackoverflow.com/questions/630372/determine-the-path-of-the-executing-bash-script

THIS_PATH=$(dirname "$0")             # relative
SAMPASYS=$(cd "$THIS_PATH/.." && pwd) # absolutized and normalized
if [[ -z "$SAMPASYS" ]] ; then
  # error; for some reason, the path is not accessible
  # to the script (e.g. permissions re-evaled after suid)
  exit 1  # fail
fi

if [ -z "${PATH}" ]; then
    PATH=$SAMPASYS/bin; export PATH
else
    PATH=$SAMPASYS/bin:$PATH; export PATH
fi

if [ -z "${LD_LIBRARY_PATH}" ]; then
    LD_LIBRARY_PATH=$SAMPASYS/lib; export LD_LIBRARY_PATH
else
    LD_LIBRARY_PATH=$SAMPASYS/lib:$LD_LIBRARY_PATH; export LD_LIBRARY_PATH
fi

if [ -z "${DYLD_LIBRARY_PATH}" ]; then
    DYLD_LIBRARY_PATH=$SAMPASYS/lib
    export DYLD_LIBRARY_PATH       # Linux, ELF HP-UX
else
    DYLD_LIBRARY_PATH=$SAMPASYS/lib:$DYLD_LIBRARY_PATH
    export DYLD_LIBRARY_PATH
fi

if [ -z "${SHLIB_PATH}" ]; then
    SHLIB_PATH=$SAMPASYS/lib
    export SHLIB_PATH       # Linux, ELF HP-UX
else
    SHLIB_PATH=$SAMPASYS/lib:$SHLIB_PATH
    export SHLIB_PATH
fi

if [ -z "${LIBPATH}" ]; then
    LIBPATH=$SAMPASYS/lib
    export LIBPATH       # Linux, ELF HP-UX
else
    LIBPATH=$SAMPASYS/lib:$LIBPATH
    export LIBPATH
fi
