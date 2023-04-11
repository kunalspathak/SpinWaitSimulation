
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>
#include <conio.h>
#include <chrono>


int g_proc_index = 0;
int g_worker_core = -1;
int g_mainthread_core = -1;
bool g_use_low_power_p = false;

const unsigned HS_CACHE_LINE_SIZE = 128;

struct __declspec(align(HS_CACHE_LINE_SIZE)) aligned_location
{
    volatile size_t loc;
    __declspec(align(HS_CACHE_LINE_SIZE)) size_t extra_field;
};

volatile aligned_location g_aligned_global_location;

ULONGLONG g_timeout = 100000;

uint64_t g_waited_count;
HANDLE g_hThread;
// these are measured with OS APIs.
uint64_t g_umode_cpu_time, g_kmode_cpu_time, g_elapsed_time, g_total_cycles;
// these are measured on the thread. 
uint64_t g_total_cycles_on_thread;
uint64_t g_elapsed_time_us_on_thread;


const char* formatNumber(uint64_t input)
{
    int64_t saved_input = input;
    char* buffer = (char*)malloc(sizeof(char) * 100);
    //sprintf_s(buffer, 100, "%4.2f%c", scaledInput, scaledUnit);

    int base = 10;
    int char_index = 0;
    int num_chars = 0;
    while (char_index < 100)
    {
        if (input)
        {
            buffer[char_index] = (char)(input % base + '0');
            input /= 10;
            num_chars++;

            //printf("input is %I64d, num_chars %d, char_index %d->%c\n", input, num_chars, char_index, buffer[char_index]);

            if (num_chars == 3)
            {
                //printf("setting char at index %d to ,\n", (char_index + 1));
                buffer[++char_index] = ',';
                num_chars = 0;
            }
            char_index++;
        }
        else
        {
            break;
        }
    }

    if (char_index > 0)
    {
        if (buffer[char_index - 1] == ',')
        {
            char_index--;
        }
    }

    if (char_index == 0)
    {
        buffer[char_index++] = '0';
    }
    buffer[char_index] = '\0';

    //printf("setting char at index %d to null, now string is %s\n", char_index, buffer);

    for (int i = 0; i < (char_index / 2); i++)
    {
        char c = buffer[i];
        buffer[i] = buffer[char_index - 1 - i];
        buffer[char_index - 1 - i] = c;
    }

    //printf("input is %I64d -> %s\n", saved_input, buffer);

    return buffer;
}

void PrintTime(HANDLE h, const char* msg, size_t waited_count)
{
    // FILETIME is in 100ns intervals, so (t / 10) is 1us
    FILETIME create_t, exit_t, kernel_t, user_t;
    if (!GetThreadTimes(h, &create_t, &exit_t, &kernel_t, &user_t))
    {
        printf("GetThreadTimes failed with %d\n", GetLastError());
        return;
    }//if

    const ULONGLONG umode_cpu_time = *reinterpret_cast<const ULONGLONG*>(&user_t);
    const ULONGLONG kmode_cpu_time = *reinterpret_cast<const ULONGLONG*>(&kernel_t);
    const ULONGLONG start_time = *reinterpret_cast<const ULONGLONG*>(&create_t);
    const ULONGLONG end_time = *reinterpret_cast<const ULONGLONG*>(&exit_t);

    printf("umode cpu %I64dms, kmode cpu %I64dms, elapsed time %I64dms\n", (umode_cpu_time / 10000), (kmode_cpu_time / 10000), ((end_time - start_time) / 10000));

    uint64_t cycles = 0;
    QueryThreadCycleTime(h, &cycles);
    printf("%s cycles total, %I64d/iteration\n", formatNumber(cycles), (cycles / waited_count));

    g_umode_cpu_time = umode_cpu_time;
    g_kmode_cpu_time = kmode_cpu_time;
    g_elapsed_time = end_time - start_time;
    g_total_cycles = cycles;
}

DWORD WINAPI ThreadFunction_pause(LPVOID lpParam)
{
    unsigned int EFLAGS = __getcallerseflags();
    printf("eflags are %d, IF bit is %s\n", EFLAGS, ((EFLAGS & 0x200) ? "enabled" : "disabled"));
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id\n", original_value);
    uint64_t start = __rdtsc();

    while (g_aligned_global_location.loc == original_value)
    {
        YieldProcessor();
        waited_count++;
    }

    g_waited_count = waited_count;

    uint64_t elapsed = __rdtsc() - start;
    //printf("[%10s] changed to %Id and waited %Is times, %I64d cycles elapsed!\n", 
    //    "pause", g_aligned_global_location.loc, formatNumber(waited_count), elapsed);

    return 0;
}

DWORD WINAPI ThreadFunction_tpause_low_power(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);

    while (g_aligned_global_location.loc == original_value)
    {
        int64_t tsc = __rdtsc();
        _tpause(0, (tsc + g_timeout));
        waited_count++;
    }

    //printf("[%10s] changed to %Id and waited %s times!\n", "tpause", g_aligned_global_location.loc, formatNumber(waited_count));
    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_tpause_high_power(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);

    while (g_aligned_global_location.loc == original_value)
    {
        int64_t tsc = __rdtsc();
        _tpause(1, (tsc + g_timeout));
        waited_count++;
    }

    //printf("[%10s] changed to %Id and waited %s times!\n", "tpause", g_aligned_global_location.loc, formatNumber(waited_count));
    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_umwait_low_power(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);

    while (g_aligned_global_location.loc == original_value)
    {
        _umonitor((void*)&g_aligned_global_location.loc);
        int64_t tsc = __rdtsc();
        _umwait(0, (tsc + g_timeout));
        waited_count++;
    }

    //printf("[%10s] changed to %Id and waited %s times!\n", "umwait", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_umwait_high_power(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);

    while (g_aligned_global_location.loc == original_value)
    {
        _umonitor((void*)&g_aligned_global_location.loc);
        int64_t tsc = __rdtsc();
        _umwait(1, (tsc + g_timeout));
        waited_count++;
    }

    //printf("[%10s] changed to %Id and waited %s times!\n", "umwait", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_mwaitx(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);
    uint64_t start_cycles = __rdtsc();
    auto time_start = std::chrono::steady_clock::now();

    while (g_aligned_global_location.loc == original_value)
    {
        _mm_monitorx((const void*)&g_aligned_global_location.loc, 0, 0);

        if (g_aligned_global_location.loc == original_value) {
            waited_count++;
            _mm_mwaitx(2, 0, (uint32_t)g_timeout);
        }
    }

    g_total_cycles_on_thread = __rdtsc() - start_cycles;
    g_elapsed_time_us_on_thread = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - time_start).count();

    printf("[%10s] changed to %Id and waited %s times!\n", "mwaitx", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_monitorx(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);
    uint64_t start_cycles = __rdtsc();
    auto time_start = std::chrono::steady_clock::now();

    while (g_aligned_global_location.loc == original_value)
    {
        _mm_monitorx((const void*)&g_aligned_global_location.loc, 0, 0);

        if (g_aligned_global_location.loc == original_value) {
            waited_count++;
        }
    }

    g_total_cycles_on_thread = __rdtsc() - start_cycles;
    g_elapsed_time_us_on_thread = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - time_start).count();

    printf("[%10s] changed to %Id and waited %s times!\n", "monitorx", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_umonitor(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);
    uint64_t start_cycles = __rdtsc();
    auto time_start = std::chrono::steady_clock::now();

    while (g_aligned_global_location.loc == original_value)
    {
        _umonitor((void*)&g_aligned_global_location.loc);
    }

    g_total_cycles_on_thread = __rdtsc() - start_cycles;
    g_elapsed_time_us_on_thread = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - time_start).count();

    printf("[%10s] changed to %Id and waited %s times!\n", "monitorx", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

DWORD WINAPI ThreadFunction_mwaitx2(LPVOID lpParam)
{
    size_t original_value = g_aligned_global_location.loc;
    size_t waited_count = 0;
    printf("original value is %Id, timeout is %I64d\n", original_value, g_timeout);

    while (g_aligned_global_location.loc == original_value)
    {
        _mm_monitorx((const void*)&g_aligned_global_location.loc, 0, 0);

        if (g_aligned_global_location.loc == original_value) {
            _mm_mwaitx(2, 0, (uint32_t)g_timeout);
            waited_count++;
        }
    }

    printf("[%10s] changed to %Id and waited %s times!\n", "mwaitx", g_aligned_global_location.loc, formatNumber(waited_count));

    g_waited_count = waited_count;
    return 0;
}

uint64_t g_total_iter = 10000;
uint64_t sum = 0;

void inc_only()
{
    uint64_t counter = 0;
    uint64_t start = __rdtsc();

    for (uint64_t i = 0; i < g_total_iter; i++)
    {
        counter += i * 2;
    }

    uint64_t elapsed = __rdtsc() - start;
    sum = counter;

    printf("[%15s] %Id iterations took %15s cycles(%5s/iter), sum is %30s\n", "inc_only",
        g_total_iter, formatNumber(elapsed), formatNumber(elapsed / g_total_iter), formatNumber(sum));
}

void inc_with_pause()
{
    uint64_t counter = 0;
    uint64_t start = __rdtsc();

    for (uint64_t i = 0; i < g_total_iter; i++)
    {
        counter += i * 2;
        YieldProcessor();
    }

    uint64_t elapsed = __rdtsc() - start;
    sum = counter;

    printf("[%15s] %Id iterations took %15s cycles(%5s/iter), sum is %30s\n", "inc_with_pause",
        g_total_iter, formatNumber(elapsed), formatNumber(elapsed / g_total_iter), formatNumber(sum));
}

void parse_cmd_args(int argc, char** argv)
{
    for (int arg_index = 1; arg_index < argc; arg_index)
    {
        if (!strcmp(argv[arg_index], "-ti"))
        {
            g_total_iter = atoi(argv[++arg_index]);
        }
        else if (!strcmp(argv[arg_index], "-timeout"))
        {
            g_timeout = atoi(argv[++arg_index]);
        }
        else if (!strcmp(argv[arg_index], "-proc"))
        {
            g_proc_index = atoi(argv[++arg_index]);
        }
        else if (!strcmp(argv[arg_index], "-worker-core"))
        {
            g_worker_core = atoi(argv[++arg_index]);
        }
        else if (!strcmp(argv[arg_index], "-main-core"))
        {
            g_mainthread_core = atoi(argv[++arg_index]);
        }
        else if (!strcmp(argv[arg_index], "-low-power"))
        {
            g_use_low_power_p = (atoi(argv[++arg_index]) == 1);
        }

        ++arg_index;
    }
}

void SetThreadAffinity(HANDLE tHandle, int procNum)
{
    GROUP_AFFINITY ga;
    ga.Group = (WORD)(procNum >> 6);
    ga.Reserved[0] = 0; // reserve must be filled with zero
    ga.Reserved[1] = 0; // otherwise call may fail
    ga.Reserved[2] = 0;
    ga.Mask = (size_t)1 << (procNum % 64);

/**********************************************************************************
* For high core machines (like the one we are experimenting with), the hierarchy
* looks like below:
* /----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\
* | Machine (242GB total)                                                                                                                                                                                                |
* |                                                                                                                                                                                                                      |
* | /------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\ |
* | | Group #0                                                                                                                                                                                                         | |
* | |                                                                                                                                                                                                                  | |
* | | /--------------------------------------------------------------\  /--------------------------------------------------------------\              /--------------------------------------------------------------\ | |
* | | | NUMANode P#0 (13GB)                                          |  | NUMANode P#2 (15GB)                                          |  ++  ++  ++  | NUMANode P#16(14GB)                                          | | |
* | | \--------------------------------------------------------------/  \--------------------------------------------------------------/              \--------------------------------------------------------------/ | |
* | |                                                                                                                                      8x total                                                                    | |
* | |                                                                                                                                                                                                                  | |
* | |                                                                                                                                                                                                                  | |
* | | /--------------\  /--------------\              /--------------\  /--------------\  /--------------\              /--------------\              /--------------\  /--------------\              /--------------\ | |
* | | | Core         |  | Core         |  ++  ++  ++  | Core         |  | Core         |  | Core         |  ++  ++  ++  | Core         |              | Core         |  | Core         |  ++  ++  ++  | Core         | | |
* | | |              |  |              |              |              |  |              |  |              |              |              |              |              |  |              |              |              | | |
* | | | /----------\ |  | /----------\ |   8x total   | /----------\ |  | /----------\ |  | /----------\ |   8x total   | /----------\ |              | /----------\ |  | /----------\ |   8x total   | /----------\ | | |
* | | | |  PU P#0  | |  | |  PU P#1  | |              | |  PU P#7  | |  | |  PU P#8  | |  | |  PU P#9  | |              | | PU P#15  | |              | | PU P#88  | |  | | PU P#89  | |              | | PU P#95  | | | |
* | | | \----------/ |  | \----------/ |              | \----------/ |  | \----------/ |  | \----------/ |              | \----------/ |              | \----------/ |  | \----------/ |              | \----------/ | | |
* | | \--------------/  \--------------/              \--------------/  \--------------/  \--------------/              \--------------/              \--------------/  \--------------/              \--------------/ | |
* | \------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------/ |
* |                                                                                                                                                                                                                      |
* | /------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\ |
* | | Group #1                                                                                                                                                                                                    | |
* | |                                                                                                                                                                                                                  | |
* | |        ...............
* | \------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------/ |
* \----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------/
*
* A machine has packages (usually 2, but may be higher) and they are sometimes referred
* to as groups. Each group has various NUMA nodes. The presence of NUMA nodes are not
* necessarily sequential. In above example, all the even numbered NUMA nodes are present
* in group #0 and odd-numbered are present in group #1. To find which NUMA nodes are present
* in which group, follow the instructions:
* Task Manager -> Detail -> right-click on any non-admin process -> Set affinity
* If the machine is multi-package or multi-group NUMA aware machine, you would see a drop-down
* to select group and the corresponding NUMA nodes will show up. Each NUMA node contains the 
* cores (typically 8 cores).
*           Group 0
*               NUMA 0      0 ~ 7
*               NUMA 2      16 ~ 23
*               NUMA 4      32 ~ 39
*               .....
*           Group 1
*               NUMA 1      8 ~ 16
*               NUMA 3      24 ~ 31
*               .....
*
* With that background, lets see how below table was calculated. For simplicity, we have turned
* off hyper-threading OFF.
*
* When affinitizing procNum 0~7, they would map 1-to-1 with core 0~7. Next, procNum 8~16 is mapped
* to the cores in Group 0, as per (procNum >> 6) calculation above. But those maps to core 16~23
* as seen in example above. Likewise, when procNum 64~71 is passed, those are the first set of 
* cores we are trying to affinitize in Group# 1 and hence maps to core 8~16.
*
* On the contrary, below calculation could have been reverse mapped, i.e. we could have treated the
* procNum as the one we want to see on uprof and recalculated the numbers to be set in GROUP_AFFINITY
* struct.
*
* To conclude, these numbers are HARDCODED for a particular machine and uprof uses numa nodes to do
* its core indexing so in this case group#0 contains numa node 0, 2, 4, etc, instead of node 0, 1, 2, etc.
* And each numa node has 8 cores in this case. so in cpu group terms, core#8 in group#0 actually is
* considered as core#16 by uprof, because it belongs to numa node#2.
* -----------------------
* procNum         uprof
* -----------------------
* 0-7             0-7      Group 0
* 8-15            16-23
* 16-23           32-39
* 24-31           48-55
* 32-39           64-71
* 40-47           80-87
* 48-55           96-103
* 56-63           112-119

* 64-71           8-15    Group 1
* 72-79           24-31
* 80-87           40-47
* 88-95           56-63
* 96-103          72-79
* 104-111         88-95
* 112-119         104-111
* 120-127         120-127
*
**********************************************************************************/

    //for (procNum = 0; procNum < 128; procNum++)
    {
        int n = procNum / 8;
        int m = procNum % 8;
        int answer = 0;
        if (procNum >= 64)
        {
            n -= 8;
            answer = 8;
        }
        answer += (2 * n * 8) + m;
        printf("ProcNum: %d maps to uProf core: %d\n", procNum, answer);
    }

    BOOL result = SetThreadGroupAffinity(tHandle, &ga, nullptr);
    if (result == 0)
    {
        printf("SetThreadGroupAffinity returned 0 for processor 4. GetLastError() = %u\n", GetLastError());
        return;
    }
}

void experiment(LPTHREAD_START_ROUTINE proc)
{
    DWORD tid;
    g_hThread = CreateThread(
        NULL,                   // default security attributes
        0,                      // use default stack size
        proc,     // thread function name
        NULL, //(LPVOID)secondsToSleep, // argument to thread function
        CREATE_SUSPENDED,
        &tid);   // returns the thread identifier

    if (g_hThread == NULL)
    {
        printf("CreateThread failed: %d\n", GetLastError());
        return;
    }
    else
    {
        printf("thread %d created\n", tid);
    }

    SetThreadPriority(g_hThread, THREAD_PRIORITY_HIGHEST);

    if (g_worker_core >= 0)
    {
        printf("Worker core: ");
        SetThreadAffinity(g_hThread, g_worker_core);
    }

    if (g_mainthread_core >= 0)
    {
        printf("Main core: ");
        SetThreadAffinity(GetCurrentThread(), g_mainthread_core);
    }

    ResumeThread(g_hThread);

    // sleep for 10s
    Sleep((DWORD)10 * 1000);
    g_aligned_global_location.loc = 5;

    printf("end...\n");
}

const char* str_wait_type[] =
{
    "pause",
    "tpause",
    "umwait",
    "mwaitx",
    "mwaitx2",
    "monitorx"
};

int main(int argc, char** argv)
{
    g_aligned_global_location.loc = 10;
    parse_cmd_args(argc, argv);

    //inc_only();
    //inc_with_pause();
    //inc_only();
    //inc_with_pause();

    //return 0;

    enum reg
    {
        EAX = 0,
        EBX = 1,
        ECX = 2,
        EDX = 3,
        COUNT = 4
    };

    // bit definitions to make code more readable
    enum bits
    {
        WAITPKG = 1 << 5,
    };
    int reg[COUNT];

    __cpuid(reg, 0);
    __cpuid(reg, 1);
    __cpuid(reg, 7);

    bool supports_umwait = (reg[ECX] & bits::WAITPKG);

    LPTHREAD_START_ROUTINE proc;

    switch (g_proc_index)
    {
    case 0:
        proc = ThreadFunction_pause;
        break;

    case 1:
        if (!supports_umwait)
        {
            printf("this machine does not support tpause!\n");
            return 1;
        }
        proc = (g_use_low_power_p ? ThreadFunction_tpause_low_power : ThreadFunction_tpause_high_power);
        break;

    case 2:
        if (!supports_umwait)
        {
            printf("this machine does not support umwait!\n");
            return 1;
        }
        proc = (g_use_low_power_p ? ThreadFunction_umwait_low_power : ThreadFunction_umwait_high_power);
        break;

    case 3:
        proc = ThreadFunction_mwaitx;
        break;

    case 4:
        proc = ThreadFunction_mwaitx2;
        break;

    case 5:
        proc = ThreadFunction_monitorx;
        break;

    case 6:
        proc = ThreadFunction_umonitor;
        break;

    default:
        printf("proc index %d does not exist!\n", g_proc_index);
        return 1;
    }

    experiment(proc);
    WaitForSingleObject(g_hThread, INFINITE);
    printf("other thread exited\n");

    PrintTime(g_hThread, str_wait_type[g_proc_index], g_waited_count);

    {
        FILE* res_file = NULL;
        errno_t err = fopen_s(&res_file, "res.csv", "a");
        if (err)
        {
            printf("file could not be opened\n");
            return 1;
        }
        char res_buf[1024];

        // time is in us
        sprintf_s(res_buf, sizeof(res_buf), "%s,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%s\n",
            str_wait_type[g_proc_index], g_timeout, g_waited_count, (g_umode_cpu_time / 10), (g_kmode_cpu_time / 10), (g_elapsed_time / 10), g_total_cycles,
            (g_total_cycles * 10 / g_elapsed_time), // this is the # of cycles per us
            g_total_cycles_on_thread, g_elapsed_time_us_on_thread,
            (g_total_cycles / g_waited_count), (g_use_low_power_p ? "low power" : "high power"));
        fputs(res_buf, res_file);
    }

    return 0;
}