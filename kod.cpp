/*ETAP 2 - zalozenia:
- pojemnośc windy ma być ograniczona (maksymalnie 3 osoby jednocześnie), klienci czekają na następne windy
- przy obsłudze może być w jednym momencie tylko jedna osoba. Reszta czeka przed miejscem obsługi aż zwolni się zmiejsce
- klienci wchodzą do windy według określonej kolejności - najpierw wchodzą najwolniejsi klienci
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
#include <queue>

using namespace std;
using namespace this_thread;

static bool shouldCloseWindow = false;
static bool shouldTerminate = false;
vector<int> stanowiska = {0,0,0};
vector<mutex> stanowiska_mtx(3);
vector<condition_variable> stanowiska_cv(3);

mutex mtx_new_winda;
condition_variable cv_new_winda;

std::condition_variable cv;
mutex mtx_stanowiska;
bool ready_specific = false;
int specific_floor;

class Klient;

class Winda {
private:
    int size, dest, stopTime = 4, speed = 2;
    bool active, stoppedAlready, notified = false;
    int maxCapacity = 3;
    int currentOccupancy = 0;
    mutex mtx;

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
        dest = rand() % 3 + 1;
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

class Klient : public enable_shared_from_this<Klient>{
private:

    int last_y, x, y, size, fig, speed, workSpace, workTime, floor = 0;
    struct CompareClients {
        bool operator()(const shared_ptr<Klient>& a, const shared_ptr<Klient>& b) {
            return a->speed > b->speed; 
        }
    };
    float r, g, b;
    static list<shared_ptr<Winda>>* windy_ptr;
    static priority_queue<shared_ptr<Klient>, vector<shared_ptr<Klient>>, CompareClients> clientQueue;
    static mutex mtx_queue;
    bool active;
    shared_ptr<Winda> currentElevator = nullptr;
    bool hasntStoppedYet = true;
    bool inElevator = false;
    bool canWork = true;
    bool hasWorkedAlready = false;

    Klient(const Klient&) = delete;
    Klient& operator=(const Klient&) = delete;

public:
    Klient() {
        x = 0;
        y = 5;
        size = rand() % 20 + 50;
        fig = rand() % 5 + 1;
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
            if (shouldTerminate) {
                break;
            }
            sleep_for(chrono::milliseconds(10));
            last_y = y;
            if (x < 365) {
                x += speed;
            } else if (x > 360 && x < 400) {
                if (!inElevator && currentElevator == nullptr && !shouldCloseWindow) {
                    addClientToQueue(shared_from_this());
                    unique_lock<mutex> lock(mtx_new_winda);
                    cv_new_winda.wait(lock, [this]() {
                        if (shouldTerminate) return true;
                        auto nextClient = getNextClientFromQueue();
                        if (nextClient.get() != this) {
                            return false;
                        }

                        for (auto& winda : *windy_ptr) {
                            if (!winda->isFull() && winda->getY() < 20) {
                                currentElevator = winda;
                                winda->addClient();
                                inElevator = true;
                                floor = currentElevator->getDest() - 1;
                                removeClientFromQueue();
                                if(!winda->isFull() && clientQueue.size() > 0) cv_new_winda.notify_all();

                                return true;
                            }
                        }
                        return false;
                    });
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

                if (x <= workSpace && x >= 500 && !hasWorkedAlready) {
                    if (stanowiska[floor] == 1) {
                        std::unique_lock<std::mutex> lock(stanowiska_mtx[floor]);
                        stanowiska_cv[floor].wait(lock, [this, floor_copy = floor] { return shouldTerminate || stanowiska[floor_copy] == 0; });
                        if (shouldTerminate) break;
                        canWork = true;
                    } else if (stanowiska[floor] < 1) {
                        canWork = true;
                        std::lock_guard<std::mutex> lock(stanowiska_mtx[floor]);
                        stanowiska[floor] += 1;
                        hasWorkedAlready = true;
                    }
                }
                if (!canWork) {
                    x += 0;
                } else {
                    x += speed;
                }
            }

            if (x > workSpace && hasntStoppedYet) {
                sleep_for(chrono::seconds(workTime));
                stanowiska_mtx[floor].lock();
                stanowiska[floor] -= 1;
                stanowiska_cv[floor].notify_one();
                stanowiska_mtx[floor].unlock();
                hasntStoppedYet = false;
            }
            if (x > 800) {
                deactivate();
            }
        }
    }

    static void addClientToQueue(shared_ptr<Klient> client) {
        lock_guard<mutex> lock(mtx_queue);
        clientQueue.push(client);
        cout << "ADDED " << client->speed << ": ";
        client->printClientQueue(clientQueue);
    }

    static void removeClientFromQueue() {
        lock_guard<mutex> lock(mtx_queue);
        if (!clientQueue.empty()) {
            shared_ptr<Klient> c = clientQueue.top();
            clientQueue.pop();
            cout << "REMOVED " << c->speed << ": ";
            c->printClientQueue(clientQueue);
        }
    }

    static shared_ptr<Klient> getNextClientFromQueue() {
        lock_guard<mutex> lock(mtx_queue);
        if (!clientQueue.empty()) {
            return clientQueue.top();
        }
        return nullptr;
    }

    mutex& getElevatorMutex() {
        if (currentElevator != nullptr) {
            return currentElevator->getMutex();
        }
        throw runtime_error("Klient nie jest przypisany do żadnej windy");
    }


    void printClientQueue(priority_queue<shared_ptr<Klient>, vector<shared_ptr<Klient>>, CompareClients> clientsQueue) {
        priority_queue<shared_ptr<Klient>, vector<shared_ptr<Klient>>, CompareClients> tempQueue = clientsQueue;

        cout << "[ ";
        while (!tempQueue.empty()) {
            shared_ptr<Klient> client = tempQueue.top();
            tempQueue.pop();
            
            cout << client->speed << " ";
        }
        cout <<"]" << endl;
    }


    void draw(int i) const {
        glColor3f(r, g, b);
        switch (fig) {
            case 1:
                glBegin(GL_QUADS);
                glVertex2i(x, y);
                glVertex2i(x + size, y);
                glVertex2i(x + size, y + size);
                glVertex2i(x, y + size);
                glEnd();
                break;
            case 2:
                glBegin(GL_TRIANGLES);
                glVertex2i(x, y);
                glVertex2i(x + size, y + size);
                glVertex2i(x, y + size);
                glEnd();
                break;
            case 3:
                glBegin(GL_TRIANGLES);
                glVertex2i(x + size, y);
                glVertex2i(x + size, y + size);
                glVertex2i(x, y + size);
                glEnd();
                break;
            case 4:
                glBegin(GL_POLYGON);
                glVertex2i(x, y);
                glVertex2i(x + size * 0.8, y);
                glVertex2i(x + size, y + size);
                glVertex2i(x + size * 0.2, y + size);
                glEnd();
                break;
            case 5:
                glBegin(GL_POLYGON);
                glVertex2i(x + size * 0.5, y);
                glVertex2i(x + size, y + size * 0.3);
                glVertex2i(x + size * 0.8, y + size);
                glVertex2i(x + size * 0.2, y + size);
                glVertex2i(x, y + size * 0.3);
                glEnd();
                break;
        }
    }

    void deactivate() {
        active = false;
    }
};

list<shared_ptr<Winda>>* Klient::windy_ptr = nullptr;
priority_queue<shared_ptr<Klient>, vector<shared_ptr<Klient>>, Klient::CompareClients> Klient::clientQueue;
mutex Klient::mtx_queue;

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
        sleep_for(chrono::seconds(rand()%4+1));
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

    shouldTerminate = true;
    for (auto& cv : stanowiska_cv) {
        cv.notify_all();
    }
    cv_new_winda.notify_all();

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
