#include <iostream>
#include <thread>

long counter = 0;                 // SHARED: global = one memory location, all threads

void hammer() {
    for (int i = 0; i < 1000000; ++i)
        counter++;                // looks like ONE step. It is NOT.
}

int main() {
    std::thread t1(hammer);
    std::thread t2(hammer);
    t1.join();                    // wait for both to finish
    t2.join();
    std::cout << "expected 2000000, got " << counter << "\n";
}