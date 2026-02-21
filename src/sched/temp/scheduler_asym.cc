/* sched/scheduler_asym.cc */
#include "sched/scheduler_asym.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <cerrno>
#include <cmath>
#include <algorithm>
#include <sys/mman.h>

namespace Charm {

// =========================================================
// HELPER FUNCTIONS 
// =========================================================

static void* allocateOnNode(size_t size, int node) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
#ifdef NUMA_AWARE
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_clearall(nodemask);
    numa_bitmask_setbit(nodemask, node);
    mbind(ptr, size, MPOL_BIND, nodemask->maskp, nodemask->size, 0);
    numa_free_nodemask(nodemask);
#endif
    return ptr;
}

static long long measureCacheMisses(pid_t tid, int cpu, int duration_ms = 100) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(pe);
    pe.config = PERF_COUNT_HW_CACHE_MISSES; 
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    int fd = syscall(__NR_perf_event_open, &pe, tid, cpu, -1, 0);
    if (fd == -1) fd = syscall(__NR_perf_event_open, &pe, tid, -1, -1, 0); 
    if (fd == -1) return 0;

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    
    long long count = 0;
    if (read(fd, &count, sizeof(count)) == -1) count = 0;
    close(fd);
    return count;
}

static bool setThreadAffinity(std::thread::native_handle_type thread, int cpuCore) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuCore, &cpuset);
    return (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
}

static bool migrateMemoryPages(void* addr, size_t len, int targetNode) {
#ifdef NUMA_AWARE
    long pageSize = sysconf(_SC_PAGESIZE);
    long numPages = (len + pageSize - 1) / pageSize;
    std::vector<void*> pages(numPages);
    for (long i = 0; i < numPages; i++) {
        pages[i] = static_cast<char*>(addr) + i * pageSize;
    }
    std::vector<int> nodes(numPages, targetNode);
    std::vector<int> status(numPages, -1);
    
    return (move_pages(0, numPages, pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE) == 0);
#else
    return true; 
#endif
}

// =========================================================
// ASYM IMPLEMENTATION
// =========================================================

AsymWorker::AsymWorker(size_t ith, int worker_num, Thread_Barrier* tb) 
    : Worker(ith, worker_num, tb) {
}

void AsymWorker::yield() {
    // Ensure we register our handle so migration works
    if (global_scheduler) {
        auto sys = static_cast<AsymScheduler*>(global_scheduler);
        if (sys->allThreads[rank].pthreadHandle == 0) {
            sys->allThreads[rank].pthreadHandle = pthread_self();
        }
        sys->runAsymRebalancing();
    }
    csched->coroutine_yield();
}

AsymScheduler::AsymScheduler(int worker_num) : Scheduler(THREAD_SIZE) {
    global_scheduler = this; // CRITICAL FIX: Init before workers start

    for (int n = 0; n < NUMA_DOMAINS; n++) {
        NodeInfo node; node.nodeId = n;
        nodes.push_back(node);
        for (int c = n * CORES_PER_NUMA_NODE; c < (n+1)*CORES_PER_NUMA_NODE; c++) {
            nodeToCores[n].push_back(c);
        }
    }

    allThreads.resize(num_threads);
    for(size_t i=0; i<num_threads; i++){
        allThreads[i].threadId = i;
        allThreads[i].appId = 100;
        allThreads[i].assignedNode = i % NUMA_DOMAINS;
        allThreads[i].assignedCore = nodeToCores[allThreads[i].assignedNode][0];
        allThreads[i].pthreadHandle = 0; // Will be set by worker
    }
    
    Application app;
    app.appId = 100;
    app.Att = 100.0;
    app.Atm = 1.0; 
    app.oldA = 0.95;
    app.Amm = {{0, 0.2}, {1, 0.3}};
    app.memSize = 32 << 20;
    app.memPtr = allocateOnNode(app.memSize, 0); 
    allApps.push_back(app);

    for(size_t i = 0; i < num_threads; ++i) {
        workers.push_back(new AsymWorker(i, worker_num, start_barrier));
    }
    while(start_barrier->get_cnt() != 1);
}

// --------------------------------------------------------
// REBALANCING ALGORITHM
// --------------------------------------------------------

std::vector<Cluster> formClusters(AsymScheduler* sys) {
    std::unordered_map<int, Cluster*> appClusterMap;
    std::vector<Cluster> clusters;
    
    // FIX: Reserve memory to prevent vector reallocation invalidating pointers in the map
    clusters.reserve(sys->allThreads.size()); 

    for (auto &th : sys->allThreads) {
        if (appClusterMap.find(th.appId) == appClusterMap.end()) {
            Cluster c;
            c.threads.push_back(&th);
            c.Crbw = 0.0; 
            c.Cweight = 1.0;
            clusters.push_back(c);
            // This pointer is now safe because of reserve()
            appClusterMap[th.appId] = &clusters.back(); 
        } else {
            appClusterMap[th.appId]->threads.push_back(&th);
        }
    }
    return clusters;
}

std::vector<Placement> computePlacements(const std::vector<Cluster> &clusters, const std::vector<NodeInfo> &nodes) {
    std::vector<Placement> placements;
    std::vector<int> mapping(clusters.size(), 0);
    std::function<void(int)> backtrack = [&](int idx) {
        if (idx == (int)clusters.size()) {
            Placement p;
            for (size_t i = 0; i < clusters.size(); i++) {
                p.clusterToNode[(Cluster*)&clusters[i]] = mapping[i];
            }
            placements.push_back(p);
            return;
        }
        for (int n = 0; n < (int)nodes.size(); n++) {
            mapping[idx] = n;
            backtrack(idx + 1);
        }
    };
    backtrack(0);
    return placements;
}

double computeClusterBandwidth(const Placement &pl, const std::vector<Cluster> &clusters) {
    double total = 0.0;
    for (auto &c : clusters) {
        total += c.Crbw * c.Cweight;
    }
    return total;
}

double computeMemoryMigrationCost(const Placement &pl, std::vector<Application> &apps) {
    double cost = 0.0;
    for (auto &app : apps) {
        cost += (0.05 * app.Att);
    }
    return cost;
}

bool clusterBelongsToApp(const Cluster &cl, int appId) {
    for (auto *th : cl.threads) {
        if (th->appId == appId) return true;
    }
    return false;
}

void identifyMigratedApplications(const Placement &pl, const std::vector<Cluster> &clusters, 
                                  AsymScheduler* sys, std::vector<Application*> &migrated) {
    for (auto &app : sys->allApps) {
        bool changed = false;
        for (auto &cl : clusters) {
            if (clusterBelongsToApp(cl, app.appId)) {
                // Check if key exists
                if(pl.clusterToNode.find(&cl) == pl.clusterToNode.end()) continue;
                
                int newNode = pl.clusterToNode.at((Cluster*)&cl);
                if (!cl.threads.empty() && newNode != cl.threads[0]->assignedNode) {
                    changed = true;
                    break;
                }
            }
        }
        if (changed) migrated.push_back(&app);
    }
}

void performThreadMigration(const Placement &pl, const std::vector<Cluster> &clusters, AsymScheduler* sys) {
    for (auto &cl : clusters) {
        if(pl.clusterToNode.find((Cluster*)&cl) == pl.clusterToNode.end()) continue;
        
        int newNode = pl.clusterToNode.at((Cluster*)&cl);
        int coreToUse = sys->nodeToCores[newNode][0];
        for (auto *th : cl.threads) {
            if (th->pthreadHandle) {
                setThreadAffinity(th->pthreadHandle, coreToUse);
            }
            th->assignedNode = newNode;
            th->assignedCore = coreToUse;
        }
    }
}

void dynamicMemoryMigration(const Placement &pl, const std::vector<Cluster> &clusters, AsymScheduler* sys) {
    for (auto &app : sys->allApps) {
        bool needMigration = false;
        int targetNode = -1;
        for (auto &cl : clusters) {
            if (clusterBelongsToApp(cl, app.appId)) {
                if(pl.clusterToNode.find((Cluster*)&cl) == pl.clusterToNode.end()) continue;

                int newNode = pl.clusterToNode.at((Cluster*)&cl);
                if (!cl.threads.empty() && newNode != cl.threads[0]->assignedNode) {
                    needMigration = true;
                    targetNode = newNode;
                    break;
                }
            }
        }
        if (needMigration && app.memPtr && app.memSize > 0) {
            migrateMemoryPages(app.memPtr, app.memSize, targetNode);
        }
    }
}

void fullyMigrateMemory(Application &app) {
    if (app.memPtr && app.memSize > 0) {
        if (migrateMemoryPages(app.memPtr, app.memSize, 0)) {
            app.oldA = 0.0;
        }
    }
}

// --------------------------------------------------------
// MAIN LOGIC
// --------------------------------------------------------
void AsymScheduler::runAsymRebalancing() {
    AsymScheduler* sys = this;

    std::vector<Cluster> clusters = formClusters(sys);
    if(clusters.empty()) return;

    for (auto &cl : clusters) {
        cl.Crbw = 0.0;
        cl.Cweight = 1.0;
        for (auto *th : cl.threads) {
            // Measure assuming simulation ID
            long long cnt = measureCacheMisses(th->threadId + 1000, th->assignedCore, 100);
            if (cnt > 0) cl.Crbw += (double)cnt;
        }
    }

    std::vector<Placement> placements = computePlacements(clusters, sys->nodes);
    if(placements.empty()) return;
  
    double Maxwbw = 0.0;
    for (auto &pl : placements) {
        pl.Pwbw = computeClusterBandwidth(pl, clusters);
        if(pl.Pwbw > Maxwbw) Maxwbw = pl.Pwbw;
    }
  
    for (auto &pl : placements) {
        if (pl.Pwbw >= 0.90 * Maxwbw) {
            pl.Pmm = computeMemoryMigrationCost(pl, sys->allApps);
        }
    }
  
    double bestPmm = 1e15;
    Placement bestPlacement;
    bool found = false;
    for (auto &pl : placements) {
        if (pl.Pwbw >= 0.90 * Maxwbw && pl.Pmm < bestPmm) {
            bestPmm = pl.Pmm;
            bestPlacement = pl;
            found = true;
        }
    }
    if (!found) return;
  
    std::vector<Application*> migratedApps;
    identifyMigratedApplications(bestPlacement, clusters, sys, migratedApps);
    bool blockMigration = false;
    for (auto *A : migratedApps) {
        double sumNodes = 0.0;
        for (auto &cl : clusters) {
            if (clusterBelongsToApp(cl, A->appId)) {
                if(bestPlacement.clusterToNode.find(&cl) == bestPlacement.clusterToNode.end()) continue;
                int newNode = bestPlacement.clusterToNode.at((Cluster*)&cl);
                if (A->Amm.find(newNode) != A->Amm.end()) {
                    sumNodes += (A->Amm[newNode] * 0.3);
                }
            }
        }
        if (A->Atm + sumNodes > 0.05 * A->Att) {
            blockMigration = true;
        }
    }
  
    if (!blockMigration) {
        performThreadMigration(bestPlacement, clusters, sys);
        dynamicMemoryMigration(bestPlacement, clusters, sys);
    }
  
    for (auto *A : migratedApps) {
        if (A->oldA > 0.90) {
            fullyMigrateMemory(*A);
        }
    }
}

} // namespace Charm