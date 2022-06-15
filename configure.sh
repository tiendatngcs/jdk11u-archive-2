num_core=`nproc --all`

bash ./configure --enable-debug --with-jvm-features=shenandoahgc --with-extra-cxxflags="-lrdmacm -libverbs" --with-extra-ldflags="-lrdmacm -libverbs"