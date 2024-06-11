/*ETAP 2 - zalozenia:
- pojemnośc windy ma być ograniczona (maksymalnie 3 osoby jednocześnie), klienci czekają na następne windy
- przy obsłudze może być w jednym momencie tylko jedna osoba. Reszta czeka przed miejscem obsługi aż zwolni się zmiejsce
*/

#include <GLFW/glfw3.h>
#include <iostream>
#include <thread>
#include <list>
#include <memory>
#include <mutex>
#include <chrono>
#include <vector>
#include <condition_variable>

using namespace std;
using namespace this_thread;

static bool shouldCloseWindow = false;
vector<int> stanowiska = {0,0,0};
mutex mtx_new_winda;
condition_variable cv_new_winda;

std::condition_variable cv;
mutex mtx_stanowiska;
bool ready_specific = false; // Dodatkowa zmienna kontrolna
int specific_floor;

class Klient;

class Winda {
private:
    int size, dest, stopTime = 4, speed = 2;
    bool active, stoppedAlready, notified = false;
    // NOWE -> ETAP 2
    int maxCapacity = 3;
    int currentOccupancy = 0;
    mutex mtx;

    // żeby sie nie kopiowało bo klasa mutex jest niekopiowalna
    Winda(const Winda&) = delete;
    Winda& operator=(const Winda&) = delete;

public:
    bool canSpawnNext;
    int x, y;

    Winda() {
        x = 350;
        y = 0;
        size = 100;
        active = true;
        canSpawnNext = false;
        dest = rand() % 3 + 1; // piętro 1/2/3
        stoppedAlready = false;
    }

    void move() {
        while (active) {
            sleep_for(chrono::milliseconds(10));
            if (y < 200 * dest - 30 || stoppedAlready) {
                y += speed;
            } else {
                stoppedAlready = true;
                sleep_for(chrono::seconds(stopTime));
                canSpawnNext = true;
            }
            if (y > 800) deactivate();
        }
    }



    int getY() const { return y; }
    int getDest() const { return dest; }
    int getSpeed() const { return speed; }
    bool getStoppedAlready() const { return stoppedAlready; }

    mutex& getMutex() {
        return mtx;
    }

    // NOWE -> ETAP 2
    bool isFull() const { return currentOccupancy >= maxCapacity; }

    void addClient() {
        lock_guard<mutex> lock(mtx);
        if (currentOccupancy < maxCapacity) {
            currentOccupancy++;
        }
    }

    void removeClient() {
        lock_guard<mutex> lock(mtx);
        if (currentOccupancy > 0) {
            currentOccupancy--;
        }
    }


    void draw() const {
        glColor3f(0.0, 1.0, 0.0);
        glBegin(GL_LINES);
        glVertex2i(x, y);
        glVertex2i(x + size, y);
        glVertex2i(x + size, y);
        glVertex2i(x + size, y + size);
        glVertex2i(x + size, y + size);
        glVertex2i(x, y + size);
        glVertex2i(x, y + size);
        glVertex2i(x, y);
        glEnd();
    }

    void deactivate() {
        active = false;
    }
};

class Klient {
private:
    int last_y, x, y, size, fig, speed, workSpace, workTime, /*// NOWE -> ETAP 2*/floor = 0;
    float r, g, b;
    static list<shared_ptr<Winda>>* windy_ptr;
    bool active;
    shared_ptr<Winda> currentElevator = nullptr;
    bool hasntStoppedYet = true;
    bool inElevator = false;
    // NOWE -> ETAP 2
    bool canWork = true;
    bool hasWorkedAlready = false;

    //  eby sie nie kopiowało bo klasa mutex jest niekopiowalna
    Klient(const Klient&) = delete;
    Klient& operator=(const Klient&) = delete;

public:
    Klient() {
        x = 0;
        y = 5;
        size = rand() % 20 + 50;
        fig = rand() % 5 + 1; // 1-5 kwadrat, koło (no koło nie bo trzebaby fora a mozna prosciej inne figury), trapez, itd
        speed = 1 + 2*(rand() % 3);
        r = static_cast<float>(rand()) / RAND_MAX;
        g = static_cast<float>(rand()) / RAND_MAX;
        b = static_cast<float>(rand()) / RAND_MAX;
        active = true;
        workSpace = rand() % (100 - size) + 600;
        workTime = rand() % 5 + 1;
    }

    static void setWindyList(list<shared_ptr<Winda>>& windy_list) {
        windy_ptr = &windy_list;
    }

    void move() {
        while (active) {
            sleep_for(chrono::milliseconds(10));
            last_y = y;
            if (x < 365) {
                x += speed;
            }
            else if (x > 360 && x < 400) {
                if (!inElevator && currentElevator == nullptr && !shouldCloseWindow) {
                    unique_lock<mutex> lock(mtx_new_winda);
                    cv_new_winda.wait(lock);
                    for (auto& winda : *windy_ptr) {
                        if (!winda->isFull() && winda->getY() < 20) {
                            currentElevator = winda;
                            winda->addClient();
                            inElevator = true;
                            floor = currentElevator->getDest() - 1;
                            break;
                        }
                    }
                }
                if (currentElevator != nullptr && inElevator) {
                    unique_lock<mutex> elevator_lock(getElevatorMutex());
                    if (y < currentElevator->getY() + 20) {
                        y += currentElevator->getSpeed() + 1;
                    } else {
                        y = currentElevator->getY() + 20;
                    }
                }
            }
            if (currentElevator != nullptr && currentElevator->getStoppedAlready()) {
                if (x > 400) {
                    currentElevator->removeClient();
                    inElevator = false;
                }
                
                
                // NOWE -> ETAP 2
                if(x <= workSpace && x >= 500 && !hasWorkedAlready){
                    if(stanowiska[floor] == 1){
                        std::unique_lock<std::mutex> lock(mtx_stanowiska);
                        std::cout << "Klient is waiting..." << std::endl;
                        cv.wait(lock, [this] { return ready_specific; }); // Czekaj, aż ready_specific będzie true lub floor będzie równe specific_floor
                        if (ready_specific && this->floor == specific_floor) { // Jeśli ready_specific jest true i floor jest równe specific_floor
                            std::cout << "Klient is awake!" << std::endl;
                            ready_specific = false; // Resetowanie ready_specific
                        } 
                        canWork = false;
                    }
                    else if(stanowiska[floor] < 1){
                        canWork = true;
                        mtx_stanowiska.lock();
                        stanowiska[floor] += 1;
                        mtx_stanowiska.unlock();
                        hasWorkedAlready = true;
                    }
                }
                if(!canWork){x += 0;}
                else x += speed;
            }
            // NOWE -> ETAP 2
            if (x > workSpace && hasntStoppedYet) {
                sleep_for(chrono::seconds(workTime));
                wake_specific_client(floor);
                hasntStoppedYet = false;
                mtx_stanowiska.lock();
                stanowiska[floor] -= 1;
                mtx_stanowiska.unlock();
            }
            if (x > 800) {
                deactivate();
            }
        }
    }
    void wake_specific_client(int floor) {
        std::lock_guard<std::mutex> lock(mtx_stanowiska);
        specific_floor = floor;
        ready_specific = true;
        cv.notify_one();
    }
    mutex& getElevatorMutex() {
        if (currentElevator != nullptr) {
            return currentElevator->getMutex();
        }
        throw runtime_error("Klient nie jest przypisany do żadnej windy");
    }

    void draw(int i) const {
        glColor3f(r, g, b); // Ustawienie koloru na podstawie wartości red, green i blue kwadratu
        switch (fig) {
            case 1:
                glBegin(GL_QUADS); //kwadrat
                glVertex2i(x, y);                // Lewy górny róg
                glVertex2i(x + size, y);         // Prawy górny róg
                glVertex2i(x + size, y + size);  // Prawy dolny róg
                glVertex2i(x, y + size);         // Lewy dolny róg
                glEnd();
                break;
            case 2:
                glBegin(GL_TRIANGLES); //trójkąt
                glVertex2i(x, y);                // Lewy górny róg
                glVertex2i(x + size, y + size);  // Prawy dolny róg
                glVertex2i(x, y + size);         // Lewy dolny róg
                glEnd();
                break;
            case 3:
                glBegin(GL_TRIANGLES);//trójkąt
                glVertex2i(x + size, y);         // Prawy górny róg
                glVertex2i(x + size, y + size);  // Prawy dolny róg
                glVertex2i(x, y + size);         // Lewy dolny róg
                glEnd();
                break;
            case 4:
                glBegin(GL_POLYGON); //trapez
                glVertex2i(x, y);
                glVertex2i(x + size * 0.8, y);
                glVertex2i(x + size, y + size);
                glVertex2i(x + size * 0.2, y + size);
                glEnd();
                break;
            case 5:
                glBegin(GL_POLYGON); //romb
                glVertex2i(x + size * 0.5, y);
                glVertex2i(x + size, y + size * 0.5);
                glVertex2i(x + size * 0.5, y + size);
                glVertex2i(x, y + size * 0.5);
                glEnd();
                break;
        }
    }

    void deactivate() {
        active = false;
    }
};

list<shared_ptr<Winda>>* Klient::windy_ptr = nullptr;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        shouldCloseWindow = true;
        cv_new_winda.notify_all();
    }
}

void create_threads(list<shared_ptr<Klient>>& klienci, list<thread>& c_threads) {
    while (!shouldCloseWindow) {
        auto klient = make_shared<Klient>();
        klienci.push_back(klient);
        c_threads.push_back(thread(&Klient::move, klient));
        sleep_for(chrono::seconds(rand()%3+1));
    }
}

void create_winda_threads(list<shared_ptr<Winda>>& windy, list<thread>& w_threads) {
    while(!shouldCloseWindow){
       if (windy.empty() || windy.back()->canSpawnNext) {
            auto winda = make_shared<Winda>();
            windy.push_back(winda);
            w_threads.push_back(thread(&Winda::move, winda));

            lock_guard<mutex> lock(mtx_new_winda);
            cv_new_winda.notify_all();
       }
        sleep_for(chrono::milliseconds(10));
    }
}

void drawClients(const list<shared_ptr<Klient>>& klienci) {
    int i = 0;
    for (const auto& klient : klienci) {
        klient->draw(i);
        i++;
    }
}

void drawWindy(const list<shared_ptr<Winda>>& windy) {
    for (const auto& winda : windy) {
        winda->draw();
    }
}

void drawEnv(){

    glColor3f(0.7, 0.7, 0.7);
    glBegin(GL_QUADS);

    //lewa strona
    glVertex2i(0, 70);
    glVertex2i(350, 70);
    glVertex2i(350, 800);
    glVertex2i(0, 800);

    //prawa strona
    glVertex2i(450, 0);
    glVertex2i(800, 0);
    glVertex2i(800, 170);
    glVertex2i(450, 170);

    //pietra
    glVertex2i(450, 270);
    glVertex2i(800, 270);
    glVertex2i(800, 370);
    glVertex2i(450, 370);

    glVertex2i(450, 470);
    glVertex2i(800, 470);
    glVertex2i(800, 570);
    glVertex2i(450, 570);

    glVertex2i(450, 670);
    glVertex2i(800, 670);
    glVertex2i(800, 800);
    glVertex2i(450, 800);

    //miejsca pracy
    glColor3f(1.0, 0.0, 0.0);
    glVertex2i(600, 270);
    glVertex2i(700, 270);
    glVertex2i(700, 300);
    glVertex2i(600, 300);

    glColor3f(0.0, 1.0, 0.0);
    glVertex2i(600, 470);
    glVertex2i(700, 470);
    glVertex2i(700, 500);
    glVertex2i(600, 500);

    glColor3f(.0, 0.0, 1.0);
    glVertex2i(600, 670);
    glVertex2i(700, 670);
    glVertex2i(700, 700);
    glVertex2i(600, 700);

    glEnd();
}

int main() {
    srand(time(NULL));

    list<shared_ptr<Klient>> klienci;
    list<thread> c_threads;
    list<shared_ptr<Winda>> windy;
    list<thread> w_threads;

    Klient::setWindyList(windy);

    // Inicjalizacja biblioteki GLFW
    if (!glfwInit()) {
        return -1;
    }

    // Utworzenie okna GLFW
    GLFWwindow* window = glfwCreateWindow(800, 800, "OpenGL Window", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwSetWindowSizeLimits(window, 800, 800, 800, 800); // Ustawienie stałego rozmiaru
    glfwSetWindowAspectRatio(window, 1, 1); // Blokowanie zmiany proporcji
    glfwMakeContextCurrent(window);

    // Inicjalizacja widoku OpenGL
    glOrtho(0, 800, 800, 0, -1, 1); // Ustawienie przestrzeni współrzędnych OpenGL

    // Ustawienie funkcji zwrotnej dla naciśnięć klawiszy
    glfwSetKeyCallback(window, key_callback);

    thread makeNewThreads(create_threads, ref(klienci), ref(c_threads));
    thread makeNewWindy(create_winda_threads, ref(windy), ref(w_threads));

    // Główna pętla programu
    while (!shouldCloseWindow) {
        // Czyszczenie bufora kolorów
        glClear(GL_COLOR_BUFFER_BIT);

        drawClients(klienci);
        drawEnv();
        drawWindy(windy);

        // Zamiana buforów
        glfwSwapBuffers(window);

        // Obsługa zdarzeń okna
        glfwPollEvents();
    }

    for (auto& k : klienci) {
        k->deactivate();
    }
    for (auto& w : windy) {
        w->deactivate();
    }

    makeNewThreads.join();
    makeNewWindy.join();

    for (auto& t : c_threads) {
        t.join();
    }
    for (auto& t : w_threads) {
        t.join();
    }

    // Zwolnienie pamięci zaalokowanej dla obiektów klientów
    klienci.clear();
    windy.clear();

    // Zakończenie biblioteki GLFW
    glfwTerminate();
    return 0;
}
