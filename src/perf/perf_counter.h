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

/* 	  uint64_t readCounter() {
		double multiplexingCorrection = static_cast<double>(data.time_enabled - prev.time_enabled) / static_cast<double>(data.time_running - prev.time_running);
		return static_cast<uint64_t>(static_cast<double>(data.value - prev.value) * multiplexingCorrection); 
	  } */

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

	  // Fill from L3 or different L2 in same CCX
	  registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");

	  // Fill from cache of different CCX in same NUMA node
	  registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL");

	  // Fill from CCX cache in different NUMA node
	  registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT");

	  // Fill from DRAM or IO connected in same NUMA node
	  registerCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL");

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

   void registerCounter(const std::string& name, EventDomain domain = ALL) {
      names.push_back(name);
      events.push_back(event());
      auto& event = events.back();
      auto& pe = event.pe;
      memset(&pe, 0, sizeof(struct perf_event_attr));

      int ret = pfm_get_perf_event_encoding(name.c_str(), PFM_PLM0|PFM_PLM3, &pe, NULL, NULL);
      if (ret != PFM_SUCCESS) {
	      std::cout << "Cannot find encoding: " << pfm_strerror(ret) << std::endl;
              events.resize(0);
              names.resize(0);
              return;
              //errx(1, "cannot find encoding: %s", pfm_strerror(ret));
      }

      /*
       * request timing information because event may be multiplexed
       * and thus it may not count all the time. The scaling information
       * will be used to scale the raw count as if the event had run all
       * along
       */
      //pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;

      /* do not start immediately after perf_event_open() */
      pe.disabled = 1;

      pe.inherit = 1;
      //pe.inherit_stat = 0;
      //pe.exclude_user = !(domain & USER);
      //pe.exclude_kernel = !(domain & KERNEL);
      //pe.exclude_hv = !(domain & HYPERVISOR);
      pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
   }


   void startCounters() {
	  for (unsigned i=0; i<events.size(); i++) {
		 auto& event = events[i];
		 ioctl(event.fd, PERF_EVENT_IOC_RESET, 0);
		 ioctl(event.fd, PERF_EVENT_IOC_ENABLE, 0);
		 if (read(event.fd, &event.prev, sizeof(uint64_t) * 3) != sizeof(uint64_t) * 3)
			std::cout << "Error reading counter " << names[i] << std::endl;
	  }
	  startTime = std::chrono::steady_clock::now();
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
