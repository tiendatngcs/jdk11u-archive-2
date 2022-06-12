
read_dom () {
    local IFS=\>
    read -d \< ENTITY CONTENT
}

help () {
    echo "./run_remote_memory.sh <MODE>"
    echo -e "\t <MODE> local, remote"
}

MODE=$1
if [[ -z $MODE ]]; then
    echo "Missing values"
    help
    exit 0
fi

if [ "$MODE" = "local" ]; then
    echo "local mode"
    ssh_addrs=()
    ib_addrs=()
    ports=()
    current_ib_addr=""
    # current_ssh_addr=""
    current_port=""
    node_count=0

    while read_dom; do
        if [[ $ENTITY = "remote_node" ]]; then
            echo "New remote node"
            # while read_dom; do
            #     if [[ $ENTITY = "remote_node" ]]; then
            # done
            ORIGINAL_IFS=$IFS
            IFS=\>
            while [[ $ENTITY != "/remote_node" ]]
            do
                read -d \< ENTITY CONTENT
                if [[ $ENTITY = "ib_addr" ]]; then
                    current_ib_addr=$CONTENT
                elif [[ $ENTITY = "port" ]]; then
                    current_port=$CONTENT
                fi
            done
            IFS=$ORIGINAL_IFS
            # only append if address is not empty
            if [ ! -z "$current_ib_addr" ]; then
                remote_addresses+=($current_ib_addr)
                if [ -z "$current_port" ]; then
                    ports+=("7471")
                else
                    ports+=($current_port)
                fi
            fi
            ((node_count+=1))
        fi
    done < remote_nodes.xml


    echo "${remote_addresses[*]}"
    echo "${ports[*]}"

elif [ "$MODE" = "remote" ]; then
    echo "remote mode"
    g++ remote_node.cc -o remote_node.exe -libverbs -lrdmacm -pthread

    
else
    echo "Invalid value for MODE"
    help
    exit 0
fi