#include <sys/mman.h>
#include <x86intrin.h>  
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <iostream>

#define CLUSTER_ONE 0
#define CLUSTER_TWO 1
#define MAX_CYCLES 600
#define SUPERPAGE (1024*1024*1024)
#define ROUNDS 100
#define POOL_LEN 5000
#define DELETED_ADDR (char*)0xffffffff
#define THRESHOLD 510
#define MAX_FUNCS 496

typedef struct cluster {
    int size;
    int center;
} cluster;

typedef struct point {
    int value;
    int cluster;
} point;

cluster cluster_one;
cluster cluster_two;

int get_dist(int x, int y) {
    return abs(x - y);
}

point *init_clusters(int values[], const int num_values) {
    cluster_one.size = 0;
    cluster_two.size = 0;

    point *points = (point*)malloc(num_values*sizeof(point));
    for(int i = 0; i < num_values; ++i) {
        points[i].value = values[i] < MAX_CYCLES ? values[i] : MAX_CYCLES;
        points[i].cluster = -1;
    }

    cluster_one.center = points[rand()%num_values].value;
    cluster_two.center = points[rand()%num_values].value;

    while(cluster_two.center == cluster_one.center) {
        cluster_two.center = points[rand()%num_values].value;
    }

    return points;
}

int assign_to_cluster(point *point) {
    int change = 0;
    if(get_dist(point->value, cluster_two.center) <= get_dist(point->value, cluster_one.center)) {
        if(point->cluster != CLUSTER_TWO) 
            change = 1;
        
        point->cluster = CLUSTER_TWO;
    
    } else {
        if(point->cluster != CLUSTER_ONE) 
            change = 1;
            
        point->cluster = CLUSTER_ONE;
    }

    return change;
}

int assign_points(point points[], const int num_values) {
    int change = 0;
    for(int i = 0; i < num_values; ++i) {
        change |= assign_to_cluster(&points[i]);
    }
    return change;
}

void update_center(point points[], const int num_values) {
    cluster_one.center = 0;
    cluster_one.size = 0;
    cluster_two.center = 0;
    cluster_two.size = 0;
    for(int i = 0; i < num_values; ++i) {
        if(points[i].cluster == CLUSTER_TWO) {
            cluster_two.center += points[i].value;
            cluster_two.size++;
        } else if(points[i].cluster == CLUSTER_ONE){
            cluster_one.center += points[i].value;
            cluster_one.size++;
        }
    }

    cluster_two.center = cluster_two.size > 0 ? cluster_two.center/cluster_two.size : -1;
    cluster_one.center = cluster_one.size > 0 ? cluster_one.center/cluster_one.size : -1;
}

int get_threshold(int values[], const int num_values) {
    point *points = init_clusters(values, num_values);
    assign_points(points, num_values);
    update_center(points, num_values);
    int change = 1;
    int i = 0;

    while(assign_points(points, num_values)) {
        update_center(points, num_values);
    }

    printf("center 1: %d\n", cluster_one.center);
    printf("center 2: %d\n", cluster_two.center);

    int min = 10000;
    int max = 0;

    if(cluster_one.center < cluster_two.center) {
        for(int i = 0; i < num_values; ++i) {
            if(points[i].cluster == CLUSTER_ONE && points[i].value > max) 
                max = points[i].value;

            if(points[i].cluster == CLUSTER_TWO && points[i].value < min) 
                min = points[i].value;
        }
        free(points);
        return max + (int)((min-max)*0.8);
    } else {
        for(int i = 0; i < num_values; ++i) {
            if(points[i].cluster == CLUSTER_TWO && points[i].value > max) 
                max = points[i].value;

            if(points[i].cluster == CLUSTER_ONE && points[i].value < min) 
                min = points[i].value;
        }
        free(points);
        return max + (int)((min-max)*0.8);
    }
}

std::vector<uint64_t> gen_addrs(const int len, char *buffer) {
  std::vector<uint64_t> addrs;

  for(int i = 0; i < len; ++i) {
    addrs.push_back((uint64_t)buffer + (int)(rand()%(SUPERPAGE/8))*8);
  }
  return addrs;
}

int cmp(const void *x, const void* y) {
  return (int*)x - (int*)y;
}

int median(int vals[]) {
  qsort(vals, ROUNDS, sizeof(int), cmp);
  return vals[(int)ROUNDS/2];
}

int time_access(volatile char *base, volatile char *addr) {
  int round = ROUNDS;
  unsigned cycles_low, cycles_high, cycles_low1, cycles_high1;
  int times[ROUNDS];

  while(round--) {
    asm volatile (
    "MFENCE\n\t"
    "CPUID\n\t"
    "RDTSC\n\t"
    "mov %%edx, %0\n\t"
    "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
    "%rax", "%rbx", "%rcx", "%rdx");

    asm volatile("MFENCE\n\t");

    *base;
    *addr;

    asm volatile (
    "MFENCE\n\t"
    "CPUID\n\t"
    "RDTSC\n\t"
    "mov %%edx, %0\n\t"
    "mov %%eax, %1\n\t": "=r" (cycles_high1), "=r" (cycles_low1)::
    "%rax", "%rbx", "%rcx", "%rdx");

    uint64_t t0 = ( ((uint64_t)cycles_high << 32) | cycles_low );
    uint64_t t1 = ( ((uint64_t)cycles_high1 << 32) | cycles_low1 );
    times[round-1] = t1 - t0;

    _mm_clflushopt((void*)base);
    _mm_clflushopt((void*)addr);
  }


  return median(times);
}

void print_help() {
  printf("usage: \t./dram-functions -b \t-> prints the bits in hexadecimal to stdout\n");
  printf("\t./dram-functions -f \t-> prints the number and functions’ masks in hexadecimal\n");
  printf("\t./dram-functions -m\t-> prints the row mask in hexadecimal\n");
}

int round_to_pow2(int num) {
  int exp = (int)log2(num);
  int max = pow(2, exp+1);
  int min = pow(2, exp);

  int dist1 = max-num;
  int dist2 = num-min;

  return dist1 > dist2 ? min : max;
}

std::vector<std::vector<uint64_t>> get_conflicts(char *buffer, int threshold) {
  std::vector<uint64_t> pool = gen_addrs(POOL_LEN, buffer);
  std::vector<std::vector<uint64_t>> conflicts;
  int num_conflicts = 0;

  while(pool.size() > 0) {
    int base_id = rand()%pool.size();
    uint64_t base = pool[base_id];
    pool.erase(pool.begin() + base_id);

    std::vector<uint64_t> set;
    set.push_back((uint64_t)base);

    auto it = pool.begin();
    
    while(it != pool.end()) {
      int time = time_access((char*)base, (char*)*it);
      if(time > threshold) { /*conflict*/
        set.push_back((uint64_t)*it);
        it = pool.erase(it);
      } else {
        ++it;
      }
    }

    conflicts.push_back(set);
  }

  return conflicts;
}

uint64_t change_bit(uint64_t addr, int bit) {
  return (uint64_t)((uint64_t)addr ^ (1 << bit));
}

void task1(char *buffer) {
  std::vector<uint64_t> pool = gen_addrs(POOL_LEN, buffer);
  uint64_t base = pool[rand()%POOL_LEN];
  uint64_t significant_bits = 0;
  std::vector<std::vector<uint64_t>> conflicts = get_conflicts(buffer, THRESHOLD);

  std::vector<uint64_t> set = conflicts[0];
  
  for(int i = 0; i < set.size(); ++i) {
    for(int bit = 0; bit < 30; ++bit) {
      uint64_t new_addr = change_bit(set[i], bit);
      //printf("%d %p %p\n",bit, conflicts[i], new_addr);
      int time = time_access((char *)set[i], (char *)new_addr);
      if(time < THRESHOLD) {
        significant_bits |= (1 << bit);
      }
    }
  }
  printf("%llx\n", significant_bits);
}

int calc_fn(uint64_t addr, uint64_t fn) {
  uint64_t mask = (uint64_t)addr & fn;
  int result = 0;
  for(int i = 0; i < 64; ++i) {
    if(mask & 1)
      result++;
    mask = mask >> 1;
  }

  return result == 2 ? 0 : result; 
}

/*source: https://math.stackexchange.com/questions/2254151/is-there-a-general-formula-to-generate-all-numbers-with-a-given-binary-hamming */
std::vector<uint64_t> get_funcs() {
  const int num_fn = MAX_FUNCS;
  std::vector<uint64_t> fns;
  fns.push_back(3);
  for(int i = 1; i < num_fn; ++i) {
    uint64_t previous = fns[i-1];
    uint64_t c = previous & -previous;
    uint64_t r = previous + c;
    fns.push_back((((r^previous) >> 2) / c) | r);
  }

  return fns;
}

void task2(char *buffer) {
  long long int significant_bits = 0;
  std::vector<std::vector<uint64_t>> conflicts = get_conflicts(buffer, THRESHOLD);
  std::vector<uint64_t> funcs = get_funcs();  
  std::vector<uint64_t> candidates;
  /* first get set of function candidates*/
  for(int i = 0; i < funcs.size(); ++i) {
    int result = calc_fn(conflicts[0][0], funcs[i]);
    int same = 1;
    for(int j = 1; j < conflicts[0].size(); ++j) {
      if(result != calc_fn(conflicts[0][j], funcs[i])) {
        same = 0;
        break;
      }
    }
    if(same) {
      candidates.push_back(funcs[i]);
    }
  }

  printf("candidates: %d\n", candidates.size());

  std::vector<uint64_t> functions;

  /* now test the candidates against one address out of each set*/
  for(int i = 0; i < candidates.size(); ++i) {
    printf("%llx\n", candidates[i]);
    int result = calc_fn(conflicts[0][1], candidates[i]);
    int same = 1;
    for(int j = 1; j < conflicts.size(); ++j) {
      if(result != calc_fn(conflicts[j][1], candidates[i])) {
        same = 0;
        break;
      }
    }

    if(!same) { /* not the same for different banks -> add to final addressing function set*/
      functions.push_back(candidates[i]);
    }
  }

  printf("%d\n", functions.size());

  for(int i = 0; i < functions.size(); ++i) {
    printf("%llx\n", functions[i]);
  }
}

void task3(char *buffer) {
  /*char **pool = gen_addrs(POOL_LEN, buffer);
  char *base = pool[rand()%POOL_LEN];
  int times[POOL_LEN];
  int min = 1000;
  int max = 0;
  
  for (int i = 0; i < POOL_LEN; ++i) {
    times[i] = time_access(base, pool[i]);
    if(times[i] < min)
      min = times[i];
    if(times[i] > max && times[i] < 1000)
      max = times[i];
  }

  int threshold = get_threshold(times, POOL_LEN);
  int num_banks = get_num_banks(buffer, threshold);

  printf("%d %d\n", threshold, num_banks);*/
}

int main(int argc, char **argv) {
  char *buffer = (char*)mmap(NULL, SUPERPAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

  if(buffer == MAP_FAILED) {
    printf("Error %d\n", errno);
    buffer = (char*)mmap(NULL, SUPERPAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if(buffer == MAP_FAILED) {
      printf("Error: %d\n", errno);
      exit(-1);
    }
  }

  switch (argc) {
    case 2:
      if(!strcmp(argv[1],"-b")) {
        task1(buffer);
      } else if(!strcmp(argv[1],"-f")) {
        task2(buffer);
      } else if(!strcmp(argv[1],"-m")) {
        task3(buffer);
      } else {
        print_help();
      }
    break;

    default:  
      print_help();
  }
}