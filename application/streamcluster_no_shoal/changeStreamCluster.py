import re

# Read the base code from the attachment
with open('streamcluster.cpp', 'r') as file:
    base_code = file.read()

# Replace pthread includes and related code with futures
base_code = re.sub(r'#include <pthread.h>', '#include <future>', base_code)
base_code = re.sub(r'pthread_t', 'std::future<void>', base_code)
base_code = re.sub(r'pthread_create\\(([^,]+), ([^,]+), ([^,]+), ([^\\)]+)\\)', r'\\1 = std::async(std::launch::async, \\3, \\4)', base_code)
base_code = re.sub(r'pthread_join\\(([^,]+), ([^\\)]+)\\)', r'\\1.get()', base_code)

# Save the modified code to a new file
with open('streamcluster_futures.cpp', 'w') as file:
    file.write(base_code)

print('Code has been modified and saved to streamcluster_futures.cpp')
