#!/bin/sh
# Prints a GLSL file as a C string constant:  embed-glsl.sh NAME FILE
printf 'static const char *%s =\n' "$1"
sed 's/\\/\\\\/g; s/"/\\"/g; s/.*/    "&\\n"/' "$2"
printf ';\n\n'
