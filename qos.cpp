//Tested on: 
//          Ubuntu 20.04.6 LTS (Focal Fossa)
//          Red Lion NT-4008-000-PN-M industrial switch with Software version: 1.0.9
//Compile:  g++ -o qos qos.cpp -pthread
//Run:      sudo chrt --fifo 99 ./qos
//NOTE:     Telnet MUST be enabled on the switch (enabled via USB/Ethernet connection in web interface)
//          Expected traffic rates values must be adapted to your traffic scenario! 
//          Otherwise, script will send out ACLs modifying the QoS for no reason!
         
#include <iostream>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <sstream>

//Event class with mutexes
class Event {
    public:
        Event() : flag(false) {}

        void set() {
            std::lock_guard<std::mutex> lock(mtx);
            flag = true;
            cv.notify_one();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]
                    { return flag; });
            flag = false;
        }

    private:
        std::mutex mtx;
        std::condition_variable cv;
        bool flag;
    };

//Struct for switch data
struct SwitchData {
    long long TxPrio7;
    long long PTxPrio7;
};

std::atomic<bool> stopFlag(false);

//Open telnet socket (port 23) session to the switch
int openSocket(const char *switchIP) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    const int port = 23;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, switchIP, &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return -1;
    }

    return sock;
}

//Monitor a switch port queue and act according to expected traffic rates, sending out ACL rules
void fetchStats(Event &faultEvent) {
    int sock = 0;
    SwitchData switchData;

    sock = openSocket("192.168.1.103");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    std::string username = "admin\r\n";
    send(sock, username.c_str(), username.length(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    std::string password = "p@ssw0rd\r\n";
    send(sock, password.c_str(), password.length(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::string command3 = "ter le 0 \r\n";
    send(sock, command3.c_str(), command3.length(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::string command1 = "con t\r\n";
    send(sock, command1.c_str(), command1.length(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    std::cout << "Telnet connection established.\n";
    std::cout << "NMA is up and running...\n\n";

    short lowThresholdCounter = 0;
    short highThresholdCounter = 0;

    while (true) {

        //Close socket
        if (stopFlag.load(std::memory_order_relaxed)) {
            close(sock);
            return;
        }

        //Short query Red Lion for QoS Statistics, filter out data & save
        std::string command2 = "do sh in G 1/2 stati | i Tx Priority 7\r\n";
        send(sock, command2.c_str(), command2.length(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        char buffer[512] = {0};
        std::memset(buffer, 0, sizeof(buffer));
        read(sock, buffer, sizeof(buffer));
        std::string line = buffer;
        std::istringstream stream(line);

        //Print output
        while (std::getline(stream, line)) {
            std::size_t pos = line.find("Tx ");
            if (pos != std::string::npos) {
                std::string key = line.substr(pos, line.find(':', pos) - pos);
                long long value = std::stoll(line.substr(line.find_last_of(" ") + 1));
                if (key == "Tx Priority 7") {
                    switchData.TxPrio7 = value;
                }
            }
        }

        //Check if queue is > or < what we expect in a certain queue
        if (switchData.PTxPrio7 != 0) {
            int DiffTxPrio7 = switchData.TxPrio7 - switchData.PTxPrio7;
            if (DiffTxPrio7 > 1050) { //Change value to what your expected packets/second in a queue!
                std::cout << "Warning " << highThresholdCounter << ": Queue No. 7 exceeded the expected rate of 1050.\n\n";
                highThresholdCounter++;
            }
            else if (DiffTxPrio7 < 900) { //Change value to what your expected packets/second in a queue!
                std::cout << "Warning " << lowThresholdCounter << ": Queue No. 7 subceeded the expected rate of 1050.\n\n";
                lowThresholdCounter++;
            }
            else {
                //Counters to keep track
                if (DiffTxPrio7 != 0) {
                    if (!lowThresholdCounter < 1) {
                        lowThresholdCounter--;
                    }
                    if (!highThresholdCounter < 1) {
                        highThresholdCounter--;
                    }
                }
            }

            //If we are confident (3 checks) that expected traffic is too high, demote "other" UDP to Q0
            if (highThresholdCounter > 2) {
                //OLD ACL version based on source MAC: q q 3 int G 1/8 s 00-d8-61-56-56-b1 a c 0
                std::string putIperfLowPrio = "q q 3 int G 1/1-2,8 f ipv4 p u a c 0\r\n";
                send(sock, putIperfLowPrio.c_str(), putIperfLowPrio.length(), 0);
                highThresholdCounter = 0;
                std::cout << "Prioritized other traffic to Q0.\n\n";
            }

            //If we are confident (3 checks) that expected traffic is too low, promote UDP with DSCP 56, demote "other" UDP
            if (lowThresholdCounter > 2) {
                // Remove old ACL
                std::string removeOldACL = "no q q 3 int G 1/1-2,8 f ipv4 p u a c 0\r\n";
                send(sock, removeOldACL.c_str(), removeOldACL.length(), 0);
                
                //Put new ACL in the correct order, i.e., first heartbeat ACL then other UDP ACL
                std::string putHbHighPrio = "q q 2 int G 1/1 f ipv4 ds 56 a c 7\r\n";
                send(sock, putHbHighPrio.c_str(), putHbHighPrio.length(), 0);

                std::string putIperfLowPrio = "q q 3 int G 1/1-2,8 f ipv4 p u a c 0\r\n";
                send(sock, putIperfLowPrio.c_str(), putIperfLowPrio.length(), 0);
                lowThresholdCounter = 0;
                std::cout << "Prioritized heartbeat to Q7.\n\n";
            }
        }
        switchData.PTxPrio7 = switchData.TxPrio7;
    }
}


int main() {
    while (true) {
        Event fault;
        std::thread thread1(fetchStats, std::ref(fault));
        thread1.join();
    }
    return 0;
}
