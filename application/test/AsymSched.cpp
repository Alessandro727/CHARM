/******************************************************************************
 * AsymSched.cpp
 *
 * A "real" Linux-oriented AsymSched implementation that uses OS calls for:
 *  - Thread migration (CPU affinity)
 *  - NUMA memory allocation & migration (move_pages / libnuma / mbind)
 *  - Some placeholders for measuring memory bandwidth or performance counters.
 *
 * DISCLAIMER: This code is non-trivial and may need modifications to compile
 * and run correctly in your specific environment. It aims to illustrate how
 * to implement the logic from "Algorithm 1" in a real-world manner.
 ******************************************************************************/

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/mman.h>    // For mmap, mbind, etc.
#include <functional>
#include <numa.h>
#include <numaif.h>
#include <time.h>        // For clock_gettime
#include <fcntl.h>


// -----------------------------------------------------------------------------
// Linux/NUMA Helpers
// -----------------------------------------------------------------------------

/**
 * Pin the calling thread (given by pthread_t handle) to the given CPU core.
 * If you have a mapping from NUMA node -> set of CPU cores, pick one from that node.
 */
bool setThreadAffinity(pthread_t thread, int cpuCore) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuCore, &cpuset);

    int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        std::cerr << "[Error] setThreadAffinity failed: " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

/**
 * Move a range of pages in `addr` of size `len` bytes to the specified NUMA node.
 * Implementation uses move_pages. Typically requires root privileges.
 */
bool migrateMemoryPages(void* addr, size_t len, int targetNode) {
    // We can only move pages at page granularity. We'll approximate here:
    long pageSize = sysconf(_SC_PAGESIZE);
    long numPages = (len + pageSize - 1) / pageSize;

    std::vector<void*> pages(numPages);
    for (long i = 0; i < numPages; i++) {
        pages[i] = static_cast<char*>(addr) + i * pageSize;
    }

    // The 'nodes' array tells the kernel which node each page should move to.
    std::vector<int> nodes(numPages, targetNode);

    // move_pages expects a pointer to an int array of status codes.
    std::vector<int> status(numPages, -1);

    // The call:
    //    move_pages(pid, count, pages, nodes, status, flags)
    // pid = 0 means current process, flags=MPOL_MF_MOVE|MPOL_MF_STRICT by default or 0
    int ret = move_pages(0, numPages, pages.data(),
                         nodes.data(), status.data(), MPOL_MF_MOVE);
    if (ret != 0) {
        if (errno == ENOSYS) {
            std::cerr << "[Error] move_pages not supported by kernel.\n";
        } else {
            std::cerr << "[Error] move_pages failed: " << strerror(errno)
                      << " (ret=" << ret << ")\n";
        }
        return false;
    }

    // Check if any page had a failure
    for (int st : status) {
        if (st < 0) {
            // Some pages might not have migrated successfully
            // st is negative => error code (e.g. -EACCES, -EBUSY, ...)
            // For now, we just note it.
            return false;
        }
    }
    return true;
}

/**
 * Example function to allocate memory pinned to a specific NUMA node using mbind or libnuma.
 */
void* allocateOnNode(size_t size, int node) {
    // We'll allocate a private anonymous mapping
    void* ptr = mmap(NULL, size, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "[Error] mmap failed: " << strerror(errno) << "\n";
        return nullptr;
    }

    // Next, use mbind to bind the entire region to the given node.
    // We'll build a bitmask with just 'node' set.
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_clearall(nodemask);
    numa_bitmask_setbit(nodemask, node);

    int mode = MPOL_BIND; // strictly bind pages to node
    int ret = mbind(ptr, size, mode, nodemask->maskp,
                    nodemask->size, 0 /*flags*/);
    if (ret != 0) {
        std::cerr << "[Error] mbind failed: " << strerror(errno) << "\n";
        // We'll still return ptr, but it's not strictly pinned.
    }
    numa_free_nodemask(nodemask);
    return ptr;
}


// -----------------------------------------------------------------------------
// Data structures from the paper's Algorithm 1
// -----------------------------------------------------------------------------

struct NodeInfo {
    int nodeId;
    // If you need to store memory-controller IDs or bandwidth usage,
    // you could do that here. 
};

// Represents a single thread. We store its PID/appId, a handle to the actual
// pthread, the node it’s on, etc.
struct ThreadInfo {
    int      threadId;      // Logical ID
    int      pid;           // Application or process ID
    pthread_t pthreadHandle; // Actual POSIX thread handle
    int      assignedNode;
    // Possibly store CPU core too, if you want fine-grained core affinity
    int      assignedCore;
    // For memory usage, overhead, etc., you might store more.
};

// Clusters (step 1–3)
struct Cluster {
    std::vector<ThreadInfo*> threads;
    double Crbw   = 0.0;
    double Cweight= 1.0;
};

// A proposed mapping from each cluster to a node
struct Placement {
    std::unordered_map<Cluster*, int> clusterToNode;
    double Pwbw = 0.0;
    double Pmm  = 0.0;
};

// Represents an application
struct Application {
    int    appId;
    double Atm;   // Overhead/time for migrating threads
    double Att;   // Total time/performance budget
    double oldA;  // Fraction of memory on old node
    // Overhead factors for memory migration to each node
    std::unordered_map<int, double> Amm;

    // For demonstration, we store an allocated memory region + size
    // so we can physically migrate it
    void*  memPtr = nullptr;
    size_t memSize= 0;

    bool   recentlyMigrated = false;
};

// -----------------------------------------------------------------------------
// SystemState: the global or "singleton" system representation
// -----------------------------------------------------------------------------

class SystemState {
public:
    static SystemState& instance() {
        static SystemState s;
        return s;
    }

    // Suppose we have up to 4 NUMA nodes in this example
    std::vector<NodeInfo>   nodes;
    std::vector<ThreadInfo> allThreads;
    std::vector<Application>allApps;

    // For demonstration: one or more CPU cores per node
    // This is an example mapping. On many systems, node 0 might have cores 0..3, node 1 => cores 4..7, etc.
    // Adjust as needed for your hardware.
    std::unordered_map<int,std::vector<int>> nodeToCores;

    // Fake method to initialize some data
    void detectSystem() {
        // e.g. we have 2 NUMA nodes, node0 => cores [0,1], node1 => cores [2,3]
        // Adjust to match your actual hardware or parse from /sys/devices/system/node
        {
            NodeInfo n0; n0.nodeId = 0; nodes.push_back(n0);
            NodeInfo n1; n1.nodeId = 1; nodes.push_back(n1);
            nodeToCores[0] = {0,1};
            nodeToCores[1] = {2,3};
        }

        // Create 4 threads from 2 apps
        for (int t = 0; t < 4; t++) {
            ThreadInfo th;
            th.threadId     = t;
            th.pid          = (t < 2) ? 100 : 200;
            th.assignedNode = t % 2; // start with node 0 for threads0,1 and node1 for threads2,3
            th.assignedCore = nodeToCores[ th.assignedNode ][0]; 
            // We'll create a real pthread so we can demonstrate migration
            pthread_t ph;
            pthread_create(&ph, nullptr, dummyThreadFunc, (void*)(intptr_t)t);
            th.pthreadHandle= ph;
            allThreads.push_back(th);
        }

        // Two apps
        {
            Application A1;
            A1.appId = 100;
            A1.Att   = 100.0;
            A1.Atm   = 1.0;
            A1.oldA  = 0.95; // e.g. 95% memory on old node
            // For each node, define Amm:
            A1.Amm[0] = 0.2; A1.Amm[1] = 0.3;

            // We'll allocate 1MB pinned to node0 for demonstration
            A1.memSize = 1<<20; // 1MB
            A1.memPtr  = allocateOnNode(A1.memSize, 0);

            allApps.push_back(A1);
        }
        {
            Application A2;
            A2.appId = 200;
            A2.Att   = 200.0;
            A2.Atm   = 2.0;
            A2.oldA  = 0.70; // e.g. 70% memory on old node
            A2.Amm[0] = 0.4; A2.Amm[1] = 0.1;

            // 2MB pinned to node1
            A2.memSize = 2<<20;
            A2.memPtr  = allocateOnNode(A2.memSize, 1);

            allApps.push_back(A2);
        }
    }

    // Dummy function each thread runs, so we have a real POSIX thread
    static void* dummyThreadFunc(void* arg) {
        // Just loop doing minor work 
        volatile int counter = 0;
        while (true) {
            // Sleep a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            counter++;
            // We could exit if signaled, etc.
        }
        return nullptr;
    }

private:
    SystemState() {}
};

// -----------------------------------------------------------------------------
// AsymSched class implementing Algorithm 1 with real OS calls
// -----------------------------------------------------------------------------

class AsymSched {
public:
    AsymSched() {
        SystemState::instance().detectSystem();
    }

    // Main function implementing “Algorithm 1”
    void runAsymSched() {
        auto &sys = SystemState::instance();

        // Step 1–3: form clusters
        // We'll check if threads share the same pid => same cluster, plus some
        // notion of "common memory controller." We'll approximate by "same node => might share a controller."
        std::vector<Cluster> clusters = formClusters(sys);

        // Step 4: compute relevant placements
        std::vector<Placement> placements = computePlacements(clusters, sys.nodes);

        // Step 5: Maxwbw = 0
        double Maxwbw = 0.0;

        // Step 6–9: compute Pwbw for each placement, track max
        for (auto &pl : placements) {
            pl.Pwbw = computeClusterBandwidth(pl, clusters);
            Maxwbw  = std::max(Maxwbw, pl.Pwbw);
        }

        // Step 10–13: skip if Pwbw < 90% * Maxwbw, compute Pmm
        for (auto &pl : placements) {
            if (pl.Pwbw >= 0.90*Maxwbw) {
                pl.Pmm = computeMemoryMigrationCost(pl, sys.allApps);
            }
        }

        // Step 14: pick the placement with the lowest Pmm among those not skipped
        double bestPmm = 1e15;
        Placement bestPlacement;
        bool found = false;
        for (auto &pl : placements) {
            if (pl.Pwbw >= 0.90*Maxwbw && pl.Pmm < bestPmm) {
                bestPmm = pl.Pmm;
                bestPlacement = pl;
                found = true;
            }
        }
        if (!found) {
            std::cout << "[AsymSched] No placement above 90% Maxwbw.\n";
            return;
        }

        // Steps 15–19: check overhead constraints
        std::vector<Application*> migratedApps;
        identifyMigratedApplications(bestPlacement, clusters, migratedApps);

        bool blockMigration = false;
        for (auto *A : migratedApps) {
            double sumNodes = 0.0;
            // Suppose we discover which node(s) they'd be moved to.
            // For demonstration, gather from bestPlacement:
            // We'll see which cluster belongs to this app
            // and see if that cluster is assigned to a node different from current
            for (auto &cl : clusters) {
                if (clusterBelongsToApp(cl, A->appId)) {
                    int newNode = bestPlacement.clusterToNode.at((Cluster*)&cl);
                    // sum overhead: Amm[newNode]*0.3
                    sumNodes += (A->Amm[newNode] * 0.3);
                }
            }
            double overheadVal = A->Atm + sumNodes;
            if (overheadVal > 0.05 * A->Att) {
                std::cout << "[AsymSched] Overhead too high for app " << A->appId
                          << "; skipping migration.\n";
                blockMigration = true;
            }
        }

        // Steps 20–21: Migrate threads & memory
        if (!blockMigration) {
            performThreadMigration(bestPlacement, clusters);
            dynamicMemoryMigration(bestPlacement, clusters);
        }

        // Step 22: sleep 2s
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Step 23–27: if oldA > 90%, fully migrate memory
        for (auto *A : migratedApps) {
            if (A->oldA > 0.90) {
                fullyMigrateMemory(*A);
            }
        }
        std::cout << "[AsymSched] Completed.\n";
    }

private:
    // -------------------------------------------------------------------------
    // Step 1–3: formClusters
    // -------------------------------------------------------------------------
    std::vector<Cluster> formClusters(SystemState &sys) {
        std::unordered_map<int, Cluster*> pidMap;
        std::vector<Cluster> clusters;

        // We'll group threads by pid
        for (auto &th : sys.allThreads) {
            auto it = pidMap.find(th.pid);
            if (it == pidMap.end()) {
                Cluster c;
                c.threads.push_back(&th);
                c.Crbw = measureMemoryControllerUsage(th.pid); // an example
                clusters.push_back(c);
                pidMap[th.pid] = &clusters.back();
            } else {
                it->second->threads.push_back(&th);
                // Increase Crbw
                it->second->Crbw += measureMemoryControllerUsage(th.pid);
            }
        }
        return clusters;
    }

    // -------------------------------------------------------------------------
    // Step 4: computePlacements
    //   We brute force all cluster→node assignments. For real usage, you might
    //   do partial enumeration or heuristics.
    // -------------------------------------------------------------------------
    std::vector<Placement> computePlacements(const std::vector<Cluster> &clusters,
                                             const std::vector<NodeInfo> &nodes)
    {
        std::vector<Placement> placements;
        std::vector<int> map(clusters.size(), 0);

        std::function<void(int)> backtrack = [&](int idx) {
            if (idx == (int)clusters.size()) {
                // build a placement
                Placement p;
                for (size_t i = 0; i < clusters.size(); i++) {
                    p.clusterToNode[(Cluster*)&clusters[i]] = map[i];
                }
                placements.push_back(p);
                return;
            }
            for (int n = 0; n < (int)nodes.size(); n++) {
                map[idx] = n;
                backtrack(idx+1);
            }
        };
        backtrack(0);

        return placements;
    }

    // -------------------------------------------------------------------------
    // Step 7: computeClusterBandwidth
    //   Summation of (C.Crbw * C.Cweight). In a real system, you might measure
    //   how well this cluster→node assignment helps or hurts memory usage.
    // -------------------------------------------------------------------------
    double computeClusterBandwidth(const Placement &pl, 
                                   const std::vector<Cluster> &clusters)
    {
        double total = 0.0;
        for (auto &c : clusters) {
            double val = c.Crbw * c.Cweight;
            // You might also factor in which node they're assigned to:
            // e.g. if node has limited bandwidth, reduce val. We'll keep it simple.
            total += val;
        }
        return total;
    }

    // -------------------------------------------------------------------------
    // Step 12: computeMemoryMigrationCost
    //   We'll just sum a cost for each app if we detect the cluster is moved
    //   from oldNode -> newNode. The paper’s pseudocode uses "Pmm" to measure overhead.
    // -------------------------------------------------------------------------
    double computeMemoryMigrationCost(const Placement &pl, 
                                      std::vector<Application> &apps)
    {
        double cost = 0.0;
        // For each app, see if they're presumably moved
        for (auto &app : apps) {
            // If the cluster that belongs to app is assigned to a node different
            // from the one it used to be on, add some overhead
            // (We do a simple approach: +0.05 * Att for each node change)
            cost += (0.05 * app.Att);
        }
        return cost;
    }

    // -------------------------------------------------------------------------
    // Step 15: identifyMigratedApplications
    //   We check if a cluster’s assigned node changed from old assignment, so the app moves
    // -------------------------------------------------------------------------
    void identifyMigratedApplications(const Placement &pl,
                                      const std::vector<Cluster> &clusters,
                                      std::vector<Application*> &migrated)
    {
        auto &sys = SystemState::instance();
        for (auto &app : sys.allApps) {
            // Check if any cluster with this app’s pid is assigned to a new node
            // This is simplified—real logic might track old node vs new node.
            bool changed = false;
            for (auto &cl : clusters) {
                if (clusterBelongsToApp(cl, app.appId)) {
                    int newNode = pl.clusterToNode.at((Cluster*)&cl);
                    // Compare with the first thread’s assignedNode, etc.
                    if (!cl.threads.empty()) {
                        int oldNode = cl.threads[0]->assignedNode;
                        if (newNode != oldNode) {
                            changed = true;
                            break;
                        }
                    }
                }
            }
            if (changed) {
                migrated.push_back(&app);
            }
        }
    }

    // Helper to check if cluster belongs to a given app (by matching pid)
    bool clusterBelongsToApp(const Cluster &cl, int appId) {
        for (auto *t : cl.threads) {
            if (t->pid == appId) return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Step 20: Actually migrate threads (set CPU affinity)
    // -------------------------------------------------------------------------
    void performThreadMigration(const Placement &pl, const std::vector<Cluster> &clusters) {
        auto &sys = SystemState::instance();

        for (auto &cl : clusters) {
            int newNode = pl.clusterToNode.at((Cluster*)&cl);
            // Choose a CPU core from newNode's set
            if (sys.nodeToCores.find(newNode) == sys.nodeToCores.end()) {
                continue; // no known cores in that node?
            }
            // We'll pick the first core for simplicity
            int coreToUse = sys.nodeToCores[newNode][0];

            // Migrate each thread in cluster
            for (auto *th : cl.threads) {
                setThreadAffinity(th->pthreadHandle, coreToUse);
                th->assignedNode = newNode;
                th->assignedCore = coreToUse;
            }
        }
        std::cout << "[AsymSched] Threads migrated.\n";
    }

    // -------------------------------------------------------------------------
    // Step 21: Use dynamic memory migration 
    //   We'll call move_pages for each app whose cluster is mapped to a new node.
    // -------------------------------------------------------------------------
    void dynamicMemoryMigration(const Placement &pl, const std::vector<Cluster> &clusters) {
        auto &sys = SystemState::instance();

        for (auto &app : sys.allApps) {
            bool needMigration = false;
            int  targetNode    = -1;

            // Check if cluster with this app is assigned to a new node
            for (auto &cl : clusters) {
                if (clusterBelongsToApp(cl, app.appId)) {
                    int newNode = pl.clusterToNode.at((Cluster*)&cl);
                    if (!cl.threads.empty()) {
                        int oldNode = cl.threads[0]->assignedNode;
                        if (newNode != oldNode) {
                            needMigration = true;
                            targetNode    = newNode;
                            break;
                        }
                    }
                }
            }
            if (needMigration && app.memPtr && app.memSize > 0) {
                bool ok = migrateMemoryPages(app.memPtr, app.memSize, targetNode);
                if (ok) {
                    std::cout << "[AsymSched] Migrated memory for app " << app.appId
                              << " to node " << targetNode << "\n";
                } else {
                    std::cout << "[AsymSched] Memory migration failed for app " 
                              << app.appId << "\n";
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Step 25: fullyMigrateMemory
    // -------------------------------------------------------------------------
    void fullyMigrateMemory(Application &app) {
        // If oldA>90%, we “fully migrate memory.” We already migrated above, but
        // let's just do it again to illustrate. 
        // In real usage, you might do a more thorough approach or confirm partial pages.
        if (app.memPtr && app.memSize > 0) {
            // Suppose we want to place everything on node 0 now
            bool ok = migrateMemoryPages(app.memPtr, app.memSize, 0);
            if (ok) {
                std::cout << "[AsymSched] Fully migrated memory for app " << app.appId << "\n";
                app.oldA = 0.0;
            }
        }
    }

    // -------------------------------------------------------------------------
    // measureMemoryControllerUsage - placeholder for real perf measurement
    // -------------------------------------------------------------------------
    double measureMemoryControllerUsage(int pid) {
        // In a real system, you might do:
        //  1. perf_event_open for a hardware counter measuring memory BW
        //  2. parse /sys/devices/system/node/* or /proc/<pid>/numa_stat
        //  3. read "uncore" counters for memory controllers
        //
        // We'll just return a fixed number for demonstration
        return 1.0;
    }
};

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main() {
    // Create and run the AsymSched algorithm
    AsymSched scheduler;
    scheduler.runAsymSched();

    // The background threads (dummyThreadFunc) will keep running unless we kill them.
    // In a real system, you might join them or signal them to exit gracefully.
    // For demonstration, let's just sleep then exit.
    std::cout << "[Main] AsymSched finished. Sleeping 2s before exit.\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
