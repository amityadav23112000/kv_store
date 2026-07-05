#include <iostream>
#include "../protocol.h"
int main() {
    KVStore s;
    std::cout << process_command(s, "SET name amit");
    std::cout << process_command(s, "GET name");
    std::cout << process_command(s, "SET msg hello world with spaces");
    std::cout << process_command(s, "GET msg");
    std::cout << process_command(s, "DEL name");
    std::cout << process_command(s, "DEL name");
    std::cout << process_command(s, "GET name");
    std::cout << process_command(s, "PING something");
    std::cout << process_command(s, "SET onlykey");
}
