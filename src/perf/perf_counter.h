/*

Copyright (c) 2024 Alessandro Fogli

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <perfmon/pfmlib_perf_event.h>

struct PerfCounter {

   struct event {
	  struct read_format {
		 uint64_t value;
		 uint64_t time_enabled;
		 uint64_t time_running;
		 uint64_t id;
	  };

	  perf_event_attr pe;
	  int fd;
	  read_format prev;
	  read_format data;
	  
	  uint64_t readCounter() {
		uint64_t count = 0, values[3];
		int ret;
		ret = read(fd, values, sizeof(values));
        if (ret != sizeof(values)) {
        	std::cout << "cannot read results: " << strerror(errno) << std::endl;
		}
		if (values[2]) {
			count = (uint64_t)((double)values[0] * values[1]/values[2]);
		}
		return count;
	  }
   };

   enum EventDomain : uint8_t { USER = 0b1, KERNEL = 0b10, HYPERVISOR = 0b100, ALL = 0b111 };

   std::vector<event> events;
   std::vector<std::string> names;
   std::chrono::time_point<std::chrono::steady_clock> startTime;
   std::chrono::time_point<std::chrono::steady_clock> stopTime;

   PerfCounter() {
	  if (pfm_initialize() != PFM_SUCCESS) {
		std::cout << "libpfm initialization failed" << std::endl;
		//errx(1, "libpfm initialization failed");
	  }

	//   Fill from L3 or different L2 in same CCX
	  //registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");

	  // Fill from cache of different CCX in same NUMA node
	  //registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL");

	  // Fill from CCX cache in remote NUMA node
	  //registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT");

	  // Fill from DRAM or IO connected in same NUMA node
	  //registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL");

	  // Fill from DRAM or IO connected in remote NUMA node
	  //registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_RMT");

	  //registerCounter("L1-DCACHE-LOAD-MISSES");
	  //registerCounter("STORE_TO_LOAD_FORWARD");

        // Event Code for OFFCORE_RESPONSE (typically 0xB7 for recent Intel CPUs)
        uint64_t offcore_event_code = 0xb7;
        // umask for OFFCORE_RESPONSE (typically 0x01 for any request type)
        uint64_t offcore_umask = 0x01;

        // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE
        // Response from Local L3 Hit (Example Mask - VERIFY!)
        registerRawCounter("OFFCORE_RESPONSE:L3_HIT_LOCAL", offcore_event_code, offcore_umask, 0x3f803c0100); // L3 Hit M/E/S/F, LLC Hit, Local Source

        // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL
        // Response from Remote Cache Hit (Same Socket) (Example Mask - VERIFY!)
         registerRawCounter("OFFCORE_RESPONSE:REMOTE_CACHE_HIT", offcore_event_code, offcore_umask, 0x3f803c0200); // L3 Hit M/E/S/F, LLC Hit, Remote Cache Source

        // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT
        // Response from Remote Cache Hit (Different Socket) (Example Mask - VERIFY!)
        registerRawCounter("OFFCORE_RESPONSE:REMOTE_SOCKET_CACHE_HIT", offcore_event_code, offcore_umask, 0x3f803c0400); // L3 Hit M/E/S/F, LLC Hit, Remote Socket Source

        // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL
        // Response from Local DRAM (Example Mask - VERIFY!)
        registerRawCounter("OFFCORE_RESPONSE:LOCAL_DRAM", offcore_event_code, offcore_umask, 0x3fbe048000); // LLC Miss, Local DRAM Source

        // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_RMT
        // Response from Remote DRAM (Example Mask - VERIFY!)
        registerRawCounter("OFFCORE_RESPONSE:REMOTE_DRAM", offcore_event_code, offcore_umask, 0x3fc2010000); // LLC Miss, Remote DRAM Source


   	  // additional counters can be found running showevtinfo in perfmon2-libpfm4/examples

	  for (unsigned i=0; i<events.size(); i++) {
		 auto& event = events[i];
		 event.fd = static_cast<int>(syscall(__NR_perf_event_open, &event.pe, 0, -1, -1, 0));
		 //event.fd = perf_event_open(&event.pe, getpid(), -1, -1, 0);
		 if (event.fd < 0) {
			std::cout << "Error opening counter " << names[i] << std::endl;
			events.resize(0);
			names.resize(0);
			return;
		 }
	  }
   }

// Structure to hold event data
// Helper to register raw OFFCORE_RESPONSE events
    void registerRawCounter(const std::string& name, uint64_t eventCode, uint64_t umask, uint64_t config1Mask) {
        names.push_back(name);
        events.emplace_back(); // Add a new PerfEventData
        auto& pe = events.back().pe;

        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = PERF_TYPE_RAW;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = eventCode;  // The raw event code (e.g., 0xb7)
        pe.config1 = config1Mask; // The specific response mask
        pe.config |= (umask << 8); // Add umask if needed (some raw events use it)

        // Configure standard options
        pe.disabled = 1; // Start disabled, enable later
        pe.exclude_kernel = 0; // Count kernel-mode
        pe.exclude_user = 0;   // Count user-mode
        pe.exclude_hv = 1;
        pe.exclude_idle = 1;
        pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
         std::cout << "Registering raw: " << name << " (code=" << eventCode << ", umask=" << umask << ", config1=" << std::hex << config1Mask << std::dec << ")" << std::endl;
    }



void registerCounter(const std::string& name, EventDomain domain = ALL) {
    const int maxRetries = 30;  // Maximum number of retries
    int retries = 0;           // Current retry count

    names.push_back(name);
    events.push_back(event());
    auto& event = events.back();
    auto& pe = event.pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));

    int ret;
    while (retries < maxRetries) {
        ret = pfm_get_perf_event_encoding(name.c_str(), PFM_PLM0 | PFM_PLM3, &pe, NULL, NULL);
        if (ret == PFM_SUCCESS) {
            break;  // Success, exit the loop
        } else {
            std::cout << "Cannot find encoding: " << pfm_strerror(ret) << std::endl;
            retries++;
            if (retries >= maxRetries) {
                events.resize(0);
                names.resize(0);
                return;
            }
        }
    }

    /*
     * request timing information because event may be multiplexed
     * and thus it may not count all the time. The scaling information
     * will be used to scale the raw count as if the event had run all
     * along
     */
    //pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

    /* do not start immediately after perf_event_open() */
    pe.disabled = 1;

    pe.inherit = 1;
    pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
}


	void startCounters() {
    	for (unsigned i = 0; i < events.size(); i++) {
    	    auto& event = events[i];
    	    ioctl(event.fd, PERF_EVENT_IOC_RESET, 0);
    	    ioctl(event.fd, PERF_EVENT_IOC_ENABLE, 0);
        
    	    // Attempt to read the counter, retrying on failure
    	    bool success = false;
    	    int retryCount = 0;
    	    const int maxRetries = 30; // You can adjust the number of retries as needed

    	    while (!success && retryCount < maxRetries) {
    	        if (read(event.fd, &event.prev, sizeof(uint64_t) * 3) == sizeof(uint64_t) * 3) {
    	            success = true;
    	        } else {
    	            std::cout << "Error reading counter " << names[i] << " (attempt " << retryCount + 1 << ")" << std::endl;
    	            retryCount++;
    	        }
    	    }

    	    if (!success) {
    	        std::cout << "Failed to read counter " << names[i] << " after " << maxRetries << " attempts" << std::endl;
    	    }
    	}
    	startTime = std::chrono::steady_clock::now();
	}

   	void resetCounter(const std::string& name) {
		for (unsigned i=0; i<events.size(); i++) {
		 	if (names[i]==name) {
				auto& event = events[i];
		 		ioctl(event.fd, PERF_EVENT_IOC_RESET, 0);
		 		ioctl(event.fd, PERF_EVENT_IOC_ENABLE, 0);
		 		if (read(event.fd, &event.prev, sizeof(uint64_t) * 3) != sizeof(uint64_t) * 3)
					std::cout << "Error reading counter " << names[i] << std::endl;
				break;
			}
		}
   	}

   ~PerfCounter() {
	  for (auto& event : events) {
		 close(event.fd);
		 pfm_terminate();
	  }
   }

   void stopCounters() {
	  stopTime = std::chrono::steady_clock::now();
	  for (unsigned i=0; i<events.size(); i++) {
		 auto& event = events[i];
		 if (read(event.fd, &event.data, sizeof(uint64_t) * 3) != sizeof(uint64_t) * 3)
			std::cout << "Error reading counter " << names[i] << std::endl;
		 ioctl(event.fd, PERF_EVENT_IOC_DISABLE, 0);
	  }
   }

   uint64_t getCounter(const std::string& name) {
	  for (unsigned i=0; i<events.size(); i++)
		 if (names[i]==name)
			return events[i].readCounter();
	  return -1;
   }

   static void printCounter(std::ostream& headerOut, std::ostream& dataOut, std::string name, std::string counterValue,bool addComma=true) {
	 auto width=std::max(name.length(),counterValue.length());
	 headerOut << std::setw(static_cast<int>(width)) << name << (addComma ? "," : "") << " ";
	 dataOut << std::setw(static_cast<int>(width)) << counterValue << (addComma ? "," : "") << " ";
   }

   template <typename T>
   static void printCounter(std::ostream& headerOut, std::ostream& dataOut, std::string name, T counterValue,bool addComma=true) {
	 std::stringstream stream;
	 stream << std::fixed << std::setprecision(2) << counterValue;
	 PerfCounter::printCounter(headerOut,dataOut,name,stream.str(),addComma);
   }

   void printReport(std::ostream& out, uint64_t normalizationConstant) {
	 std::stringstream header;
	 std::stringstream data;
	 printReport(header,data,normalizationConstant);
	 out << header.str() << std::endl;
	 out << data.str() << std::endl;
   }

   void printReport(std::ostream& headerOut, std::ostream& dataOut, uint64_t normalizationConstant) {
	  if (!events.size())
		 return;

	  // print all metrics
	  for (unsigned i=0; i<events.size(); i++) {
		 printCounter(headerOut,dataOut,names[i],events[i].readCounter()/static_cast<uint64_t>(normalizationConstant));
	  }

	  printCounter(headerOut,dataOut,"scale",normalizationConstant);
   }

   static void printCounterValuePair(std::ostream& out, std::string name, std::string counterValue, bool addNewLine = true) {
        out << name << ": " << counterValue;
        if (addNewLine) {
            out << std::endl;
        }
    }

    template <typename T>
    static void printCounterValuePair(std::ostream& out, std::string name, T counterValue, bool addNewLine = true) {
        std::stringstream stream;
        stream << std::fixed << std::setprecision(2) << counterValue;
        PerfCounter::printCounterValuePair(out, name, stream.str(), addNewLine);
    }

   void printReportByLine(std::ostream& out, uint64_t normalizationConstant) {
          std::stringstream data;
          if (!events.size())
                 return;

          // print all metrics
          for (unsigned i=0; i<events.size(); i++) {
                 printCounterValuePair(data,names[i],events[i].readCounter()/static_cast<uint64_t>(normalizationConstant));
          }

          printCounterValuePair(data,"scale",normalizationConstant);
          out << data.str() << std::endl;
   }

};
