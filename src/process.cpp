#include "process.h"

// Process class methods
Process::Process(ProcessDetails details, uint64_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    is_interrupted = false;
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    total_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        total_time += burst_times[i];
    }
    remain_time = total_time;
    
    update_time = 0;
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

uint64_t Process::getBurstStartTime() const
{
    return burst_start_time;
}

Process::State Process::getState() const
{
    return state;
}

bool Process::isInterrupted() const
{
    return is_interrupted;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getTotalRunTime() const
{
    return (double)remain_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}

void Process::setBurstStartTime(uint64_t current_time)
{
    burst_start_time = current_time;
}

void Process::setState(State new_state, uint64_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
    }
    state = new_state;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::interrupt()
{
    is_interrupted = true;
}

void Process::interruptHandled()
{
    is_interrupted = false;
}

// Thomas
/*
    uint16_t pid;               // process ID
    uint32_t start_time;        // ms after program starts that process should be 'launched'
    uint16_t num_bursts;        // number of CPU/IO bursts
    uint16_t current_burst;     // current index into the CPU/IO burst array
    uint32_t *burst_times;      // CPU/IO burst array of times (in ms)
    uint8_t priority;           // process priority (0-4)
    uint64_t burst_start_time;  // time that the current CPU/IO burst began
    State state;                // process state
    bool is_interrupted;        // whether or not the process is being interrupted
    int8_t core;                // CPU core currently running on
    int32_t turn_time;          // total time since 'launch' (until terminated)
    int32_t wait_time;          // total time spent in ready queue
    int32_t cpu_time;           // total time spent running on a CPU core
    int32_t remain_time;        // CPU time remaining until terminated
    int32_t total_time;         // total CPU time for all bursts
    uint64_t launch_time;       // actual time in ms (since epoch) that process was 'launched'

    int32_t update_time;        // time since updateProcess() was called
    enum State : uint8_t { NotStarted, Ready, Running, IO, Terminated };
*/
void Process::updateProcess(uint64_t current_time)
{
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time

    // wait time updated only if waiting
    if(state == State::Ready){
        wait_time = (current_time - update_time); // wait time is how long it is in the READY state (assuming in wait)
    }
    // burst times, CPU, and remain time updated if process is running
    else if(state == State::Running){
        if(remain_time <= update_time){
            remain_time = 0;
            cpu_time += remain_time; // CPU time spent if remain time < update_time
        }
        else{
            remain_time -= update_time;
            cpu_time += update_time; // CPU time spent if remain time > update_time
        }

        if((burst_times[current_burst] - update_time) <= 0){ // finished burst of CPU
            burst_times[current_burst] = 0;
            current_burst++;
        }
        else{
            burst_times[current_burst] -= update_time; // decrement remaining burst time
        }
    }
    // burst times updated if process is in IO
    else if(state == State::IO){
        if(burst_times[current_burst] <= update_time){ // finished burst of IO
            burst_times[current_burst] = 0;
            current_burst++;
        }
        else{
            burst_times[current_burst] -= update_time; // decrement remaining burst time
        }
    }
    // turnaround time updated once Terminated and not after
    else if(state == State::Terminated){
        if(turn_time == 0){
            turn_time = current_time - start_time; // turnaround time defined as time it takes to complete a task
        } // using start_time not launch_time since time from start to launch is waiting
    } 

    update_time = current_time; // update update_time
}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}
