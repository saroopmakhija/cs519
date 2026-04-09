#! /bin/bash
#logfile=test.out

buf_sizes=(65536 262144 1048576 4194304 16777216 67108864 268435456)
threads=(2 4 8 16 32 64)

run_sim() {
    iters=1000
    src=../../../
    stat="sudo perf stat -e faults -r $iters --table --output"
    for buf in ${buf_sizes[@]}
    do
        mkdir $buf
        cd $buf
        mkdir 1
        cd  1
        $stat st.out $src/st.o 1 $buf > st.csv
        $stat est.out $src/est.o 1 $buf > est.csv
        cd ..
        for proc in ${threads[@]}
        do
            mkdir $proc
            cd $proc
            $stat st.out $src/ast.o $proc $buf > st.csv
            $stat est.out $src/east.o $proc $buf > est.csv
            cd ..
        done
        cd ..
    done
}

mmap=mmap
echo "running mmap fault"
make clean
make MIN_PRINT=1 -j 16 all
rm -rf $mmap
mkdir $mmap
cd $mmap
run_sim
cd ..

fmap=fmap
echo "running fmap fault"
make clean
make MIN_PRINT=1 IS_FILE=1 -j 16 all
rm -rf $fmap
mkdir $fmap
cd $fmap
run_sim
cd ..

python3 gen_graph.py
