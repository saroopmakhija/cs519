#! /bin/bash
outdir=p2_patch
if [ -d "$outdir" ]; then
    rm -rf $outdir
fi
mkdir $outdir

. ./set_patch_env.sh
for i in ${!patch_dirs[@]}
do
    pF="${p21}/${patch_dirs[i]}"
    oF="${outdir}/${out_dirs[i]}"
    diff -u "${pF}.orig" "${pF}" > $oF
    echo "Added patch file: ${oF}"
done

patch_dirs=(mm/my_extent.c include/linux/my_extent.h)
out_dirs=(my_extent_c.patch my_extent_h.patch)
for i in ${!patch_dirs[@]}
do
    pF="${p21}/${patch_dirs[i]}"
    oF="${outdir}/${out_dirs[i]}"
    diff -Nu /dev/null ${pF} > $oF
    echo "Added patch file: ${oF}"
done
