// compile: make
// run: ./bin/osscheduler ./resrc/config_01.txt
#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <ncurses.h>
#include "configreader.h"
#include "process.h"

// Algos to Implement
// PP - Thomas
// RR, FCFS, SJF - Andres

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex queue_mutex;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
void printProcessOutput(std::vector<Process*>& processes);
void insertIntoReadyQueue(SchedulerData *shared_data, Process *p);
std::string makeProgressString(double percent, uint32_t width);
uint64_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char *argv[])
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data = new SchedulerData();
    std::vector<Process*> processes;
    uint64_t end_time = 0;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = scr::readConfigFile(argv[1]);

    // Store number of cores in local variable for future access
    uint8_t num_cores = config->cores;

    // Store configuration parameters in shared data object
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            //shared_data->ready_queue.push_back(p);
            insertIntoReadyQueue(shared_data, p); // even launched processes should follow algo
        }
    }

    // Free configuration data from memory
    scr::deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // Main thread work goes here
    initscr();
    while (!(shared_data->all_terminated))
    {
        // Do the following:
        //   - Get current time
        uint64_t time = currentTime();

        //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
        for(int i = 0; i < processes.size(); i++){
            // If not started process should be launched now, add to ready queue
            if((time-start >= processes[i]->getStartTime()) && (processes[i]->getState() == Process::State::NotStarted)){
                processes[i]->setState(Process::State::Ready, time);
                shared_data->queue_mutex.lock();
                insertIntoReadyQueue(shared_data, processes[i]);
                shared_data->queue_mutex.unlock();
            }
        }

        //   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
        for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::IO) {
                processes[i]->updateProcess(time); // make progress in IO burst
                if (processes[i]->getState() == Process::State::Ready) { // add back into the ready queue
                    shared_data->queue_mutex.lock();
                    insertIntoReadyQueue(shared_data, processes[i]);
                    shared_data->queue_mutex.unlock();
                }
            }
        }

        int num_of_running = 0;
        int *indicies = new int[num_cores];
        int *priorities = new int[num_cores];

        // update wait times
        for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::Ready) {
                processes[i]->updateProcess(time);
            }
            
            // for interrupt logic to know all running processes and their priorities
            if (processes[i]->getState() == Process::State::Running){
                indicies[num_of_running] = i; // index of first process running
                priorities[num_of_running] = processes[i]->getPriority(); // priority level
                num_of_running++;
            }
        }


        //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
        //     - NOTE: ensure processes are inserted into the ready queue at the proper position based on algorithm
        int max_priority = 100; // variable that holds the highest priority (low number) of a process in the ready queue
        if(shared_data->algorithm == ScheduleAlgorithm::PP){
            shared_data->queue_mutex.lock();
            // get highest priority of ready processes
            std::list<Process*>::iterator readyqueue; // have to use this to iterate through the ready_queue list
            for (readyqueue = shared_data->ready_queue.begin(); readyqueue != shared_data->ready_queue.end(); readyqueue++){
                Process *p = *readyqueue;
                if(p->getPriority() < max_priority){
                    max_priority = p->getPriority();
                }
            }
            shared_data->queue_mutex.unlock();

            int min_priority = -1; // variable that holds the lowest priority (high number) of a running process
            int min_index = -1; // variable that holds the index of the min_priority

            // prempt a process if no open core and there is a process in the queue
            if(num_of_running == num_cores && max_priority < 100){
                // look for the lowest priority running process
                for(int i = 0; i < num_of_running; i++){
                    if(priorities[i] > min_priority){
                        min_priority = priorities[i];
                        min_index = indicies[i];
                    }
                }

                if(max_priority < min_priority){
                    processes[min_index]->interrupt();
                }
            }
        }

        //   - Determine if all processes are in the terminated state
        for (int i = 0; i < processes.size(); i++) {
            if (processes[i]->getState() == Process::State::Terminated) {
                if(i == processes.size()-1){
                    shared_data->all_terminated = true;
                    end_time = currentTime();
                }
            }
            else{
                break;
            }
        }

        delete[] indicies;
        delete[] priorities;
        
        printProcessOutput(processes);

        // sleep 50 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // clear outout
        erase();
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    // Thomas
    // print final statistics (use `printw()` for each print, and `refresh()` after all prints)
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time
    double total_cpu_time = 0;
    double total_waiting_time = 0;
    double total_turnaround_time = 0;
    double total_time = 0;

    // Throughputs (will work as long as num processes > 1)
    double *finish_times = new double[processes.size()];
    for (int i = 0; i < processes.size(); i++) {
        total_cpu_time += 1000 * processes[i]->getCpuTime(); // converted to ms as end_time - start is in ms
        total_waiting_time += processes[i]->getWaitTime();
        total_turnaround_time += processes[i]->getTurnaroundTime();
        total_time += processes[i]->getTotalRunTime();

        // finish time is time it took to run the process (turn_time) + start time
        finish_times[i] = processes[i]->getTurnaroundTime() + (processes[i]->getStartTime())/1000.0;
    }
    printw("CPU Utilization %.2f\n", ((total_cpu_time)/(double)(num_cores * (end_time - start))));
    
    std::sort(finish_times, finish_times + processes.size()); // have an array of time took to complete each process in increasing order
    int mid = processes.size() / 2;
    double time_for_half = finish_times[mid - 1];
    double time_for_last_half = finish_times[processes.size() - 1] - time_for_half;

    // first half = half of the processes / time taken to complete the first half
    printw("Throughput First Half %.2f\n", (double)(mid) / time_for_half);

    // second half is the remaining processes / time taken to complete them
    printw("Throughput Second Half %.2f\n", (double)(processes.size() - mid) / time_for_last_half);

    // total num of processes / total time taken for all processes / 1000 (ms to seconds)
    printw("Overall Throughput %.2f\n", ((double)(processes.size()*1000)/(double)(end_time - start)));

    printw("Average Wait Time %.2f\n", (total_waiting_time/(double)(processes.size())));
    printw("Average Turnaround Time %.2f\n", (total_turnaround_time/(double)(processes.size())));
    refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(15000)); // wait so I can see the results

    // Clean up before quitting program
    delete[] finish_times;
    processes.clear();
    endwin();

    return 0;
}

void insertIntoReadyQueue(SchedulerData *shared_data, Process *p){
    if(shared_data->algorithm == ScheduleAlgorithm::SJF){

    }
    else if(shared_data->algorithm == ScheduleAlgorithm::RR){

    }
    else if(shared_data->algorithm == ScheduleAlgorithm::PP){
        std::list<Process*>::iterator readyqueue; // have to use this to iterate through the ready_queue list
        for (readyqueue = shared_data->ready_queue.begin(); readyqueue != shared_data->ready_queue.end(); readyqueue++){
            // increment through the ready queue until the current process is a higher priority
            if((*readyqueue)->getPriority() > p->getPriority()){
                break;
            }
        }
        shared_data->ready_queue.insert(readyqueue, p);
    }
    else { // do FCFS by default
        shared_data->ready_queue.push_back(p);
    }
}

// Thomas
void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core idependent of the other cores
    // Repeat until all processes in terminated state:
    while(shared_data->all_terminated == false){
        
        //   - IF READY QUEUE WAS NOT EMPTY
        shared_data->queue_mutex.lock();
        if(!shared_data->ready_queue.empty()){
            //   - *Get process at front of ready queue
            Process *p = shared_data->ready_queue.front();
            shared_data->ready_queue.pop_front(); // remove from the front of the queue
            shared_data->queue_mutex.unlock();
            
            //    - Wait context switching load time
            std::this_thread::sleep_for(std::chrono::milliseconds(shared_data->context_switch));
            
            p->setCpuCore(core_id); // put in on a core
            p->setState(Process::State::Running, currentTime()); // now proccess is running

            //    - Simulate the processes running (i.e. sleep for short bits, e.g. 5 ms, and call the processes `updateProcess()` method)
            //      until one of the following:
            //      - CPU burst time has elapsed
            //      - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
            while((p->getState() == Process::State::Running) && (p->isInterrupted() == false)){
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                p->updateProcess(currentTime());
            }

            // This all happens in updateProcess()
            //   - Place the process back in the appropriate queue
            //      - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
            //      - Terminated if CPU burst finished and no more bursts remain -- set state to Terminated

            //      - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
            if(p->isInterrupted() == true){
                shared_data->queue_mutex.lock();
                p->updateProcess(currentTime()); // modify CPU burst time
                insertIntoReadyQueue(shared_data, p); // push back onto ready queue
                p->interruptHandled();
                shared_data->queue_mutex.unlock();
            }
            p->setCpuCore(-1); // take process off the CPU
            //   - Wait context switching save time
            std::this_thread::sleep_for(std::chrono::milliseconds(shared_data->context_switch));
        }
            //  - IF READY QUEUE WAS EMPTY
        else{
            shared_data->queue_mutex.unlock();
            //   - Wait short bit (i.e. sleep 5 ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void printProcessOutput(std::vector<Process*>& processes)
{
    printw("|   PID | Priority |    State    | Core |               Progress               |\n"); // 36 chars for prog
    printw("+-------+----------+-------------+------+--------------------------------------+\n");
    for (int i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double total_time = processes[i]->getTotalRunTime();
            double completed_time = total_time - processes[i]->getRemainingTime();
            std::string progress = makeProgressString(completed_time / total_time, 36);
            printw("| %5u | %8u | %11s | %4s | %36s |\n", pid, priority,
                   process_state.c_str(), cpu_core.c_str(), progress.c_str());
        }
    }
    refresh();
}

std::string makeProgressString(double percent, uint32_t width)
{
    uint32_t n_chars = percent * width;
    std::string progress_bar(n_chars, '#');
    progress_bar.resize(width, ' ');
    return progress_bar;
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
