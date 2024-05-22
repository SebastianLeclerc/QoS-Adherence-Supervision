# QoS-Adherence-Supervision
The agent polls a switch (Red Lion) over Telnet (default port 23) for Tx counters every second from the Secondary. \
These values were called QoS statistics, according to the switch documentation. \
If the Tx counters are below or above an expected threshold (1050), the agent sends appropriate Access Control List (ACL) to correct the Quality of Service (QoS) switch configuration by re-prioritizing traffic flows. \
Can be adapted for any traffic flows, currently only implemented for three different flows (High priority UDP-based heartbeats, Medium priority Telnet, and Low priority UDP traffic).

Topology: \
Primary ------ Red Lion switch ------ Cisco switch ------ Secondary

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
Upload and apply switch configuration: 
1. Red Lion, Model: NT-4008-000-PN-M, Firmware: 1.0.9
2. Cisco, Model: IE-2000-8TC-L, IOS: 15.2(7)E2

Change the priority of the network interrupts for each core, by first finding the PID of each process
```bash
ps -A | grep ksoftirq
```
And then elevating the priority to 98
```bash
sudo chrt -p 98 PID
```

Build and run the code:

1. Build and run the QoS agent on the secondary controller with maximum priority.
```bash
sudo g++ -o qos qos.cpp -pthread 
sudo chrt --fifo 99 ./qos
```

## Long and short CLI for the Red Lion switch
Querying the QoS statistics on interface 1/2, filtering out unnecessary data
```bash
show interface GigabitEth 1/2 statistics | include Tx Priority 7:
sh int G 1/2 stati | i Tx Priority 7:
```

Re-marking matching L3 DSCP 56 packets on interface 1/1 to L2 CoS 7
```bash
qos qce 1 interface GigabitEth 1/1 frame-type ipv4 dscp 56 action cos 7
q q 1 int G 1/1 f ipv4 ds 56 a c 7
```

Marking all other [UDP or TCP] packets with L2 CoS 0, on interfaces 1/1, 1/2, and 1/8
```bash
qos qce 2 interface GigabitEth 1/1-2,8 frame-type ipv4 proto [UDP/TCP] action cos 0
q q 2 int G 1/1-2,8 f ipv4 p [U/T] a c 0
```

Marking TCP traffic toward destination port 23 with L2 CoS 6
```bash
qos qce 3 frame-type ipv4 proto TCP dport 23 action cos 6
q q 3 int f ipv4 p t d 23 a c 6
```
