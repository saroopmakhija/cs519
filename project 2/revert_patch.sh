#! /bin/bash
. ./set_patch_env.sh
for i in ${!patch_dirs[@]}
do
    pF="${p21}/${patch_dirs[i]}"
    if [ -f "${pF}.orig" ]; then
        echo "Reverting ${pF}"
        mv "${pF}.orig" "${pF}"
    fi
done
