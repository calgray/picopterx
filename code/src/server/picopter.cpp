/**
 * @file picopter.cpp
 * @brief The main entry point to the server.
 */
 
#include "picopter.h"
 
/**
  * @brief A test class.
  */
class TESTER {
    public:
   
    /**
     * @brief A test method
     * IT DOES NOTHING
     */
    void test() { Log(LOG_WARNING, "HAHA"); }
};
 
int main(int argc, char *argv[]) {
    Log(LOG_NOTICE, "BUZZER TEST STARTED");
    picopter::Buzzer b;
    picopter::GPS gps;
    
    b.play(1000, 200, 80);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    b.play(1000, 100, 80);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    b.play(1000, 1200, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    
    std::cout << gps.timeSinceLastFix();
    return 0;
}