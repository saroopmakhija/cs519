#! /bin/bash
#logfile=test.out

buf_sizes=(65536 262144 1048576 4194304 16777216 67108864 268435456)
threads=(2 4 8 16 32 64)

run_sim() {
    src=../../../
    # record="sudo perf record -e probe:$pmode -a "
    record="sudo perf record -e faults -a "
    # script="sudo perf script | grep -w 'st.o\|est.o\|ast.o\|east.o' | awk --bignum '{print substr(\$3, 2, length(\$3)-2),substr(\$4, 1, length(\$4)-1)*1e6}'"
    script="sudo perf script | grep 'set_bufs' | awk --bignum '{print substr(\$3, 2, length(\$3)-2),substr(\$4, 1, length(\$4)-1)*1e6,\$5}'"
    for buf in ${buf_sizes[@]}
    do
        mkdir $buf
        cd $buf
        mkdir 1
        cd  1
        $record $src/st.o 1 $buf
        echo "$(eval $script)" > st.csv
        $record $src/est.o 1 $buf
        echo "$(eval $script)" > est.csv
        cd ..
        for proc in ${threads[@]}
        do
            mkdir $proc
            cd $proc
            $record $src/ast.o $proc $buf
            echo "$(eval $script)" > st.csv
            $record $src/east.o $proc $buf
            echo "$(eval $script)" > est.csv
            cd ..
        done
        cd ..
    done
}

pmode=do_anonymous_page
mmap=mmapr
sudo perf probe $pmode
echo "running mmap fault"
make clean
make MIN_PRINT=1 -j 16 all
rm -rf $mmap
mkdir $mmap
cd $mmap
run_sim
cd ..
sudo perf probe -d $pmode

pmode=filemap_fault
fmap=fmapr
sudo perf probe $pmode
echo "running fmap fault"
make clean
make MIN_PRINT=1 IS_FILE=1 -j 16 all
rm -rf $fmap
mkdir $fmap
cd $fmap
run_sim
cd ..
sudo perf probe -d $pmode

python3 gen_plot.py
