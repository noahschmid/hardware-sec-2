#include <sys/mman.h>
#include <x86intrin.h>  
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define CLUSTER_ONE 0
#define CLUSTER_TWO 1
#define MAX_CYCLES 600
#define SUPERPAGE (1024*1024*1024)
#define ROUNDS 100
#define POOL_LEN 5000
#define DELETED_ADDR (char*)0xffffffff
#define THRESHOLD 547
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

char **gen_addrs(const int len, char *buffer) {
  char **addrs = malloc(len*sizeof(char*));
  if(!addrs) {
    printf("Error allocating memory\n");
    exit(-1);
  }
  for(int i = 0; i < len; ++i) {
    addrs[i] = buffer + (int)(rand()%(SUPERPAGE/8))*8;
  }
  return addrs;
}

int cmp(void *x, void* y) {
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

void remove_addr(char **pool, int i) {
  pool[i] = DELETED_ADDR;
}

void normalize(char **pool, int *pool_len) {
    int original_length = *pool_len;
    for(int i = 0; i < *pool_len; ++i) {
        if(pool[i] == DELETED_ADDR) {
            for(int j = i; j < original_length; j++) 
                pool[j] = pool[j+1];
            --i;
            (*pool_len)--;
        }
    }
}

int round_to_pow2(int num) {
  int exp = (int)log2(num);
  int max = pow(2, exp+1);
  int min = pow(2, exp);

  int dist1 = max-num;
  int dist2 = num-min;

  return dist1 > dist2 ? min : max;
}

char** get_conflicts(char *buffer, int threshold, int *num_conflicts) {
  char **pool = gen_addrs(POOL_LEN, buffer);
  char **conflicts = calloc(POOL_LEN, sizeof(char*));
  int pool_len = POOL_LEN;
  *num_conflicts = 0;

  char *base = pool[rand()%pool_len];
  
  for (int i = 0; i < pool_len; ++i) {
    int time = time_access(base, pool[i]);
    if(time > threshold) { /*conflict*/
      conflicts[*num_conflicts] = pool[i];
      (*num_conflicts)++;
    }
  }

  conflicts[*num_conflicts] = base;

  return conflicts;
}

char *change_bit(char *addr, int bit) {
  return (char*)((long long int)addr ^ (1 << bit));
}

void task1(char *buffer) {
  char **pool = gen_addrs(POOL_LEN, buffer);
  char *base = pool[rand()%POOL_LEN];
  long long int significant_bits = 0;
  int num_conflicts;
  char **conflicts = get_conflicts(buffer, THRESHOLD, &num_conflicts);
  
  for(int i = 0; i < num_conflicts; ++i) {
    for(int bit = 0; bit < 30; ++bit) {
      char *new_addr = change_bit(conflicts[i], bit);
      //printf("%d %p %p\n",bit, conflicts[i], new_addr);
      int time = time_access(conflicts[i], new_addr);
      if(time < THRESHOLD) {
        significant_bits |= (1 << bit);
      }
    }
  }
  printf("%08x\n", significant_bits);
}

int calc_fn(char *addr, uint64_t fn) {
  uint64_t mask = (uint64_t)addr & fn;
  int result = 0;
  printf("addr: %p fn: %llx mask: %llx ");
  for(int i = 0; i < 32; ++i) {
    if(mask & 1)
      result++;
    mask = mask >> 1;
  }

  result = result == 2 ? 0 : result; 
  printf("result: %d\n", result);
  return result;
}

uint64_t *get_fns() {
  const int num_fn = MAX_FUNCS;
  uint64_t *fns = malloc(num_fn*sizeof(uint64_t));
  fns[0] = 3;
  for(int i = 1; i < num_fn; ++i) {
    uint64_t previous = fns[i-1];
    uint64_t c = previous & -previous;
    uint64_t r = previous + c;
    fns[i] = (((r^previous) >> 2) / c) | r;
  }

  return fns;
}

void task2(char *buffer) {
  char *fns = get_fns();
  char **pool = gen_addrs(POOL_LEN, buffer);
  char *base = pool[rand()%POOL_LEN];
  long long int significant_bits = 0;
  int num_conflicts;
  char **conflicts = get_conflicts(buffer, THRESHOLD, &num_conflicts);

  int num_funcs = 0;
  uint64_t funcs[MAX_FUNCS];

  for(int i = 0; i < MAX_FUNCS; ++i) {
    int result = calc_fn(conflicts[0], fns[i]);
    int same = 1;
    for(int j = 1; j < num_conflicts; ++j) {
      if(result != calc_fn(conflicts[j], fns[i])) {
        same = 0;
        break;
      }
    }
    if(same) {
      funcs[num_funcs] = fns[i];
      num_funcs++;
    }
  }

  printf("num_funcs: %d\n", num_funcs);

  for(int i = 0; i < num_funcs; ++i) {
    printf("%llx\n", funcs[i]);
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
  char *buffer = mmap(NULL, SUPERPAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

  if(buffer == MAP_FAILED) {
    printf("Error %d\n", errno);
    buffer = mmap(NULL, SUPERPAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

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