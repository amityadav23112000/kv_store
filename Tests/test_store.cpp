#include <iostream>
#include "../kvstore.h"     // quotes = "my file", <> = system headers

int main() {
    KVStore store;
    store.set("name", "amit");

    auto v = store.get("name");
    std::cout << "GET name -> " << (v ? *v : "(nil)") << "\n";   // *v unpacks the optional

    std::cout << "DEL name -> " << (store.del("name") ? "deleted" : "not found") << "\n";

    v = store.get("name");
    std::cout << "GET name -> " << (v ? *v : "(nil)") << "\n";
}