# QoS-Adherence-Supervision
## Installation


Install the required packages and libraries. 
```bash
sudo apt update 
sudo apt upgrade
sudo apt-get install build-essential
sudo apt install cmake
sudo apt install net-tools
sudo apt-get install libboost-all-dev
```

## Usage
Change the priority of the network interrupts for each core.
```bash
ps -A | grep ksoftirq // To find the PID of each process
sudo chrt -p 98 PID // Change the PID of the process to 98
```

Build and run the code in the following order:

1. Build and run the agent on the secondary controller.
```bash
sudo g++ -o secondary secondary.cpp -pthread 
sudo chrt --fifo 99 ./main
```
