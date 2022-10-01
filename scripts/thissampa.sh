
# Source this script to set up the SampaSRS build that this script is part of.
#
# Conveniently an alias like this can be defined in .bashrc or .zshrc:
#   alias thissampa=". bin/thissampa.sh"
#
# Adapted from: https://github.com/root-project/root/blob/master/config/thisroot.sh
# Original Author: Fons Rademakers, 18/8/2006

drop_from_path()
{
   # Assert that we got enough arguments
   if test $# -ne 2 ; then
      echo "drop_from_path: needs 2 arguments"
      return 1
   fi

   local p=$1
   local drop=$2

   newpath=`echo $p | sed -e "s;:${drop}:;:;g" \
                          -e "s;:${drop}\$;;g"   \
                          -e "s;^${drop}:;;g"   \
                          -e "s;^${drop}\$;;g"`
}

clean_environment()
{

   if [ -n "${old_sampasys}" ] ; then
      if [ -n "${PATH}" ]; then
         drop_from_path "$PATH" "${old_sampasys}/bin"
         PATH=$newpath
      fi
      if [ -n "${LD_LIBRARY_PATH}" ]; then
         drop_from_path "$LD_LIBRARY_PATH" "${old_sampasys}/lib"
         LD_LIBRARY_PATH=$newpath
      fi
      if [ -n "${DYLD_LIBRARY_PATH}" ]; then
         drop_from_path "$DYLD_LIBRARY_PATH" "${old_sampasys}/lib"
         DYLD_LIBRARY_PATH=$newpath
      fi
      if [ -n "${SHLIB_PATH}" ]; then
         drop_from_path "$SHLIB_PATH" "${old_sampasys}/lib"
         SHLIB_PATH=$newpath
      fi
      if [ -n "${LIBPATH}" ]; then
         drop_from_path "$LIBPATH" "${old_sampasys}/lib"
         LIBPATH=$newpath
      fi
      if [ -n "${PYTHONPATH}" ]; then
         drop_from_path "$PYTHONPATH" "${old_sampasys}/lib"
         PYTHONPATH=$newpath
      fi
      if [ -n "${MANPATH}" ]; then
         drop_from_path "$MANPATH" "${old_sampasys}/man"
         MANPATH=$newpath
      fi
      if [ -n "${CMAKE_PREFIX_PATH}" ]; then
         drop_from_path "$CMAKE_PREFIX_PATH" "${old_sampasys}"
         CMAKE_PREFIX_PATH=$newpath
      fi
      if [ -n "${JUPYTER_PATH}" ]; then
         drop_from_path "$JUPYTER_PATH" "${old_sampasys}/etc/notebook"
         JUPYTER_PATH=$newpath
      fi
      if [ -n "${JUPYTER_CONFIG_DIR}" ]; then
         drop_from_path "$JUPYTER_CONFIG_DIR" "${old_sampasys}/etc/notebook"
         JUPYTER_CONFIG_DIR=$newpath
      fi
   fi
   if [ -z "${MANPATH}" ]; then
      # Grab the default man path before setting the path to avoid duplicates
      if command -v manpath >/dev/null; then
         default_manpath=`manpath`
      elif command -v man >/dev/null; then
         default_manpath=`man -w 2> /dev/null`
      else
         default_manpath=""
      fi
   fi
}

set_environment()
{
   if [ -z "${PATH}" ]; then
      PATH=$SAMPASYS/bin; export PATH
   else
      PATH=$SAMPASYS/bin:$PATH; export PATH
   fi

   if [ -z "${LD_LIBRARY_PATH}" ]; then
      LD_LIBRARY_PATH=$SAMPASYS/lib
      export LD_LIBRARY_PATH       # Linux, ELF HP-UX
   else
      LD_LIBRARY_PATH=$SAMPASYS/lib:$LD_LIBRARY_PATH
      export LD_LIBRARY_PATH
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

   if [ -z "${PYTHONPATH}" ]; then
      PYTHONPATH=$SAMPASYS/lib
      export PYTHONPATH       # Linux, ELF HP-UX
   else
      PYTHONPATH=$SAMPASYS/lib:$PYTHONPATH
      export PYTHONPATH
   fi

   if [ -z "${MANPATH}" ]; then
      MANPATH=$SAMPASYS/man:${default_manpath}; export MANPATH
   else
      MANPATH=$SAMPASYS/man:$MANPATH; export MANPATH
   fi

   if [ -z "${CMAKE_PREFIX_PATH}" ]; then
      CMAKE_PREFIX_PATH=$SAMPASYS; export CMAKE_PREFIX_PATH       # Linux, ELF HP-UX
   else
      CMAKE_PREFIX_PATH=$SAMPASYS:$CMAKE_PREFIX_PATH; export CMAKE_PREFIX_PATH
   fi

   if [ -z "${JUPYTER_PATH}" ]; then
      JUPYTER_PATH=$SAMPASYS/etc/notebook; export JUPYTER_PATH       # Linux, ELF HP-UX
   else
      JUPYTER_PATH=$SAMPASYS/etc/notebook:$JUPYTER_PATH; export JUPYTER_PATH
   fi

   if [ -z "${JUPYTER_CONFIG_DIR}" ]; then
      JUPYTER_CONFIG_DIR=$SAMPASYS/etc/notebook; export JUPYTER_CONFIG_DIR # Linux, ELF HP-UX
   else
      JUPYTER_CONFIG_DIR=$SAMPASYS/etc/notebook:$JUPYTER_CONFIG_DIR; export JUPYTER_CONFIG_DIR
   fi
}

getTrueShellExeName() { # mklement0 https://stackoverflow.com/a/23011530/7471760
  local trueExe nextTarget 2>/dev/null # ignore error in shells without `local`
  # Determine the shell executable filename.
  if [ -r "/proc/$$/cmdline" ]; then
    trueExe=$(cut -d '' -f1 /proc/$$/cmdline) || return 1
  else
    trueExe=$(ps -p $$ -o comm=) || return 1
  fi
  # Strip a leading "-", as added e.g. by macOS for login shells.
  [ "${trueExe#-}" = "$trueExe" ] || trueExe=${trueExe#-}
  # Determine full executable path.
  [ "${trueExe#/}" != "$trueExe" ] || trueExe=$([ -n "$ZSH_VERSION" ] && which -p "$trueExe" || which "$trueExe")
  # If the executable is a symlink, resolve it to its *ultimate*
  # target.
  while nextTarget=$(readlink "$trueExe"); do trueExe=$nextTarget; done
  # Output the executable name only.
  printf '%s' "$(basename "$trueExe")"
}

### main ###


if [ -n "${SAMPASYS}" ] ; then
   old_sampasys=${SAMPASYS}
fi


SHELLNAME=$(getTrueShellExeName)
if [ "$SHELLNAME" = "bash" ]; then
   SOURCE=${BASH_ARGV[0]}
elif [ "x${SHELLNAME}" = "x" ]; then # in case getTrueShellExeName does not work, fall back to default
   echo "WARNING: shell name was not found. Assuming 'bash'."
   SOURCE=${BASH_ARGV[0]}
elif [ "$SHELLNAME" = "zsh" ]; then
   SOURCE=${(%):-%N}
else # dash or ksh
   x=$(lsof -p $$ -Fn0 2>/dev/null | tail -1); # Paul Brannan https://stackoverflow.com/a/42815001/7471760
   SOURCE=${x#*n}
fi


if [ "x${SOURCE}" = "x" ]; then
   if [ -f bin/thissampa.sh ]; then
      SAMPASYS="$PWD"; export SAMPASYS
   elif [ -f ./thissampa.sh ]; then
      SAMPASYS=$(cd ..  > /dev/null; pwd); export SAMPASYS
   else
      echo ERROR: must "cd where/sampa/is" before calling ". bin/thissampa.sh" for this version of "$SHELLNAME"!
      SAMPASYS=; export SAMPASYS
      return 1
   fi
else
   # get param to "."
   thissampa=$(dirname ${SOURCE})
   SAMPASYS=$(cd ${thissampa}/.. > /dev/null;pwd); export SAMPASYS
fi


clean_environment
set_environment

unset old_sampasys
unset thissampa
unset -f drop_from_path
unset -f clean_environment
unset -f set_environment
